#ifndef SUBOWN_ZMQ_ADAPTER_H
#define SUBOWN_ZMQ_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

#include "subown.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SUBOWN_WIRE_MAGIC 0x534f574eu /* 'SOWN' */
#define SUBOWN_WIRE_VERSION_V1 1u
#define SUBOWN_WIRE_V1_FRAME_SIZE 40u

typedef struct {
    const char *pub_bind_endpoint;
    const char *sub_connect_endpoint;
    int io_threads;
    int sndhwm;
    int rcvhwm;
} subown_zmq_cfg_t;

typedef struct subown_zmq_adapter subown_zmq_adapter_t;

subown_status_t subown_zmq_adapter_init(subown_zmq_adapter_t **adapter,
                                        const subown_zmq_cfg_t *cfg);

void subown_zmq_adapter_close(subown_zmq_adapter_t *adapter);

subown_status_t subown_zmq_emit_update(const subown_update_t *update,
                                       void *user_ctx);

subown_status_t subown_zmq_recv_update(subown_zmq_adapter_t *adapter,
                                       subown_update_t *out_update,
                                       int *has_message);

/* Explicit wire contract helpers for tests and future compatibility checks */
subown_status_t subown_zmq_encode_update_v1(const subown_update_t *in,
                                            uint8_t *out_buf,
                                            size_t out_cap,
                                            size_t *out_len);

subown_status_t subown_zmq_decode_update_v1(const uint8_t *buf,
                                            size_t len,
                                            subown_update_t *out);

#ifdef __cplusplus
}
#endif

#endif
