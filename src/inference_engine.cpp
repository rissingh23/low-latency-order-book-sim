#include "lob/inference.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace lob {
namespace {

std::array<double, 3> softmax(std::array<double, 3> logits) {
  const double max_logit = std::max({logits[0], logits[1], logits[2]});
  double sum = 0.0;
  for (double& value : logits) {
    value = std::exp(value - max_logit);
    sum += value;
  }
  if (sum == 0.0) {
    return {0.0, 1.0, 0.0};
  }
  for (double& value : logits) {
    value /= sum;
  }
  return logits;
}

MidPriceMove index_to_move(std::size_t index) {
  switch (index) {
    case 0:
      return MidPriceMove::Down;
    case 1:
      return MidPriceMove::Flat;
    case 2:
      return MidPriceMove::Up;
    default:
      return MidPriceMove::Flat;
  }
}

std::vector<double> parse_line_of_doubles(const std::string& line) {
  std::vector<double> values;
  std::stringstream stream(line);
  std::string token;
  while (std::getline(stream, token, ',')) {
    if (!token.empty()) {
      values.push_back(std::stod(token));
    }
  }
  return values;
}

std::vector<std::string> parse_line_of_strings(const std::string& line) {
  std::vector<std::string> values;
  std::stringstream stream(line);
  std::string token;
  while (std::getline(stream, token, ',')) {
    if (!token.empty() && token.back() == '\r') {
      token.pop_back();
    }
    values.push_back(token);
  }
  return values;
}

}  // namespace

Prediction HeuristicInferenceEngine::predict(const FeatureVector& features) const {
  const double score =
      (0.65 * features.imbalance_l1) +
      (0.45 * features.imbalance_l3) +
      (0.35 * features.top_depletion_imbalance) +
      (0.15 * features.last_microprice_delta) +
      (0.05 * features.order_flow_imbalance);
  MidPriceMove move = MidPriceMove::Flat;
  if (score > 0.15) {
    move = MidPriceMove::Up;
  } else if (score < -0.15) {
    move = MidPriceMove::Down;
  }

  return {
      .move = move,
      .confidence = std::min(1.0, std::abs(score)),
      .logits = {-score, 1.0 - std::abs(score), score},
      .model_name = name(),
  };
}

Prediction LinearInferenceEngine::predict(const FeatureVector& features) const {
  const auto values = features.values();
  std::array<double, 3> logits = {-1.0e9, -1.0e9, -1.0e9};
  for (std::size_t cls = 0; cls < model_.class_count; ++cls) {
    logits[cls] = model_.bias[cls];
    for (std::size_t i = 0; i < values.size(); ++i) {
      const double scale = model_.scale[i] == 0.0 ? 1.0 : model_.scale[i];
      const double normalized = (values[i] - model_.mean[i]) / scale;
      logits[cls] += model_.weights[cls][i] * normalized;
    }
  }

  const auto probs = softmax(logits);
  const auto it = std::max_element(probs.begin(), probs.end());
  const std::size_t index = static_cast<std::size_t>(std::distance(probs.begin(), it));
  MidPriceMove move = index_to_move(index);
  if (model_.class_count == 2) {
    move = index == 0 ? MidPriceMove::Down : MidPriceMove::Up;
  }
  return {
      .move = move,
      .confidence = *it,
      .logits = logits,
      .model_name = model_name_,
  };
}

LinearModel load_linear_model(const std::string& model_path) {
  std::ifstream input(model_path);
  if (!input) {
    throw std::runtime_error("unable to open linear model: " + model_path);
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    lines.push_back(line);
  }

  if (lines.empty()) {
    throw std::runtime_error("linear model file is empty");
  }

  std::size_t row_offset = 0;
  std::string task = "ternary";
  const auto header = parse_line_of_strings(lines[0]);
  if (header.size() == 2 && header[0] == "task") {
    task = header[1];
    row_offset = 1;
  }

  const bool binary = task == "binary";
  const std::size_t expected_rows = binary ? 5 : 6;
  if (lines.size() - row_offset != expected_rows) {
    throw std::runtime_error("linear model file has unexpected row count for task " + task);
  }

  std::vector<std::vector<double>> rows;
  rows.reserve(lines.size() - row_offset);
  for (std::size_t i = row_offset; i < lines.size(); ++i) {
    rows.push_back(parse_line_of_doubles(lines[i]));
  }

  LinearModel model;
  model.class_count = binary ? 2 : 3;
  for (std::size_t i = 0; i < kFeatureCount; ++i) {
    model.mean[i] = rows[0][i];
    model.scale[i] = rows[1][i];
    model.weights[0][i] = rows[3][i];
    if (binary) {
      model.weights[1][i] = rows[4][i];
    } else {
      model.weights[1][i] = rows[4][i];
      model.weights[2][i] = rows[5][i];
    }
  }
  for (std::size_t i = 0; i < rows[2].size() && i < 3; ++i) {
    if (binary) {
      model.bias[i] = rows[2][i];
    } else {
      model.bias[i] = rows[2][i];
    }
  }
  return model;
}

}  // namespace lob
