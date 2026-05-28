#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "subown.h"

#define DEFAULT_REQUESTS 50000u

typedef struct {
    uint64_t subscriber_id;
    uint64_t start_ns;
} req_ctx_t;

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

static subown_status_t fast_emitter(const subown_update_t *u, void *ctx)
{
    (void)u;
    (void)ctx;
    return SUBOWN_OK;
}

int main(int argc, char **argv)
{
    uint32_t requests = DEFAULT_REQUESTS;
    uint32_t i;
    uint64_t total_ns;
    double rps;
    double avg_ms;
    uint64_t p50_ns, p95_ns, p99_ns, max_ns;
    uint64_t *lat_ns;
    subown_record_t rec;
    subown_result_t out;

    if (argc > 1) {
        unsigned long v = strtoul(argv[1], NULL, 10);
        if (v > 0 && v <= 50000000ul) requests = (uint32_t)v;
    }

    lat_ns = (uint64_t *)calloc(requests, sizeof(uint64_t));
    if (!lat_ns) {
        fprintf(stderr, "alloc failed\n");
        return 1;
    }

    memset(&rec, 0, sizeof(rec));
    rec.subscriber_id = 1001;
    rec.owner_box_id = 0;
    rec.generation = 1;
    rec.owner_epoch = 1;
    rec.state = SUBOWN_STATE_LOCAL;

    {
        uint64_t run_start = monotonic_ns();
        for (i = 0; i < requests; i++) {
            uint64_t t0 = monotonic_ns();

            if ((i % 2u) == 0) {
                (void)subown_claim_local(&rec, 0, t0 / 1000000ull, fast_emitter, NULL, &out);
            } else {
                subown_update_t in;
                in.msg_version = SUBOWN_MSG_VERSION_1;
                in.subscriber_id = rec.subscriber_id;
                in.owner_box_id = (uint16_t)((i / 2u) % 2u);
                in.generation = rec.generation + 1u;
                in.timestamp = t0 / 1000000ull;
                in.owner_epoch = rec.owner_epoch;
                (void)subown_apply_remote_update(&rec, &in, &out);
            }

            lat_ns[i] = monotonic_ns() - t0;
        }
        total_ns = monotonic_ns() - run_start;
    }

    qsort(lat_ns, requests, sizeof(uint64_t), cmp_u64);

    p50_ns = lat_ns[(requests * 50u) / 100u];
    p95_ns = lat_ns[(requests * 95u) / 100u];
    p99_ns = lat_ns[(requests * 99u) / 100u];
    max_ns = lat_ns[requests - 1u];

    rps = ((double)requests * 1000000000.0) / (double)total_ns;
    avg_ms = ((double)total_ns / (double)requests) / 1000000.0;

    printf("requests=%u\n", requests);
    printf("throughput_rps=%.2f\n", rps);
    printf("latency_avg_ms=%.6f\n", avg_ms);
    printf("latency_p50_ms=%.6f\n", (double)p50_ns / 1000000.0);
    printf("latency_p95_ms=%.6f\n", (double)p95_ns / 1000000.0);
    printf("latency_p99_ms=%.6f\n", (double)p99_ns / 1000000.0);
    printf("latency_max_ms=%.6f\n", (double)max_ns / 1000000.0);

    free(lat_ns);
    return 0;
}
