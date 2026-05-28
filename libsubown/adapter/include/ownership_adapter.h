#ifndef OWNERSHIP_ADAPTER_H
#define OWNERSHIP_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

#include "subown.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OWN_MODE_DISABLED = 0,
    OWN_MODE_OBSERVE,
    OWN_MODE_ENFORCE
} own_mode_t;

typedef enum {
    OWN_ACT_NOOP = 0,
    OWN_ACT_PROCESS_LOCAL,
    OWN_ACT_FORWARD_REMOTE,
    OWN_ACT_TRIGGER_MIGRATION,
    OWN_ACT_CLAIM_EMITTED
} own_action_t;

typedef enum {
    OWN_EVT_MISROUTE = 0,
    OWN_EVT_UNOWNED_CLAIM,
    OWN_EVT_MIGRATION_HINT
} own_event_type_t;

typedef struct {
    own_action_t action;
    uint8_t peer_owner;
    uint8_t reason;
} own_result_t;

typedef struct {
    uint64_t subscriber_id;
    own_event_type_t event_type;
    uint8_t local_box_id;
    uint64_t tsc;
} own_ring_event_t;

typedef struct {
    own_mode_t mode;
    uint8_t local_box_id;
    uint32_t shard_count;
    uint64_t migration_threshold;
} own_adapter_cfg_t;

typedef struct {
    uint64_t own_worker_events_total;
    uint64_t own_worker_ring_drop_total;
    uint64_t own_apply_remote_total;
    uint64_t own_conflict_total;
    uint64_t own_stale_drop_total;
    uint64_t own_dup_drop_total;
    uint64_t own_migration_trigger_total;
    uint64_t own_zmq_recv_err_total;
    uint64_t own_zmq_send_err_total;
} own_adapter_metrics_t;

typedef struct ownership_adapter ownership_adapter_t;

/* Lifecycle */
int ownership_adapter_create(const own_adapter_cfg_t *cfg,
                             ownership_adapter_t **out_adapter);
void ownership_adapter_destroy(ownership_adapter_t *adapter);

/* Worker-side hot-path entry (must be non-blocking in real engine integration) */
int ownership_adapter_on_packet(ownership_adapter_t *adapter,
                                uint64_t subscriber_id,
                                uint64_t now_tsc,
                                own_result_t *out);

/* Control-side hooks */
int ownership_adapter_on_remote_msg(ownership_adapter_t *adapter,
                                    const uint8_t *msg,
                                    size_t len);
int ownership_adapter_flush_outbound(ownership_adapter_t *adapter);
int ownership_adapter_on_peer_down(ownership_adapter_t *adapter,
                                   uint8_t peer_box_id);
int ownership_adapter_on_aging_tick(ownership_adapter_t *adapter,
                                    uint64_t now_tsc);

/* Ring/event bridge helpers for staged integration */
int ownership_adapter_push_event(ownership_adapter_t *adapter,
                                 const own_ring_event_t *evt);
int ownership_adapter_pop_event(ownership_adapter_t *adapter,
                                own_ring_event_t *evt,
                                int *has_event);

int ownership_adapter_get_metrics(const ownership_adapter_t *adapter,
                                  own_adapter_metrics_t *out_metrics);

#ifdef __cplusplus
}
#endif

#endif
