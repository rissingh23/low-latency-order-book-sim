# Low-Latency Limit Order Book Simulator

Single-book C++20 matching engine project for quant-dev and systems interviews. The repo focuses on one question:

> How much faster can a price-time-priority order book get from better data structures, and what happens when we wrap that same matcher in a concurrent pipeline?

## Abstract

This project implements a deterministic limit order book with strict price-time priority for `LIMIT`, `MARKET`, and `CANCEL` events. It now has two layers:

- a low-latency single-book matching core
- a deterministic replay / features / inference path on top of historical events

The core engine still compares three execution modes:

- `baseline_single_thread`: simple reference implementation
- `optimized_single_thread`: same matching rules, lower hot-path overhead
- `optimized_concurrent_pipeline`: lock-free ingress/egress queues around the same single matching thread

The optimized engine improves single-book throughput materially on both synthetic and real data. The concurrent pipeline keeps matcher service time low but worsens end-to-end latency under burst load because queueing dominates once the single matcher saturates. The new replay pipeline makes it possible to export labeled feature datasets, evaluate simple models offline, and benchmark the cost of integrating inference back into the engine loop.

## Research Question

For a single order book:

1. Which data-structure choices actually reduce matcher latency?
2. Does adding concurrency help if matching itself is still serialized?
3. How do synthetic benchmark results compare with a normalized real event stream?

## System Model

The engine simulates an exchange-style central limit order book.

- A `bid` is a buy order.
- An `ask` is a sell order.
- A `limit order` rests in the book unless it crosses the opposite side.
- A `market order` immediately consumes the best available liquidity.
- A `cancel` removes some or all of a resting order.
- `price-time priority` means better price wins first, then older order wins at the same price.

This project intentionally stays single-book. That keeps the concurrency story honest: the pipeline is measuring handoff and queueing behavior, not pretending that one book can be matched in parallel without tradeoffs.

## Implemented Variants

### 1. Baseline

File:
- [src/engines/baseline_order_book.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/engines/baseline_order_book.cpp)

Design:
- `std::map<price, std::deque<order>>` per side
- per-level quantity recomputed by scanning
- cancel uses a level scan to find the target order

Purpose:
- easiest version to reason about
- correctness reference
- “before optimization” benchmark

### 2. Optimized

File:
- [src/engines/optimized_order_book.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/engines/optimized_order_book.cpp)

Design changes:
- direct order lookup by ID using stored iterators
- cached aggregated quantity per price level
- pooled allocators via `std::pmr::unsynchronized_pool_resource` to reduce allocator churn for resting-order structures

Purpose:
- same external behavior as baseline
- less work per event on the hot path

### 3. Concurrent Pipeline

File:
- [src/benchmark.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/benchmark.cpp)

Design:
- producer thread -> ingress SPSC queue -> single matcher thread -> egress SPSC queue -> consumer thread

Purpose:
- keep matching deterministic
- measure whether transport concurrency helps or just adds queueing

Important:
- the matcher is still single-threaded
- this is not a shared-mutation multi-threaded book

## Code Map

Read the repo in this order:

1. [include/lob/types.hpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/include/lob/types.hpp)  
   Core domain types: `OrderEvent`, `Execution`, `TopOfBook`, `BookDepth`, `RunSummary`

2. [include/lob/order_book.hpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/include/lob/order_book.hpp)  
   Shared engine interface and benchmark/replay APIs

3. [src/engines/baseline_order_book.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/engines/baseline_order_book.cpp)  
   Best place to understand matching logic clearly

4. [src/engines/optimized_order_book.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/engines/optimized_order_book.cpp)  
   Same logic, lower overhead

5. [src/workload.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/workload.cpp)  
   Deterministic synthetic traffic generation

6. [src/dataset.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/dataset.cpp)  
   Real dataset loader

7. [src/replay_engine.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/replay_engine.cpp)  
   Deterministic replay engine and stage-by-stage replay benchmarks

8. [src/feature_extractor.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/feature_extractor.cpp)  
   Order-book feature extraction and labeled feature export

9. [src/inference_engine.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/inference_engine.cpp)  
   Heuristic and linear-model inference path

10. [src/benchmark.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/benchmark.cpp)  
   Throughput/latency measurement

11. [src/replay.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/replay.cpp)  
   Replay export for the dashboard

12. [src/main.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/main.cpp)  
   CLI modes and report generation

13. [tests/test_order_books.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/tests/test_order_books.cpp)  
    Correctness and determinism checks

## Experimental Setup

### Synthetic workloads

Generated in [src/workload.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/workload.cpp):

- `balanced`
- `cancel_heavy`
- `bursty`

### Real dataset

Normalized event CSV loader:
- [src/dataset.cpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/src/dataset.cpp)

LOBSTER normalizer:
- [scripts/normalize_lobster.py](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/scripts/normalize_lobster.py)

The loader expects order events, not L2 snapshots.

### Metrics

Measured per run:

- throughput in `ops/s`
- service `p50/p95/p99`
- end-to-end `p50/p95/p99`
- queue delay `p50/p95/p99`
- max queue depth
- fills and rejects

Definitions:

- `service latency`: time spent inside `process_event(...)`
- `end-to-end latency`: time from entering the system to leaving it
- `queue delay`: `end_to_end - service`

For single-thread modes, end-to-end equals service and queue delay is zero.

## Replay / Features / Inference

The V2 path stays intentionally small:

```text
historical events -> ReplayEngine -> optimized matcher -> FeatureExtractor -> InferenceEngine
```

Core headers:

- [include/lob/replay_engine.hpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/include/lob/replay_engine.hpp)
- [include/lob/features.hpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/include/lob/features.hpp)
- [include/lob/inference.hpp](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/include/lob/inference.hpp)

Current feature set:

- spread
- mid price
- microprice
- top-level imbalance
- 3-level imbalance
- depth slope
- top-of-book depletion imbalance
- market-order ratio
- cancel ratio
- rolling signed order-flow imbalance
- last mid-price delta
- last microprice delta

Current inference options:

- heuristic imbalance rule
- linear softmax model loaded from exported weights
- tiny MLP
- XGBoost for offline comparison

This keeps the matcher single-threaded and deterministic while adding enough ML systems structure to benchmark latency/accuracy tradeoffs honestly.

## Current Results

These are fresh local runs from the current codebase.

### Balanced synthetic workload

Command:

```bash
./build/lob_simulator --mode benchmark --profile balanced --orders 200000 --seed 42 --output results/balanced.csv
```

Results:

| Engine | Throughput (ops/s) | Service p50 (ns) | Service p99 (ns) | E2E p99 (ns) | Queue p99 (ns) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | 8.06M | 83 | 375 | 375 | 0 |
| Optimized | 16.33M | 41 | 84 | 84 | 0 |
| Pipeline | 8.93M | 41 | 84 | 7,905,420 | 7,905,380 |

Interpretation:

- optimized throughput is about `+121.9%` vs baseline
- optimized service p50 drops about `50%`
- optimized service p99 drops about `70%`
- pipeline matcher service time stays low, but the system saturates and queue delay dominates

### Real AAPL LOBSTER replay

Command:

```bash
./build/lob_simulator --mode benchmark --dataset data/aapl_lobster_normalized.csv --output results/aapl_lobster.csv
```

Results:

| Engine | Throughput (ops/s) | Service p50 (ns) | Service p99 (ns) | E2E p99 (ns) | Queue p99 (ns) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | 11.54M | 83 | 166 | 166 | 0 |
| Optimized | 18.44M | 41 | 84 | 84 | 0 |
| Pipeline | 10.80M | 41 | 83 | 2,575,710 | 2,575,710 |

Interpretation:

- optimized throughput is about `+67.0%` vs baseline on real data
- optimized service p99 improves from `167 ns` to `84 ns`
- pipeline service p99 is still excellent, but end-to-end latency is much worse because the queue backs up

### Stage-by-stage replay benchmark

Command:

```bash
./build/lob_simulator --mode benchmark-stages --dataset data/aapl_lobster_normalized.csv --depth 3 --output results/aapl_stage_bench_binary.csv
```

Results:

| Mode | Throughput (ops/s) | Service p50 (ns) | Service p99 (ns) | E2E p50 (ns) | E2E p99 (ns) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Matcher only | 19.13M | 41 | 83 | 41 | 83 |
| Replay + matcher | 9.28M | 42 | 84 | 42 | 84 |
| Replay + features | 7.36M | 41 | 83 | 42 | 84 |
| Replay + features + heuristic inference | 7.05M | 41 | 83 | 42 | 125 |

This is the main systems result for V2: feature extraction and lightweight inference add measurable overhead, but the core matcher service time remains essentially unchanged.

### Predictive evaluation

The project now sweeps several label definitions and keeps the best one:

- fixed horizon: 10 / 25 / 50 events
- next non-zero move
- thresholded horizon with a 100-unit move threshold

Best target:

- `threshold_h25_t100`

Meaning:

- look 25 events ahead
- treat moves smaller than 100 price units as flat/noise
- classify the resulting move as `down` or `up`

Final selected AAPL results:

- majority class: accuracy `0.4687`, macro F1 `0.3191`
- heuristic imbalance: accuracy `0.5601`, macro F1 `0.5547`
- linear model: accuracy `0.4693`, macro F1 `0.3258`
- tiny MLP: accuracy `0.4726`, macro F1 `0.3291`
- XGBoost: accuracy `0.5070`, macro F1 `0.4594`

The current winner is still the heuristic. That is actually a good outcome for the project because it shows:

- the evaluation is real
- target definition mattered more than adding model complexity
- the repo can now compare predictive value against model/inference cost honestly

The model evaluator also records per-example offline inference timing for each model, which is surfaced in the frontend `Models` tab alongside accuracy and macro F1.

## Main Finding

For a single book, the best gain came from better single-threaded data structures, not from wrapping the matcher in concurrency.

That is the core result of the project:

- `baseline -> optimized` improved the matcher itself
- `optimized -> pipeline` preserved matcher speed but hurt system latency under saturation

This is a useful quant/systems conclusion because it matches how real exchange-style engines are often designed: keep one matcher deterministic and fast before adding transport complexity around it.

## Build

### CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Direct clang++ fallback

```bash
mkdir -p build
clang++ -std=c++20 -O3 -pthread -Iinclude \
  src/engines/baseline_order_book.cpp \
  src/engines/optimized_order_book.cpp \
  src/engines/intrusive_order_book.cpp \
  src/feature_extractor.cpp \
  src/inference_engine.cpp \
  src/replay_engine.cpp \
  src/workload.cpp \
  src/dataset.cpp \
  src/replay.cpp \
  src/benchmark.cpp \
  src/main.cpp \
  -o build/lob_simulator

clang++ -std=c++20 -O2 -pthread -Iinclude \
  src/engines/baseline_order_book.cpp \
  src/engines/optimized_order_book.cpp \
  src/engines/intrusive_order_book.cpp \
  src/feature_extractor.cpp \
  src/inference_engine.cpp \
  src/replay_engine.cpp \
  src/workload.cpp \
  src/dataset.cpp \
  src/replay.cpp \
  src/benchmark.cpp \
  tests/test_order_books.cpp \
  -o build/lob_tests
```

## Repro Commands

### Tests

```bash
./build/lob_tests
```

### Synthetic benchmark

```bash
./build/lob_simulator \
  --mode benchmark \
  --profile balanced \
  --orders 200000 \
  --seed 42 \
  --output results/balanced.csv
```

### Real-data benchmark

```bash
./build/lob_simulator \
  --mode benchmark \
  --dataset data/aapl_lobster_normalized.csv \
  --output results/aapl_lobster.csv
```

### Replay/dashboard export

```bash
./build/lob_simulator \
  --mode export-dashboard \
  --dataset data/aapl_lobster_normalized.csv \
  --output results/aapl_lobster.csv
```

### Feature export

```bash
./build/lob_simulator \
  --mode export-features \
  --dataset data/aapl_lobster_normalized.csv \
  --depth 3 \
  --horizon-events 10 \
  --output results/aapl_features.csv
```

### Stage benchmark

```bash
./build/lob_simulator \
  --mode benchmark-stages \
  --dataset data/aapl_lobster_normalized.csv \
  --depth 3 \
  --output results/aapl_stage_bench.csv
```

### Frontend

```bash
bash scripts/serve_frontend.sh
```

Open [http://localhost:8000/frontend/index.html](http://localhost:8000/frontend/index.html)

## Profiling Workflow

Profile helper:
- [scripts/profile_engine.sh](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/scripts/profile_engine.sh)

Flame graph converter:
- [scripts/sample_to_flamegraph.py](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/scripts/sample_to_flamegraph.py)

Run:

```bash
./scripts/profile_engine.sh balanced 2000000 balanced
```

Behavior:

- on Linux with `perf`, it writes `perf` artifacts
- on macOS, it falls back to `sample` and generates a flamegraph-style SVG

Current checked-in profiling artifacts:

- [results/balanced_perf.txt](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/results/balanced_perf.txt)
- [results/balanced_flamegraph.svg](/Users/rishabhsingh/Desktop/low-latency-order-book-sim/results/balanced_flamegraph.svg)

## Dataset Schema

Expected normalized CSV columns:

- required: `type`, `side`, `order_id`
- optional: `timestamp`, `sequence`, `price`, `qty`

Example:

```csv
timestamp,sequence,type,side,order_id,price,qty
1710000001,1,limit,buy,10001,4312500,5
1710000002,2,limit,sell,10002,4312600,3
1710000003,3,market,buy,10003,0,2
1710000004,4,cancel,sell,10002,0,0
```

## Limitations

- only one book is modeled at a time
- hidden liquidity and advanced exchange order types are not implemented
- pipeline results are intentionally about queueing around one matcher, not full exchange-scale parallelism
- the checked-in profiling artifacts on this machine use macOS `sample`, not Linux `perf`

## Why This Is A Good Quant Project

The strongest signal here is not the frontend. It is the backend evidence:

- deterministic exchange-style matching
- controlled baseline vs optimized comparison
- real-data replay path
- latency decomposition into service vs end-to-end vs queue delay
- profiling workflow with saved artifacts

That gives you a concrete interview story:

1. build the correct matcher
2. profile and optimize the matcher
3. measure what concurrency really does
4. validate the same engine on real event data
