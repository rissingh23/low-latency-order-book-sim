# Benchmark Comparison

- Input: bursty
- Orders: 200000

## Clear Optimizations

1. Baseline -> optimized matcher: direct cancel lookup, cached per-level quantity, and pooled allocators for resting-order storage.
2. Optimized matcher -> pipeline: batched queue draining to reduce handoff overhead.

## Measured Deltas

- Optimized throughput vs baseline: 61.78%
- Optimized service p50 vs baseline: -50.60%
- Optimized service p99 vs baseline: -66.40%
- Pipeline throughput vs optimized: -47.55%
- Pipeline service p50 vs optimized: 0.00%
- Pipeline queue p50: 6751625.00 ns
- Pipeline max queue depth: 65536

## Interpretation

- If optimized service latency drops, the matcher itself got faster.
- If pipeline service latency stays close to optimized but end-to-end latency rises, queueing is dominating rather than matching.
