# Benchmark Comparison

- Input: aapl_lobster_normalized
- Orders: 48389

## Clear Optimizations

1. Baseline -> optimized matcher: direct cancel lookup plus cached per-level quantity.
2. Optimized matcher -> pipeline: batched queue draining to reduce handoff overhead.

## Measured Deltas

- Optimized throughput vs baseline: 31.70%
- Optimized service p50 vs baseline: -50.00%
- Optimized service p99 vs baseline: -40.19%
- Pipeline throughput vs optimized: -28.68%
- Pipeline service p50 vs optimized: 0.00%
- Pipeline queue p50: 2561250.00 ns
- Pipeline max queue depth: 36526

## Interpretation

- If optimized service latency drops, the matcher itself got faster.
- If pipeline service latency stays close to optimized but end-to-end latency rises, queueing is dominating rather than matching.
