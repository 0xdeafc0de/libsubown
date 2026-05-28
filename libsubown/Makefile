CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2
INC = -Iinclude

WITH_ZMQ ?= 0
HAVE_ZMQ := 0

ifeq ($(WITH_ZMQ),1)
ifneq ($(shell pkg-config --exists libzmq && echo yes),)
HAVE_ZMQ := 1
INC += $(shell pkg-config --cflags libzmq)
LDFLAGS += $(shell pkg-config --libs libzmq)
CFLAGS += -DSUBOWN_HAVE_ZMQ
endif
endif

LIB_SRC = src/subown.c
ifeq ($(WITH_ZMQ),1)
LIB_SRC += adapters/zmq/subown_zmq_adapter.c
endif

TEST_BIN = tests/test_conflict_resolution
TEST_SRC = tests/test_conflict_resolution.c
FASTPATH_TEST_BIN = tests/test_fastpath_logic
FASTPATH_TEST_SRC = tests/test_fastpath_logic.c

ZMQ_TEST_BIN = tests/test_zmq_adapter
ZMQ_TEST_SRC = tests/test_zmq_adapter.c

SIM_BIN = sim/box_sim
SIM_SRC = sim/box_sim.c

BENCH_BIN = bench/bench_subown
BENCH_SRC = bench/bench_subown.c

PIPE_BENCH_BIN = bench/bench_pipeline_4k
PIPE_BENCH_SRC = bench/bench_pipeline_4k.c

IMPACT_BENCH_BIN = bench/bench_worker_rss_impact
IMPACT_BENCH_SRC = bench/bench_worker_rss_impact.c
ADAPTER_SMOKE_BIN = adapter/tests/test_ownership_adapter_smoke
ADAPTER_SMOKE_SRC = adapter/tests/test_ownership_adapter_smoke.c
ADAPTER_SRC = adapter/src/ownership_adapter.c

.PHONY: all clean test test-zmq sim-box bench bench-pipeline bench-impact adapter-smoke test-it-mobility test-it-epoch test-it-fault-dup test-it-fault-drop test-it-tiebreak test-perf test-perf-pipeline test-impact

all: $(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) $(LIB_SRC) include/subown.h
	$(CC) $(CFLAGS) $(INC) $(TEST_SRC) $(LIB_SRC) $(LDFLAGS) -o $@

$(FASTPATH_TEST_BIN): $(FASTPATH_TEST_SRC) $(LIB_SRC) include/subown.h
	$(CC) $(CFLAGS) $(INC) $(FASTPATH_TEST_SRC) $(LIB_SRC) $(LDFLAGS) -o $@

$(ZMQ_TEST_BIN): $(ZMQ_TEST_SRC) $(LIB_SRC) include/subown.h include/adapters/subown_zmq_adapter.h
	$(CC) $(CFLAGS) $(INC) $(ZMQ_TEST_SRC) $(LIB_SRC) $(LDFLAGS) -o $@

$(SIM_BIN): $(SIM_SRC) $(LIB_SRC) include/subown.h include/adapters/subown_zmq_adapter.h
	$(CC) $(CFLAGS) $(INC) $(SIM_SRC) $(LIB_SRC) $(LDFLAGS) -o $@

$(BENCH_BIN): $(BENCH_SRC) src/subown.c include/subown.h
	$(CC) $(CFLAGS) $(INC) $(BENCH_SRC) src/subown.c -o $@

$(PIPE_BENCH_BIN): $(PIPE_BENCH_SRC) src/subown.c include/subown.h
	$(CC) $(CFLAGS) $(INC) $(PIPE_BENCH_SRC) src/subown.c -lpthread -o $@

$(IMPACT_BENCH_BIN): $(IMPACT_BENCH_SRC) src/subown.c include/subown.h
	$(CC) $(CFLAGS) $(INC) $(IMPACT_BENCH_SRC) src/subown.c -lpthread -o $@

$(ADAPTER_SMOKE_BIN): $(ADAPTER_SMOKE_SRC) $(ADAPTER_SRC) adapter/include/ownership_adapter.h include/subown.h include/adapters/subown_zmq_adapter.h
	$(CC) $(CFLAGS) $(INC) -Iadapter/include $(ADAPTER_SMOKE_SRC) $(ADAPTER_SRC) src/subown.c adapters/zmq/subown_zmq_adapter.c $(LDFLAGS) -o $@

test: $(TEST_BIN) $(FASTPATH_TEST_BIN)
	./$(TEST_BIN)
	./$(FASTPATH_TEST_BIN)

test-zmq: $(ZMQ_TEST_BIN)
	./$(ZMQ_TEST_BIN)

sim-box: $(SIM_BIN)

bench: $(BENCH_BIN)

bench-pipeline: $(PIPE_BENCH_BIN)

bench-impact: $(IMPACT_BENCH_BIN)

test-it-mobility: sim-box
	./scripts/test_it_mobility.sh

test-it-epoch: sim-box
	./scripts/test_it_epoch_restart.sh

test-it-fault-dup: sim-box
	./scripts/test_it_fault_duplicate.sh

test-it-fault-drop: sim-box
	./scripts/test_it_fault_drop.sh

test-it-tiebreak: sim-box
	./scripts/test_it_tiebreak_split.sh

test-perf: bench
	./scripts/test_perf_scale_latency.sh

test-perf-pipeline: bench-pipeline
	./scripts/test_perf_pipeline_4k.sh

test-impact: bench-impact
	./scripts/test_impact_worker_rss.sh

adapter-smoke: $(ADAPTER_SMOKE_BIN)
	./$(ADAPTER_SMOKE_BIN)

clean:
	rm -f $(TEST_BIN) $(FASTPATH_TEST_BIN) $(ZMQ_TEST_BIN) $(SIM_BIN) $(BENCH_BIN) $(PIPE_BENCH_BIN) $(IMPACT_BENCH_BIN) $(ADAPTER_SMOKE_BIN)
