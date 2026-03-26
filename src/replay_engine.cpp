#include "lob/replay_engine.hpp"

#include <algorithm>
#include <chrono>

namespace lob {
namespace {

RunSummary summarize_stage(
    std::string name,
    std::string input_label,
    std::size_t order_count,
    std::vector<double> service,
    std::vector<double> end_to_end,
    std::chrono::steady_clock::duration total_duration,
    std::uint64_t total_fills,
    std::uint64_t total_rejects) {
  const double seconds = std::chrono::duration<double>(total_duration).count();
  auto percentile = [](std::vector<double> values, double p) {
    if (values.empty()) {
      return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1))];
  };

  return {
      .engine_name = std::move(name),
      .input_label = std::move(input_label),
      .profile = WorkloadProfile::Balanced,
      .order_count = order_count,
      .throughput_ops_per_sec = seconds > 0.0 ? static_cast<double>(order_count) / seconds : 0.0,
      .service_p50_ns = percentile(service, 0.50),
      .service_p95_ns = percentile(service, 0.95),
      .service_p99_ns = percentile(service, 0.99),
      .end_to_end_p50_ns = percentile(end_to_end, 0.50),
      .end_to_end_p95_ns = percentile(end_to_end, 0.95),
      .end_to_end_p99_ns = percentile(end_to_end, 0.99),
      .queue_delay_p50_ns = 0.0,
      .queue_delay_p95_ns = 0.0,
      .queue_delay_p99_ns = 0.0,
      .max_queue_depth = 0,
      .total_fills = total_fills,
      .total_rejects = total_rejects,
  };
}

class NoOpObserver final : public ReplayObserver {
 public:
  void on_step(const ReplayStep&) override {}
};

}  // namespace

void ReplayEngine::run(IOrderBook& book, const std::vector<OrderEvent>& events, ReplayObserver* observer) const {
  book.reset();
  NoOpObserver no_op;
  ReplayObserver* sink = observer == nullptr ? static_cast<ReplayObserver*>(&no_op) : observer;

  for (const auto& event : events) {
    const auto start = std::chrono::steady_clock::now();
    auto result = book.process_event(event);
    const auto end = std::chrono::steady_clock::now();
    sink->on_step({
        .event = event,
        .result = std::move(result),
        .top = book.snapshot_top_of_book(),
        .depth = book.snapshot_depth(depth_levels_),
        .service_latency_ns = std::chrono::duration<double, std::nano>(end - start).count(),
    });
  }
}

RunSummary run_replay_benchmark(
    IOrderBook& book,
    std::string_view input_label,
    const std::vector<OrderEvent>& events,
    std::size_t depth_levels) {
  std::vector<double> service;
  std::vector<double> end_to_end;
  service.reserve(events.size());
  end_to_end.reserve(events.size());
  std::uint64_t total_fills = 0;
  std::uint64_t total_rejects = 0;

  struct Observer final : ReplayObserver {
    std::vector<double>& service;
    std::vector<double>& end_to_end;
    std::uint64_t& total_fills;
    std::uint64_t& total_rejects;

    Observer(
        std::vector<double>& service,
        std::vector<double>& end_to_end,
        std::uint64_t& total_fills,
        std::uint64_t& total_rejects)
        : service(service), end_to_end(end_to_end), total_fills(total_fills), total_rejects(total_rejects) {}

    void on_step(const ReplayStep& step) override {
      service.push_back(step.service_latency_ns);
      end_to_end.push_back(step.service_latency_ns);
      total_fills += step.result.executions.size();
      total_rejects += step.result.status == EventStatus::Rejected ? 1 : 0;
    }
  } observer{service, end_to_end, total_fills, total_rejects};

  ReplayEngine replay(depth_levels);
  const auto start = std::chrono::steady_clock::now();
  replay.run(book, events, &observer);
  const auto end = std::chrono::steady_clock::now();
  return summarize_stage("replay_matcher", std::string(input_label), events.size(), std::move(service), std::move(end_to_end), end - start, total_fills, total_rejects);
}

RunSummary run_feature_pipeline_benchmark(
    IOrderBook& book,
    FeatureExtractor& feature_extractor,
    std::string_view input_label,
    const std::vector<OrderEvent>& events,
    std::size_t depth_levels) {
  std::vector<double> service;
  std::vector<double> end_to_end;
  service.reserve(events.size());
  end_to_end.reserve(events.size());
  std::uint64_t total_fills = 0;
  std::uint64_t total_rejects = 0;
  feature_extractor.reset();

  struct Observer final : ReplayObserver {
    FeatureExtractor& extractor;
    std::vector<double>& service;
    std::vector<double>& end_to_end;
    std::uint64_t& total_fills;
    std::uint64_t& total_rejects;

    Observer(
        FeatureExtractor& extractor,
        std::vector<double>& service,
        std::vector<double>& end_to_end,
        std::uint64_t& total_fills,
        std::uint64_t& total_rejects)
        : extractor(extractor),
          service(service),
          end_to_end(end_to_end),
          total_fills(total_fills),
          total_rejects(total_rejects) {}

    void on_step(const ReplayStep& step) override {
      const auto feature_start = std::chrono::steady_clock::now();
      (void)extractor.update(step.event, step.result, step.top, step.depth);
      const auto feature_end = std::chrono::steady_clock::now();
      const double end_to_end_ns = step.service_latency_ns
          + std::chrono::duration<double, std::nano>(feature_end - feature_start).count();
      service.push_back(step.service_latency_ns);
      end_to_end.push_back(end_to_end_ns);
      total_fills += step.result.executions.size();
      total_rejects += step.result.status == EventStatus::Rejected ? 1 : 0;
    }
  } observer{feature_extractor, service, end_to_end, total_fills, total_rejects};

  ReplayEngine replay(depth_levels);
  const auto start = std::chrono::steady_clock::now();
  replay.run(book, events, &observer);
  const auto end = std::chrono::steady_clock::now();
  return summarize_stage("replay_features", std::string(input_label), events.size(), std::move(service), std::move(end_to_end), end - start, total_fills, total_rejects);
}

RunSummary run_inference_pipeline_benchmark(
    IOrderBook& book,
    FeatureExtractor& feature_extractor,
    const InferenceEngine& inference_engine,
    std::string_view input_label,
    const std::vector<OrderEvent>& events,
    std::string_view stage_name,
    std::size_t depth_levels) {
  std::vector<double> service;
  std::vector<double> end_to_end;
  service.reserve(events.size());
  end_to_end.reserve(events.size());
  std::uint64_t total_fills = 0;
  std::uint64_t total_rejects = 0;
  feature_extractor.reset();

  struct Observer final : ReplayObserver {
    FeatureExtractor& extractor;
    const InferenceEngine& inference_engine;
    std::vector<double>& service;
    std::vector<double>& end_to_end;
    std::uint64_t& total_fills;
    std::uint64_t& total_rejects;

    Observer(
        FeatureExtractor& extractor,
        const InferenceEngine& inference_engine,
        std::vector<double>& service,
        std::vector<double>& end_to_end,
        std::uint64_t& total_fills,
        std::uint64_t& total_rejects)
        : extractor(extractor),
          inference_engine(inference_engine),
          service(service),
          end_to_end(end_to_end),
          total_fills(total_fills),
          total_rejects(total_rejects) {}

    void on_step(const ReplayStep& step) override {
      const auto stage_start = std::chrono::steady_clock::now();
      const auto features = extractor.update(step.event, step.result, step.top, step.depth);
      (void)inference_engine.predict(features);
      const auto stage_end = std::chrono::steady_clock::now();
      const double end_to_end_ns = step.service_latency_ns
          + std::chrono::duration<double, std::nano>(stage_end - stage_start).count();
      service.push_back(step.service_latency_ns);
      end_to_end.push_back(end_to_end_ns);
      total_fills += step.result.executions.size();
      total_rejects += step.result.status == EventStatus::Rejected ? 1 : 0;
    }
  } observer{feature_extractor, inference_engine, service, end_to_end, total_fills, total_rejects};

  ReplayEngine replay(depth_levels);
  const auto start = std::chrono::steady_clock::now();
  replay.run(book, events, &observer);
  const auto end = std::chrono::steady_clock::now();
  return summarize_stage(std::string(stage_name), std::string(input_label), events.size(), std::move(service), std::move(end_to_end), end - start, total_fills, total_rejects);
}

}  // namespace lob
