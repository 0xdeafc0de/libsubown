# libsubown

`libsubown` is a C library for distributed subscriber ownership in a 2-box active/active DPI cluster.

It implements deterministic ownership conflict resolution, fast-path ownership decisions, and a staged adapter layer for engine integration.

## What It Solves

- First-packet local claim for unowned subscribers.
- Async replication of ownership state between two boxes.
- Deterministic conflict resolution (generation, tie-break, epoch).
- Fast-path actions for packet handling:
  - process local
  - forward remote
  - trigger migration
- Adapter modes for rollout:
  - `disabled`
  - `observe`
  - `enforce`

## Repository Layout

- `include/subown.h` - core public API and data structures.
- `src/subown.c` - conflict resolution + fast-path library logic.
- `include/adapters/subown_zmq_adapter.h` - wire/ZMQ adapter API.
- `adapters/zmq/subown_zmq_adapter.c` - explicit V1 wire protocol + ZMQ send/recv.
- `adapter/include/ownership_adapter.h` - engine-facing adapter contract.
- `adapter/src/ownership_adapter.c` - stateful adapter implementation + metrics.
- `tests/` - unit tests.
- `sim/box_sim.c` - 2-process simulator executable.
- `scripts/` - IT and benchmark runners.
- `bench/` - synthetic throughput/latency impact benchmarks.

## Build Prerequisites

- C compiler with C11 support (`cc`, `clang`, or `gcc`).
- `make`.
- Optional for ZMQ tests/sim: `libzmq` with `pkg-config`.

## Build and Test

Run from `libsubown/`.

### Core unit tests (no ZMQ required)

```bash
make clean
make test
```

### Adapter behavior + metrics tests

```bash
make adapter-smoke
```

### ZMQ wire + runtime tests

```bash
make test-zmq WITH_ZMQ=1
```

If `libzmq` is not installed, build without `WITH_ZMQ=1` and run core tests only.

## Integration Test Scenarios (2-process simulation)

Build simulator:

```bash
make sim-box WITH_ZMQ=1
```

Run scenarios:

```bash
make test-it-mobility WITH_ZMQ=1
make test-it-epoch WITH_ZMQ=1
make test-it-fault-dup WITH_ZMQ=1
make test-it-fault-drop WITH_ZMQ=1
make test-it-tiebreak WITH_ZMQ=1
```

## Performance and Impact Benchmarks

```bash
make test-perf
make test-perf-pipeline
make test-impact
```

These are synthetic/local benchmarks for directional validation and regression tracking.

## Core APIs

### Library

- `subown_apply_remote_update(...)`
- `subown_claim_local(...)`
- `subown_process_packet_event(...)`

### Adapter (engine-facing)

- `ownership_adapter_on_packet(...)`
- `ownership_adapter_on_remote_msg(...)`
- `ownership_adapter_flush_outbound(...)`
- `ownership_adapter_on_peer_down(...)`
- `ownership_adapter_on_aging_tick(...)`
- `ownership_adapter_get_metrics(...)`

### Metrics exposed by adapter

- `own_worker_events_total`
- `own_worker_ring_drop_total`
- `own_apply_remote_total`
- `own_conflict_total`
- `own_stale_drop_total`
- `own_dup_drop_total`
- `own_migration_trigger_total`
- `own_zmq_recv_err_total`
- `own_zmq_send_err_total`

## Wire Protocol Notes

The explicit wire contract is versioned (`V1`) and validated on decode:

- magic
- version
- frame length
- subscriber id
- generation
- owner box
- activation timestamp
- policy mask
- owner epoch

See `include/adapters/subown_zmq_adapter.h` and `adapters/zmq/subown_zmq_adapter.c`.

## Portability

- Code is written to build on Linux and macOS.
- No macOS-specific APIs are required by library logic or tests.
- ZMQ runtime paths are compile-time guarded (`WITH_ZMQ` / `SUBOWN_HAVE_ZMQ`).

## Current Status

Implemented and tested:

- conflict resolution correctness (including epoch precedence)
- fast-path decision logic
- wire encode/decode validation
- 2-box simulation fault scenarios
- adapter behavior modes
- adapter metrics contract and deterministic metric tests

Remaining for full production integration:

- bind adapter to real engine rings/workers/control threads
- connect adapter outbound queue to live CP ZMQ publisher loop
- integrate engine observability export path for adapter counters

## References

- spec: `../Spec.md`
- adapter contract: `../ownership_adapter.md`
- task breakdown: `../Tasks.md`, `../Tasks_phase1.md`
- session progress log: `../SESSION_LOG.md`
