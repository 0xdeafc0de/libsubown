#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "subown.h"

#define MAX_WORKERS 128
#define MAX_SHARDS  32
#define RING_SIZE   65536u

typedef struct {
    uint64_t seq;
    uint64_t subscriber_id;
    uint16_t owner_box_id;
    uint64_t enqueue_ns;
} evt_t;

typedef struct {
    evt_t buf[RING_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    uint64_t dropped;
    pthread_mutex_t m;
} ring_t;

typedef struct {
    uint32_t req_rate;
    uint32_t duration_s;
    uint32_t workers;
    uint32_t shards;
    uint32_t cpu_pin;
} cfg_t;

typedef struct {
    cfg_t *cfg;
    ring_t *shard_rings;
    volatile int stop;
    volatile uint64_t produced;
    volatile uint64_t consumed;
    uint64_t *lat_ns;
    uint64_t lat_cap;
    volatile uint64_t lat_cnt;
    volatile uint64_t global_seq;

    pthread_mutex_t start_m;
    pthread_cond_t start_cv;
    int start_ready;
} ctx_t;

typedef struct {
    ctx_t *ctx;
    uint32_t worker_id;
} worker_arg_t;

typedef struct {
    ctx_t *ctx;
    uint32_t shard_id;
} shard_arg_t;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static int ring_push(ring_t *r, const evt_t *e) {
    uint32_t next;
    pthread_mutex_lock(&r->m);
    next = (r->tail + 1u) & (RING_SIZE - 1u);
    if (next == r->head) {
        r->dropped++;
        pthread_mutex_unlock(&r->m);
        return -1;
    }
    r->buf[r->tail] = *e;
    r->tail = next;
    pthread_mutex_unlock(&r->m);
    return 0;
}

static int ring_pop(ring_t *r, evt_t *e) {
    pthread_mutex_lock(&r->m);
    if (r->head == r->tail) {
        pthread_mutex_unlock(&r->m);
        return -1;
    }
    *e = r->buf[r->head];
    r->head = (r->head + 1u) & (RING_SIZE - 1u);
    pthread_mutex_unlock(&r->m);
    return 0;
}

static void maybe_pin_thread(uint32_t idx, uint32_t cpu_pin_enabled) {
#ifdef __linux__
    if (cpu_pin_enabled) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET((int)(idx % sysconf(_SC_NPROCESSORS_ONLN)), &cpuset);
        (void)pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }
#else
    (void)idx;
    (void)cpu_pin_enabled;
#endif
}

static void wait_for_start(ctx_t *ctx) {
    pthread_mutex_lock(&ctx->start_m);
    while (!ctx->start_ready) {
        pthread_cond_wait(&ctx->start_cv, &ctx->start_m);
    }
    pthread_mutex_unlock(&ctx->start_m);
}

static void *worker_thread(void *arg) {
    worker_arg_t *wa = (worker_arg_t *)arg;
    ctx_t *ctx = wa->ctx;
    uint32_t wid = wa->worker_id;
    cfg_t *cfg = ctx->cfg;
    uint64_t spacing_ns;
    uint64_t next_emit;

    maybe_pin_thread(wid, cfg->cpu_pin);
    wait_for_start(ctx);

    spacing_ns = ((1000000000ull * (uint64_t)cfg->workers) / (uint64_t)cfg->req_rate);
    next_emit = now_ns();

    while (!ctx->stop) {
        uint64_t t = now_ns();
        if (t >= next_emit) {
            evt_t e;
            uint64_t seq = __sync_add_and_fetch(&ctx->global_seq, 1ull);
            e.seq = seq;
            e.subscriber_id = 1000000ull + (seq % 100000ull);
            e.owner_box_id = (uint16_t)(wid & 1u);
            e.enqueue_ns = t;

            {
                uint32_t shard = (uint32_t)(e.subscriber_id % cfg->shards);
                (void)ring_push(&ctx->shard_rings[shard], &e);
            }
            __sync_add_and_fetch(&ctx->produced, 1ull);
            next_emit += spacing_ns;
        } else {
            struct timespec ts;
            uint64_t wait = next_emit - t;
            ts.tv_sec = 0;
            ts.tv_nsec = (long)(wait > 2000000ull ? 2000000ull : wait);
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

static void *shard_thread(void *arg) {
    shard_arg_t *sa = (shard_arg_t *)arg;
    ctx_t *ctx = sa->ctx;
    uint32_t sid = sa->shard_id;
    cfg_t *cfg = ctx->cfg;
    subown_record_t rec;
    subown_result_t out;

    maybe_pin_thread(cfg->workers + sid, cfg->cpu_pin);
    wait_for_start(ctx);

    memset(&rec, 0, sizeof(rec));
    rec.subscriber_id = 0;
    rec.owner_box_id = 0;
    rec.generation = 1;
    rec.owner_epoch = 1;
    rec.state = SUBOWN_STATE_LOCAL;

    while (!ctx->stop || ctx->consumed < ctx->produced) {
        evt_t e;
        if (ring_pop(&ctx->shard_rings[sid], &e) == 0) {
            uint64_t done;
            uint64_t l;
            subown_update_t in;

            rec.subscriber_id = e.subscriber_id;
            in.msg_version = SUBOWN_MSG_VERSION_1;
            in.subscriber_id = e.subscriber_id;
            in.owner_box_id = e.owner_box_id;
            in.generation = rec.generation + 1u;
            in.timestamp = now_ns() / 1000000ull;
            in.owner_epoch = rec.owner_epoch;
            (void)subown_apply_remote_update(&rec, &in, &out);

            done = now_ns();
            l = done - e.enqueue_ns;
            {
                uint64_t idx = __sync_fetch_and_add(&ctx->lat_cnt, 1ull);
                if (idx < ctx->lat_cap) ctx->lat_ns[idx] = l;
            }
            __sync_add_and_fetch(&ctx->consumed, 1ull);
        } else {
            struct timespec ts = {0, 500000};
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

static void run_case(uint32_t workers, uint32_t shards, uint32_t req_rate, uint32_t duration_s, uint32_t cpu_pin) {
    cfg_t cfg;
    ctx_t ctx;
    pthread_t wth[MAX_WORKERS];
    pthread_t sth[MAX_SHARDS];
    worker_arg_t wargs[MAX_WORKERS];
    shard_arg_t sargs[MAX_SHARDS];
    ring_t *rings;
    uint64_t start_ns, end_ns, run_ns;
    uint64_t cnt;
    double p50_ms, p95_ms, p99_ms, max_ms, rps;
    uint64_t dropped_total = 0;
    uint32_t i;

    if (workers > MAX_WORKERS || shards > MAX_SHARDS) {
        fprintf(stderr, "case exceeds max bounds\n");
        return;
    }

    cfg.req_rate = req_rate;
    cfg.duration_s = duration_s;
    cfg.workers = workers;
    cfg.shards = shards;
    cfg.cpu_pin = cpu_pin;

    rings = (ring_t *)calloc(shards, sizeof(ring_t));
    if (!rings) return;
    for (i = 0; i < shards; i++) pthread_mutex_init(&rings[i].m, NULL);

    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = &cfg;
    ctx.shard_rings = rings;
    ctx.lat_cap = (uint64_t)req_rate * (uint64_t)duration_s * 2ull;
    ctx.lat_ns = (uint64_t *)calloc(ctx.lat_cap, sizeof(uint64_t));
    if (!ctx.lat_ns) {
        free(rings);
        return;
    }

    pthread_mutex_init(&ctx.start_m, NULL);
    pthread_cond_init(&ctx.start_cv, NULL);
    ctx.start_ready = 0;

    for (i = 0; i < shards; i++) {
        sargs[i].ctx = &ctx;
        sargs[i].shard_id = i;
        pthread_create(&sth[i], NULL, shard_thread, &sargs[i]);
    }
    for (i = 0; i < workers; i++) {
        wargs[i].ctx = &ctx;
        wargs[i].worker_id = i;
        pthread_create(&wth[i], NULL, worker_thread, &wargs[i]);
    }

    pthread_mutex_lock(&ctx.start_m);
    ctx.start_ready = 1;
    pthread_cond_broadcast(&ctx.start_cv);
    pthread_mutex_unlock(&ctx.start_m);

    start_ns = now_ns();
    sleep(duration_s);
    ctx.stop = 1;

    for (i = 0; i < workers; i++) pthread_join(wth[i], NULL);
    for (i = 0; i < shards; i++) pthread_join(sth[i], NULL);
    end_ns = now_ns();

    run_ns = end_ns - start_ns;
    cnt = ctx.lat_cnt;
    if (cnt > ctx.lat_cap) cnt = ctx.lat_cap;
    qsort(ctx.lat_ns, (size_t)cnt, sizeof(uint64_t), cmp_u64);

    if (cnt == 0) cnt = 1;
    p50_ms = (double)ctx.lat_ns[(cnt * 50u) / 100u] / 1000000.0;
    p95_ms = (double)ctx.lat_ns[(cnt * 95u) / 100u] / 1000000.0;
    p99_ms = (double)ctx.lat_ns[(cnt * 99u) / 100u] / 1000000.0;
    max_ms = (double)ctx.lat_ns[cnt - 1u] / 1000000.0;
    rps = ((double)ctx.consumed * 1000000000.0) / (double)run_ns;

    for (i = 0; i < shards; i++) dropped_total += rings[i].dropped;

    printf("case workers=%u shards=%u rate=%u/s duration=%us pin=%u\n", workers, shards, req_rate, duration_s, cpu_pin);
    printf("produced=%" PRIu64 " consumed=%" PRIu64 " dropped=%" PRIu64 "\n", (uint64_t)ctx.produced, (uint64_t)ctx.consumed, dropped_total);
    printf("throughput_rps=%.2f p50_ms=%.4f p95_ms=%.4f p99_ms=%.4f max_ms=%.4f\n", rps, p50_ms, p95_ms, p99_ms, max_ms);

    free(ctx.lat_ns);
    free(rings);
}

int main(int argc, char **argv) {
    uint32_t rate = 4000;
    if (argc > 1) {
        unsigned long v = strtoul(argv[1], NULL, 10);
        if (v > 0 && v <= 10000000ul) rate = (uint32_t)v;
    }
    run_case(40, 4, rate, 10, 0);
    run_case(40, 8, rate, 10, 0);
    run_case(80, 8, rate, 10, 0);
    run_case(80, 16, rate, 10, 0);
    return 0;
}
