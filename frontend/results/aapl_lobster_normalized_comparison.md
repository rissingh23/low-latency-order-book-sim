# Benchmark Comparison

- Input: aapl_lobster_normalized
- Orders: 48389

## Clear Optimizations

1. Baseline -> optimized matcher: direct cancel lookup, cached per-level quantity, and pooled allocators for resting-order storage.
2. Optimized matcher -> pipeline: batched queue draining to reduce handoff overhead.

## Measured Deltas

- Optimized throughput vs baseline: 57.92%
- Optimized service p50 vs baseline: -50.60%
- Optimized service p99 vs baseline: -32.80%
- Pipeline throughput vs optimized: -42.27%
- Pipeline service p50 vs optimized: 0.00%
- Pipeline queue p50: 1679917.00 ns
- Pipeline max queue depth: 33657

## Interpretation

- If optimized service latency drops, the matcher itself got faster.
- If pipeline service latency stays close to optimized but end-to-end latency rises, queueing is dominating rather than matching.
