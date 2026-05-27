# Conflict Resolution Spec (TDD)

## Scope
This spec defines test-first requirements for Milestone A:
- async replication contract
- deterministic conflict resolution
- convergence under duplicate/reordered delivery

Reference rule source:
- [conflict_resolution_scheme.md](/Volumes/V23/ws/yuvo/projects/dist_sub_ownership/conflict_resolution_scheme.md)

## Canonical Precedence Rules
1. Higher `generation` wins.
2. If `generation` is equal, lower physical `box_id` wins.
3. Losing side must yield and mark owner as remote.
4. Duplicate messages must be idempotent.
5. Reordered delivery must still converge to one deterministic final owner.

## Proposed Core Types (for tests)
- `OwnershipRecord`:
  - `subscriber_id: u64`
  - `owner_box_id: u16`
  - `generation: u32`
  - `timestamp: u64`
  - `owner_epoch: u32`
  - `state: LOCAL|REMOTE|UNOWNED|MIGRATING`
- `OwnershipUpdate`:
  - `subscriber_id: u64`
  - `owner_box_id: u16`
  - `generation: u32`
  - `timestamp: u64`
  - `owner_epoch: u32`
  - `msg_version: u16`
- `ResolverDecision`:
  - `APPLY_REMOTE`
  - `KEEP_LOCAL`
  - `NOOP`

## Test Groups

## Group A: Pure Resolver Unit Tests

### A1. Accept higher generation from remote
- Given local `(gen=5, owner=1)`
- And incoming `(gen=6, owner=0)`
- Expect decision `APPLY_REMOTE`
- Expect final owner `0`, generation `6`

### A2. Reject lower generation as stale
- Given local `(gen=10, owner=0)`
- And incoming `(gen=9, owner=1)`
- Expect decision `KEEP_LOCAL`
- Expect local unchanged

### A3. Tie on generation picks lower box id
- Given local `(gen=11, owner=1)`
- And incoming `(gen=11, owner=0)`
- Expect decision `APPLY_REMOTE`
- Expect final owner `0`

### A4. Tie on generation keeps local if local box id is lower
- Given local `(gen=11, owner=0)`
- And incoming `(gen=11, owner=1)`
- Expect decision `KEEP_LOCAL`
- Expect owner remains `0`

### A5. Exact duplicate update is idempotent
- Given local equal to incoming on all conflict keys
- Apply incoming twice
- Expect first apply possibly `NOOP` or `APPLY_REMOTE` (implementation-defined)
- Expect second apply `NOOP`
- Expect final record unchanged after second apply

### A6. Wrong subscriber id rejected
- Given local `subscriber_id=A`
- Incoming `subscriber_id=B`
- Expect validation error and no mutation

### A7. Unsupported message version rejected
- Incoming `msg_version` unsupported
- Expect validation error and no mutation

## Group B: Async Replication Contract Tests

### B1. Emit on local winning claim
- Given local claim event creates/bumps ownership
- Expect one outbound `OwnershipUpdate` via `emit_update`
- Assert message contains new owner and generation

### B2. No blocking behavior in core API
- Use mocked emitter with forced delay/backpressure simulation
- Core API should return without transport wait
- Verify emitted events are queued/callbacked without blocking decision path

### B3. Duplicate inbound messages remain safe
- Apply same inbound update N times
- Final state identical to single apply
- Conflict/drop counters reflect duplicates

### B4. Reordered inbound delivery converges
- Apply updates in order U3, U1, U2 where logical order is U1->U2->U3
- Final state must equal ordered-apply final state

## Group C: Two-Node Race Simulations

### C1. Simultaneous claim, same generation
- Node0 local claim `(gen=1, owner=0)`
- Node1 local claim `(gen=1, owner=1)`
- Exchange updates both ways
- Expect both nodes converge to owner `0`
- Node1 must record yield event

### C2. Simultaneous claim, different generations
- Node0 claim `(gen=2, owner=0)`
- Node1 claim `(gen=3, owner=1)`
- Exchange updates
- Expect both converge to owner `1`, generation `3`

### C3. Delayed old update after convergence
- Converge both to `(gen=8, owner=0)`
- Inject delayed old update `(gen=7, owner=1)`
- Expect ignored stale update on both

### C4. Duplicate and reorder chaos run
- For one subscriber, replay a bag of updates with duplicates/random order
- Expect same final owner across both nodes
- Expect convergence within bounded steps

## Group D: Property-Based Tests

### D1. Deterministic convergence
- Generate random permutations of the same valid update multiset
- Apply to two fresh nodes
- Final canonical state must match

### D2. Monotonic accepted generation
- Across any update sequence, accepted generation never decreases

### D3. Idempotency
- Applying sequence S then S again yields same final state as one pass

### D4. Tie-break determinism
- For equal generation contenders, winner always min(`box_id`)

## Required Test Instrumentation
- Virtual deterministic clock.
- Structured decision reason codes:
  - `WIN_HIGHER_GENERATION`
  - `WIN_TIE_BREAK_BOX_ID`
  - `DROP_STALE_GENERATION`
  - `DROP_DUPLICATE`
  - `INVALID_MESSAGE`
- Counters:
  - `conflict_total`
  - `conflict_won_total`
  - `conflict_lost_total`
  - `stale_drop_total`
  - `duplicate_drop_total`
  - `apply_remote_total`
  - `keep_local_total`

## Definition of Done for Milestone A
1. All Group A/B/C tests pass.
2. Property tests (Group D) pass with agreed run count/seed set.
3. No flaky tests across repeated CI runs.
4. Resolver behavior documented with examples from A1-A4.

## Suggested Test File Split (implementation hint)
- `tests/unit/test_resolver_precedence.*`
- `tests/unit/test_resolver_validation.*`
- `tests/unit/test_replication_contract.*`
- `tests/sim/test_two_node_races.*`
- `tests/property/test_convergence_props.*`
