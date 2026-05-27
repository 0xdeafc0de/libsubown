# Implementation Backlog (P0/P1/P2)

This backlog maps `Spec.md` gaps to concrete implementation work across current code modules.

Reference:
- Spec: /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/Spec.md
- Current library root: /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/libsubown

## P0 (Must-Have Before Engine Cut-In)

## P0.1 Spec Contract Normalization
- Goal: clean and freeze spec contract so coding targets are deterministic.
- Files:
  - /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/Spec.md
- Tasks:
  - Remove trailing non-spec prose/unmatched quote block.
  - Freeze normative constants (`UNOWNED_STATE`, migration threshold, ownership rules).
  - Freeze wire-format and endianness policy.
- Exit:
  - `Spec.md` is clean, machine-reviewable, and versioned.

## P0.2 Data Model Parity with Spec
- Goal: align library structs with required session/wire fields.
- Files:
  - /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/libsubown/include/subown.h
  - /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/libsubown/src/subown.c
- Tasks:
  - Add/align `policy_mask`, `activation_ts`, `last_seen_ts`, `remote_packet_counter`.
  - Align ownership state constants (`0/1/0xFF`) and generation semantics.
  - Keep compatibility layer for existing tests while transitioning.
- Exit:
  - Structs and transitions match spec-required fields/meaning.

## P0.3 Wire Protocol Hardening (ZMQ)
- Goal: make replication payload format explicit, stable, and validated.
- Files:
  - /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/libsubown/include/adapters/subown_zmq_adapter.h
  - /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/libsubown/adapters/zmq/subown_zmq_adapter.c
  - /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/libsubown/tests/test_zmq_adapter.c
- Tasks:
  - Replace implicit packed struct behavior with explicit encode/decode contract.
  - Add strict validation on length/version/field ranges.
  - Add negative tests: malformed/truncated/unknown-version payloads.
- Exit:
  - Versioned wire contract + tests for backward/forward safety.

## P0.4 Fast-Path Logic Completeness (Library Side)
- Goal: complete state machine semantics before engine integration.
- Files:
  - /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/libsubown/src/subown.c
  - /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/libsubown/tests/test_conflict_resolution.c
- Tasks:
  - Add remote-misroute counter logic and threshold-trigger migration signal.
  - Add `last_seen_ts` updates on local-hit path.
  - Add explicit actions for local/remote/unowned case outputs.
- Exit:
  - Library state machine covers CASE A/B/C behavior at logic level.

## P0.5 IT Assertions Coverage Expansion
- Goal: convert current scenarios into deterministic pass/fail gates.
- Files:
  - /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/libsubown/scripts/test_it_mobility.sh
  - /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/libsubown/scripts/test_it_epoch_restart.sh
  - /Volumes/V23/ws/yuvo/projects/dist_sub_ownership/libsubown/sim/box_sim.c
- Tasks:
  - Add assertions for duplicate/reorder/drop fault scenarios.
  - Add tie-break race scenario assertions.
  - Add expected counter assertions (drops/conflicts/noops).
- Exit:
  - CI-style integration test matrix for mobility, restart, and fault paths.

## P1 (Engine Integration Readiness)

## P1.1 DPDK Adapter Layer
- Goal: integrate library decisions into real packet path without blocking workers.
- Files (new planned in engine repo):
  - `src/.../ownership_adapter.*` (engine)
- Tasks:
  - Worker-side: key extract, ownership query/action, enqueue control events.
  - Control-side: apply updates, send replication, consume peer updates.
  - Maintain lockless worker path.
- Exit:
  - Engine can run with feature flag in `observe` mode.

## P1.2 NUMA-Aware Dual Table Implementation
- Goal: implement per-socket ownership maps with locality guarantees.
- Files (engine-side planned):
  - DP init + memory/socket config modules
- Tasks:
  - Create one table per NUMA socket.
  - Route worker lookups to local socket table only.
  - Replication apply updates both required local map instances safely.
- Exit:
  - NUMA-local access proven by counters and profiling.

## P1.3 Threading and Core Role Layout
- Goal: map control and sweeper roles per spec topology.
- Files:
  - Engine launch/config/runtime scripts (planned)
- Tasks:
  - Define CP, PMD, sweeper core sets.
  - Add deployment configs for pinning/isolation.
  - Add runtime verification output of pinning map.
- Exit:
  - Deterministic role-to-core mapping in runtime.

## P1.4 Sweeper + Failover Paths
- Goal: aging and failover transitions in integration context.
- Files:
  - Engine control/sweeper modules (planned)
- Tasks:
  - Implement aging (`LOCAL -> UNOWNED`) with generation bump + replication.
  - Implement peer-down takeover (`REMOTE -> LOCAL`) path.
  - Add tests for node flap/rejoin stabilization.
- Exit:
  - Recovery correctness validated in two-node integration tests.

## P2 (Scale, Hardening, and Ops)

## P2.1 Performance Validation Against Spec Thresholds
- Goal: validate spec success metrics in realistic traffic profile.
- Files:
  - Existing perf scripts + planned engine perf harness
- Tasks:
  - Measure CF share target (`<=5%` steady-state) under representative load.
  - Measure lookup/decision cycle budgets in packet path.
  - Track p99 packet latency deltas by mode.
- Exit:
  - Signed-off perf report for canary gate.

## P2.2 Observability and Incident Tooling
- Goal: ship production-grade diagnostics for ownership behavior.
- Files:
  - Library metrics hooks + engine stats surfaces
- Tasks:
  - Add metrics for conflicts, stale drops, migration trigger rate, apply lag.
  - Add per-RSS/per-worker deltas for observe/enforce impact.
  - Add runbook thresholds and alert rules.
- Exit:
  - Operational dashboards and alerting ready.

## P2.3 Canary and Rollout Controls
- Goal: safe progressive deployment.
- Files:
  - Engine config/feature flags (planned)
- Tasks:
  - Flags: `disabled/observe/enforce`, per-subscriber/per-subnet canary.
  - Automated rollback triggers from metrics.
  - Deployment playbook for 2-box A/A environments.
- Exit:
  - Controlled rollout plan executable by ops.

## Suggested Execution Sequence
1. Finish all P0 in library/sim environment.
2. Do P1 observe-mode integration in engine.
3. Run P2 scale validation and canary controls.

## Current Status Snapshot
- Completed foundations:
  - conflict resolution core
  - ZMQ adapter + tests
  - mobility/epoch IT scripts
  - perf benchmarks (4k/10k/100k aggregate)
- Not yet done:
  - DPDK/NUMA real integration
  - strict spec-struct parity in engine context
  - production CF and cycle-budget validation
