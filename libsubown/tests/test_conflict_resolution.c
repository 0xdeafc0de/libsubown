#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "subown.h"

#define ASSERT_TRUE(expr) do { if (!(expr)) { \
    fprintf(stderr, "ASSERT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    exit(1); } } while (0)

#define ASSERT_EQ_U64(a,b) ASSERT_TRUE((uint64_t)(a) == (uint64_t)(b))
#define ASSERT_EQ_U32(a,b) ASSERT_TRUE((uint32_t)(a) == (uint32_t)(b))
#define ASSERT_EQ_U16(a,b) ASSERT_TRUE((uint16_t)(a) == (uint16_t)(b))
#define ASSERT_EQ_I(a,b) ASSERT_TRUE((int)(a) == (int)(b))

static subown_record_t mk_record(uint64_t sid, uint16_t owner, uint32_t gen, uint64_t ts, uint32_t epoch) {
    subown_record_t r;
    memset(&r, 0, sizeof(r));
    r.subscriber_id = sid;
    r.owner_box = (uint8_t)owner;
    r.owner_box_id = owner;
    r.generation = gen;
    r.activation_ts = ts;
    r.last_seen_ts = ts;
    r.timestamp = ts;
    r.owner_epoch = epoch;
    r.state = SUBOWN_STATE_LOCAL;
    return r;
}

static subown_update_t mk_update(uint64_t sid, uint16_t owner, uint32_t gen, uint64_t ts, uint32_t epoch) {
    subown_update_t u;
    memset(&u, 0, sizeof(u));
    u.msg_version = SUBOWN_MSG_VERSION_1;
    u.subscriber_id = sid;
    u.owner_box = (uint8_t)owner;
    u.owner_box_id = owner;
    u.generation = gen;
    u.activation_ts = ts;
    u.timestamp = ts;
    u.owner_epoch = epoch;
    return u;
}

static subown_status_t ok_emitter(const subown_update_t *u, void *ctx) {
    subown_update_t *captured = (subown_update_t *)ctx;
    *captured = *u;
    return SUBOWN_OK;
}

static subown_status_t fail_emitter(const subown_update_t *u, void *ctx) {
    (void)u;
    (void)ctx;
    return SUBOWN_ERR_INVALID_ARGUMENT;
}

static void test_higher_generation_wins(void) {
    subown_record_t local = mk_record(100, 1, 5, 10, 1);
    subown_update_t in = mk_update(100, 0, 6, 11, 1);
    subown_result_t out;

    ASSERT_EQ_I(SUBOWN_OK, subown_apply_remote_update(&local, &in, &out));
    ASSERT_EQ_I(SUBOWN_DECISION_APPLY_REMOTE, out.decision);
    ASSERT_EQ_I(SUBOWN_REASON_WIN_HIGHER_GENERATION, out.reason);
    ASSERT_EQ_U16(0, local.owner_box_id);
    ASSERT_EQ_U32(6, local.generation);
}

static void test_tie_lower_box_id_wins(void) {
    subown_record_t local = mk_record(100, 1, 9, 10, 1);
    subown_update_t in = mk_update(100, 0, 9, 10, 1);
    subown_result_t out;

    ASSERT_EQ_I(SUBOWN_OK, subown_apply_remote_update(&local, &in, &out));
    ASSERT_EQ_I(SUBOWN_DECISION_APPLY_REMOTE, out.decision);
    ASSERT_EQ_I(SUBOWN_REASON_WIN_TIE_BREAK_BOX_ID, out.reason);
    ASSERT_EQ_U16(0, local.owner_box_id);
    ASSERT_EQ_U32(9, local.generation);
}

static void test_stale_generation_rejected(void) {
    subown_record_t local = mk_record(100, 0, 10, 10, 1);
    subown_update_t in = mk_update(100, 1, 9, 11, 1);
    subown_result_t out;

    ASSERT_EQ_I(SUBOWN_OK, subown_apply_remote_update(&local, &in, &out));
    ASSERT_EQ_I(SUBOWN_DECISION_KEEP_LOCAL, out.decision);
    ASSERT_EQ_I(SUBOWN_REASON_DROP_STALE_GENERATION, out.reason);
    ASSERT_EQ_U16(0, local.owner_box_id);
    ASSERT_EQ_U32(10, local.generation);
}

static void test_newer_epoch_wins_even_with_lower_generation(void) {
    subown_record_t local = mk_record(300, 0, 20, 200, 2);
    subown_update_t in = mk_update(300, 1, 1, 201, 3);
    subown_result_t out;

    ASSERT_EQ_I(SUBOWN_OK, subown_apply_remote_update(&local, &in, &out));
    ASSERT_EQ_I(SUBOWN_DECISION_APPLY_REMOTE, out.decision);
    ASSERT_EQ_I(SUBOWN_REASON_WIN_NEWER_EPOCH, out.reason);
    ASSERT_EQ_U16(1, local.owner_box_id);
    ASSERT_EQ_U32(1, local.generation);
    ASSERT_EQ_U32(3, local.owner_epoch);
}

static void test_stale_epoch_rejected_even_with_higher_generation(void) {
    subown_record_t local = mk_record(301, 0, 5, 200, 5);
    subown_update_t in = mk_update(301, 1, 100, 201, 4);
    subown_result_t out;

    ASSERT_EQ_I(SUBOWN_OK, subown_apply_remote_update(&local, &in, &out));
    ASSERT_EQ_I(SUBOWN_DECISION_KEEP_LOCAL, out.decision);
    ASSERT_EQ_I(SUBOWN_REASON_DROP_STALE_EPOCH, out.reason);
    ASSERT_EQ_U16(0, local.owner_box_id);
    ASSERT_EQ_U32(5, local.owner_epoch);
}

static void test_duplicate_is_idempotent(void) {
    subown_record_t local = mk_record(100, 0, 10, 10, 1);
    subown_update_t in = mk_update(100, 0, 10, 10, 1);
    subown_result_t out;

    ASSERT_EQ_I(SUBOWN_OK, subown_apply_remote_update(&local, &in, &out));
    ASSERT_EQ_I(SUBOWN_DECISION_NOOP, out.decision);
    ASSERT_EQ_I(SUBOWN_REASON_DROP_DUPLICATE, out.reason);
}

static void test_invalid_subscriber_id_fails(void) {
    subown_record_t local = mk_record(100, 0, 1, 1, 1);
    subown_update_t in = mk_update(101, 1, 2, 2, 1);
    subown_result_t out;

    ASSERT_EQ_I(SUBOWN_ERR_INVALID_ARGUMENT, subown_apply_remote_update(&local, &in, &out));
}

static void test_invalid_msg_version_fails(void) {
    subown_record_t local = mk_record(100, 0, 1, 1, 1);
    subown_update_t in = mk_update(100, 1, 2, 2, 1);
    subown_result_t out;

    in.msg_version = 99;
    ASSERT_EQ_I(SUBOWN_ERR_INVALID_ARGUMENT, subown_apply_remote_update(&local, &in, &out));
}


static void test_two_node_same_generation_converges_to_lower_id(void) {
    subown_record_t n0 = mk_record(200, 0, 1, 100, 1);
    subown_record_t n1 = mk_record(200, 1, 1, 100, 1);
    subown_update_t u0 = mk_update(200, 0, 1, 100, 1);
    subown_update_t u1 = mk_update(200, 1, 1, 100, 1);
    subown_result_t out;

    ASSERT_EQ_I(SUBOWN_OK, subown_apply_remote_update(&n1, &u0, &out));
    ASSERT_EQ_U16(0, n1.owner_box_id);

    ASSERT_EQ_I(SUBOWN_OK, subown_apply_remote_update(&n0, &u1, &out));
    ASSERT_EQ_U16(0, n0.owner_box_id);

    ASSERT_EQ_U16(n0.owner_box_id, n1.owner_box_id);
    ASSERT_EQ_U32(n0.generation, n1.generation);
}

static void test_local_claim_emits_update(void) {
    subown_record_t r = mk_record(400, 1, 7, 10, 2);
    subown_result_t out;
    subown_update_t captured;
    memset(&captured, 0, sizeof(captured));

    ASSERT_EQ_I(SUBOWN_OK, subown_claim_local(&r, 0, 500, ok_emitter, &captured, &out));
    ASSERT_EQ_I(SUBOWN_DECISION_EMIT_LOCAL_UPDATE, out.decision);
    ASSERT_EQ_I(SUBOWN_REASON_LOCAL_CLAIM, out.reason);
    ASSERT_EQ_U16(0, r.owner_box_id);
    ASSERT_EQ_U32(8, r.generation);
    ASSERT_EQ_U64(500, r.timestamp);
    ASSERT_EQ_U16(0, captured.owner_box_id);
    ASSERT_EQ_U32(8, captured.generation);
    ASSERT_EQ_U64(400, captured.subscriber_id);
}

static void test_local_claim_same_owner_is_noop(void) {
    subown_record_t r = mk_record(401, 0, 3, 10, 1);
    subown_result_t out;
    subown_update_t captured;
    memset(&captured, 0, sizeof(captured));

    ASSERT_EQ_I(SUBOWN_OK, subown_claim_local(&r, 0, 999, ok_emitter, &captured, &out));
    ASSERT_EQ_I(SUBOWN_DECISION_NOOP, out.decision);
    ASSERT_EQ_I(SUBOWN_REASON_LOCAL_REFRESH, out.reason);
    ASSERT_EQ_U32(3, r.generation);
}

static void test_local_claim_emitter_failure_propagates(void) {
    subown_record_t r = mk_record(402, 1, 9, 10, 1);
    subown_result_t out;

    ASSERT_EQ_I(SUBOWN_ERR_CALLBACK_FAILED, subown_claim_local(&r, 0, 1000, fail_emitter, NULL, &out));
}

int main(void) {
    test_higher_generation_wins();
    test_tie_lower_box_id_wins();
    test_stale_generation_rejected();
    test_newer_epoch_wins_even_with_lower_generation();
    test_stale_epoch_rejected_even_with_higher_generation();
    test_duplicate_is_idempotent();
    test_invalid_subscriber_id_fails();
    test_invalid_msg_version_fails();
    test_two_node_same_generation_converges_to_lower_id();
    test_local_claim_emits_update();
    test_local_claim_same_owner_is_noop();
    test_local_claim_emitter_failure_propagates();
    printf("PASS: test_conflict_resolution\n");
    return 0;
}
