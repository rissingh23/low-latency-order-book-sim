# Benchmark Comparison

- Input: lob_real_dataset
- Orders: 4

## Clear Optimizations

1. Baseline -> optimized matcher: direct cancel lookup plus cached per-level quantity.
2. Optimized matcher -> pipeline: batched queue draining to reduce handoff overhead.

## Measured Deltas

- Optimized throughput vs baseline: 166.63%
- Optimized service p50 vs baseline: -33.60%
- Optimized service p99 vs baseline: -50.15%
- Pipeline throughput vs optimized: -97.46%
- Pipeline service p50 vs optimized: 25.90%
- Pipeline queue p50: 8166.00 ns
- Pipeline max queue depth: 4

## Interpretation

- If optimized service latency drops, the matcher itself got faster.
- If pipeline service latency stays close to optimized but end-to-end latency rises, queueing is dominating rather than matching.
