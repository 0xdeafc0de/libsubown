# Ownership Adapter Contract (P1.1)

This document defines the integration contract between DDI engine packet/control paths and `libsubown`.

References:
- Library root: `/Volumes/V23/ws/yuvo/projects/dist_sub_ownership/libsubown`
- Engine reference path: `/Users/sspingal/ws/yuvo/DDI_ENGINE`

## 1. Integration Goals
- Keep worker fast path non-blocking.
- Keep ownership conflict logic centralized in control shards.
- Allow phased rollout (`disabled -> observe -> enforce`).

## 2. Adapter Boundaries

## 2.1 Worker-side adapter (PB/DP hot path)
- Input:
  - subscriber key (`sub_id` or mapped key)
  - local node id
  - packet timestamp/cycle
- Output action:
  - `PROCESS_LOCAL`
  - `FORWARD_REMOTE(peer)`
  - `TRIGGER_MIGRATION(subscriber)`
  - `CLAIM_EMITTED` (for unowned first packet)
- Behavior:
  - no socket I/O
  - no blocking locks
  - bounded per-packet instructions

## 2.2 Control-side adapter (CP threads)
- Receives:
  - worker ownership events via lockless ring(s)
  - peer ownership updates from ZMQ channel
- Performs:
  - `subown_apply_remote_update(...)`
  - migration decision/event handling
  - replication emit through `subown_zmq_emit_update(...)`

## 3. Mode Flags
- `ownership_mode=disabled`
  - current behavior unchanged
- `ownership_mode=observe`
  - compute ownership decisions and metrics only
  - dataplane forwarding behavior unchanged
- `ownership_mode=enforce`
  - dataplane applies library decision actions

Optional scoped flags:
- `ownership_canary_subnet`
- `ownership_canary_subscriber_ids`

## 4. Threading/Shard Contract
- Suggested start:
  - 8 RSS / 40 workers: 4 ownership control shards + 1 ZMQ I/O thread
  - 16 RSS / 80 workers: 8 ownership control shards + 2 ZMQ I/O threads
- Routing rule:
  - `shard_id = subscriber_id % ownership_shard_count`
- Ownership table write rule:
  - single-writer per shard

## 5. Data Mapping Contract
Map DDI subscriber node/session fields to `libsubown` record fields:
- `subscriber_id` -> `subown_record_t.subscriber_id`
- `node_id` -> `owner_box`
- lifecycle/update timestamps -> `activation_ts`, `last_seen_ts`
- policy reference -> `policy_mask`

Compatibility note:
- adapter should use spec-native fields (`owner_box`, `activation_ts`, `policy_mask`) and ignore compatibility fields where possible.

## 6. Worker API Contract
Recommended wrapper (engine-local):

```c
typedef enum {
  OWN_ACT_PROCESS_LOCAL,
  OWN_ACT_FORWARD_REMOTE,
  OWN_ACT_TRIGGER_MIGRATION,
  OWN_ACT_NOOP
} own_action_t;

typedef struct {
  own_action_t action;
  uint8_t peer_owner;
  uint8_t reason;
} own_result_t;

int ownership_adapter_on_packet(uint64_t subscriber_id,
                                uint8_t local_box_id,
                                uint64_t now_tsc,
                                own_result_t *out);
```

Semantics:
- must complete without blocking
- must not call ZMQ directly in worker context
- may enqueue events to control ring

## 7. Control API Contract
Recommended wrappers:

```c
int ownership_adapter_on_remote_msg(const uint8_t *msg, size_t len);
int ownership_adapter_flush_outbound(void);
int ownership_adapter_on_peer_down(uint8_t peer_box_id);
int ownership_adapter_on_aging_tick(uint64_t now_tsc);
```

Semantics:
- safe to run on dedicated CP/control cores
- can perform batching/coalescing before outbound sends

## 8. Ring Interface Contract
- Worker -> control ring item fields:
  - `subscriber_id`
  - `event_type` (`MISROUTE`, `UNOWNED_CLAIM`, `MIGRATION_HINT`)
  - `local_box_id`
  - `tsc`
- Ring requirements:
  - lockless MPSC preferred
  - bounded capacity + drop counters

## 9. Metrics Contract
Minimum counters to expose from adapter:
- `own_worker_events_total`
- `own_worker_ring_drop_total`
- `own_apply_remote_total`
- `own_conflict_total`
- `own_stale_drop_total`
- `own_dup_drop_total`
- `own_migration_trigger_total`
- `own_zmq_recv_err_total`
- `own_zmq_send_err_total`

Per-mode deltas:
- packet latency p99 delta (`observe` vs `disabled`, `enforce` vs `disabled`)
- worker cycles/packet delta

## 10. Failure/Recovery Hooks
- Peer down:
  - trigger takeover sweep hook in control thread
- Peer up:
  - enable dampened reconciliation window
- Split/race:
  - rely on generation + tie-break + epoch semantics already in library

## 11. Integration Phases
1. Phase A: compile/link adapter; `ownership_mode=disabled`
2. Phase B: `observe` mode in canary traffic; validate counters/latency deltas
3. Phase C: partial `enforce` canary
4. Phase D: full `enforce` rollout with rollback thresholds

## 12. Acceptance Criteria (P1.1)
- Adapter compiles and links into engine build.
- `observe` mode runs with no packet path blocking regressions.
- Worker->control ring and ZMQ control path stable (no unbounded queue growth).
- Deterministic actions for tie-break, duplicate, stale, and migration-threshold paths.
