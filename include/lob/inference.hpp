#pragma once

#include <array>
#include <string>
#include <vector>

#include "lob/features.hpp"

namespace lob {

struct Prediction {
  MidPriceMove move{MidPriceMove::Flat};
  double confidence{};
  std::array<double, 3> logits{};
  std::string model_name{};
};

struct LinearModel {
  std::size_t class_count{3};
  std::array<double, kFeatureCount> mean{};
  std::array<double, kFeatureCount> scale{};
  std::array<double, 3> bias{};
  std::array<std::array<double, kFeatureCount>, 3> weights{};
};

class InferenceEngine {
 public:
  virtual ~InferenceEngine() = default;
  virtual const char* name() const = 0;
  virtual Prediction predict(const FeatureVector& features) const = 0;
};

class HeuristicInferenceEngine final : public InferenceEngine {
 public:
  const char* name() const override { return "heuristic_imbalance"; }
  Prediction predict(const FeatureVector& features) const override;
};

class LinearInferenceEngine final : public InferenceEngine {
 public:
  explicit LinearInferenceEngine(LinearModel model, std::string model_name = "linear_model")
      : model_(std::move(model)), model_name_(std::move(model_name)) {}

  const char* name() const override { return model_name_.c_str(); }
  Prediction predict(const FeatureVector& features) const override;

 private:
  LinearModel model_;
  std::string model_name_;
};

LinearModel load_linear_model(const std::string& model_path);

}  // namespace lob
