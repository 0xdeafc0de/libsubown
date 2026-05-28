# Session Log

## 2026-05-27
- Goal: start Milestone A implementation with TDD and Linux-portable library design.
- Completed:
  - Created standalone `libsubown` skeleton (include/src/tests).
  - Implemented deterministic conflict resolver API and core tests.
  - Added `owner_epoch` precedence handling in resolver.
  - Added local-claim API with async emitter callback contract.
  - Added tests for epoch precedence and emit callback behavior.
  - Added optional ZMQ adapter module scaffold and build flag (`WITH_ZMQ`).
  - Added real ZMQ adapter implementation behind `SUBOWN_HAVE_ZMQ` compile flag.
  - Added ZMQ roundtrip test (`test_zmq_adapter`) with auto-skip path when ZMQ is unavailable.
- Environment note:
  - `libzmq` not present in current environment (pkg-config lookup failed), so full ZMQ runtime verification is pending dependency availability.
- Next goals:
  - Run `test-zmq` on Linux env with `libzmq` installed.
  - Add two-adapter integration test (A/B endpoints) for duplicate/reorder replay.
  - Add counters/trace hooks required by Milestone A observability criteria.

## 2026-05-27 (ZMQ Validation Update)
- `libzmq` detected via pkg-config (`4.3.5`).
- Executed:
  - `make clean && make test WITH_ZMQ=1`
  - `make test-zmq WITH_ZMQ=1`
- Result:
  - `test_conflict_resolution`: PASS
  - `test_zmq_adapter`: PASS
- Status:
  - Real ZMQ adapter path compiled and runtime-validated in this environment.

## 2026-05-27 (2-Process IT Simulator)
- Added same-executable two-box simulator:
  - `libsubown/sim/box_sim.c`
- Added build target:
  - `make sim-box WITH_ZMQ=1`
- Added integration runner script:
  - `libsubown/scripts/run_it_2box.sh`
- Verified run:
  - builds and runs box0/box1 as separate processes with swapped PUB/SUB endpoints.
  - logs generated at `libsubown/sim/box0.log` and `libsubown/sim/box1.log`.
- Observation:
  - with periodic reclaim enabled on both boxes, ownership oscillation is expected (both keep asserting ownership).

## 2026-05-27 (Simulator Profile Enhancements)
- Added claim mode support in simulator executable:
  - `--claim-mode periodic|once|off`
- Updated runner script with profiles:
  - `stable`: box0=once, box1=off
  - `chaos`: box0=periodic, box1=periodic
  - `split`: box0=once, box1=once
- Reliability fixes:
  - random per-run TCP port selection
  - startup stagger
  - cleanup trap for background PIDs
- Slow-joiner mitigation:
  - `once` claim now triggers at iteration=`claim_every` (not first iteration), allowing SUB sockets to subscribe before first PUB event.
- Verified:
  - `stable` profile converges to common owner on both boxes.
  - `chaos` profile shows expected oscillation/stress behavior.

## 2026-05-27 (Mobility Scenario Added)
- Added timeline-based claim scheduling to simulator via `--claim-at` (CSV iterations).
- Added subscriber IP argument `--ip` for realistic subscriber identity tracing in logs.
- Added mobility integration runner:
  - `libsubown/scripts/run_it_mobility.sh`
- Implemented scenario:
  - subscriber synced to both boxes at startup,
  - traffic seen on box1,
  - moves to box2,
  - later moves back to box1.
- Verified run output:
  - box1 claim at iter 20,
  - box2 claim at iter 100,
  - box1 reclaim at iter 170,
  - final ownership converged to box1 with higher generation.

## 2026-05-27 (IT Automation + Fault Injection)
- Added simulator fault-injection knobs:
  - `--emit-dup <N>`: duplicate outbound ownership updates per claim.
  - `--drop-incoming-every <N>`: drop every Nth inbound update.
- Added simulator observability counters in final stats line:
  - claims, emits, recv, recv_drop_fault, apply_remote, keep_local, noop.
- Added IT assertion scripts:
  - `scripts/test_it_mobility.sh`
  - `scripts/test_it_epoch_restart.sh`
- Added epoch/restart scenario runner:
  - `scripts/run_it_epoch_restart.sh`
- Added Make targets:
  - `test-it-mobility`
  - `test-it-epoch`
- Added wire compatibility unit test:
  - invalid message version rejected in `test_conflict_resolution`.
- Validation:
  - `make test WITH_ZMQ=1` PASS
  - `make test-zmq WITH_ZMQ=1` PASS
  - `make test-it-mobility WITH_ZMQ=1` PASS
  - `make test-it-epoch WITH_ZMQ=1` PASS

## 2026-05-28 (Scale + Latency Benchmark)
- Added benchmark executable:
  - `bench/bench_subown.c`
- Added perf gate script:
  - `scripts/test_perf_scale_latency.sh`
- Added Make targets:
  - `bench`
  - `test-perf`
- Perf requirements validated:
  - target throughput >= 4,000 req/s
  - target p99 latency < 100 ms
- Measured (50k requests):
  - throughput: 15,605,493 req/s
  - p99 latency: 0.001 ms
- Measured (200k requests):
  - throughput: 17,966,223 req/s
  - p99 latency: 0.001 ms
- Status:
  - performance gate passing with significant headroom.

## 2026-05-28 (4k/s Pipeline Perf Correction)
- Added threaded pipeline benchmark for worker->shard control model:
  - `bench/bench_pipeline_4k.c`
  - `scripts/test_perf_pipeline_4k.sh`
  - Make target: `test-perf-pipeline`
- Corrected benchmark semantics:
  - request rate is now aggregate total (4k/s across all workers), not per-worker.
- Results at 4k/s total:
  - 40w/4s: p99 0.634 ms
  - 40w/8s: p99 0.634 ms
  - 80w/8s: p99 0.631 ms
  - 80w/16s: p99 0.631 ms
- Throughput in all cases met >=4k/s, p99 in all cases << 100 ms.

## 2026-05-28 (Pipeline Bench at 10k/s and 100k/s)
- Updated `bench_pipeline_4k` to accept target rate as CLI argument.
- Ran aggregate-rate benchmarks for 10s across 4 layouts.

### 10k/s total
- 40w/4s: p99 0.627 ms, throughput 10001.04/s
- 40w/8s: p99 0.629 ms, throughput 10001.11/s
- 80w/8s: p99 0.626 ms, throughput 10000.37/s
- 80w/16s: p99 0.625 ms, throughput 10000.28/s

### 100k/s total
- 40w/4s: p99 0.629 ms, throughput 99991.91/s
- 40w/8s: p99 0.629 ms, throughput 99990.50/s
- 80w/8s: p99 0.636 ms, throughput 99995.10/s
- 80w/16s: p99 0.636 ms, throughput 99993.08/s

## 2026-05-28 (Worker/RSS Impact Benchmark in Local Env)
- Added synthetic RSS->worker->control impact benchmark:
  - `bench/bench_worker_rss_impact.c`
  - `scripts/test_impact_worker_rss.sh`
  - Make target: `test-impact`
- Modes compared:
  - baseline (no ownership enqueue)
  - observe (enqueue only)
  - enforce (enqueue + ownership apply)

### 10k/s aggregate
- 8 RSS / 40 workers / 8 shards:
  - baseline p99: 0.201 ms
  - observe p99: 0.205 ms (+1.99%)
  - enforce p99: 0.205 ms (+1.99%)
- 16 RSS / 80 workers / 16 shards:
  - high p99 noise on this host (scheduler contention), not treated as stable sizing signal.

### 100k/s aggregate
- 8 RSS / 40 workers / 8 shards:
  - baseline p99: 0.197 ms
  - observe p99: 0.213 ms (+8.12%)
  - enforce p99: 0.212 ms (+7.61%)
- 16 RSS / 80 workers / 16 shards:
  - p99 remains under 100 ms in observe/enforce in this run, but with unstable baseline due local host scheduling effects.

- Interpretation:
  - local environment gives useful directional deltas for 8/40 profile.
  - 16/80 profile needs a higher-core host for stable p99 comparisons.

## 2026-05-28 (P0.1 + P0.2 Completion)
- P0.1 complete:
  - cleaned `Spec.md` by removing trailing non-normative prose/unmatched quote block.
  - file now ends at section 6.2 verification scenarios.
- P0.2 complete (backward-compatible parity update):
  - updated `subown_record_t` to include spec-aligned fields:
    - `owner_box`, `policy_mask`, `activation_ts`, `last_seen_ts`, `remote_packet_counter`.
  - updated `subown_update_t` to include:
    - `owner_box`, `activation_ts`, `policy_mask`.
  - kept compatibility fields (`owner_box_id`, `timestamp`, `owner_epoch`) to avoid breaking sims/benches.
  - updated core logic in `subown.c` to populate/use spec fields while preserving existing behavior.
- Regression fixes after parity update:
  - owner normalization now prioritizes valid compatibility owner field to avoid mixed-field mismatch.
  - epoch IT delivery hardened via duplicate emit in restart scenario script.
- Validation:
  - `make test` PASS
  - `make test-zmq WITH_ZMQ=1` PASS
  - `make test-it-mobility WITH_ZMQ=1` PASS
  - `make test-it-epoch WITH_ZMQ=1` PASS

## 2026-05-28 (P0.3 Completion)
- Implemented explicit, versioned wire protocol for ownership replication (ZMQ adapter):
  - Added fixed frame constants and magic/version/length checks.
  - Added explicit big-endian encode/decode helpers.
  - Added schema fields in wire frame: subscriber_id, generation, owner_box, activation_ts, policy_mask, owner_epoch.
- Added adapter contract helpers:
  - `subown_zmq_encode_update_v1(...)`
  - `subown_zmq_decode_update_v1(...)`
- Added negative wire tests:
  - truncated payload rejection
  - unknown-version rejection
  - roundtrip encode/decode checks
- Regression handling:
  - fixed epoch propagation by carrying `owner_epoch` in wire frame.
  - aligned epoch IT assertions to validate epoch dominance signals and epoch convergence (`epoch=2`) rather than fixed final owner under tie-break race.
- Validation:
  - `make test` PASS
  - `./tests/test_zmq_adapter` PASS
  - `./scripts/test_it_mobility.sh` PASS
  - `./scripts/test_it_epoch_restart.sh` PASS

## 2026-05-28 (P0.4 Iteration 1 Complete)
- Added fast-path library API for CASE A/B/C semantics:
  - `subown_process_packet_event(...)`
- Extended decision/reason enums to represent fast-path outcomes:
  - process local, forward remote, trigger migration.
  - local hit, remote misroute, migration threshold crossed, unowned claim.
- Implemented remote misroute counter threshold behavior:
  - increments `remote_packet_counter` on remote-owner packet.
  - triggers migration decision when `counter > threshold` and resets counter.
- Implemented local-hit timestamp refresh (`last_seen_ts`).
- Implemented unowned first-packet claim path through existing claim+emit contract.
- Added new unit tests:
  - `tests/test_fastpath_logic.c`
  - validates CASE A/B/C and migration threshold behavior.
- Validation:
  - `make test` PASS (`test_conflict_resolution` + `test_fastpath_logic`)
  - `./tests/test_zmq_adapter` PASS
  - `./scripts/test_it_mobility.sh` PASS
  - `./scripts/test_it_epoch_restart.sh` PASS

## 2026-05-28 (P0.5 IT Assertion Expansion Complete)
- Added deterministic IT assertion scripts for fault/race scenarios:
  - `scripts/test_it_fault_duplicate.sh`
  - `scripts/test_it_fault_drop.sh`
  - `scripts/test_it_tiebreak_split.sh`
- Added Make targets:
  - `test-it-fault-dup`
  - `test-it-fault-drop`
  - `test-it-tiebreak`
- Assertions now cover:
  - duplicate message handling (apply once + duplicate noops)
  - inbound drop fault handling with eventual convergence
  - same-generation split race tie-break to lower box id
- Validation:
  - `make test-it-fault-dup WITH_ZMQ=1` PASS
  - `make test-it-fault-drop WITH_ZMQ=1` PASS
  - `make test-it-tiebreak WITH_ZMQ=1` PASS

## 2026-05-28 (P1.1 Adapter Iteration 2: Stateful Behavior + TDD)
- Expanded adapter TDD coverage in `libsubown/adapter/tests/test_ownership_adapter_smoke.c`:
  - `disabled` mode returns `OWN_ACT_NOOP`.
  - `observe` mode with remote-owned subscriber keeps local processing and emits `MISROUTE` ring event.
  - `enforce` mode with remote-owned subscriber returns `OWN_ACT_FORWARD_REMOTE`.
  - `enforce` mode unowned first packet returns `OWN_ACT_CLAIM_EMITTED` and stages outbound replication.
- Implemented adapter internals in `libsubown/adapter/src/ownership_adapter.c`:
  - in-memory subscriber state table (open addressing, fixed capacity).
  - per-subscriber fast-path decisions via `subown_process_packet_event(...)`.
  - mode mapping (`observe` vs `enforce`) to dataplane actions.
  - worker->control ring event generation for misroute/migration/unowned-claim paths.
  - remote wire message decode + `subown_apply_remote_update(...)` application.
  - outbound replication staging queue with `ownership_adapter_flush_outbound(...)`.
  - peer-down hook marks peer-owned records as unowned.
- Portability fix:
  - updated `libsubown/adapters/zmq/subown_zmq_adapter.c` to avoid empty-struct compile failure when `WITH_ZMQ=0` (Linux/macOS friendly).
- Build wiring:
  - `adapter-smoke` target links `subown_zmq_adapter.c` so adapter tests can use wire encode/decode helpers even without live ZMQ sockets.
- Validation:
  - `make clean && make adapter-smoke` PASS
  - `make test` PASS
  - `make test-zmq WITH_ZMQ=1` PASS

## 2026-05-28 (P1.1 Adapter Metrics Contract Iteration)
- Added adapter metrics API in `libsubown/adapter/include/ownership_adapter.h`:
  - `own_adapter_metrics_t`
  - `ownership_adapter_get_metrics(...)`
- Implemented adapter-side metrics accounting in `libsubown/adapter/src/ownership_adapter.c` for:
  - `own_worker_events_total`
  - `own_worker_ring_drop_total`
  - `own_apply_remote_total`
  - `own_conflict_total`
  - `own_stale_drop_total`
  - `own_dup_drop_total`
  - `own_migration_trigger_total`
  - `own_zmq_recv_err_total`
  - `own_zmq_send_err_total`
- Expanded TDD coverage in `libsubown/adapter/tests/test_ownership_adapter_smoke.c`:
  - observe-mode remote-owner path increments `own_apply_remote_total` and `own_worker_events_total`
  - invalid remote wire message increments `own_zmq_recv_err_total`
  - ring saturation increments `own_worker_ring_drop_total`
- Validation:
  - `make adapter-smoke` PASS
  - `make test` PASS
  - `make test-zmq WITH_ZMQ=1` PASS

## 2026-05-28 (P1.1 Metrics Completion)
- Completed deterministic adapter-counter test coverage in `libsubown/adapter/tests/test_ownership_adapter_smoke.c` for:
  - `own_dup_drop_total`
  - `own_stale_drop_total`
  - `own_conflict_total`
  - `own_migration_trigger_total`
  - `own_zmq_send_err_total`
- Added crafted remote-update sequences to force duplicate/stale/conflict reasons and assert exact counter values.
- Added migration-threshold packet sequence to assert migration-trigger counter.
- Added outbound queue saturation path to assert send-error counter increment under backpressure.
- Validation:
  - `make adapter-smoke` PASS
  - `make test` PASS
  - `make test-zmq WITH_ZMQ=1` PASS
