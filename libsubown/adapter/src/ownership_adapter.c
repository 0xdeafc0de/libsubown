#include "ownership_adapter.h"

#include <stdlib.h>
#include <string.h>

#include "adapters/subown_zmq_adapter.h"

#define OWN_EVENT_Q_CAP 2048u
#define OWN_RECORD_CAP 4096u
#define OWN_OUTBOUND_CAP 512u

struct own_slot {
    int used;
    subown_record_t rec;
};

struct ownership_adapter {
    own_adapter_cfg_t cfg;
    own_ring_event_t q[OWN_EVENT_Q_CAP];
    uint32_t q_head;
    uint32_t q_tail;

    struct own_slot table[OWN_RECORD_CAP];

    uint8_t outbound[OWN_OUTBOUND_CAP][SUBOWN_WIRE_V1_FRAME_SIZE];
    uint32_t outbound_len[OWN_OUTBOUND_CAP];
    uint32_t out_head;
    uint32_t out_tail;

    own_adapter_metrics_t metrics;
};

static uint32_t next_idx(uint32_t i, uint32_t cap)
{
    return (uint32_t)((i + 1u) % cap);
}

static int push_event(ownership_adapter_t *adapter,
                      uint64_t subscriber_id,
                      own_event_type_t event_type,
                      uint64_t tsc)
{
    own_ring_event_t evt;

    if (!adapter) {
        return -1;
    }

    evt.subscriber_id = subscriber_id;
    evt.event_type = event_type;
    evt.local_box_id = adapter->cfg.local_box_id;
    evt.tsc = tsc;
    return ownership_adapter_push_event(adapter, &evt);
}

static subown_status_t adapter_emit_local(const subown_update_t *update, void *user_ctx)
{
    ownership_adapter_t *adapter = (ownership_adapter_t *)user_ctx;
    size_t wire_len = 0;
    uint32_t n;

    if (!adapter || !update) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    n = next_idx(adapter->out_tail, OWN_OUTBOUND_CAP);
    if (n == adapter->out_head) {
        adapter->metrics.own_zmq_send_err_total++;
        return SUBOWN_ERR_CALLBACK_FAILED;
    }

    if (subown_zmq_encode_update_v1(update,
                                    adapter->outbound[adapter->out_tail],
                                    SUBOWN_WIRE_V1_FRAME_SIZE,
                                    &wire_len) != SUBOWN_OK) {
        adapter->metrics.own_zmq_send_err_total++;
        return SUBOWN_ERR_CALLBACK_FAILED;
    }

    adapter->outbound_len[adapter->out_tail] = (uint32_t)wire_len;
    adapter->out_tail = n;
    return SUBOWN_OK;
}

static struct own_slot *lookup_slot(ownership_adapter_t *adapter,
                                    uint64_t subscriber_id,
                                    int create)
{
    uint32_t start;
    uint32_t i;

    if (!adapter) {
        return NULL;
    }

    start = (uint32_t)(subscriber_id % OWN_RECORD_CAP);
    for (i = 0; i < OWN_RECORD_CAP; ++i) {
        uint32_t idx = (uint32_t)((start + i) % OWN_RECORD_CAP);
        struct own_slot *slot = &adapter->table[idx];

        if (!slot->used) {
            if (!create) {
                return NULL;
            }
            memset(slot, 0, sizeof(*slot));
            slot->used = 1;
            slot->rec.subscriber_id = subscriber_id;
            slot->rec.owner_box = SUBOWN_UNOWNED_STATE;
            slot->rec.owner_box_id = SUBOWN_UNOWNED_STATE;
            slot->rec.state = SUBOWN_STATE_UNOWNED;
            slot->rec.owner_epoch = 1;
            return slot;
        }

        if (slot->rec.subscriber_id == subscriber_id) {
            return slot;
        }
    }

    return NULL;
}

int ownership_adapter_create(const own_adapter_cfg_t *cfg,
                             ownership_adapter_t **out_adapter)
{
    ownership_adapter_t *a;

    if (!cfg || !out_adapter) {
        return -1;
    }

    a = (ownership_adapter_t *)calloc(1, sizeof(*a));
    if (!a) {
        return -1;
    }

    a->cfg = *cfg;
    *out_adapter = a;
    return 0;
}

void ownership_adapter_destroy(ownership_adapter_t *adapter)
{
    free(adapter);
}

int ownership_adapter_on_packet(ownership_adapter_t *adapter,
                                uint64_t subscriber_id,
                                uint64_t now_tsc,
                                own_result_t *out)
{
    struct own_slot *slot;
    subown_result_t res;

    if (!adapter || !out) {
        return -1;
    }

    out->action = OWN_ACT_NOOP;
    out->peer_owner = 0;
    out->reason = 0;

    if (adapter->cfg.mode == OWN_MODE_DISABLED) {
        return 0;
    }

    slot = lookup_slot(adapter, subscriber_id, 1);
    if (!slot) {
        return -1;
    }

    if (subown_process_packet_event(&slot->rec,
                                    adapter->cfg.local_box_id,
                                    now_tsc,
                                    adapter->cfg.migration_threshold,
                                    adapter_emit_local,
                                    adapter,
                                    &res) != SUBOWN_OK) {
        return -1;
    }

    out->reason = (uint8_t)res.reason;
    out->peer_owner = slot->rec.owner_box;

    switch (res.decision) {
        case SUBOWN_DECISION_PROCESS_LOCAL:
            out->action = OWN_ACT_PROCESS_LOCAL;
            return 0;
        case SUBOWN_DECISION_FORWARD_REMOTE:
            if (adapter->cfg.mode == OWN_MODE_ENFORCE) {
                out->action = OWN_ACT_FORWARD_REMOTE;
            } else {
                if (push_event(adapter, subscriber_id, OWN_EVT_MISROUTE, now_tsc) == 0) {
                    adapter->metrics.own_worker_events_total++;
                }
                out->action = OWN_ACT_PROCESS_LOCAL;
            }
            return 0;
        case SUBOWN_DECISION_TRIGGER_MIGRATION:
            if (push_event(adapter, subscriber_id, OWN_EVT_MIGRATION_HINT, now_tsc) == 0) {
                adapter->metrics.own_worker_events_total++;
            }
            adapter->metrics.own_migration_trigger_total++;
            if (adapter->cfg.mode == OWN_MODE_ENFORCE) {
                out->action = OWN_ACT_TRIGGER_MIGRATION;
            } else {
                out->action = OWN_ACT_PROCESS_LOCAL;
            }
            return 0;
        case SUBOWN_DECISION_EMIT_LOCAL_UPDATE:
            if (push_event(adapter, subscriber_id, OWN_EVT_UNOWNED_CLAIM, now_tsc) == 0) {
                adapter->metrics.own_worker_events_total++;
            }
            if (adapter->cfg.mode == OWN_MODE_ENFORCE) {
                out->action = OWN_ACT_CLAIM_EMITTED;
            } else {
                out->action = OWN_ACT_PROCESS_LOCAL;
            }
            return 0;
        default:
            out->action = OWN_ACT_NOOP;
            return 0;
    }
}

int ownership_adapter_on_remote_msg(ownership_adapter_t *adapter,
                                    const uint8_t *msg,
                                    size_t len)
{
    struct own_slot *slot;
    subown_update_t in;
    subown_result_t out;

    if (!adapter || !msg) {
        return -1;
    }

    if (subown_zmq_decode_update_v1(msg, len, &in) != SUBOWN_OK) {
        adapter->metrics.own_zmq_recv_err_total++;
        return -1;
    }

    slot = lookup_slot(adapter, in.subscriber_id, 1);
    if (!slot) {
        return -1;
    }

    if (subown_apply_remote_update(&slot->rec, &in, &out) != SUBOWN_OK) {
        adapter->metrics.own_zmq_recv_err_total++;
        return -1;
    }

    switch (out.reason) {
        case SUBOWN_REASON_DROP_STALE_EPOCH:
        case SUBOWN_REASON_DROP_STALE_GENERATION:
            adapter->metrics.own_stale_drop_total++;
            break;
        case SUBOWN_REASON_DROP_DUPLICATE:
            adapter->metrics.own_dup_drop_total++;
            break;
        case SUBOWN_REASON_WIN_TIE_BREAK_BOX_ID:
        case SUBOWN_REASON_KEEP_LOCAL_TIE_BREAK:
            adapter->metrics.own_conflict_total++;
            break;
        default:
            break;
    }
    adapter->metrics.own_apply_remote_total++;

    return 0;
}

int ownership_adapter_flush_outbound(ownership_adapter_t *adapter)
{
    int flushed = 0;

    if (!adapter) {
        return -1;
    }

    while (adapter->out_head != adapter->out_tail) {
        adapter->out_head = next_idx(adapter->out_head, OWN_OUTBOUND_CAP);
        flushed++;
    }

    return flushed;
}

int ownership_adapter_on_peer_down(ownership_adapter_t *adapter,
                                   uint8_t peer_box_id)
{
    uint32_t i;

    if (!adapter) {
        return -1;
    }

    for (i = 0; i < OWN_RECORD_CAP; ++i) {
        if (!adapter->table[i].used) {
            continue;
        }
        if (adapter->table[i].rec.owner_box == peer_box_id) {
            adapter->table[i].rec.owner_box = SUBOWN_UNOWNED_STATE;
            adapter->table[i].rec.owner_box_id = SUBOWN_UNOWNED_STATE;
            adapter->table[i].rec.state = SUBOWN_STATE_UNOWNED;
            adapter->table[i].rec.remote_packet_counter = 0;
        }
    }

    return 0;
}

int ownership_adapter_on_aging_tick(ownership_adapter_t *adapter,
                                    uint64_t now_tsc)
{
    (void)now_tsc;
    if (!adapter) {
        return -1;
    }
    return 0;
}

int ownership_adapter_push_event(ownership_adapter_t *adapter,
                                 const own_ring_event_t *evt)
{
    uint32_t n;

    if (!adapter || !evt) {
        return -1;
    }

    n = next_idx(adapter->q_tail, OWN_EVENT_Q_CAP);
    if (n == adapter->q_head) {
        adapter->metrics.own_worker_ring_drop_total++;
        return -1;
    }

    adapter->q[adapter->q_tail] = *evt;
    adapter->q_tail = n;
    return 0;
}

int ownership_adapter_pop_event(ownership_adapter_t *adapter,
                                own_ring_event_t *evt,
                                int *has_event)
{
    if (!adapter || !evt || !has_event) {
        return -1;
    }

    if (adapter->q_head == adapter->q_tail) {
        *has_event = 0;
        return 0;
    }

    *evt = adapter->q[adapter->q_head];
    adapter->q_head = next_idx(adapter->q_head, OWN_EVENT_Q_CAP);
    *has_event = 1;
    return 0;
}

int ownership_adapter_get_metrics(const ownership_adapter_t *adapter,
                                  own_adapter_metrics_t *out_metrics)
{
    if (!adapter || !out_metrics) {
        return -1;
    }
    *out_metrics = adapter->metrics;
    return 0;
}
