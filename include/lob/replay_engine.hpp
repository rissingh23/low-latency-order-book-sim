#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "lob/features.hpp"
#include "lob/inference.hpp"

namespace lob {

struct ReplayStep {
  OrderEvent event{};
  ProcessResult result{};
  TopOfBook top{};
  BookDepth depth{};
  double service_latency_ns{};
};

class ReplayObserver {
 public:
  virtual ~ReplayObserver() = default;
  virtual void on_step(const ReplayStep& step) = 0;
};

class ReplayEngine {
 public:
  explicit ReplayEngine(std::size_t depth_levels = 3) : depth_levels_(depth_levels) {}

  void run(IOrderBook& book, const std::vector<OrderEvent>& events, ReplayObserver* observer = nullptr) const;

 private:
  std::size_t depth_levels_{3};
};

RunSummary run_replay_benchmark(
    IOrderBook& book,
    std::string_view input_label,
    const std::vector<OrderEvent>& events,
    std::size_t depth_levels = 3);

RunSummary run_feature_pipeline_benchmark(
    IOrderBook& book,
    FeatureExtractor& feature_extractor,
    std::string_view input_label,
    const std::vector<OrderEvent>& events,
    std::size_t depth_levels = 3);

RunSummary run_inference_pipeline_benchmark(
    IOrderBook& book,
    FeatureExtractor& feature_extractor,
    const InferenceEngine& inference_engine,
    std::string_view input_label,
    const std::vector<OrderEvent>& events,
    std::string_view stage_name = "replay_features_inference",
    std::size_t depth_levels = 3);

}  // namespace lob
