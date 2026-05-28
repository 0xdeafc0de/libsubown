#ifndef SUBOWN_H
#define SUBOWN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBOWN_MSG_VERSION_1 1u

/* Spec-aligned ownership constants */
#define SUBOWN_BOX0_ID 0x00u
#define SUBOWN_BOX1_ID 0x01u
#define SUBOWN_UNOWNED_STATE 0xFFu

typedef enum {
    SUBOWN_STATE_UNOWNED = 0,
    SUBOWN_STATE_LOCAL,
    SUBOWN_STATE_REMOTE,
    SUBOWN_STATE_MIGRATING
} subown_state_t;

typedef enum {
    SUBOWN_DECISION_APPLY_REMOTE = 0,
    SUBOWN_DECISION_KEEP_LOCAL,
    SUBOWN_DECISION_NOOP,
    SUBOWN_DECISION_EMIT_LOCAL_UPDATE,
    SUBOWN_DECISION_PROCESS_LOCAL,
    SUBOWN_DECISION_FORWARD_REMOTE,
    SUBOWN_DECISION_TRIGGER_MIGRATION
} subown_decision_t;

typedef enum {
    SUBOWN_REASON_WIN_HIGHER_GENERATION = 0,
    SUBOWN_REASON_WIN_TIE_BREAK_BOX_ID,
    SUBOWN_REASON_DROP_STALE_GENERATION,
    SUBOWN_REASON_DROP_DUPLICATE,
    SUBOWN_REASON_KEEP_LOCAL_TIE_BREAK,
    SUBOWN_REASON_WIN_NEWER_EPOCH,
    SUBOWN_REASON_DROP_STALE_EPOCH,
    SUBOWN_REASON_INVALID_MESSAGE,
    SUBOWN_REASON_LOCAL_CLAIM,
    SUBOWN_REASON_LOCAL_REFRESH,
    SUBOWN_REASON_LOCAL_HIT,
    SUBOWN_REASON_REMOTE_MISROUTE,
    SUBOWN_REASON_REMOTE_MIGRATION_THRESHOLD,
    SUBOWN_REASON_UNOWNED_CLAIM
} subown_reason_t;

typedef enum {
    SUBOWN_OK = 0,
    SUBOWN_ERR_INVALID_ARGUMENT = -1,
    SUBOWN_ERR_CALLBACK_FAILED = -2
} subown_status_t;

typedef struct {
    uint64_t subscriber_id;
    uint32_t generation;

    /* Spec-native fields */
    uint8_t owner_box;               /* 0x00 box0, 0x01 box1, 0xFF unowned */
    uint8_t reserved[3];
    uint32_t policy_mask;
    uint64_t activation_ts;
    uint64_t last_seen_ts;
    uint64_t remote_packet_counter;

    /* Backward compatibility fields for existing bench/sim code */
    uint16_t owner_box_id;
    uint64_t timestamp;
    uint32_t owner_epoch;

    subown_state_t state;
} subown_record_t;

typedef struct {
    uint16_t msg_version;
    uint64_t subscriber_id;

    /* Spec-native fields */
    uint32_t generation;
    uint8_t owner_box;
    uint8_t reserved[3];
    uint64_t activation_ts;
    uint32_t policy_mask;

    /* Backward compatibility fields */
    uint16_t owner_box_id;
    uint64_t timestamp;
    uint32_t owner_epoch;
} subown_update_t;

typedef struct {
    subown_decision_t decision;
    subown_reason_t reason;
} subown_result_t;

typedef subown_status_t (*subown_emit_update_fn)(const subown_update_t *update,
                                                 void *user_ctx);

subown_status_t subown_apply_remote_update(subown_record_t *local,
                                           const subown_update_t *incoming,
                                           subown_result_t *out);

subown_status_t subown_claim_local(subown_record_t *record,
                                   uint16_t local_box_id,
                                   uint64_t now,
                                   subown_emit_update_fn emitter,
                                   void *user_ctx,
                                   subown_result_t *out);

subown_status_t subown_process_packet_event(subown_record_t *record,
                                            uint8_t local_box_id,
                                            uint64_t now,
                                            uint64_t migration_threshold,
                                            subown_emit_update_fn emitter,
                                            void *user_ctx,
                                            subown_result_t *out);

#ifdef __cplusplus
}
#endif

#endif
