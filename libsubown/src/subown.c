#include "subown.h"

static uint8_t subown_normalize_owner(uint16_t owner_box_id, uint8_t owner_box)
{
    if (owner_box_id == SUBOWN_BOX0_ID || owner_box_id == SUBOWN_BOX1_ID || owner_box_id == SUBOWN_UNOWNED_STATE) {
        return (uint8_t)owner_box_id;
    }
    if (owner_box == SUBOWN_BOX0_ID || owner_box == SUBOWN_BOX1_ID || owner_box == SUBOWN_UNOWNED_STATE) {
        return owner_box;
    }
    return SUBOWN_UNOWNED_STATE;
}

static uint64_t subown_normalize_ts(uint64_t activation_ts, uint64_t timestamp)
{
    return activation_ts != 0 ? activation_ts : timestamp;
}

static int subown_validate(const subown_record_t *local,
                           const subown_update_t *incoming) {
    if (!local || !incoming) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }
    if (incoming->msg_version != SUBOWN_MSG_VERSION_1) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }
    if (incoming->subscriber_id != local->subscriber_id) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }
    return SUBOWN_OK;
}

subown_status_t subown_apply_remote_update(subown_record_t *local,
                                           const subown_update_t *incoming,
                                           subown_result_t *out) {
    uint8_t incoming_owner;
    uint64_t incoming_ts;

    if (!out) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }
    if (subown_validate(local, incoming) != SUBOWN_OK) {
        out->decision = SUBOWN_DECISION_NOOP;
        out->reason = SUBOWN_REASON_INVALID_MESSAGE;
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    incoming_owner = subown_normalize_owner(incoming->owner_box_id, incoming->owner_box);
    incoming_ts = subown_normalize_ts(incoming->activation_ts, incoming->timestamp);

    if (incoming->owner_epoch > local->owner_epoch) {
        local->owner_epoch = incoming->owner_epoch;
        local->owner_box = incoming_owner;
        local->owner_box_id = incoming_owner;
        local->generation = incoming->generation;
        local->activation_ts = incoming_ts;
        local->timestamp = incoming_ts;
        local->last_seen_ts = incoming_ts;
        local->policy_mask = incoming->policy_mask;
        local->state = (incoming_owner == SUBOWN_UNOWNED_STATE) ? SUBOWN_STATE_UNOWNED : SUBOWN_STATE_REMOTE;
        out->decision = SUBOWN_DECISION_APPLY_REMOTE;
        out->reason = SUBOWN_REASON_WIN_NEWER_EPOCH;
        return SUBOWN_OK;
    }

    if (incoming->owner_epoch < local->owner_epoch) {
        out->decision = SUBOWN_DECISION_KEEP_LOCAL;
        out->reason = SUBOWN_REASON_DROP_STALE_EPOCH;
        return SUBOWN_OK;
    }

    if (incoming->generation > local->generation) {
        local->owner_box = incoming_owner;
        local->owner_box_id = incoming_owner;
        local->generation = incoming->generation;
        local->activation_ts = incoming_ts;
        local->timestamp = incoming_ts;
        local->last_seen_ts = incoming_ts;
        local->policy_mask = incoming->policy_mask;
        local->state = (incoming_owner == SUBOWN_UNOWNED_STATE) ? SUBOWN_STATE_UNOWNED : SUBOWN_STATE_REMOTE;
        out->decision = SUBOWN_DECISION_APPLY_REMOTE;
        out->reason = SUBOWN_REASON_WIN_HIGHER_GENERATION;
        return SUBOWN_OK;
    }

    if (incoming->generation < local->generation) {
        out->decision = SUBOWN_DECISION_KEEP_LOCAL;
        out->reason = SUBOWN_REASON_DROP_STALE_GENERATION;
        return SUBOWN_OK;
    }

    if (incoming_owner == local->owner_box) {
        out->decision = SUBOWN_DECISION_NOOP;
        out->reason = SUBOWN_REASON_DROP_DUPLICATE;
        return SUBOWN_OK;
    }

    if (incoming_owner < local->owner_box) {
        local->owner_box = incoming_owner;
        local->owner_box_id = incoming_owner;
        local->activation_ts = incoming_ts;
        local->timestamp = incoming_ts;
        local->last_seen_ts = incoming_ts;
        local->policy_mask = incoming->policy_mask;
        local->state = (incoming_owner == SUBOWN_UNOWNED_STATE) ? SUBOWN_STATE_UNOWNED : SUBOWN_STATE_REMOTE;
        out->decision = SUBOWN_DECISION_APPLY_REMOTE;
        out->reason = SUBOWN_REASON_WIN_TIE_BREAK_BOX_ID;
        return SUBOWN_OK;
    }

    out->decision = SUBOWN_DECISION_KEEP_LOCAL;
    out->reason = SUBOWN_REASON_KEEP_LOCAL_TIE_BREAK;
    return SUBOWN_OK;
}

subown_status_t subown_claim_local(subown_record_t *record,
                                   uint16_t local_box_id,
                                   uint64_t now,
                                   subown_emit_update_fn emitter,
                                   void *user_ctx,
                                   subown_result_t *out) {
    subown_update_t update;
    uint8_t local_owner = (uint8_t)local_box_id;

    if (!record || !out || !emitter) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    if (record->owner_box == local_owner && record->state == SUBOWN_STATE_LOCAL) {
        record->last_seen_ts = now;
        out->decision = SUBOWN_DECISION_NOOP;
        out->reason = SUBOWN_REASON_LOCAL_REFRESH;
        return SUBOWN_OK;
    }

    record->owner_box = local_owner;
    record->owner_box_id = local_owner;
    record->generation += 1u;
    record->activation_ts = now;
    record->timestamp = now;
    record->last_seen_ts = now;
    record->remote_packet_counter = 0;
    record->state = SUBOWN_STATE_LOCAL;

    update.msg_version = SUBOWN_MSG_VERSION_1;
    update.subscriber_id = record->subscriber_id;
    update.generation = record->generation;
    update.owner_box = record->owner_box;
    update.owner_box_id = record->owner_box;
    update.activation_ts = record->activation_ts;
    update.timestamp = record->activation_ts;
    update.policy_mask = record->policy_mask;
    update.owner_epoch = record->owner_epoch;

    if (emitter(&update, user_ctx) != SUBOWN_OK) {
        return SUBOWN_ERR_CALLBACK_FAILED;
    }

    out->decision = SUBOWN_DECISION_EMIT_LOCAL_UPDATE;
    out->reason = SUBOWN_REASON_LOCAL_CLAIM;
    return SUBOWN_OK;
}

subown_status_t subown_process_packet_event(subown_record_t *record,
                                            uint8_t local_box_id,
                                            uint64_t now,
                                            uint64_t migration_threshold,
                                            subown_emit_update_fn emitter,
                                            void *user_ctx,
                                            subown_result_t *out) {
    uint8_t remote_box_id;

    if (!record || !out) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }
    if (!(local_box_id == SUBOWN_BOX0_ID || local_box_id == SUBOWN_BOX1_ID)) {
        return SUBOWN_ERR_INVALID_ARGUMENT;
    }

    remote_box_id = (uint8_t)(local_box_id ^ 0x01u);

    if (record->owner_box == local_box_id) {
        record->last_seen_ts = now;
        out->decision = SUBOWN_DECISION_PROCESS_LOCAL;
        out->reason = SUBOWN_REASON_LOCAL_HIT;
        return SUBOWN_OK;
    }

    if (record->owner_box == remote_box_id) {
        uint64_t bad_pkts = ++record->remote_packet_counter;
        if (migration_threshold > 0 && bad_pkts > migration_threshold) {
            record->remote_packet_counter = 0;
            out->decision = SUBOWN_DECISION_TRIGGER_MIGRATION;
            out->reason = SUBOWN_REASON_REMOTE_MIGRATION_THRESHOLD;
            return SUBOWN_OK;
        }
        out->decision = SUBOWN_DECISION_FORWARD_REMOTE;
        out->reason = SUBOWN_REASON_REMOTE_MISROUTE;
        return SUBOWN_OK;
    }

    if (record->owner_box == SUBOWN_UNOWNED_STATE) {
        if (!emitter) {
            return SUBOWN_ERR_INVALID_ARGUMENT;
        }
        if (subown_claim_local(record, local_box_id, now, emitter, user_ctx, out) != SUBOWN_OK) {
            return SUBOWN_ERR_CALLBACK_FAILED;
        }
        out->reason = SUBOWN_REASON_UNOWNED_CLAIM;
        return SUBOWN_OK;
    }

    out->decision = SUBOWN_DECISION_FORWARD_REMOTE;
    out->reason = SUBOWN_REASON_REMOTE_MISROUTE;
    return SUBOWN_OK;
}
