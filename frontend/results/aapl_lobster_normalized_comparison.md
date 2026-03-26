# Benchmark Comparison

- Input: aapl_lobster_normalized
- Orders: 48389

## Clear Optimizations

1. Baseline -> optimized matcher: direct cancel lookup, cached per-level quantity, and pooled allocators for resting-order storage.
2. Optimized -> intrusive matcher: indexed order nodes replace per-level list iterators to improve locality on cancels and FIFO walks.
3. Optimized matcher -> pipeline: batched queue draining to reduce handoff overhead.

## Measured Deltas

- Optimized throughput vs baseline: 59.19%
- Optimized service p50 vs baseline: -50.60%
- Optimized service p99 vs baseline: -49.40%
- Intrusive throughput vs optimized: -17.73%
- Intrusive service p99 vs optimized: 48.81%
- Pipeline throughput vs optimized: -51.82%
- Pipeline service p50 vs optimized: 0.00%
- Pipeline queue p50: 2429376.00 ns
- Pipeline max queue depth: 33987

## Interpretation

- If optimized service latency drops, the matcher itself got faster.
- If the intrusive matcher beats the PMR/list-based optimized book, the workload is benefiting from flatter order-node storage and fewer iterator-heavy structures.
- If pipeline service latency stays close to optimized but end-to-end latency rises, queueing is dominating rather than matching.
