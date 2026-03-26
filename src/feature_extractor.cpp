#include "lob/features.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "lob/replay_engine.hpp"

namespace lob {
namespace {

double imbalance(const std::vector<DepthLevel>& bids, const std::vector<DepthLevel>& asks, std::size_t levels) {
  double bid_qty = 0.0;
  double ask_qty = 0.0;
  for (std::size_t i = 0; i < std::min(levels, bids.size()); ++i) {
    bid_qty += static_cast<double>(bids[i].qty);
  }
  for (std::size_t i = 0; i < std::min(levels, asks.size()); ++i) {
    ask_qty += static_cast<double>(asks[i].qty);
  }
  const double total = bid_qty + ask_qty;
  return total == 0.0 ? 0.0 : (bid_qty - ask_qty) / total;
}

double mid_price(const TopOfBook& top) {
  if (!top.best_bid || !top.best_ask) {
    return 0.0;
  }
  return (static_cast<double>(*top.best_bid) + static_cast<double>(*top.best_ask)) * 0.5;
}

double microprice_value(const TopOfBook& top) {
  if (!top.best_bid || !top.best_ask || !top.best_bid_qty || !top.best_ask_qty) {
    return 0.0;
  }
  const double bid = static_cast<double>(*top.best_bid);
  const double ask = static_cast<double>(*top.best_ask);
  const double bid_qty = static_cast<double>(*top.best_bid_qty);
  const double ask_qty = static_cast<double>(*top.best_ask_qty);
  const double total = bid_qty + ask_qty;
  return total == 0.0 ? 0.0 : ((ask * bid_qty) + (bid * ask_qty)) / total;
}

double spread_value(const TopOfBook& top) {
  if (!top.best_bid || !top.best_ask) {
    return 0.0;
  }
  return static_cast<double>(*top.best_ask - *top.best_bid);
}

double book_slope_value(const BookDepth& depth) {
  auto side_slope = [](const std::vector<DepthLevel>& levels, bool bid_side) {
    if (levels.size() < 2) {
      return 0.0;
    }
    const double best = static_cast<double>(levels.front().price);
    double weighted_distance = 0.0;
    double qty_total = 0.0;
    for (const auto& level : levels) {
      const double px = static_cast<double>(level.price);
      const double distance = bid_side ? (best - px) : (px - best);
      weighted_distance += distance * static_cast<double>(level.qty);
      qty_total += static_cast<double>(level.qty);
    }
    return qty_total == 0.0 ? 0.0 : weighted_distance / qty_total;
  };
  return side_slope(depth.asks, false) - side_slope(depth.bids, true);
}

double signed_order_flow(const OrderEvent& event, const ProcessResult& result) {
  const double sign = event.side == Side::Buy ? 1.0 : -1.0;
  switch (event.type) {
    case OrderType::Limit:
      return sign * static_cast<double>(result.remaining_qty);
    case OrderType::Market:
      return sign * static_cast<double>(result.filled_qty);
    case OrderType::Cancel:
      return -sign * static_cast<double>(event.qty);
  }
  return 0.0;
}

MidPriceMove label_from_mid(double current_mid, double future_mid) {
  if (future_mid > current_mid) {
    return MidPriceMove::Up;
  }
  if (future_mid < current_mid) {
    return MidPriceMove::Down;
  }
  return MidPriceMove::Flat;
}

MidPriceMove label_from_threshold(double current_mid, double future_mid, double threshold) {
  const double delta = future_mid - current_mid;
  if (delta > threshold) {
    return MidPriceMove::Up;
  }
  if (delta < -threshold) {
    return MidPriceMove::Down;
  }
  return MidPriceMove::Flat;
}

}  // namespace

FeatureExtractor::FeatureExtractor(std::size_t depth_levels, std::size_t flow_window)
    : depth_levels_(depth_levels),
      flow_window_(flow_window),
      signed_flow_window_(flow_window, 0.0),
      type_window_(flow_window, OrderType::Limit) {}

void FeatureExtractor::reset() {
  std::fill(signed_flow_window_.begin(), signed_flow_window_.end(), 0.0);
  signed_flow_cursor_ = 0;
  signed_flow_count_ = 0;
  signed_flow_sum_ = 0.0;
  previous_mid_ = 0.0;
  previous_microprice_ = 0.0;
  previous_best_bid_qty_ = 0;
  previous_best_ask_qty_ = 0;
  has_previous_mid_ = false;
}

FeatureVector FeatureExtractor::update(
    const OrderEvent& event,
    const ProcessResult& result,
    const TopOfBook& top,
    const BookDepth& depth) {
  const double current_mid = mid_price(top);
  const double current_microprice = microprice_value(top);
  const double flow = signed_order_flow(event, result);
  if (!signed_flow_window_.empty()) {
    signed_flow_sum_ -= signed_flow_window_[signed_flow_cursor_];
    signed_flow_window_[signed_flow_cursor_] = flow;
    signed_flow_sum_ += flow;
    type_window_[signed_flow_cursor_] = event.type;
    signed_flow_cursor_ = (signed_flow_cursor_ + 1) % signed_flow_window_.size();
    signed_flow_count_ = std::min(signed_flow_count_ + 1, signed_flow_window_.size());
  }

  const double order_flow_imbalance = signed_flow_count_ == 0
      ? 0.0
      : signed_flow_sum_ / static_cast<double>(signed_flow_count_);
  const double mid_delta = has_previous_mid_ ? current_mid - previous_mid_ : 0.0;
  const double microprice_delta = has_previous_mid_ ? current_microprice - previous_microprice_ : 0.0;
  std::size_t market_count = 0;
  std::size_t cancel_count = 0;
  for (std::size_t i = 0; i < signed_flow_count_; ++i) {
    market_count += type_window_[i] == OrderType::Market;
    cancel_count += type_window_[i] == OrderType::Cancel;
  }
  const double window_size = signed_flow_count_ == 0 ? 1.0 : static_cast<double>(signed_flow_count_);
  const double market_order_ratio = static_cast<double>(market_count) / window_size;
  const double cancel_ratio = static_cast<double>(cancel_count) / window_size;
  const double current_bid_qty = top.best_bid_qty ? static_cast<double>(*top.best_bid_qty) : 0.0;
  const double current_ask_qty = top.best_ask_qty ? static_cast<double>(*top.best_ask_qty) : 0.0;
  const double bid_depletion = static_cast<double>(previous_best_bid_qty_) - current_bid_qty;
  const double ask_depletion = static_cast<double>(previous_best_ask_qty_) - current_ask_qty;
  const double depletion_total = std::abs(bid_depletion) + std::abs(ask_depletion);
  const double top_depletion_imbalance = depletion_total == 0.0 ? 0.0 : (ask_depletion - bid_depletion) / depletion_total;
  previous_mid_ = current_mid;
  previous_microprice_ = current_microprice;
  previous_best_bid_qty_ = top.best_bid_qty.value_or(0);
  previous_best_ask_qty_ = top.best_ask_qty.value_or(0);
  has_previous_mid_ = true;

  return {
      .sequence = event.sequence,
      .timestamp = event.timestamp,
      .spread = spread_value(top),
      .mid_price = current_mid,
      .microprice = current_microprice,
      .imbalance_l1 = imbalance(depth.bids, depth.asks, 1),
      .imbalance_l3 = imbalance(depth.bids, depth.asks, depth_levels_),
      .depth_slope = book_slope_value(depth),
      .top_depletion_imbalance = top_depletion_imbalance,
      .market_order_ratio = market_order_ratio,
      .cancel_ratio = cancel_ratio,
      .order_flow_imbalance = order_flow_imbalance,
      .last_mid_delta = mid_delta,
      .last_microprice_delta = microprice_delta,
  };
}

std::vector<LabeledFeatureRow> build_labeled_feature_rows(
    IOrderBook& book,
    const std::vector<OrderEvent>& events,
    std::size_t depth_levels,
    std::size_t horizon_events,
    LabelMode label_mode,
    double move_threshold) {
  book.reset();
  FeatureExtractor extractor(depth_levels);

  std::vector<FeatureVector> features;
  features.reserve(events.size());

  ReplayEngine replay(depth_levels);
  struct Collector final : ReplayObserver {
    FeatureExtractor& extractor;
    std::vector<FeatureVector>& out;

    Collector(FeatureExtractor& extractor, std::vector<FeatureVector>& out) : extractor(extractor), out(out) {}

    void on_step(const ReplayStep& step) override {
      out.push_back(extractor.update(step.event, step.result, step.top, step.depth));
    }
  } collector{extractor, features};

  replay.run(book, events, &collector);

  std::vector<LabeledFeatureRow> rows;
  if (features.size() <= horizon_events) {
    return rows;
  }

  rows.reserve(features.size() - horizon_events);
  for (std::size_t i = 0; i + horizon_events < features.size(); ++i) {
    if (features[i].mid_price == 0.0) {
      continue;
    }
    MidPriceMove label = MidPriceMove::Flat;
    if (label_mode == LabelMode::FixedHorizon) {
      if (features[i + horizon_events].mid_price == 0.0) {
        continue;
      }
      label = label_from_mid(features[i].mid_price, features[i + horizon_events].mid_price);
    } else if (label_mode == LabelMode::ThresholdedHorizon) {
      if (features[i + horizon_events].mid_price == 0.0) {
        continue;
      }
      label = label_from_threshold(features[i].mid_price, features[i + horizon_events].mid_price, move_threshold);
    } else {
      std::size_t j = i + 1;
      while (j < features.size() && features[j].mid_price == features[i].mid_price) {
        ++j;
      }
      if (j >= features.size() || features[j].mid_price == 0.0) {
        continue;
      }
      label = label_from_mid(features[i].mid_price, features[j].mid_price);
    }
    rows.push_back({
        .features = features[i],
        .label = label,
    });
  }
  return rows;
}

void write_feature_dataset_csv(const std::string& output_path, const std::vector<LabeledFeatureRow>& rows) {
  std::ofstream out(output_path);
  out << "sequence,timestamp,spread,mid_price,microprice,imbalance_l1,imbalance_l3,depth_slope,top_depletion_imbalance,market_order_ratio,cancel_ratio,order_flow_imbalance,last_mid_delta,last_microprice_delta,label\n";
  for (const auto& row : rows) {
    out << row.features.sequence << ','
        << row.features.timestamp << ','
        << row.features.spread << ','
        << row.features.mid_price << ','
        << row.features.microprice << ','
        << row.features.imbalance_l1 << ','
        << row.features.imbalance_l3 << ','
        << row.features.depth_slope << ','
        << row.features.top_depletion_imbalance << ','
        << row.features.market_order_ratio << ','
        << row.features.cancel_ratio << ','
        << row.features.order_flow_imbalance << ','
        << row.features.last_mid_delta << ','
        << row.features.last_microprice_delta << ','
        << to_string(row.label) << '\n';
  }
}

std::string to_string(MidPriceMove move) {
  switch (move) {
    case MidPriceMove::Down:
      return "down";
    case MidPriceMove::Flat:
      return "flat";
    case MidPriceMove::Up:
      return "up";
  }
  return "flat";
}

std::string to_string(LabelMode mode) {
  switch (mode) {
    case LabelMode::FixedHorizon:
      return "fixed_horizon";
    case LabelMode::NextNonZero:
      return "next_non_zero";
    case LabelMode::ThresholdedHorizon:
      return "thresholded_horizon";
  }
  return "fixed_horizon";
}

LabelMode parse_label_mode(std::string_view value) {
  if (value == "fixed_horizon" || value == "fixed-horizon") {
    return LabelMode::FixedHorizon;
  }
  if (value == "next_non_zero" || value == "next-non-zero") {
    return LabelMode::NextNonZero;
  }
  if (value == "thresholded_horizon" || value == "thresholded-horizon" || value == "thresholded") {
    return LabelMode::ThresholdedHorizon;
  }
  throw std::runtime_error("unknown label mode: " + std::string(value));
}

}  // namespace lob
