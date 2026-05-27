# Phase 1 Plan (Library-First, TDD-First)

## Phase 1 Objective
Deliver a standalone ownership library core where async replication and conflict resolution are correct, deterministic, and proven by tests before enabling dynamic ownership behavior.

## Hard Rule for Phase 1
No downstream feature (first-packet claim, migration, aging, engine adapter) is allowed to proceed until async replication + conflict resolution test gates are green.

## Milestone Order
1. Milestone A (Blocking): Async replication contract + conflict resolution correctness.
2. Milestone B: Ownership state machine using the validated conflict/replication core.
3. Milestone C: Packaging, observability, and integration-ready API freeze.

## Milestone A (Blocking): Async Replication + Conflict Resolution
Aligned to [conflict_resolution_scheme.md](/Volumes/V23/ws/yuvo/projects/dist_sub_ownership/conflict_resolution_scheme.md):
- Winner rule:
  - Higher `generation` wins.
  - If same generation, lower physical `box_id` wins.
- Loser must yield and move local entry to remote owner.
- Transport model: async, non-blocking replication (transport-agnostic in library; ZeroMQ-friendly adapter contract).

### A1. Domain and Wire Contract (TDD)
- Define `OwnershipUpdate` message:
  - `subscriber_id`, `owner_box_id`, `generation (u32)`, `timestamp (u64)`, `owner_epoch`, `msg_version`.
- Define idempotency key and stale-update rules.
- Define validation errors for malformed/out-of-range messages.

### A2. Conflict Resolver Core (TDD)
- Implement single pure resolver function:
  - Inputs: local record + incoming update.
  - Outputs: decision (`APPLY_REMOTE`, `KEEP_LOCAL`, `NOOP`) + reason.
- Encode deterministic precedence from scheme doc.
- Add explicit loser-yield behavior and state transition output.

### A3. Async Replication Engine Contract (TDD)
- Define library interfaces:
  - outbound: `emit_update(update)` callback
  - inbound: `on_remote_update(update)`
- Guarantee non-blocking behavior from core API perspective.
- Ensure duplicate/reordered delivery convergence.

### A4. Blocking Test Gate (must pass before Milestone B)
- Unit tests:
  - higher generation wins
  - same generation -> lower box id wins
  - stale generation rejected
  - duplicate update idempotent
  - reordered updates converge to same final owner
  - loser yield action emitted
- Property tests:
  - deterministic convergence under random message order
  - monotonic accepted-generation invariant
- Race simulation tests (2-node):
  - simultaneous claim with same generation
  - simultaneous claim with different generations

## Milestone B: Ownership State Machine (depends on A)

### B1. State Model
- States: `UNOWNED`, `LOCAL`, `REMOTE`, `MIGRATING`.
- Events: local packet seen, remote update, peer down/up, aging tick.

### B2. Decision API
- `on_packet(...)` returns one of:
  - `PROCESS_LOCAL`
  - `FORWARD_REMOTE(node)`
  - `EMIT_OWNERSHIP_UPDATE`
  - `NOOP`

### B3. TDD Coverage
- First-packet claim from `UNOWNED`.
- Local/remote forwarding decisions.
- Conflict-driven transitions using Milestone A resolver.

## Milestone C: Integration-Ready Library Finish

### C1. Packaging
- Build standalone library artifact + API docs.
- Keep engine-independent in-memory store as default.

### C2. Observability
- Counters:
  - conflicts detected
  - wins/losses by reason
  - stale dropped
  - duplicates ignored
  - updates emitted/applied

### C3. API Freeze for Engine Integration
- Freeze v1 signatures and message schema.
- Publish integration notes for engine adapter phase.

## Suggested 2-Week Execution Slice

## Week 1
1. Day 1: Finalize message schema + resolver spec + acceptance tests list.
2. Day 2: Write failing tests for all conflict precedence and idempotency cases.
3. Day 3: Implement resolver minimal code to pass core unit tests.
4. Day 4: Add reorder/duplicate simulation tests; implement convergence fixes.
5. Day 5: Add property tests and close Milestone A blocking gate.

## Week 2
1. Day 6: Implement basic state model + `on_packet` API (using resolver from A).
2. Day 7: Add tests for `UNOWNED/LOCAL/REMOTE` transitions.
3. Day 8: Add `MIGRATING` and peer event handling tests.
4. Day 9: Add metrics hooks + docs; stabilize API.
5. Day 10: Package library artifact + publish Phase 1 completion report.

## Phase 1 Exit Criteria
- Milestone A tests all pass (mandatory blocker cleared).
- Library passes full unit + property + 2-node simulation suite.
- Public API and wire contract documented and frozen for integration.
