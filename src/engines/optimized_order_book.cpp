#include "lob/order_book.hpp"

#include <algorithm>
#include <list>
#include <map>
#include <memory_resource>
#include <unordered_map>
#include <utility>

namespace lob {
namespace {

struct OrderNode {
  OrderId order_id{};
  Price price{};
  Qty qty{};
  Timestamp timestamp{};
};

struct LevelData {
  std::pmr::list<OrderNode> orders;
  Qty total_qty{};

  explicit LevelData(std::pmr::memory_resource* memory_resource = std::pmr::get_default_resource())
      : orders(memory_resource) {}
};

template <typename Comparator>
using LevelMap = std::pmr::map<Price, LevelData, Comparator>;

struct OrderHandle {
  Side side{Side::Buy};
  Price price{};
  LevelMap<std::greater<Price>>::iterator bid_level{};
  LevelMap<std::less<Price>>::iterator ask_level{};
  // Direct iterator access is the key baseline -> optimized improvement for cancels.
  std::list<OrderNode>::iterator order_it{};
};

class OptimizedOrderBook final : public IOrderBook {
 public:
  explicit OptimizedOrderBook(std::size_t expected_orders)
      : order_pool_(),
        bids_(&order_pool_),
        asks_(&order_pool_),
        order_index_(&order_pool_) {
    // Reserve once so the hot path is less likely to rehash under load.
    order_index_.reserve(expected_orders);
  }

  const char* name() const override { return "optimized_single_thread"; }

  void reset() override {
    bids_.clear();
    asks_.clear();
    order_index_.clear();
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
      const auto& [price, level] = *bids_.begin();
      snapshot.best_bid = price;
      // Unlike the baseline, aggregated quantity is cached on every update.
      snapshot.best_bid_qty = level.total_qty;
    }
    if (!asks_.empty()) {
      const auto& [price, level] = *asks_.begin();
      snapshot.best_ask = price;
      snapshot.best_ask_qty = level.total_qty;
    }
    return snapshot;
  }

  BookDepth snapshot_depth(std::size_t levels) const override {
    BookDepth depth;
    depth.bids.reserve(levels);
    depth.asks.reserve(levels);

    for (auto it = bids_.begin(); it != bids_.end() && depth.bids.size() < levels; ++it) {
      depth.bids.push_back({.price = it->first, .qty = it->second.total_qty});
    }
    for (auto it = asks_.begin(); it != asks_.end() && depth.asks.size() < levels; ++it) {
      depth.asks.push_back({.price = it->first, .qty = it->second.total_qty});
    }
    return depth;
  }

 private:
  ProcessResult handle_limit(const OrderEvent& event) {
    if (event.qty == 0) {
      return {.status = EventStatus::Rejected, .message = "zero quantity"};
    }
    if (order_index_.contains(event.order_id)) {
      return {.status = EventStatus::Rejected, .message = "duplicate order id"};
    }

    Qty remaining = event.qty;
    std::vector<Execution> executions;
    executions.reserve(8);
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
    executions.reserve(8);
    match_order(event, remaining, executions, true);
    return finalize_result(event, remaining, std::move(executions));
  }

  ProcessResult handle_cancel(const OrderEvent& event) {
    const auto it = order_index_.find(event.order_id);
    if (it == order_index_.end()) {
      return {.status = EventStatus::Rejected, .message = "order not found"};
    }

    auto& handle = it->second;
    // The order handle points straight at the resting node, so cancel avoids
    // the baseline's per-level scan.
    const Qty cancelled_qty = event.qty == 0 ? handle.order_it->qty : std::min(event.qty, handle.order_it->qty);
    const Qty remaining_qty = handle.order_it->qty - cancelled_qty;
    if (handle.side == Side::Buy) {
      auto level_it = handle.bid_level;
      level_it->second.total_qty -= cancelled_qty;
      handle.order_it->qty = remaining_qty;
      if (handle.order_it->qty == 0) {
        level_it->second.orders.erase(handle.order_it);
        if (level_it->second.orders.empty()) {
          bids_.erase(level_it);
        }
      }
    } else {
      auto level_it = handle.ask_level;
      level_it->second.total_qty -= cancelled_qty;
      handle.order_it->qty = remaining_qty;
      if (handle.order_it->qty == 0) {
        level_it->second.orders.erase(handle.order_it);
        if (level_it->second.orders.empty()) {
          asks_.erase(level_it);
        }
      }
    }
    if (remaining_qty == 0) {
      order_index_.erase(it);
    }

    return {
        .status = EventStatus::Cancelled,
        .filled_qty = 0,
        .remaining_qty = remaining_qty,
        .message = remaining_qty == 0 ? "cancelled" : "partially cancelled",
    };
  }

  void add_resting(const OrderEvent& event, Qty remaining) {
    OrderNode order{
        .order_id = event.order_id,
        .price = event.price,
        .qty = remaining,
        .timestamp = event.timestamp,
    };

    if (event.side == Side::Buy) {
      // PMR containers route node allocations through the pool resource, which
      // reduces small-allocation overhead for resting order state.
      auto [level_it, inserted] = bids_.try_emplace(event.price, &order_pool_);
      (void)inserted;
      level_it->second.orders.push_back(order);
      level_it->second.total_qty += remaining;
      auto order_it = std::prev(level_it->second.orders.end());
      order_index_[event.order_id] = {
          .side = Side::Buy,
          .price = event.price,
          .bid_level = level_it,
          .ask_level = asks_.end(),
          .order_it = order_it,
      };
    } else {
      auto [level_it, inserted] = asks_.try_emplace(event.price, &order_pool_);
      (void)inserted;
      level_it->second.orders.push_back(order);
      level_it->second.total_qty += remaining;
      auto order_it = std::prev(level_it->second.orders.end());
      order_index_[event.order_id] = {
          .side = Side::Sell,
          .price = event.price,
          .bid_level = bids_.end(),
          .ask_level = level_it,
          .order_it = order_it,
      };
    }
  }

  void match_order(const OrderEvent& event, Qty& remaining, std::vector<Execution>& executions, bool is_market) {
    // Matching policy is identical to baseline. The speedup here comes from the
    // supporting structures around the loop, not from changing market behavior.
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
        auto& level = level_it->second;
        auto& orders = level.orders;
        // Price priority is handled by the map ordering; time priority is the
        // FIFO walk through the per-level order list.
        while (remaining > 0 && !orders.empty()) {
          auto order_it = orders.begin();
          const Qty trade_qty = std::min(remaining, order_it->qty);
          executions.push_back(make_execution(*order_it, event, trade_qty));
          remaining -= trade_qty;
          order_it->qty -= trade_qty;
          level.total_qty -= trade_qty;
          if (order_it->qty == 0) {
            order_index_.erase(order_it->order_id);
            orders.erase(order_it);
          }
        }
        if (orders.empty()) {
          asks_.erase(level_it);
        }
      }
    } else {
      while (remaining > 0 && !bids_.empty() && crosses(bids_.begin()->first)) {
        auto level_it = bids_.begin();
        auto& level = level_it->second;
        auto& orders = level.orders;
        // Price priority is handled by the map ordering; time priority is the
        // FIFO walk through the per-level order list.
        while (remaining > 0 && !orders.empty()) {
          auto order_it = orders.begin();
          const Qty trade_qty = std::min(remaining, order_it->qty);
          executions.push_back(make_execution(*order_it, event, trade_qty));
          remaining -= trade_qty;
          order_it->qty -= trade_qty;
          level.total_qty -= trade_qty;
          if (order_it->qty == 0) {
            order_index_.erase(order_it->order_id);
            orders.erase(order_it);
          }
        }
        if (orders.empty()) {
          bids_.erase(level_it);
        }
      }
    }
  }

  static Execution make_execution(const OrderNode& resting, const OrderEvent& aggressive, Qty qty) {
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

  std::pmr::unsynchronized_pool_resource order_pool_;
  LevelMap<std::greater<Price>> bids_;
  LevelMap<std::less<Price>> asks_;
  std::pmr::unordered_map<OrderId, OrderHandle> order_index_;
};

}  // namespace

std::unique_ptr<IOrderBook> make_optimized_order_book(std::size_t expected_orders) {
  return std::make_unique<OptimizedOrderBook>(expected_orders);
}

}  // namespace lob
