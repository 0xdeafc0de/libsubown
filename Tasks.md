# Distributed Subscriber Ownership: Tasks and Subtasks (Library-First + TDD)

## 1. Scope and Success Criteria
- Goal: build ownership handling as an independent library first, then integrate into engine later.
- Constraint: no direct dependency on DPDK/engine internals in core ownership logic.
- Success criteria:
  - Steady-state decisions support traffic-local ownership.
  - Conflict-safe ownership convergence across 2 nodes.
  - Deterministic behavior under races, reorder, duplicates.
  - Full test suite passing before engine integration.

### Subtasks
- Define measurable acceptance criteria for library behavior.
- Define non-goals for v1 (for example: no full flow-state replication).
- Define API stability expectations for integration phase.

## 2. Architecture and Packaging for Ownership Library
- Create standalone library module (`libsubown` or equivalent).
- Separate into:
  - Pure state machine core (deterministic, side-effect free where possible).
  - Adapter layer for time/source of truth/event transport.
- Keep transport optional via interface/trait abstraction.

### Subtasks
- Define module layout (`core`, `model`, `sync`, `store`, `api`, `tests`).
- Define build outputs (static lib/shared lib + headers or language package).
- Define semantic versioning and changelog approach.

## 3. Ownership Domain Model
- Model entities:
  - `SubscriberId`
  - `OwnerNodeId`
  - `Generation`
  - `OwnerEpoch`
  - `LastSeen`
  - `State` (`UNOWNED`, `LOCAL`, `REMOTE`, `MIGRATING`)
- Model events:
  - `PacketSeenLocal`
  - `RemoteOwnerUpdate`
  - `PeerDown`
  - `PeerUp`
  - `AgingTick`

### Subtasks
- Define schema with explicit invariants.
- Define serialization format for ownership update messages.
- Define backward-compatible message version field.

## 4. Public API Definition (Engine-Agnostic)
- API should expose deterministic operations:
  - `on_packet(subscriber_id, local_node, now)`
  - `on_remote_update(update_msg)`
  - `on_peer_state_change(node, up_down, epoch)`
  - `on_aging_tick(now)`
  - `get_owner(subscriber_id)`
- API should return decisions/actions:
  - `PROCESS_LOCAL`
  - `FORWARD_REMOTE(node)`
  - `EMIT_OWNERSHIP_UPDATE(msg)`
  - `NOOP`

### Subtasks
- Freeze v1 API signatures.
- Define error model and validation failures.
- Add interface contract doc with examples.

## 5. Conflict Resolution Rules
- Deterministic winner policy:
  - Higher `generation` wins.
  - If equal generation, tie-break by deterministic node-id order.
  - `owner_epoch` prevents stale peer resurrection after restart/split-brain.

### Subtasks
- Encode rules in one central resolver function.
- Add strict stale-update rejection logic.
- Add explicit conflict outcome enum for observability/tests.

## 6. Ownership Claim and Migration Logic
- Implement first-packet claim from `UNOWNED` to `LOCAL`.
- Implement remote-hit tracking and migration thresholds.
- Implement aging transition to `UNOWNED` when idle timeout reached.

### Subtasks
- Define threshold knobs and defaults.
- Define hysteresis/dampening to avoid ownership flapping.
- Define migration action generation rules.

## 7. Storage Abstraction
- Keep storage pluggable (in-memory map for tests; engine-backed adapter later).
- No lock model assumptions in core API (caller controls threading model).

### Subtasks
- Define `OwnershipStore` interface.
- Implement deterministic in-memory store for TDD.
- Add capacity, eviction, and memory-pressure behavior spec.

## 8. Sync Transport Abstraction
- Library emits/consumes ownership update messages, but does not own sockets.
- Transport implementation deferred to adapter/integration layer.

### Subtasks
- Define `OwnershipUpdate` wire contract.
- Define idempotency keys (`subscriber_id`, `generation`, `owner_epoch`).
- Define retry/reorder tolerance expectations.

## 9. TDD Plan (Red-Green-Refactor)
- Implement feature slices through tests first.

### Subtasks
- Write failing unit tests for:
  - First-packet claim.
  - Local vs remote decision.
  - Conflict resolution permutations.
  - Stale/duplicate/reordered update handling.
  - Peer down takeover.
  - Aging to unowned.
- Implement minimal code to pass each test.
- Refactor while keeping tests green.
- Add property-based tests for invariants:
  - Single winner convergence.
  - Monotonic generation behavior.
  - Idempotent re-application of same update.

## 10. Test Harness and Tooling
- Build standalone test harness independent of engine runtime.
- Include deterministic virtual clock and deterministic event runner.

### Subtasks
- Add fixture builders for subscribers/nodes/events.
- Add simulation tests for 2-node race timelines.
- Add fuzz tests for random event orderings.
- Add coverage gate and CI pass criteria.

## 11. Non-Functional Validation
- Validate performance of ownership decisions in isolation.
- Validate memory footprint under high subscriber counts.

### Subtasks
- Add microbenchmarks for hot APIs.
- Add scale tests (millions of ownership entries).
- Add latency budget checks per operation.

## 12. Observability in Library
- Emit counters and debug-friendly decision traces via callback hooks.

### Subtasks
- Define metrics surface:
  - claims
  - remote forwards requested
  - conflicts
  - stale updates dropped
  - migrations
  - aging transitions
- Define structured trace/event hooks for integration.

## 13. Engine Integration (Post Feature-Complete Library)
- Integrate library behind adapter layer in PB/CP paths.
- Keep integration thin and reversible via feature flags.

### Subtasks
- Implement engine adapter for state store and sync transport.
- Wire `PROCESS_LOCAL`/`FORWARD_REMOTE` decisions into PB flow.
- Wire ownership updates to existing cluster control channel.
- Add integration tests with mocked engine wrappers.

## 14. Rollout Strategy
- Phase 1: shadow mode (library computes decisions, engine behavior unchanged).
- Phase 2: canary enforce mode for subset traffic.
- Phase 3: full enforce mode and static owner deprecation.

### Subtasks
- Define rollout config flags.
- Define rollback triggers (CF ratio, conflict spikes, latency regression).
- Define operational runbook and dashboards.

## 15. Deliverables Checklist
- Ownership library code (feature complete).
- API/spec documentation.
- Passing TDD suite + property tests + benchmarks.
- Integration adapter plan and test plan.
- Rollout and rollback playbook.
