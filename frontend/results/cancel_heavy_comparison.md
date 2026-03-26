# Benchmark Comparison

- Input: cancel_heavy
- Orders: 200000

## Clear Optimizations

1. Baseline -> optimized matcher: direct cancel lookup, cached per-level quantity, and pooled allocators for resting-order storage.
2. Optimized -> intrusive matcher: indexed order nodes replace per-level list iterators to improve locality on cancels and FIFO walks.
3. Optimized matcher -> pipeline: batched queue draining to reduce handoff overhead.

## Measured Deltas

- Optimized throughput vs baseline: 43.36%
- Optimized service p50 vs baseline: -2.38%
- Optimized service p99 vs baseline: -59.62%
- Intrusive throughput vs optimized: -15.26%
- Intrusive service p99 vs optimized: 48.81%
- Pipeline throughput vs optimized: -52.83%
- Pipeline service p50 vs optimized: 0.00%
- Pipeline queue p50: 6366875.00 ns
- Pipeline max queue depth: 65536

## Interpretation

- If optimized service latency drops, the matcher itself got faster.
- If the intrusive matcher beats the PMR/list-based optimized book, the workload is benefiting from flatter order-node storage and fewer iterator-heavy structures.
- If pipeline service latency stays close to optimized but end-to-end latency rises, queueing is dominating rather than matching.
