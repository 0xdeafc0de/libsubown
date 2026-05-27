#include "adapters/subown_zmq_adapter.h"

#include <stdlib.h>
#include <string.h>

#ifdef SUBOWN_HAVE_ZMQ
#include <zmq.h>
#endif

struct subown_zmq_adapter {
#ifdef SUBOWN_HAVE_ZMQ
    void *ctx;
    void *pub;
    void *sub;
#else
    int unused;
#endif
};

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

static uint16_t rd_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t rd_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint64_t rd_u64_be(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) |
           ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) |
           ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) |
           (uint64_t)p[7];
}

subown_status_t subown_zmq_encode_update_v1(const subown_update_t *in,
                                            uint8_t *out_buf,
                                            size_t out_cap,
                                            size_t *out_len)
{
    uint8_t owner;

    if (!in || !out_buf || !out_len) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }
    if (out_cap < SUBOWN_WIRE_V1_FRAME_SIZE) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    owner = in->owner_box;
    if (!(owner == SUBOWN_BOX0_ID || owner == SUBOWN_BOX1_ID || owner == SUBOWN_UNOWNED_STATE)) {
        owner = (uint8_t)in->owner_box_id;
    }

    wr_u32_be(out_buf + 0, SUBOWN_WIRE_MAGIC);
    wr_u16_be(out_buf + 4, SUBOWN_WIRE_VERSION_V1);
    wr_u16_be(out_buf + 6, (uint16_t)SUBOWN_WIRE_V1_FRAME_SIZE);

    wr_u64_be(out_buf + 8, in->subscriber_id);
    wr_u32_be(out_buf + 16, in->generation);
    out_buf[20] = owner;
    out_buf[21] = 0;
    out_buf[22] = 0;
    out_buf[23] = 0;
    wr_u64_be(out_buf + 24, in->activation_ts ? in->activation_ts : in->timestamp);
    wr_u32_be(out_buf + 32, in->policy_mask);
    wr_u32_be(out_buf + 36, in->owner_epoch);

    *out_len = SUBOWN_WIRE_V1_FRAME_SIZE;
    return SUBOWN_OK;
}

subown_status_t subown_zmq_decode_update_v1(const uint8_t *buf,
                                            size_t len,
                                            subown_update_t *out)
{
    uint32_t magic;
    uint16_t version;
    uint16_t frame_len;
    uint8_t owner;

    if (!buf || !out) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }
    if (len != SUBOWN_WIRE_V1_FRAME_SIZE) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    magic = rd_u32_be(buf + 0);
    version = rd_u16_be(buf + 4);
    frame_len = rd_u16_be(buf + 6);

    if (magic != SUBOWN_WIRE_MAGIC) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }
    if (version != SUBOWN_WIRE_VERSION_V1) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }
    if (frame_len != SUBOWN_WIRE_V1_FRAME_SIZE) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    memset(out, 0, sizeof(*out));
    out->msg_version = SUBOWN_MSG_VERSION_1;
    out->subscriber_id = rd_u64_be(buf + 8);
    out->generation = rd_u32_be(buf + 16);
    owner = buf[20];
    if (!(owner == SUBOWN_BOX0_ID || owner == SUBOWN_BOX1_ID || owner == SUBOWN_UNOWNED_STATE)) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }
    out->owner_box = owner;
    out->owner_box_id = owner;
    out->activation_ts = rd_u64_be(buf + 24);
    out->timestamp = out->activation_ts;
    out->policy_mask = rd_u32_be(buf + 32);
    out->owner_epoch = rd_u32_be(buf + 36);
    return SUBOWN_OK;
}

subown_status_t subown_zmq_adapter_init(subown_zmq_adapter_t **adapter,
                                        const subown_zmq_cfg_t *cfg)
{
    if (!adapter || !cfg || !cfg->pub_bind_endpoint || !cfg->sub_connect_endpoint) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

#ifndef SUBOWN_HAVE_ZMQ
    (void)cfg;
    *adapter = NULL;
    return SUBOWN_ERR_INVALID_ARGUMENT;
#else
    int rc;
    subown_zmq_adapter_t *a = (subown_zmq_adapter_t *)calloc(1, sizeof(*a));
    if (!a) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    a->ctx = zmq_ctx_new();
    if (!a->ctx) {
        free(a);
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    if (cfg->io_threads > 0) {
        (void)zmq_ctx_set(a->ctx, ZMQ_IO_THREADS, cfg->io_threads);
    }

    a->pub = zmq_socket(a->ctx, ZMQ_PUB);
    a->sub = zmq_socket(a->ctx, ZMQ_SUB);
    if (!a->pub || !a->sub) {
        subown_zmq_adapter_close(a);
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    if (cfg->sndhwm > 0) {
        (void)zmq_setsockopt(a->pub, ZMQ_SNDHWM, &cfg->sndhwm, sizeof(cfg->sndhwm));
    }
    if (cfg->rcvhwm > 0) {
        (void)zmq_setsockopt(a->sub, ZMQ_RCVHWM, &cfg->rcvhwm, sizeof(cfg->rcvhwm));
    }

    {
        const int linger0 = 0;
        (void)zmq_setsockopt(a->pub, ZMQ_LINGER, &linger0, sizeof(linger0));
        (void)zmq_setsockopt(a->sub, ZMQ_LINGER, &linger0, sizeof(linger0));
    }

    rc = zmq_bind(a->pub, cfg->pub_bind_endpoint);
    if (rc != 0) {
        subown_zmq_adapter_close(a);
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    rc = zmq_connect(a->sub, cfg->sub_connect_endpoint);
    if (rc != 0) {
        subown_zmq_adapter_close(a);
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    {
        const char *all = "";
        rc = zmq_setsockopt(a->sub, ZMQ_SUBSCRIBE, all, 0);
        if (rc != 0) {
            subown_zmq_adapter_close(a);
            return SUBOWN_ERR_INVALID_ARGUMENT;
        }
    }

    *adapter = a;
    return SUBOWN_OK;
#endif
}

void subown_zmq_adapter_close(subown_zmq_adapter_t *adapter)
{
#ifdef SUBOWN_HAVE_ZMQ
    if (!adapter) {
        return;
    }
    if (adapter->pub) {
        (void)zmq_close(adapter->pub);
    }
    if (adapter->sub) {
        (void)zmq_close(adapter->sub);
    }
    if (adapter->ctx) {
        (void)zmq_ctx_term(adapter->ctx);
    }
    free(adapter);
#else
    (void)adapter;
#endif
}

subown_status_t subown_zmq_emit_update(const subown_update_t *update,
                                       void *user_ctx)
{
#ifndef SUBOWN_HAVE_ZMQ
    (void)update;
    (void)user_ctx;
    return SUBOWN_ERR_INVALID_ARGUMENT;
#else
    subown_zmq_adapter_t *adapter = (subown_zmq_adapter_t *)user_ctx;
    uint8_t wire[SUBOWN_WIRE_V1_FRAME_SIZE];
    size_t wire_len = 0;
    int rc;

    if (!update || !adapter || !adapter->pub) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    if (subown_zmq_encode_update_v1(update, wire, sizeof(wire), &wire_len) != SUBOWN_OK) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    rc = (int)zmq_send(adapter->pub, wire, wire_len, ZMQ_DONTWAIT);
    if (rc == (int)wire_len) {
        return SUBOWN_OK;
    }
    return SUBOWN_ERR_CALLBACK_FAILED;
#endif
}

subown_status_t subown_zmq_recv_update(subown_zmq_adapter_t *adapter,
                                       subown_update_t *out_update,
                                       int *has_message)
{
#ifndef SUBOWN_HAVE_ZMQ
    (void)adapter;
    (void)out_update;
    if (has_message) {
        *has_message = 0;
    }
    return SUBOWN_ERR_INVALID_ARGUMENT;
#else
    uint8_t wire[SUBOWN_WIRE_V1_FRAME_SIZE + 32u];
    int rc;

    if (!adapter || !out_update || !has_message || !adapter->sub) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    *has_message = 0;
    rc = (int)zmq_recv(adapter->sub, wire, sizeof(wire), ZMQ_DONTWAIT);
    if (rc < 0) {
        return SUBOWN_OK;
    }

    if (subown_zmq_decode_update_v1(wire, (size_t)rc, out_update) != SUBOWN_OK) {
        return SUBOWN_ERR_CALLBACK_FAILED;
    }

    *has_message = 1;
    return SUBOWN_OK;
#endif
}
