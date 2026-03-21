#include "lob/order_book.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <unordered_map>

namespace lob {
namespace {

struct BaselineOrder {
  OrderId order_id{};
  Price price{};
  Qty qty{};
  Timestamp timestamp{};
};

struct OrderLocator {
  Side side{Side::Buy};
  Price price{};
};

class BaselineOrderBook final : public IOrderBook {
 public:
  const char* name() const override { return "baseline_single_thread"; }

  void reset() override {
    bids_.clear();
    asks_.clear();
    index_.clear();
  }

  ProcessResult process_event(const OrderEvent& event) override {
    switch (event.type) {
      case OrderType::Limit:
        return handle_limit(event);
      case OrderType::Market:
        return handle_market(event);
      case OrderType::Cancel:
        return handle_cancel(event);
    }
    return {.status = EventStatus::Rejected, .message = "unsupported event"};
  }

  TopOfBook snapshot_top_of_book() const override {
    TopOfBook snapshot;
    if (!bids_.empty()) {
      const auto& [price, orders] = *bids_.begin();
      snapshot.best_bid = price;
      snapshot.best_bid_qty = aggregate_qty(orders);
    }
    if (!asks_.empty()) {
      const auto& [price, orders] = *asks_.begin();
      snapshot.best_ask = price;
      snapshot.best_ask_qty = aggregate_qty(orders);
    }
    return snapshot;
  }

  BookDepth snapshot_depth(std::size_t levels) const override {
    BookDepth depth;
    depth.bids.reserve(levels);
    depth.asks.reserve(levels);

    for (auto it = bids_.begin(); it != bids_.end() && depth.bids.size() < levels; ++it) {
      depth.bids.push_back({.price = it->first, .qty = aggregate_qty(it->second)});
    }
    for (auto it = asks_.begin(); it != asks_.end() && depth.asks.size() < levels; ++it) {
      depth.asks.push_back({.price = it->first, .qty = aggregate_qty(it->second)});
    }
    return depth;
  }

 private:
  using BidMap = std::map<Price, std::deque<BaselineOrder>, std::greater<Price>>;
  using AskMap = std::map<Price, std::deque<BaselineOrder>, std::less<Price>>;

  static Qty aggregate_qty(const std::deque<BaselineOrder>& orders) {
    Qty total = 0;
    for (const auto& order : orders) {
      total += order.qty;
    }
    return total;
  }

  ProcessResult handle_limit(const OrderEvent& event) {
    if (event.qty == 0) {
      return {.status = EventStatus::Rejected, .message = "zero quantity"};
    }
    if (index_.contains(event.order_id)) {
      return {.status = EventStatus::Rejected, .message = "duplicate order id"};
    }

    Qty remaining = event.qty;
    std::vector<Execution> executions;
    match_order(event, remaining, executions, false);

    if (remaining > 0) {
      add_resting(event, remaining);
    }

    return finalize_result(event, remaining, std::move(executions));
  }

  ProcessResult handle_market(const OrderEvent& event) {
    if (event.qty == 0) {
      return {.status = EventStatus::Rejected, .message = "zero quantity"};
    }

    Qty remaining = event.qty;
    std::vector<Execution> executions;
    match_order(event, remaining, executions, true);

    return finalize_result(event, remaining, std::move(executions));
  }

  ProcessResult handle_cancel(const OrderEvent& event) {
    const auto index_it = index_.find(event.order_id);
    if (index_it == index_.end()) {
      return {.status = EventStatus::Rejected, .message = "order not found"};
    }

    const auto locator = index_it->second;
    Qty cancelled_qty = 0;
    Qty remaining_qty = 0;
    auto erase_from_level = [&](auto& levels) {
      auto level_it = levels.find(locator.price);
      if (level_it == levels.end()) {
        return false;
      }
      auto& orders = level_it->second;
      const auto it = std::find_if(orders.begin(), orders.end(), [&](const auto& order) {
        return order.order_id == event.order_id;
      });
      if (it == orders.end()) {
        return false;
      }
      cancelled_qty = event.qty == 0 ? it->qty : std::min(event.qty, it->qty);
      it->qty -= cancelled_qty;
      remaining_qty = it->qty;
      if (it->qty == 0) {
        orders.erase(it);
        if (orders.empty()) {
          levels.erase(level_it);
        }
      }
      return true;
    };

    const bool erased = locator.side == Side::Buy ? erase_from_level(bids_) : erase_from_level(asks_);

    if (!erased) {
      return {.status = EventStatus::Rejected, .message = "order index corrupted"};
    }

    if (remaining_qty == 0) {
      index_.erase(index_it);
    }

    return {
        .status = EventStatus::Cancelled,
        .filled_qty = 0,
        .remaining_qty = remaining_qty,
        .message = remaining_qty == 0 ? "cancelled" : "partially cancelled",
    };
  }

  void add_resting(const OrderEvent& event, Qty remaining) {
    BaselineOrder order{
        .order_id = event.order_id,
        .price = event.price,
        .qty = remaining,
        .timestamp = event.timestamp,
    };

    if (event.side == Side::Buy) {
      bids_[event.price].push_back(order);
    } else {
      asks_[event.price].push_back(order);
    }
    index_[event.order_id] = {.side = event.side, .price = event.price};
  }

  void match_order(const OrderEvent& event, Qty& remaining, std::vector<Execution>& executions, bool is_market) {
    auto crosses = [&](Price best_price) {
      if (is_market) {
        return true;
      }
      if (event.side == Side::Buy) {
        return event.price >= best_price;
      }
      return event.price <= best_price;
    };

    if (event.side == Side::Buy) {
      while (remaining > 0 && !asks_.empty() && crosses(asks_.begin()->first)) {
        auto level_it = asks_.begin();
        auto& queue = level_it->second;
        while (remaining > 0 && !queue.empty()) {
          auto& resting = queue.front();
          const Qty trade_qty = std::min(remaining, resting.qty);
          executions.push_back(make_execution(resting, event, trade_qty));
          remaining -= trade_qty;
          resting.qty -= trade_qty;
          if (resting.qty == 0) {
            index_.erase(resting.order_id);
            queue.pop_front();
          }
        }
        if (queue.empty()) {
          asks_.erase(level_it);
        }
      }
    } else {
      while (remaining > 0 && !bids_.empty() && crosses(bids_.begin()->first)) {
        auto level_it = bids_.begin();
        auto& queue = level_it->second;
        while (remaining > 0 && !queue.empty()) {
          auto& resting = queue.front();
          const Qty trade_qty = std::min(remaining, resting.qty);
          executions.push_back(make_execution(resting, event, trade_qty));
          remaining -= trade_qty;
          resting.qty -= trade_qty;
          if (resting.qty == 0) {
            index_.erase(resting.order_id);
            queue.pop_front();
          }
        }
        if (queue.empty()) {
          bids_.erase(level_it);
        }
      }
    }
  }

  static Execution make_execution(const BaselineOrder& resting, const OrderEvent& aggressive, Qty qty) {
    return {
        .resting_order_id = resting.order_id,
        .aggressive_order_id = aggressive.order_id,
        .price = resting.price,
        .qty = qty,
        .timestamp = aggressive.timestamp,
        .aggressive_is_buy = aggressive.side == Side::Buy,
    };
  }

  static ProcessResult finalize_result(
      const OrderEvent& event,
      Qty remaining,
      std::vector<Execution> executions) {
    const Qty filled = event.qty - remaining;
    EventStatus status = EventStatus::Accepted;
    if (filled > 0 && remaining > 0) {
      status = EventStatus::PartiallyFilled;
    } else if (filled == event.qty && event.qty > 0) {
      status = EventStatus::Filled;
    }

    return {
        .status = status,
        .filled_qty = filled,
        .remaining_qty = remaining,
        .executions = std::move(executions),
    };
  }

  BidMap bids_;
  AskMap asks_;
  std::unordered_map<OrderId, OrderLocator> index_;
};

}  // namespace

std::unique_ptr<IOrderBook> make_baseline_order_book() {
  return std::make_unique<BaselineOrderBook>();
}

}  // namespace lob
