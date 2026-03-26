#include "lob/order_book.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lob {
namespace {

struct OrderNode {
  OrderId order_id{};
  Qty qty{};
  Timestamp timestamp{};
  std::size_t prev{std::numeric_limits<std::size_t>::max()};
  std::size_t next{std::numeric_limits<std::size_t>::max()};
  bool active{false};
};

struct LevelData {
  std::size_t head{std::numeric_limits<std::size_t>::max()};
  std::size_t tail{std::numeric_limits<std::size_t>::max()};
  Qty total_qty{};
};

template <typename Comparator>
using LevelMap = std::map<Price, LevelData, Comparator>;

struct OrderHandle {
  Side side{Side::Buy};
  Price price{};
  LevelMap<std::greater<Price>>::iterator bid_level{};
  LevelMap<std::less<Price>>::iterator ask_level{};
  std::size_t node_index{std::numeric_limits<std::size_t>::max()};
};

constexpr std::size_t kNullIndex = std::numeric_limits<std::size_t>::max();

class IntrusiveOrderBook final : public IOrderBook {
 public:
  explicit IntrusiveOrderBook(std::size_t expected_orders) {
    nodes_.reserve(expected_orders);
    free_list_.reserve(expected_orders / 4);
    order_index_.reserve(expected_orders);
  }

  const char* name() const override { return "intrusive_single_thread"; }

  void reset() override {
    bids_.clear();
    asks_.clear();
    order_index_.clear();
    nodes_.clear();
    free_list_.clear();
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
    const auto handle_it = order_index_.find(event.order_id);
    if (handle_it == order_index_.end()) {
      return {.status = EventStatus::Rejected, .message = "order not found"};
    }

    auto& handle = handle_it->second;
    auto& node = nodes_[handle.node_index];
    const Qty cancelled_qty = event.qty == 0 ? node.qty : std::min(event.qty, node.qty);
    const Qty remaining_qty = node.qty - cancelled_qty;

    if (handle.side == Side::Buy) {
      auto level_it = handle.bid_level;
      level_it->second.total_qty -= cancelled_qty;
      node.qty = remaining_qty;
      if (node.qty == 0) {
        unlink_node(level_it->second, handle.node_index);
        if (level_it->second.head == kNullIndex) {
          bids_.erase(level_it);
        }
      }
    } else {
      auto level_it = handle.ask_level;
      level_it->second.total_qty -= cancelled_qty;
      node.qty = remaining_qty;
      if (node.qty == 0) {
        unlink_node(level_it->second, handle.node_index);
        if (level_it->second.head == kNullIndex) {
          asks_.erase(level_it);
        }
      }
    }

    if (remaining_qty == 0) {
      retire_node(handle.node_index);
      order_index_.erase(handle_it);
    }

    return {
        .status = EventStatus::Cancelled,
        .filled_qty = 0,
        .remaining_qty = remaining_qty,
        .message = remaining_qty == 0 ? "cancelled" : "partially cancelled",
    };
  }

  void add_resting(const OrderEvent& event, Qty remaining) {
    const std::size_t node_index = allocate_node({
        .order_id = event.order_id,
        .qty = remaining,
        .timestamp = event.timestamp,
        .prev = kNullIndex,
        .next = kNullIndex,
        .active = true,
    });

    if (event.side == Side::Buy) {
      auto [level_it, inserted] = bids_.try_emplace(event.price);
      (void)inserted;
      append_node(level_it->second, node_index);
      level_it->second.total_qty += remaining;
      order_index_[event.order_id] = {
          .side = Side::Buy,
          .price = event.price,
          .bid_level = level_it,
          .ask_level = asks_.end(),
          .node_index = node_index,
      };
    } else {
      auto [level_it, inserted] = asks_.try_emplace(event.price);
      (void)inserted;
      append_node(level_it->second, node_index);
      level_it->second.total_qty += remaining;
      order_index_[event.order_id] = {
          .side = Side::Sell,
          .price = event.price,
          .bid_level = bids_.end(),
          .ask_level = level_it,
          .node_index = node_index,
      };
    }
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
        while (remaining > 0 && level_it->second.head != kNullIndex) {
          std::size_t node_index = level_it->second.head;
          auto& resting = nodes_[node_index];
          const Qty trade_qty = std::min(remaining, resting.qty);
          executions.push_back({
              .resting_order_id = resting.order_id,
              .aggressive_order_id = event.order_id,
              .price = level_it->first,
              .qty = trade_qty,
              .timestamp = event.timestamp,
              .aggressive_is_buy = true,
          });
          resting.qty -= trade_qty;
          level_it->second.total_qty -= trade_qty;
          remaining -= trade_qty;
          if (resting.qty == 0) {
            order_index_.erase(resting.order_id);
            unlink_node(level_it->second, node_index);
            retire_node(node_index);
          }
        }
        if (level_it->second.head == kNullIndex) {
          asks_.erase(level_it);
        }
      }
      return;
    }

    while (remaining > 0 && !bids_.empty() && crosses(bids_.begin()->first)) {
      auto level_it = bids_.begin();
      while (remaining > 0 && level_it->second.head != kNullIndex) {
        std::size_t node_index = level_it->second.head;
        auto& resting = nodes_[node_index];
        const Qty trade_qty = std::min(remaining, resting.qty);
        executions.push_back({
            .resting_order_id = resting.order_id,
            .aggressive_order_id = event.order_id,
            .price = level_it->first,
            .qty = trade_qty,
            .timestamp = event.timestamp,
            .aggressive_is_buy = false,
        });
        resting.qty -= trade_qty;
        level_it->second.total_qty -= trade_qty;
        remaining -= trade_qty;
        if (resting.qty == 0) {
          order_index_.erase(resting.order_id);
          unlink_node(level_it->second, node_index);
          retire_node(node_index);
        }
      }
      if (level_it->second.head == kNullIndex) {
        bids_.erase(level_it);
      }
    }
  }

  ProcessResult finalize_result(const OrderEvent& event, Qty remaining, std::vector<Execution> executions) const {
    const Qty filled = event.qty - remaining;
    EventStatus status = EventStatus::Accepted;
    if (filled == 0) {
      status = event.type == OrderType::Market ? EventStatus::Rejected : EventStatus::Accepted;
    } else if (remaining == 0) {
      status = EventStatus::Filled;
    } else {
      status = EventStatus::PartiallyFilled;
    }

    return {
        .status = status,
        .filled_qty = filled,
        .remaining_qty = remaining,
        .executions = std::move(executions),
        .message = status == EventStatus::Rejected ? "insufficient liquidity" : "",
    };
  }

  std::size_t allocate_node(OrderNode node) {
    if (!free_list_.empty()) {
      const std::size_t index = free_list_.back();
      free_list_.pop_back();
      nodes_[index] = node;
      return index;
    }
    nodes_.push_back(node);
    return nodes_.size() - 1;
  }

  void retire_node(std::size_t node_index) {
    nodes_[node_index].active = false;
    nodes_[node_index].prev = kNullIndex;
    nodes_[node_index].next = kNullIndex;
    free_list_.push_back(node_index);
  }

  void append_node(LevelData& level, std::size_t node_index) {
    auto& node = nodes_[node_index];
    node.prev = level.tail;
    node.next = kNullIndex;
    if (level.tail != kNullIndex) {
      nodes_[level.tail].next = node_index;
    } else {
      level.head = node_index;
    }
    level.tail = node_index;
  }

  void unlink_node(LevelData& level, std::size_t node_index) {
    auto& node = nodes_[node_index];
    if (node.prev != kNullIndex) {
      nodes_[node.prev].next = node.next;
    } else {
      level.head = node.next;
    }
    if (node.next != kNullIndex) {
      nodes_[node.next].prev = node.prev;
    } else {
      level.tail = node.prev;
    }
    node.prev = kNullIndex;
    node.next = kNullIndex;
  }

  LevelMap<std::greater<Price>> bids_;
  LevelMap<std::less<Price>> asks_;
  std::unordered_map<OrderId, OrderHandle> order_index_;
  std::vector<OrderNode> nodes_;
  std::vector<std::size_t> free_list_;
};

}  // namespace

std::unique_ptr<IOrderBook> make_intrusive_order_book(std::size_t expected_orders) {
  return std::make_unique<IntrusiveOrderBook>(expected_orders);
}

}  // namespace lob
