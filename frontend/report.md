# Research Report

## Question

Can a deterministic single-book order book engine replay real historical events, extract useful limit-order-book features, and integrate lightweight prediction without losing its low-latency systems story?

## Project Scope

This repo is now two things at once:

- a low-latency C++ matching engine with strict price-time priority
- a deterministic replay + features + inference pipeline on top of real AAPL LOBSTER-style events

The matching core still supports:

- `LIMIT`
- `MARKET`
- `CANCEL`

The replay / inference layer adds:

- deterministic historical event playback
- order-book feature extraction
- offline model evaluation
- staged latency and throughput measurement

## System Architecture

The current architecture is intentionally small:

```text
historical events
  -> ReplayEngine
  -> optimized_single_thread matcher
  -> FeatureExtractor
  -> optional InferenceEngine
  -> metrics / evaluation artifacts
```

This keeps the strongest part of the repo intact: one fast deterministic matcher.

## Engine Variants

### Baseline

- `std::map<price, std::deque<order>>`
- level quantity recomputed by scanning
- cancel scans within a level

### Optimized

- direct order lookup by ID
- cached quantity per price level
- pooled PMR allocators for resting order structures

### Pipeline

- producer -> SPSC queue -> matcher -> SPSC queue -> consumer

Important:

- the pipeline still has one matcher thread
- it is a queueing experiment, not true parallel matching of one book

## Replay / Features / Inference

Real data comes from a normalized AAPL LOBSTER event file. Events are replayed in deterministic sequence order.

After each processed event, the system computes features such as:

- spread
- mid price
- microprice
- top-level imbalance
- 3-level imbalance
- depth slope
- top-of-book depletion imbalance
- market-order ratio
- cancel ratio
- rolling order-flow imbalance
- last mid-price delta
- last microprice delta

The model side compares:

- majority-class baseline
- heuristic imbalance signal
- linear model
- tiny MLP
- XGBoost

## Label Sweep

The project now evaluates multiple target definitions instead of assuming one label is correct.

Tried targets:

- fixed horizon, 10 events
- fixed horizon, 25 events
- fixed horizon, 50 events
- next non-zero move
- thresholded horizon, 10 events, threshold 100
- thresholded horizon, 25 events, threshold 100
- thresholded horizon, 50 events, threshold 100

Best target:

- `threshold_h25_t100`

Meaning:

- look 25 events ahead
- treat moves smaller than 100 price units as flat/noise
- then classify `down` vs `up`

This turned out to be a better prediction target than the earlier noisier binary setup.

## Final Model Results

Current selected evaluation on the AAPL replay:

- target: `threshold_h25_t100`
- train/test split: time-ordered
- selection metric: macro F1, then accuracy

Results:

- majority class: accuracy `0.4687`, macro F1 `0.3191`
- heuristic imbalance: accuracy `0.5601`, macro F1 `0.5547`
- linear model: accuracy `0.4693`, macro F1 `0.3258`
- tiny MLP: accuracy `0.4726`, macro F1 `0.3291`
- XGBoost: accuracy `0.5070`, macro F1 `0.4594`

## Model Latency

The repo now also measures per-example offline inference latency for each model during evaluation.

This is not the same thing as full replay-stage latency.

- model latency = time to score one held-out example in the evaluator
- stage latency = cost of replay + features + inference when integrated into the C++ pipeline

That distinction matters:

- the `Models` tab is about prediction quality and relative model cost
- the `Analysis` tab is about system-level throughput and p50/p99 stage behavior

## Systems Results

### Core engine comparison on AAPL replay

- Baseline: `11.54M ops/s`, service p99 `166 ns`
- Optimized: `18.44M ops/s`, service p99 `84 ns`
- Pipeline: `10.80M ops/s`, service p99 `83 ns`, end-to-end p99 `2.58 ms`

Interpretation:

- the optimized matcher is clearly better than baseline
- the pipeline keeps matcher service time low
- but end-to-end latency is worse because queueing dominates once handoff backlog builds

### Stage-by-stage replay benchmark

- matcher only: `19.13M ops/s`
- replay + matcher: `9.28M ops/s`
- replay + features: `7.36M ops/s`
- replay + features + inference: `7.05M ops/s`

Interpretation:

- replay and features add real but understandable overhead
- inference adds another small step down in throughput
- the core matcher service latency stays low

## Main Findings

### 1. Better data structures mattered more than concurrency

For one order book, the biggest win came from improving the single-threaded matcher itself.

### 2. Target definition mattered more than model complexity

Changing the label from a noisy short-horizon direction target to a thresholded horizon target helped more than switching from linear to neural or boosted models.

### 3. The heuristic is still the best predictor right now

That is not a failure. It means:

- the evaluation is honest
- the pipeline is real
- more model complexity alone does not guarantee better signal

### 4. The project is now both a systems project and an ML-systems project

It demonstrates:

- deterministic replay on real market events
- low-latency matching
- feature extraction
- offline model comparison
- latency/throughput measurement of inference integration

## Limitations

- historical replay only, not a live exchange feed
- one order book only
- no advanced order types
- no strategy / PnL loop yet
- model latency is measured offline per example, not yet exported as a per-event trace from the C++ replay loop

## Takeaway

This project is strongest because it connects:

- market microstructure
- low-latency C++
- deterministic replay
- feature engineering
- lightweight inference
- honest latency vs accuracy tradeoffs

The final result is not “we found alpha.”

The final result is:

> we built a reproducible low-latency market replay and inference pipeline, tested multiple targets and models, and found that better label design and simple heuristics still beat heavier models on this current setup.
