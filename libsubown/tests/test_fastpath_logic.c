#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "subown.h"

#define ASSERT_TRUE(expr) do { if (!(expr)) { \
    fprintf(stderr, "ASSERT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    exit(1); } } while (0)
#define ASSERT_EQ_I(a,b) ASSERT_TRUE((int)(a) == (int)(b))
#define ASSERT_EQ_U64(a,b) ASSERT_TRUE((uint64_t)(a) == (uint64_t)(b))
#define ASSERT_EQ_U32(a,b) ASSERT_TRUE((uint32_t)(a) == (uint32_t)(b))
#define ASSERT_EQ_U16(a,b) ASSERT_TRUE((uint16_t)(a) == (uint16_t)(b))

static subown_record_t mk_record(uint64_t sid, uint8_t owner, uint32_t gen)
{
    subown_record_t r;
    memset(&r, 0, sizeof(r));
    r.subscriber_id = sid;
    r.owner_box = owner;
    r.owner_box_id = owner;
    r.generation = gen;
    r.owner_epoch = 1;
    r.state = (owner == SUBOWN_UNOWNED_STATE) ? SUBOWN_STATE_UNOWNED : SUBOWN_STATE_LOCAL;
    return r;
}

static subown_status_t ok_emit(const subown_update_t *u, void *ctx)
{
    subown_update_t *cap = (subown_update_t *)ctx;
    *cap = *u;
    return SUBOWN_OK;
}

static void test_case_a_local_hit(void)
{
    subown_record_t r = mk_record(1001, SUBOWN_BOX0_ID, 10);
    subown_result_t out;

    ASSERT_EQ_I(SUBOWN_OK, subown_process_packet_event(&r, SUBOWN_BOX0_ID, 1000, 50000, ok_emit, NULL, &out));
    ASSERT_EQ_I(SUBOWN_DECISION_PROCESS_LOCAL, out.decision);
    ASSERT_EQ_I(SUBOWN_REASON_LOCAL_HIT, out.reason);
    ASSERT_EQ_U64(1000, r.last_seen_ts);
}

static void test_case_b_remote_forward(void)
{
    subown_record_t r = mk_record(1002, SUBOWN_BOX1_ID, 11);
    subown_result_t out;

    ASSERT_EQ_I(SUBOWN_OK, subown_process_packet_event(&r, SUBOWN_BOX0_ID, 1000, 5, ok_emit, NULL, &out));
    ASSERT_EQ_I(SUBOWN_DECISION_FORWARD_REMOTE, out.decision);
    ASSERT_EQ_I(SUBOWN_REASON_REMOTE_MISROUTE, out.reason);
    ASSERT_EQ_U64(1, r.remote_packet_counter);
}

static void test_case_b_remote_migration_threshold(void)
{
    subown_record_t r = mk_record(1003, SUBOWN_BOX1_ID, 11);
    subown_result_t out;

    r.remote_packet_counter = 5;
    ASSERT_EQ_I(SUBOWN_OK, subown_process_packet_event(&r, SUBOWN_BOX0_ID, 1000, 5, ok_emit, NULL, &out));
    ASSERT_EQ_I(SUBOWN_DECISION_TRIGGER_MIGRATION, out.decision);
    ASSERT_EQ_I(SUBOWN_REASON_REMOTE_MIGRATION_THRESHOLD, out.reason);
    ASSERT_EQ_U64(0, r.remote_packet_counter);
}

static void test_case_c_unowned_claim(void)
{
    subown_record_t r = mk_record(1004, SUBOWN_UNOWNED_STATE, 1);
    subown_result_t out;
    subown_update_t cap;

    memset(&cap, 0, sizeof(cap));
    ASSERT_EQ_I(SUBOWN_OK, subown_process_packet_event(&r, SUBOWN_BOX0_ID, 1234, 50000, ok_emit, &cap, &out));
    ASSERT_EQ_I(SUBOWN_DECISION_EMIT_LOCAL_UPDATE, out.decision);
    ASSERT_EQ_I(SUBOWN_REASON_UNOWNED_CLAIM, out.reason);
    ASSERT_EQ_U16(SUBOWN_BOX0_ID, r.owner_box_id);
    ASSERT_EQ_U32(2, r.generation);
    ASSERT_EQ_U64(1234, r.activation_ts);
    ASSERT_EQ_U64(1234, cap.activation_ts);
}

int main(void)
{
    test_case_a_local_hit();
    test_case_b_remote_forward();
    test_case_b_remote_migration_threshold();
    test_case_c_unowned_claim();
    printf("PASS: test_fastpath_logic\n");
    return 0;
}
