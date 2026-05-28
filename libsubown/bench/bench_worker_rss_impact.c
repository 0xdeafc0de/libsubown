#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "subown.h"

#define MAX_RSS 32
#define MAX_WORKERS 128
#define MAX_SHARDS 32
#define RING_SIZE 16384u

typedef enum {
    IMPACT_MODE_BASELINE = 0,
    IMPACT_MODE_OBSERVE,
    IMPACT_MODE_ENFORCE
} impact_mode_t;

typedef struct {
    uint64_t id;
    uint64_t subscriber_id;
    uint64_t enqueue_ns;
} pkt_t;

typedef struct {
    pkt_t *buf;
    uint32_t head;
    uint32_t tail;
    uint64_t drops;
    pthread_mutex_t m;
} ring_t;

typedef struct {
    uint32_t rss;
    uint32_t workers;
    uint32_t shards;
    uint32_t total_rate;
    uint32_t duration_s;
    impact_mode_t mode;
} cfg_t;

typedef struct {
    cfg_t cfg;
    volatile int stop;

    ring_t worker_q[MAX_WORKERS];
    ring_t shard_q[MAX_SHARDS];

    uint64_t *worker_lat_ns;
    uint64_t worker_lat_cap;
    volatile uint64_t worker_lat_cnt;

    volatile uint64_t pkt_gen;
    volatile uint64_t pkt_done;
    volatile uint64_t own_evt_gen;
    volatile uint64_t own_evt_done;

    pthread_mutex_t start_m;
    pthread_cond_t start_cv;
    int start_ready;
} ctx_t;

typedef struct { ctx_t *ctx; uint32_t id; } th_arg_t;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static int ring_push(ring_t *r, const pkt_t *p) {
    int ok = 0;
    pthread_mutex_lock(&r->m);
    {
        uint32_t n = (r->tail + 1u) & (RING_SIZE - 1u);
        if (n == r->head) {
            r->drops++;
        } else {
            r->buf[r->tail] = *p;
            r->tail = n;
            ok = 1;
        }
    }
    pthread_mutex_unlock(&r->m);
    return ok;
}

static int ring_pop(ring_t *r, pkt_t *p) {
    int ok = 0;
    pthread_mutex_lock(&r->m);
    if (r->head != r->tail) {
        *p = r->buf[r->head];
        r->head = (r->head + 1u) & (RING_SIZE - 1u);
        ok = 1;
    }
    pthread_mutex_unlock(&r->m);
    return ok;
}

static void wait_start(ctx_t *ctx) {
    pthread_mutex_lock(&ctx->start_m);
    while (!ctx->start_ready) pthread_cond_wait(&ctx->start_cv, &ctx->start_m);
    pthread_mutex_unlock(&ctx->start_m);
}

static void *rss_thread(void *arg) {
    th_arg_t *a = (th_arg_t *)arg;
    ctx_t *ctx = a->ctx;
    uint32_t rid = a->id;
    uint64_t per_rss = (uint64_t)ctx->cfg.total_rate / (uint64_t)ctx->cfg.rss;
    uint64_t spacing_ns = per_rss ? (1000000000ull / per_rss) : 1000000000ull;
    uint64_t next = 0;
    uint64_t seq = rid + 1;

    wait_start(ctx);
    next = now_ns();

    while (!ctx->stop) {
        uint64_t t = now_ns();
        if (t >= next) {
            pkt_t p;
            uint32_t w;
            p.id = seq;
            p.subscriber_id = 1000000ull + (seq % 200000ull);
            p.enqueue_ns = t;
            w = (uint32_t)(p.subscriber_id % ctx->cfg.workers);
            if (ring_push(&ctx->worker_q[w], &p)) {
                __sync_add_and_fetch(&ctx->pkt_gen, 1ull);
            }
            seq += ctx->cfg.rss;
            next += spacing_ns;
        } else {
            struct timespec slp = {0, 100000};
            nanosleep(&slp, NULL);
        }
    }
    return NULL;
}

static subown_status_t noop_emit(const subown_update_t *u, void *ctx) {
    (void)u; (void)ctx; return SUBOWN_OK;
}

static void *worker_thread(void *arg) {
    th_arg_t *a = (th_arg_t *)arg;
    ctx_t *ctx = a->ctx;
    uint32_t wid = a->id;
    subown_record_t rec;
    subown_result_t out;

    memset(&rec, 0, sizeof(rec));
    rec.owner_box_id = (uint16_t)(wid & 1u);
    rec.generation = 1;
    rec.owner_epoch = 1;
    rec.state = SUBOWN_STATE_LOCAL;

    wait_start(ctx);

    while (!ctx->stop) {
        pkt_t p;
        if (ring_pop(&ctx->worker_q[wid], &p)) {
            uint64_t t0 = now_ns();

            if (ctx->cfg.mode != IMPACT_MODE_BASELINE) {
                pkt_t evt = p;
                uint32_t s = (uint32_t)(p.subscriber_id % ctx->cfg.shards);
                if (ring_push(&ctx->shard_q[s], &evt)) {
                    __sync_add_and_fetch(&ctx->own_evt_gen, 1ull);
                }
            }

            rec.subscriber_id = p.subscriber_id;
            (void)subown_claim_local(&rec, rec.owner_box_id, t0 / 1000000ull, noop_emit, NULL, &out);

            {
                uint64_t done = now_ns();
                uint64_t lat = done - p.enqueue_ns;
                uint64_t idx = __sync_fetch_and_add(&ctx->worker_lat_cnt, 1ull);
                if (idx < ctx->worker_lat_cap) ctx->worker_lat_ns[idx] = lat;
            }
            __sync_add_and_fetch(&ctx->pkt_done, 1ull);
        } else {
            struct timespec slp = {0, 100000};
            nanosleep(&slp, NULL);
        }
    }
    return NULL;
}

static void *shard_thread(void *arg) {
    th_arg_t *a = (th_arg_t *)arg;
    ctx_t *ctx = a->ctx;
    uint32_t sid = a->id;
    subown_record_t rec;
    subown_result_t out;

    memset(&rec, 0, sizeof(rec));
    rec.owner_box_id = (uint16_t)(sid & 1u);
    rec.generation = 1;
    rec.owner_epoch = 1;
    rec.state = SUBOWN_STATE_LOCAL;

    wait_start(ctx);

    while (!ctx->stop) {
        pkt_t e;
        if (ring_pop(&ctx->shard_q[sid], &e)) {
            if (ctx->cfg.mode == IMPACT_MODE_ENFORCE) {
                subown_update_t in;
                rec.subscriber_id = e.subscriber_id;
                in.msg_version = SUBOWN_MSG_VERSION_1;
                in.subscriber_id = e.subscriber_id;
                in.owner_box_id = (uint16_t)(e.id & 1u);
                in.generation = rec.generation + 1u;
                in.timestamp = now_ns() / 1000000ull;
                in.owner_epoch = rec.owner_epoch;
                (void)subown_apply_remote_update(&rec, &in, &out);
            }
            __sync_add_and_fetch(&ctx->own_evt_done, 1ull);
        } else {
            struct timespec slp = {0, 100000};
            nanosleep(&slp, NULL);
        }
    }
    return NULL;
}

static void run_once(const cfg_t *cfg, double *rps_out, double *p99_ms_out, uint64_t *drops_out) {
    ctx_t *ctx;
    pthread_t rss_th[MAX_RSS], w_th[MAX_WORKERS], s_th[MAX_SHARDS];
    th_arg_t rss_a[MAX_RSS], w_a[MAX_WORKERS], s_a[MAX_SHARDS];
    uint64_t cnt;
    uint64_t drops = 0;
    uint64_t start_ns, run_ns;
    uint32_t i;

    ctx = (ctx_t *)calloc(1, sizeof(ctx_t));
    if (!ctx) return;
    ctx->cfg = *cfg;
    ctx->worker_lat_cap = (uint64_t)cfg->total_rate * (uint64_t)cfg->duration_s * 2ull;
    ctx->worker_lat_ns = (uint64_t *)calloc(ctx->worker_lat_cap, sizeof(uint64_t));
    if (!ctx->worker_lat_ns) { free(ctx); return; }

    pthread_mutex_init(&ctx->start_m, NULL);
    pthread_cond_init(&ctx->start_cv, NULL);

    for (i = 0; i < cfg->workers; i++) {
        ctx->worker_q[i].buf = (pkt_t *)calloc(RING_SIZE, sizeof(pkt_t));
        pthread_mutex_init(&ctx->worker_q[i].m, NULL);
    }
    for (i = 0; i < cfg->shards; i++) {
        ctx->shard_q[i].buf = (pkt_t *)calloc(RING_SIZE, sizeof(pkt_t));
        pthread_mutex_init(&ctx->shard_q[i].m, NULL);
    }

    for (i = 0; i < cfg->shards; i++) {
        s_a[i].ctx = ctx; s_a[i].id = i;
        pthread_create(&s_th[i], NULL, shard_thread, &s_a[i]);
    }
    for (i = 0; i < cfg->workers; i++) {
        w_a[i].ctx = ctx; w_a[i].id = i;
        pthread_create(&w_th[i], NULL, worker_thread, &w_a[i]);
    }
    for (i = 0; i < cfg->rss; i++) {
        rss_a[i].ctx = ctx; rss_a[i].id = i;
        pthread_create(&rss_th[i], NULL, rss_thread, &rss_a[i]);
    }

    pthread_mutex_lock(&ctx->start_m);
    ctx->start_ready = 1;
    pthread_cond_broadcast(&ctx->start_cv);
    pthread_mutex_unlock(&ctx->start_m);

    start_ns = now_ns();
    sleep(cfg->duration_s);
    ctx->stop = 1;

    for (i = 0; i < cfg->rss; i++) pthread_join(rss_th[i], NULL);
    for (i = 0; i < cfg->workers; i++) pthread_join(w_th[i], NULL);
    for (i = 0; i < cfg->shards; i++) pthread_join(s_th[i], NULL);
    run_ns = now_ns() - start_ns;

    cnt = ctx->worker_lat_cnt;
    if (cnt > ctx->worker_lat_cap) cnt = ctx->worker_lat_cap;
    if (cnt == 0) cnt = 1;
    qsort(ctx->worker_lat_ns, (size_t)cnt, sizeof(uint64_t), cmp_u64);

    for (i = 0; i < cfg->workers; i++) drops += ctx->worker_q[i].drops;

    *rps_out = ((double)ctx->pkt_done * 1000000000.0) / (double)run_ns;
    *p99_ms_out = (double)ctx->worker_lat_ns[(cnt * 99u) / 100u] / 1000000.0;
    *drops_out = drops;

    for (i = 0; i < cfg->workers; i++) free(ctx->worker_q[i].buf);
    for (i = 0; i < cfg->shards; i++) free(ctx->shard_q[i].buf);
    free(ctx->worker_lat_ns);
    free(ctx);
}

static void run_profile(uint32_t rss, uint32_t workers, uint32_t shards, uint32_t rate, uint32_t duration_s)
{
    cfg_t c;
    double rps_b, p99_b, rps_o, p99_o, rps_e, p99_e;
    uint64_t d_b, d_o, d_e;

    c.rss = rss; c.workers = workers; c.shards = shards; c.total_rate = rate; c.duration_s = duration_s;

    c.mode = IMPACT_MODE_BASELINE; run_once(&c, &rps_b, &p99_b, &d_b);
    c.mode = IMPACT_MODE_OBSERVE;  run_once(&c, &rps_o, &p99_o, &d_o);
    c.mode = IMPACT_MODE_ENFORCE;  run_once(&c, &rps_e, &p99_e, &d_e);

    printf("profile rss=%u workers=%u shards=%u rate=%u/s dur=%us\n", rss, workers, shards, rate, duration_s);
    printf("baseline rps=%.2f p99_ms=%.4f drops=%" PRIu64 "\n", rps_b, p99_b, d_b);
    printf("observe  rps=%.2f p99_ms=%.4f drops=%" PRIu64 " delta_p99=%.2f%%\n", rps_o, p99_o, d_o, ((p99_o - p99_b) * 100.0) / p99_b);
    printf("enforce  rps=%.2f p99_ms=%.4f drops=%" PRIu64 " delta_p99=%.2f%%\n", rps_e, p99_e, d_e, ((p99_e - p99_b) * 100.0) / p99_b);
}

int main(int argc, char **argv)
{
    uint32_t rate = 100000;
    if (argc > 1) {
        unsigned long v = strtoul(argv[1], NULL, 10);
        if (v > 0 && v <= 10000000ul) rate = (uint32_t)v;
    }

    run_profile(8, 40, 8, rate, 10);
    run_profile(16, 80, 16, rate, 10);
    return 0;
}
