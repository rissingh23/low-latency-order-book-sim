# Benchmark Comparison

- Input: cancel_heavy
- Orders: 200000

## Clear Optimizations

1. Baseline -> optimized matcher: direct cancel lookup, cached per-level quantity, and pooled allocators for resting-order storage.
2. Optimized matcher -> pipeline: batched queue draining to reduce handoff overhead.

## Measured Deltas

- Optimized throughput vs baseline: 131.23%
- Optimized service p50 vs baseline: -2.38%
- Optimized service p99 vs baseline: -75.08%
- Pipeline throughput vs optimized: -53.48%
- Pipeline service p50 vs optimized: 0.00%
- Pipeline queue p50: 6032375.00 ns
- Pipeline max queue depth: 65536

## Interpretation

- If optimized service latency drops, the matcher itself got faster.
- If pipeline service latency stays close to optimized but end-to-end latency rises, queueing is dominating rather than matching.
