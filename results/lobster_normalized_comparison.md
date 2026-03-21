# Benchmark Comparison

- Input: lobster_normalized
- Orders: 5

## Clear Optimizations

1. Baseline -> optimized matcher: direct cancel lookup plus cached per-level quantity.
2. Optimized matcher -> pipeline: batched queue draining to reduce handoff overhead.

## Measured Deltas

- Optimized throughput vs baseline: 130.01%
- Optimized service p50 vs baseline: -39.90%
- Optimized service p99 vs baseline: -33.60%
- Pipeline throughput vs optimized: -97.82%
- Pipeline service p50 vs optimized: 67.20%
- Pipeline queue p50: 9291.00 ns
- Pipeline max queue depth: 5

## Interpretation

- If optimized service latency drops, the matcher itself got faster.
- If pipeline service latency stays close to optimized but end-to-end latency rises, queueing is dominating rather than matching.
