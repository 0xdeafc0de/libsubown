#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ownership_adapter.h"
#include "adapters/subown_zmq_adapter.h"

#define ASSERT_TRUE(x) do { if (!(x)) { fprintf(stderr, "assert fail: %s (%s:%d)\n", #x, __FILE__, __LINE__); exit(1);} } while (0)
#define ASSERT_EQ_I(a,b) ASSERT_TRUE((int)(a) == (int)(b))
#define ASSERT_EQ_U8(a,b) ASSERT_TRUE((uint8_t)(a) == (uint8_t)(b))
#define ASSERT_EQ_U64(a,b) ASSERT_TRUE((uint64_t)(a) == (uint64_t)(b))

static own_adapter_cfg_t base_cfg(own_mode_t mode)
{
    own_adapter_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = mode;
    cfg.local_box_id = 0;
    cfg.shard_count = 4;
    cfg.migration_threshold = 2;
    return cfg;
}

static void wr_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xffu);
    p[1] = (uint8_t)(v & 0xffu);
}

static void wr_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xffu);
    p[1] = (uint8_t)((v >> 16) & 0xffu);
    p[2] = (uint8_t)((v >> 8) & 0xffu);
    p[3] = (uint8_t)(v & 0xffu);
}

static void wr_u64_be(uint8_t *p, uint64_t v)
{
    p[0] = (uint8_t)((v >> 56) & 0xffu);
    p[1] = (uint8_t)((v >> 48) & 0xffu);
    p[2] = (uint8_t)((v >> 40) & 0xffu);
    p[3] = (uint8_t)((v >> 32) & 0xffu);
    p[4] = (uint8_t)((v >> 24) & 0xffu);
    p[5] = (uint8_t)((v >> 16) & 0xffu);
    p[6] = (uint8_t)((v >> 8) & 0xffu);
    p[7] = (uint8_t)(v & 0xffu);
}

static void make_wire_update(uint8_t owner_box,
                             uint64_t subscriber_id,
                             uint32_t generation,
                             uint64_t ts,
                             uint32_t epoch,
                             uint8_t wire[SUBOWN_WIRE_V1_FRAME_SIZE])
{
    memset(wire, 0, SUBOWN_WIRE_V1_FRAME_SIZE);
    wr_u32_be(wire + 0, SUBOWN_WIRE_MAGIC);
    wr_u16_be(wire + 4, SUBOWN_WIRE_VERSION_V1);
    wr_u16_be(wire + 6, (uint16_t)SUBOWN_WIRE_V1_FRAME_SIZE);
    wr_u64_be(wire + 8, subscriber_id);
    wr_u32_be(wire + 16, generation);
    wire[20] = owner_box;
    wr_u64_be(wire + 24, ts);
    wr_u32_be(wire + 32, 0);
    wr_u32_be(wire + 36, epoch);
}

static own_adapter_metrics_t metrics_of(ownership_adapter_t *a)
{
    own_adapter_metrics_t m;
    memset(&m, 0, sizeof(m));
    ASSERT_EQ_I(0, ownership_adapter_get_metrics(a, &m));
    return m;
}

static void test_disabled_mode_noop(void)
{
    ownership_adapter_t *a = NULL;
    own_adapter_cfg_t cfg = base_cfg(OWN_MODE_DISABLED);
    own_result_t out;

    ASSERT_EQ_I(0, ownership_adapter_create(&cfg, &a));
    ASSERT_EQ_I(0, ownership_adapter_on_packet(a, 1001, 10, &out));
    ASSERT_EQ_I(OWN_ACT_NOOP, out.action);
    ownership_adapter_destroy(a);
}

static void test_observe_remote_owner_processes_local_and_emits_event(void)
{
    ownership_adapter_t *a = NULL;
    own_adapter_cfg_t cfg = base_cfg(OWN_MODE_OBSERVE);
    own_result_t out;
    own_ring_event_t evt;
    int has = 0;
    uint8_t wire[SUBOWN_WIRE_V1_FRAME_SIZE];
    own_adapter_metrics_t m;

    ASSERT_EQ_I(0, ownership_adapter_create(&cfg, &a));

    make_wire_update(SUBOWN_BOX1_ID, 1002, 9, 20, 1, wire);
    ASSERT_EQ_I(0, ownership_adapter_on_remote_msg(a, wire, sizeof(wire)));

    ASSERT_EQ_I(0, ownership_adapter_on_packet(a, 1002, 30, &out));
    ASSERT_EQ_I(OWN_ACT_PROCESS_LOCAL, out.action);
    ASSERT_EQ_U8(1, out.peer_owner);

    ASSERT_EQ_I(0, ownership_adapter_pop_event(a, &evt, &has));
    ASSERT_EQ_I(1, has);
    ASSERT_EQ_I(OWN_EVT_MISROUTE, evt.event_type);
    ASSERT_TRUE(evt.subscriber_id == 1002);

    m = metrics_of(a);
    ASSERT_EQ_U64(1, m.own_apply_remote_total);
    ASSERT_EQ_U64(1, m.own_worker_events_total);

    ownership_adapter_destroy(a);
}

static void test_enforce_remote_owner_forwards(void)
{
    ownership_adapter_t *a = NULL;
    own_adapter_cfg_t cfg = base_cfg(OWN_MODE_ENFORCE);
    own_result_t out;
    uint8_t wire[SUBOWN_WIRE_V1_FRAME_SIZE];

    ASSERT_EQ_I(0, ownership_adapter_create(&cfg, &a));

    make_wire_update(SUBOWN_BOX1_ID, 1003, 2, 50, 1, wire);
    ASSERT_EQ_I(0, ownership_adapter_on_remote_msg(a, wire, sizeof(wire)));

    ASSERT_EQ_I(0, ownership_adapter_on_packet(a, 1003, 60, &out));
    ASSERT_EQ_I(OWN_ACT_FORWARD_REMOTE, out.action);
    ASSERT_EQ_U8(1, out.peer_owner);

    ownership_adapter_destroy(a);
}

static void test_enforce_unowned_claim_emitted(void)
{
    ownership_adapter_t *a = NULL;
    own_adapter_cfg_t cfg = base_cfg(OWN_MODE_ENFORCE);
    own_result_t out;
    own_adapter_metrics_t m;

    ASSERT_EQ_I(0, ownership_adapter_create(&cfg, &a));

    ASSERT_EQ_I(0, ownership_adapter_on_packet(a, 7777, 99, &out));
    ASSERT_EQ_I(OWN_ACT_CLAIM_EMITTED, out.action);
    ASSERT_EQ_U8(0, out.peer_owner);

    ASSERT_TRUE(ownership_adapter_flush_outbound(a) > 0);

    m = metrics_of(a);
    ASSERT_EQ_U64(1, m.own_worker_events_total);

    ownership_adapter_destroy(a);
}

static void test_remote_decode_error_counter(void)
{
    ownership_adapter_t *a = NULL;
    own_adapter_cfg_t cfg = base_cfg(OWN_MODE_OBSERVE);
    uint8_t bad[8];
    own_adapter_metrics_t m;

    memset(bad, 0, sizeof(bad));
    ASSERT_EQ_I(0, ownership_adapter_create(&cfg, &a));
    ASSERT_EQ_I(-1, ownership_adapter_on_remote_msg(a, bad, sizeof(bad)));

    m = metrics_of(a);
    ASSERT_EQ_U64(1, m.own_zmq_recv_err_total);

    ownership_adapter_destroy(a);
}

static void test_ring_drop_counter(void)
{
    ownership_adapter_t *a = NULL;
    own_adapter_cfg_t cfg = base_cfg(OWN_MODE_OBSERVE);
    own_ring_event_t evt;
    own_adapter_metrics_t m;
    int i;

    ASSERT_EQ_I(0, ownership_adapter_create(&cfg, &a));
    memset(&evt, 0, sizeof(evt));
    evt.event_type = OWN_EVT_MISROUTE;

    for (i = 0; i < 3000; ++i) {
        evt.subscriber_id = (uint64_t)i;
        (void)ownership_adapter_push_event(a, &evt);
    }

    m = metrics_of(a);
    ASSERT_TRUE(m.own_worker_ring_drop_total > 0);

    ownership_adapter_destroy(a);
}

static void test_remote_dup_stale_conflict_counters(void)
{
    ownership_adapter_t *a = NULL;
    own_adapter_cfg_t cfg = base_cfg(OWN_MODE_OBSERVE);
    uint8_t wire[SUBOWN_WIRE_V1_FRAME_SIZE];
    own_adapter_metrics_t m;

    ASSERT_EQ_I(0, ownership_adapter_create(&cfg, &a));

    make_wire_update(SUBOWN_BOX1_ID, 6001, 10, 100, 1, wire);
    ASSERT_EQ_I(0, ownership_adapter_on_remote_msg(a, wire, sizeof(wire))); /* baseline apply */

    make_wire_update(SUBOWN_BOX1_ID, 6001, 10, 100, 1, wire);
    ASSERT_EQ_I(0, ownership_adapter_on_remote_msg(a, wire, sizeof(wire))); /* duplicate */

    make_wire_update(SUBOWN_BOX0_ID, 6001, 10, 101, 1, wire);
    ASSERT_EQ_I(0, ownership_adapter_on_remote_msg(a, wire, sizeof(wire))); /* conflict tie-break */

    make_wire_update(SUBOWN_BOX1_ID, 6001, 9, 90, 1, wire);
    ASSERT_EQ_I(0, ownership_adapter_on_remote_msg(a, wire, sizeof(wire))); /* stale generation */

    make_wire_update(SUBOWN_BOX1_ID, 6001, 11, 110, 0, wire);
    ASSERT_EQ_I(0, ownership_adapter_on_remote_msg(a, wire, sizeof(wire))); /* stale epoch */

    m = metrics_of(a);
    ASSERT_EQ_U64(5, m.own_apply_remote_total);
    ASSERT_EQ_U64(1, m.own_dup_drop_total);
    ASSERT_EQ_U64(2, m.own_stale_drop_total);
    ASSERT_EQ_U64(1, m.own_conflict_total);

    ownership_adapter_destroy(a);
}

static void test_migration_trigger_counter(void)
{
    ownership_adapter_t *a = NULL;
    own_adapter_cfg_t cfg = base_cfg(OWN_MODE_ENFORCE);
    own_result_t out;
    uint8_t wire[SUBOWN_WIRE_V1_FRAME_SIZE];
    own_adapter_metrics_t m;

    ASSERT_EQ_I(0, ownership_adapter_create(&cfg, &a));

    make_wire_update(SUBOWN_BOX1_ID, 7001, 3, 10, 1, wire);
    ASSERT_EQ_I(0, ownership_adapter_on_remote_msg(a, wire, sizeof(wire)));

    ASSERT_EQ_I(0, ownership_adapter_on_packet(a, 7001, 20, &out));
    ASSERT_EQ_I(OWN_ACT_FORWARD_REMOTE, out.action);
    ASSERT_EQ_I(0, ownership_adapter_on_packet(a, 7001, 21, &out));
    ASSERT_EQ_I(OWN_ACT_FORWARD_REMOTE, out.action);
    ASSERT_EQ_I(0, ownership_adapter_on_packet(a, 7001, 22, &out));
    ASSERT_EQ_I(OWN_ACT_TRIGGER_MIGRATION, out.action);

    m = metrics_of(a);
    ASSERT_EQ_U64(1, m.own_migration_trigger_total);

    ownership_adapter_destroy(a);
}

static void test_send_error_counter_on_outbound_queue_full(void)
{
    ownership_adapter_t *a = NULL;
    own_adapter_cfg_t cfg = base_cfg(OWN_MODE_ENFORCE);
    own_result_t out;
    own_adapter_metrics_t m;
    int i;
    int saw_fail = 0;

    ASSERT_EQ_I(0, ownership_adapter_create(&cfg, &a));

    for (i = 0; i < 700; ++i) {
        int rc = ownership_adapter_on_packet(a, (uint64_t)(8000 + i), (uint64_t)(i + 1), &out);
        if (rc != 0) {
            saw_fail = 1;
            break;
        }
        ASSERT_EQ_I(OWN_ACT_CLAIM_EMITTED, out.action);
    }

    ASSERT_TRUE(saw_fail == 1);
    m = metrics_of(a);
    ASSERT_TRUE(m.own_zmq_send_err_total > 0);

    ownership_adapter_destroy(a);
}

int main(void)
{
    test_disabled_mode_noop();
    test_observe_remote_owner_processes_local_and_emits_event();
    test_enforce_remote_owner_forwards();
    test_enforce_unowned_claim_emitted();
    test_remote_decode_error_counter();
    test_ring_drop_counter();
    test_remote_dup_stale_conflict_counters();
    test_migration_trigger_counter();
    test_send_error_counter_on_outbound_queue_full();
    printf("PASS: test_ownership_adapter_smoke\n");
    return 0;
}
