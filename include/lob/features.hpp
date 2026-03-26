#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "lob/order_book.hpp"

namespace lob {

inline constexpr std::size_t kFeatureCount = 12;

enum class MidPriceMove {
  Down = -1,
  Flat = 0,
  Up = 1,
};

enum class LabelMode {
  FixedHorizon,
  NextNonZero,
  ThresholdedHorizon,
};

struct FeatureVector {
  std::uint64_t sequence{};
  Timestamp timestamp{};
  double spread{};
  double mid_price{};
  double microprice{};
  double imbalance_l1{};
  double imbalance_l3{};
  double depth_slope{};
  double top_depletion_imbalance{};
  double market_order_ratio{};
  double cancel_ratio{};
  double order_flow_imbalance{};
  double last_mid_delta{};
  double last_microprice_delta{};

  [[nodiscard]] std::array<double, kFeatureCount> values() const {
    return {
        spread,
        mid_price,
        microprice,
        imbalance_l1,
        imbalance_l3,
        depth_slope,
        top_depletion_imbalance,
        market_order_ratio,
        cancel_ratio,
        order_flow_imbalance,
        last_mid_delta,
        last_microprice_delta,
    };
  }
};

struct LabeledFeatureRow {
  FeatureVector features{};
  MidPriceMove label{MidPriceMove::Flat};
};

class FeatureExtractor {
 public:
  explicit FeatureExtractor(std::size_t depth_levels = 3, std::size_t flow_window = 32);

  void reset();
  FeatureVector update(const OrderEvent& event, const ProcessResult& result, const TopOfBook& top, const BookDepth& depth);

 private:
  std::size_t depth_levels_{3};
  std::size_t flow_window_{32};
  std::vector<double> signed_flow_window_{};
  std::vector<OrderType> type_window_{};
  std::size_t signed_flow_cursor_{0};
  std::size_t signed_flow_count_{0};
  double signed_flow_sum_{0.0};
  double previous_mid_{0.0};
  double previous_microprice_{0.0};
  Qty previous_best_bid_qty_{0};
  Qty previous_best_ask_qty_{0};
  bool has_previous_mid_{false};
};

std::vector<LabeledFeatureRow> build_labeled_feature_rows(
    IOrderBook& book,
    const std::vector<OrderEvent>& events,
    std::size_t depth_levels,
    std::size_t horizon_events,
    LabelMode label_mode = LabelMode::FixedHorizon,
    double move_threshold = 0.0);

void write_feature_dataset_csv(const std::string& output_path, const std::vector<LabeledFeatureRow>& rows);

std::string to_string(MidPriceMove move);
std::string to_string(LabelMode mode);
LabelMode parse_label_mode(std::string_view value);

}  // namespace lob
