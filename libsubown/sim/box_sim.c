#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "subown.h"
#include "adapters/subown_zmq_adapter.h"

#define MAX_CLAIM_AT 64

typedef enum {
    CLAIM_MODE_PERIODIC = 0,
    CLAIM_MODE_ONCE,
    CLAIM_MODE_OFF
} claim_mode_t;

typedef struct {
    uint16_t box_id;
    const char *pub_ep;
    const char *sub_ep;
    uint64_t subscriber_id;
    const char *subscriber_ip;
    uint32_t iterations;
    uint32_t claim_every;
    uint32_t sleep_ms;
    uint32_t owner_epoch;
    claim_mode_t claim_mode;
    uint32_t claim_at[MAX_CLAIM_AT];
    uint32_t claim_at_count;
    uint32_t emit_dup_count;
    uint32_t drop_incoming_every;
} sim_cfg_t;

typedef struct {
    uint64_t claims_total;
    uint64_t emits_total;
    uint64_t recv_total;
    uint64_t recv_dropped_fault;
    uint64_t apply_remote_total;
    uint64_t keep_local_total;
    uint64_t noop_total;
} sim_stats_t;

static const char *claim_mode_str(claim_mode_t m)
{
    if (m == CLAIM_MODE_ONCE) return "once";
    if (m == CLAIM_MODE_OFF) return "off";
    return "periodic";
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s --box-id <0|1> --pub <endpoint> --sub <endpoint> [options]\n"
            "Options:\n"
            "  --subscriber-id <u64>   default: 1001\n"
            "  --ip <string>           default: 10.0.0.10\n"
            "  --iterations <u32>      default: 200\n"
            "  --claim-mode <periodic|once|off> default: periodic\n"
            "  --claim-every <u32>     default: 20 (periodic/once mode)\n"
            "  --claim-at <csv>        explicit claim iterations, e.g. 20,80,140\n"
            "  --emit-dup <u32>        duplicate sends per claim (fault injection), default: 0\n"
            "  --drop-incoming-every <u32> drop every Nth inbound update, default: 0 (off)\n"
            "  --sleep-ms <u32>        default: 20\n"
            "  --owner-epoch <u32>     default: 1\n",
            prog);
}

static int parse_u32(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;
    errno = 0;
    v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v > UINT32_MAX) {
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

static int parse_u16(const char *s, uint16_t *out)
{
    uint32_t tmp;
    if (parse_u32(s, &tmp) != 0 || tmp > UINT16_MAX) {
        return -1;
    }
    *out = (uint16_t)tmp;
    return 0;
}

static int parse_u64(const char *s, uint64_t *out)
{
    char *end = NULL;
    unsigned long long v;
    errno = 0;
    v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    *out = (uint64_t)v;
    return 0;
}

static int parse_claim_mode(const char *s, claim_mode_t *out)
{
    if (strcmp(s, "periodic") == 0) {
        *out = CLAIM_MODE_PERIODIC;
        return 0;
    }
    if (strcmp(s, "once") == 0) {
        *out = CLAIM_MODE_ONCE;
        return 0;
    }
    if (strcmp(s, "off") == 0) {
        *out = CLAIM_MODE_OFF;
        return 0;
    }
    return -1;
}

static int parse_claim_at_csv(const char *s, uint32_t *out, uint32_t *count)
{
    char buf[512];
    char *tok;
    char *save = NULL;
    uint32_t n = 0;

    if (!s || !out || !count) return -1;
    if (strlen(s) >= sizeof(buf)) return -1;

    strcpy(buf, s);
    tok = strtok_r(buf, ",", &save);
    while (tok != NULL) {
        uint32_t v;
        if (n >= MAX_CLAIM_AT) return -1;
        if (parse_u32(tok, &v) != 0 || v == 0) return -1;
        out[n++] = v;
        tok = strtok_r(NULL, ",", &save);
    }

    *count = n;
    return (n > 0) ? 0 : -1;
}

static int should_claim_on_iter(const sim_cfg_t *cfg, uint32_t iter, int claimed_once)
{
    uint32_t i;

    if (cfg->claim_at_count > 0) {
        for (i = 0; i < cfg->claim_at_count; i++) {
            if (cfg->claim_at[i] == iter) return 1;
        }
        return 0;
    }

    if (cfg->claim_mode == CLAIM_MODE_PERIODIC) return ((iter % cfg->claim_every) == 0);
    if (cfg->claim_mode == CLAIM_MODE_ONCE) return (!claimed_once && iter == cfg->claim_every);
    return 0;
}

static int parse_args(int argc, char **argv, sim_cfg_t *cfg)
{
    int i;
    memset(cfg, 0, sizeof(*cfg));
    cfg->subscriber_id = 1001;
    cfg->subscriber_ip = "10.0.0.10";
    cfg->iterations = 200;
    cfg->claim_every = 20;
    cfg->sleep_ms = 20;
    cfg->owner_epoch = 1;
    cfg->claim_mode = CLAIM_MODE_PERIODIC;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--box-id") == 0 && i + 1 < argc) {
            if (parse_u16(argv[++i], &cfg->box_id) != 0) return -1;
        } else if (strcmp(argv[i], "--pub") == 0 && i + 1 < argc) {
            cfg->pub_ep = argv[++i];
        } else if (strcmp(argv[i], "--sub") == 0 && i + 1 < argc) {
            cfg->sub_ep = argv[++i];
        } else if (strcmp(argv[i], "--subscriber-id") == 0 && i + 1 < argc) {
            if (parse_u64(argv[++i], &cfg->subscriber_id) != 0) return -1;
        } else if (strcmp(argv[i], "--ip") == 0 && i + 1 < argc) {
            cfg->subscriber_ip = argv[++i];
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], &cfg->iterations) != 0) return -1;
        } else if (strcmp(argv[i], "--claim-mode") == 0 && i + 1 < argc) {
            if (parse_claim_mode(argv[++i], &cfg->claim_mode) != 0) return -1;
        } else if (strcmp(argv[i], "--claim-every") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], &cfg->claim_every) != 0) return -1;
        } else if (strcmp(argv[i], "--claim-at") == 0 && i + 1 < argc) {
            if (parse_claim_at_csv(argv[++i], cfg->claim_at, &cfg->claim_at_count) != 0) return -1;
        } else if (strcmp(argv[i], "--emit-dup") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], &cfg->emit_dup_count) != 0) return -1;
        } else if (strcmp(argv[i], "--drop-incoming-every") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], &cfg->drop_incoming_every) != 0) return -1;
        } else if (strcmp(argv[i], "--sleep-ms") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], &cfg->sleep_ms) != 0) return -1;
        } else if (strcmp(argv[i], "--owner-epoch") == 0 && i + 1 < argc) {
            if (parse_u32(argv[++i], &cfg->owner_epoch) != 0) return -1;
        } else {
            return -1;
        }
    }

    if (!cfg->pub_ep || !cfg->sub_ep) return -1;
    if (cfg->box_id > 1) return -1;
    if (cfg->claim_every == 0) cfg->claim_every = 1;
    return 0;
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec * 1000ull) + ((uint64_t)ts.tv_nsec / 1000000ull);
}

int main(int argc, char **argv)
{
#ifdef SUBOWN_HAVE_ZMQ
    sim_cfg_t cfg;
    sim_stats_t st;
    subown_record_t rec;
    subown_result_t result;
    subown_zmq_adapter_t *adapter = NULL;
    subown_zmq_cfg_t zcfg;
    uint32_t i;
    int claimed_once = 0;

    memset(&st, 0, sizeof(st));

    if (parse_args(argc, argv, &cfg) != 0) {
        usage(argv[0]);
        return 2;
    }

    memset(&zcfg, 0, sizeof(zcfg));
    zcfg.pub_bind_endpoint = cfg.pub_ep;
    zcfg.sub_connect_endpoint = cfg.sub_ep;
    zcfg.io_threads = 1;
    zcfg.sndhwm = 10000;
    zcfg.rcvhwm = 10000;

    if (subown_zmq_adapter_init(&adapter, &zcfg) != SUBOWN_OK) {
        fprintf(stderr, "box%u: failed to init zmq adapter\n", cfg.box_id);
        return 1;
    }

    memset(&rec, 0, sizeof(rec));
    rec.subscriber_id = cfg.subscriber_id;
    rec.owner_box_id = UINT16_MAX;
    rec.generation = 0;
    rec.timestamp = now_ms();
    rec.owner_epoch = cfg.owner_epoch;
    rec.state = SUBOWN_STATE_UNOWNED;

    fprintf(stdout,
            "box%u start: pub=%s sub=%s sub_id=%" PRIu64 " ip=%s iter=%u claim_mode=%s claim_every=%u claim_at_count=%u emit_dup=%u drop_incoming_every=%u\n",
            cfg.box_id, cfg.pub_ep, cfg.sub_ep, cfg.subscriber_id, cfg.subscriber_ip,
            cfg.iterations, claim_mode_str(cfg.claim_mode), cfg.claim_every, cfg.claim_at_count,
            cfg.emit_dup_count, cfg.drop_incoming_every);
    fprintf(stdout,
            "box%u sync: subscriber_id=%" PRIu64 " ip=%s synced locally (simulated control-plane replication)\n",
            cfg.box_id, cfg.subscriber_id, cfg.subscriber_ip);

    for (i = 1; i <= cfg.iterations; i++) {
        int has_msg = 0;
        subown_update_t incoming;

        if (should_claim_on_iter(&cfg, i, claimed_once)) {
            subown_status_t s = subown_claim_local(&rec, cfg.box_id, now_ms(), subown_zmq_emit_update, adapter, &result);
            if (s == SUBOWN_OK && result.decision == SUBOWN_DECISION_EMIT_LOCAL_UPDATE) {
                uint32_t d;
                claimed_once = 1;
                st.claims_total++;
                st.emits_total++;
                for (d = 0; d < cfg.emit_dup_count; d++) {
                    (void)subown_zmq_emit_update(&(subown_update_t){
                        .msg_version = SUBOWN_MSG_VERSION_1,
                        .subscriber_id = rec.subscriber_id,
                        .owner_box_id = rec.owner_box_id,
                        .generation = rec.generation,
                        .timestamp = rec.timestamp,
                        .owner_epoch = rec.owner_epoch
                    }, adapter);
                    st.emits_total++;
                }
                fprintf(stdout,
                        "box%u traffic_seen: iter=%u ip=%s -> claim gen=%u owner=%u reason=%d dup_sent=%u\n",
                        cfg.box_id, i, cfg.subscriber_ip, rec.generation, rec.owner_box_id, result.reason, cfg.emit_dup_count);
            }
        }

        do {
            subown_status_t rs = subown_zmq_recv_update(adapter, &incoming, &has_msg);
            if (rs != SUBOWN_OK) break;
            if (has_msg) {
                st.recv_total++;
                if (cfg.drop_incoming_every > 0 && (st.recv_total % cfg.drop_incoming_every) == 0) {
                    st.recv_dropped_fault++;
                    fprintf(stdout,
                            "box%u recv_drop_fault: in_owner=%u in_gen=%u recv_idx=%" PRIu64 "\n",
                            cfg.box_id, incoming.owner_box_id, incoming.generation, st.recv_total);
                    continue;
                }

                (void)subown_apply_remote_update(&rec, &incoming, &result);
                if (result.decision == SUBOWN_DECISION_APPLY_REMOTE) st.apply_remote_total++;
                else if (result.decision == SUBOWN_DECISION_KEEP_LOCAL) st.keep_local_total++;
                else st.noop_total++;

                fprintf(stdout,
                        "box%u recv: in_owner=%u in_gen=%u -> local_owner=%u local_gen=%u decision=%d reason=%d\n",
                        cfg.box_id,
                        incoming.owner_box_id,
                        incoming.generation,
                        rec.owner_box_id,
                        rec.generation,
                        result.decision,
                        result.reason);
            }
        } while (has_msg);

        {
            struct timespec slp;
            slp.tv_sec = cfg.sleep_ms / 1000;
            slp.tv_nsec = (long)((cfg.sleep_ms % 1000) * 1000000u);
            nanosleep(&slp, NULL);
        }
    }

    fprintf(stdout,
            "box%u stats: claims=%" PRIu64 " emits=%" PRIu64 " recv=%" PRIu64 " recv_drop_fault=%" PRIu64 " apply_remote=%" PRIu64 " keep_local=%" PRIu64 " noop=%" PRIu64 "\n",
            cfg.box_id,
            st.claims_total,
            st.emits_total,
            st.recv_total,
            st.recv_dropped_fault,
            st.apply_remote_total,
            st.keep_local_total,
            st.noop_total);

    fprintf(stdout,
            "box%u final: owner=%u gen=%u epoch=%u\n",
            cfg.box_id, rec.owner_box_id, rec.generation, rec.owner_epoch);

    subown_zmq_adapter_close(adapter);
    return 0;
#else
    (void)argc;
    (void)argv;
    fprintf(stderr, "This simulator requires SUBOWN_HAVE_ZMQ (build with WITH_ZMQ=1 and libzmq installed).\n");
    return 3;
#endif
}
