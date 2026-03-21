# Benchmark Comparison

- Input: balanced
- Orders: 30000000

## Clear Optimizations

1. Baseline -> optimized matcher: direct cancel lookup plus cached per-level quantity.
2. Optimized matcher -> pipeline: batched queue draining to reduce handoff overhead.

## Measured Deltas

- Optimized throughput vs baseline: 458.76%
- Optimized service p50 vs baseline: -33.60%
- Optimized service p99 vs baseline: -96.49%
- Pipeline throughput vs optimized: -38.63%
- Pipeline service p50 vs optimized: -49.40%
- Pipeline queue p50: 12052876.00 ns
- Pipeline max queue depth: 65536

## Interpretation

- If optimized service latency drops, the matcher itself got faster.
- If pipeline service latency stays close to optimized but end-to-end latency rises, queueing is dominating rather than matching.
