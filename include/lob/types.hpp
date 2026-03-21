#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lob {

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Qty = std::uint32_t;
using Timestamp = std::uint64_t;

enum class Side {
  Buy,
  Sell,
};

enum class OrderType {
  Limit,
  Market,
  Cancel,
};

enum class EventStatus {
  Accepted,
  PartiallyFilled,
  Filled,
  Cancelled,
  Rejected,
};

enum class WorkloadProfile {
  Balanced,
  CancelHeavy,
  Bursty,
};

struct OrderEvent {
  Timestamp timestamp{};
  std::uint64_t sequence{};
  OrderType type{OrderType::Limit};
  Side side{Side::Buy};
  OrderId order_id{};
  Price price{};
  Qty qty{};
};

struct Execution {
  OrderId resting_order_id{};
  OrderId aggressive_order_id{};
  Price price{};
  Qty qty{};
  Timestamp timestamp{};
  bool aggressive_is_buy{};
};

struct TopOfBook {
  std::optional<Price> best_bid{};
  std::optional<Qty> best_bid_qty{};
  std::optional<Price> best_ask{};
  std::optional<Qty> best_ask_qty{};
};

struct DepthLevel {
  Price price{};
  Qty qty{};
};

struct BookDepth {
  std::vector<DepthLevel> bids{};
  std::vector<DepthLevel> asks{};
};

struct ProcessResult {
  EventStatus status{EventStatus::Accepted};
  Qty filled_qty{};
  Qty remaining_qty{};
  std::vector<Execution> executions{};
  std::string message{};
};

struct RunSummary {
  std::string engine_name{};
  std::string input_label{};
  WorkloadProfile profile{WorkloadProfile::Balanced};
  std::size_t order_count{};
  double throughput_ops_per_sec{};
  double service_p50_ns{};
  double service_p95_ns{};
  double service_p99_ns{};
  double end_to_end_p50_ns{};
  double end_to_end_p95_ns{};
  double end_to_end_p99_ns{};
  double queue_delay_p50_ns{};
  double queue_delay_p95_ns{};
  double queue_delay_p99_ns{};
  std::uint64_t max_queue_depth{};
  std::uint64_t total_fills{};
  std::uint64_t total_rejects{};
};

inline std::string_view to_string(Side side) {
  return side == Side::Buy ? "BUY" : "SELL";
}

inline std::string_view to_string(OrderType type) {
  switch (type) {
    case OrderType::Limit:
      return "LIMIT";
    case OrderType::Market:
      return "MARKET";
    case OrderType::Cancel:
      return "CANCEL";
  }
  return "UNKNOWN";
}

inline std::string_view to_string(WorkloadProfile profile) {
  switch (profile) {
    case WorkloadProfile::Balanced:
      return "balanced";
    case WorkloadProfile::CancelHeavy:
      return "cancel_heavy";
    case WorkloadProfile::Bursty:
      return "bursty";
  }
  return "unknown";
}

}  // namespace lob
