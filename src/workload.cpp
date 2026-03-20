#include "lob/order_book.hpp"

#include <algorithm>
#include <random>
#include <vector>

namespace lob {
namespace {

Price random_price(std::mt19937_64& rng) {
  std::uniform_int_distribution<Price> price_dist(9'900, 10'100);
  return price_dist(rng);
}

Qty random_qty(std::mt19937_64& rng) {
  std::uniform_int_distribution<Qty> qty_dist(10, 200);
  return qty_dist(rng);
}

Side random_side(std::mt19937_64& rng) {
  std::bernoulli_distribution dist(0.5);
  return dist(rng) ? Side::Buy : Side::Sell;
}

OrderType sample_order_type(WorkloadProfile profile, std::mt19937_64& rng, bool can_cancel) {
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  const double p = unit(rng);

  switch (profile) {
    case WorkloadProfile::Balanced:
      if (can_cancel && p < 0.18) {
        return OrderType::Cancel;
      }
      return p < 0.30 ? OrderType::Market : OrderType::Limit;
    case WorkloadProfile::CancelHeavy:
      if (can_cancel && p < 0.45) {
        return OrderType::Cancel;
      }
      return p < 0.60 ? OrderType::Market : OrderType::Limit;
    case WorkloadProfile::Bursty:
      if (can_cancel && p < 0.12) {
        return OrderType::Cancel;
      }
      return p < 0.40 ? OrderType::Market : OrderType::Limit;
  }
  return OrderType::Limit;
}

}  // namespace

std::vector<OrderEvent> generate_workload(WorkloadProfile profile, std::size_t order_count, std::uint64_t seed) {
  std::mt19937_64 rng(seed);
  std::vector<OrderEvent> events;
  events.reserve(order_count);

  std::vector<OrderId> live_order_ids;
  live_order_ids.reserve(order_count / 2);

  Timestamp timestamp = 1;
  OrderId next_order_id = 1;

  for (std::size_t i = 0; i < order_count; ++i) {
    const bool can_cancel = !live_order_ids.empty();
    const OrderType type = sample_order_type(profile, rng, can_cancel);

    OrderEvent event;
    event.sequence = i + 1;
    event.timestamp = timestamp;

    if (profile == WorkloadProfile::Bursty && (i % 1024) < 48) {
      timestamp += 1;
    } else {
      timestamp += 50;
    }

    if (type == OrderType::Cancel) {
      std::uniform_int_distribution<std::size_t> live_dist(0, live_order_ids.size() - 1);
      const auto live_index = live_dist(rng);
      event.type = OrderType::Cancel;
      event.side = Side::Buy;
      event.order_id = live_order_ids[live_index];
      event.qty = 0;
      live_order_ids[live_index] = live_order_ids.back();
      live_order_ids.pop_back();
      events.push_back(event);
      continue;
    }

    event.type = type;
    event.side = random_side(rng);
    event.order_id = next_order_id++;
    event.price = random_price(rng);
    event.qty = random_qty(rng);

    if (event.type == OrderType::Limit) {
      live_order_ids.push_back(event.order_id);
    } else {
      event.price = 0;
    }

    events.push_back(event);
  }

  return events;
}

}  // namespace lob
