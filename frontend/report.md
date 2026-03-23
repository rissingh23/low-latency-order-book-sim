# Research Report

## Question

How much faster can a single price-time-priority order book get from better data structures, and what happens when we wrap that same matcher in a concurrent pipeline?

## Setup

This project implements one deterministic limit order book with:

- `LIMIT`
- `MARKET`
- `CANCEL`

The comparison uses three modes:

- `baseline_single_thread`
- `optimized_single_thread`
- `optimized_concurrent_pipeline`

The pipeline still keeps one matching thread. It adds lock-free transport around the matcher instead of matching one book on many threads.

## Engine Variants

### Baseline

- `std::map<price, std::deque<order>>`
- level quantity recomputed by scanning
- cancel finds the price level, then scans within that level

### Optimized

- direct order lookup by ID using stored iterators
- cached quantity per price level
- pooled PMR allocators for resting-order structures

### Pipeline

- producer thread -> SPSC ingress queue -> matcher thread -> SPSC egress queue -> consumer thread

## Metrics

- throughput in ops/sec
- service p50/p95/p99
- end-to-end p50/p95/p99
- queue delay p50/p95/p99
- max queue depth

`service latency` is time spent inside the matcher.

`end-to-end latency` is total time through the system.

`queue delay` is `end_to_end - service`.

## Current Results

### Balanced synthetic workload

- Baseline: `6.69M ops/s`, service p99 `417 ns`
- Optimized: `14.84M ops/s`, service p99 `125 ns`
- Pipeline: `7.80M ops/s`, service p99 `125 ns`, end-to-end p99 `8.28 ms`

### Real AAPL LOBSTER replay

- Baseline: `9.65M ops/s`, service p99 `167 ns`
- Optimized: `16.11M ops/s`, service p99 `84 ns`
- Pipeline: `9.86M ops/s`, service p99 `83 ns`, end-to-end p99 `2.93 ms`

## Findings

### 1. The optimized matcher is materially better

The main gains come from:

- removing cancel scans
- caching level quantity
- reducing allocator churn

The matching policy itself did not change. The engine simply does less bookkeeping work per event.

### 2. Pipeline service time stays good, but queueing dominates

The pipeline does not make one order book process events in parallel. It only moves events between threads more efficiently.

That means:

- matcher service time can remain excellent
- but end-to-end latency can still explode when the queue backs up

This is exactly what the benchmarks show.

### 3. For a single book, single-threaded optimization mattered more than concurrency

That is the central systems result of this repo.

## Limitations

- one order book only
- no hidden liquidity
- no advanced exchange order types
- checked-in profiling artifact is from macOS `sample` fallback on this machine, not Linux `perf`

## Takeaway

This repo is strongest as a quant/systems project because it demonstrates:

- deterministic exchange-style matching
- baseline vs optimized benchmarking
- real-data replay
- service vs queue-delay decomposition
- profiling-backed optimization workflow
