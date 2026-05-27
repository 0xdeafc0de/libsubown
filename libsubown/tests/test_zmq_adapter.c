#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "subown.h"
#include "adapters/subown_zmq_adapter.h"

#define ASSERT_TRUE(expr) do { if (!(expr)) { \
    fprintf(stderr, "ASSERT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    exit(1); } } while (0)
#define ASSERT_EQ_I(a,b) ASSERT_TRUE((int)(a) == (int)(b))
#define ASSERT_EQ_U64(a,b) ASSERT_TRUE((uint64_t)(a) == (uint64_t)(b))
#define ASSERT_EQ_U32(a,b) ASSERT_TRUE((uint32_t)(a) == (uint32_t)(b))
#define ASSERT_EQ_U16(a,b) ASSERT_TRUE((uint16_t)(a) == (uint16_t)(b))

static void fill_update(subown_update_t *u)
{
    memset(u, 0, sizeof(*u));
    u->msg_version = SUBOWN_MSG_VERSION_1;
    u->subscriber_id = 555;
    u->owner_box = SUBOWN_BOX0_ID;
    u->owner_box_id = SUBOWN_BOX0_ID;
    u->generation = 77;
    u->activation_ts = 12345;
    u->timestamp = 12345;
    u->policy_mask = 0xABCD1234u;
    u->owner_epoch = 9;
}

static void test_wire_encode_decode_roundtrip(void)
{
    subown_update_t in, out;
    uint8_t buf[SUBOWN_WIRE_V1_FRAME_SIZE];
    size_t out_len = 0;

    fill_update(&in);
    ASSERT_EQ_I(SUBOWN_OK, subown_zmq_encode_update_v1(&in, buf, sizeof(buf), &out_len));
    ASSERT_EQ_U64(SUBOWN_WIRE_V1_FRAME_SIZE, out_len);

    ASSERT_EQ_I(SUBOWN_OK, subown_zmq_decode_update_v1(buf, out_len, &out));
    ASSERT_EQ_U16(SUBOWN_MSG_VERSION_1, out.msg_version);
    ASSERT_EQ_U64(in.subscriber_id, out.subscriber_id);
    ASSERT_EQ_U16(in.owner_box, out.owner_box);
    ASSERT_EQ_U32(in.generation, out.generation);
    ASSERT_EQ_U64(in.activation_ts, out.activation_ts);
    ASSERT_EQ_U32(in.policy_mask, out.policy_mask);
}

static void test_wire_decode_rejects_truncated(void)
{
    subown_update_t out;
    uint8_t buf[SUBOWN_WIRE_V1_FRAME_SIZE];
    size_t out_len = 0;
    subown_update_t in;

    fill_update(&in);
    ASSERT_EQ_I(SUBOWN_OK, subown_zmq_encode_update_v1(&in, buf, sizeof(buf), &out_len));
    ASSERT_EQ_I(SUBOWN_ERR_INVALID_ARGUMENT, subown_zmq_decode_update_v1(buf, out_len - 1u, &out));
}

static void test_wire_decode_rejects_unknown_version(void)
{
    subown_update_t out;
    uint8_t buf[SUBOWN_WIRE_V1_FRAME_SIZE];
    size_t out_len = 0;
    subown_update_t in;

    fill_update(&in);
    ASSERT_EQ_I(SUBOWN_OK, subown_zmq_encode_update_v1(&in, buf, sizeof(buf), &out_len));
    buf[4] = 0;
    buf[5] = 2; /* version 2 */
    ASSERT_EQ_I(SUBOWN_ERR_INVALID_ARGUMENT, subown_zmq_decode_update_v1(buf, out_len, &out));
}

#ifdef SUBOWN_HAVE_ZMQ
#include <time.h>

static void test_pub_sub_roundtrip(void) {
    subown_zmq_adapter_t *a = NULL;
    subown_zmq_cfg_t cfg;
    subown_update_t in;
    subown_update_t out;
    int has = 0;
    int i;

    memset(&cfg, 0, sizeof(cfg));
    cfg.pub_bind_endpoint = "inproc://subown-test";
    cfg.sub_connect_endpoint = "inproc://subown-test";
    cfg.io_threads = 1;

    ASSERT_EQ_I(SUBOWN_OK, subown_zmq_adapter_init(&a, &cfg));

    fill_update(&in);

    ASSERT_EQ_I(SUBOWN_OK, subown_zmq_emit_update(&in, a));

    for (i = 0; i < 200; i++) {
        ASSERT_EQ_I(SUBOWN_OK, subown_zmq_recv_update(a, &out, &has));
        if (has) {
            break;
        }
        {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000;
            nanosleep(&ts, NULL);
        }
    }

    ASSERT_TRUE(has == 1);
    ASSERT_EQ_U16(in.msg_version, out.msg_version);
    ASSERT_EQ_U64(in.subscriber_id, out.subscriber_id);
    ASSERT_EQ_U16(in.owner_box_id, out.owner_box_id);
    ASSERT_EQ_U32(in.generation, out.generation);
    ASSERT_EQ_U64(in.timestamp, out.timestamp);
    ASSERT_EQ_U32(in.policy_mask, out.policy_mask);

    subown_zmq_adapter_close(a);
}

int main(void) {
    test_wire_encode_decode_roundtrip();
    test_wire_decode_rejects_truncated();
    test_wire_decode_rejects_unknown_version();
    test_pub_sub_roundtrip();
    printf("PASS: test_zmq_adapter\n");
    return 0;
}

#else
int main(void) {
    test_wire_encode_decode_roundtrip();
    test_wire_decode_rejects_truncated();
    test_wire_decode_rejects_unknown_version();
    printf("SKIP: test_zmq_adapter (SUBOWN_HAVE_ZMQ not enabled)\n");
    return 0;
}
#endif
