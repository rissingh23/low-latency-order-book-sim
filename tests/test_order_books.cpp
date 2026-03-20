#include "lob/order_book.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
    std::exit(1);
  }
}

void run_shared_order_book_checks(lob::IOrderBook& book) {
  book.reset();

  auto add_buy = book.process_event({
      .timestamp = 1,
      .sequence = 1,
      .type = lob::OrderType::Limit,
      .side = lob::Side::Buy,
      .order_id = 1,
      .price = 100,
      .qty = 50,
  });
  expect(add_buy.status == lob::EventStatus::Accepted, "resting buy should be accepted");

  auto add_buy_2 = book.process_event({
      .timestamp = 2,
      .sequence = 2,
      .type = lob::OrderType::Limit,
      .side = lob::Side::Buy,
      .order_id = 2,
      .price = 100,
      .qty = 30,
  });
  expect(add_buy_2.status == lob::EventStatus::Accepted, "second resting buy should be accepted");

  auto partial_sell = book.process_event({
      .timestamp = 3,
      .sequence = 3,
      .type = lob::OrderType::Limit,
      .side = lob::Side::Sell,
      .order_id = 3,
      .price = 100,
      .qty = 60,
  });
  expect(partial_sell.executions.size() == 2, "sell should trade against both resting orders");
  expect(partial_sell.executions.front().resting_order_id == 1, "older order should fill first");
  expect(partial_sell.executions.back().resting_order_id == 2, "second order should fill after first");

  const auto top = book.snapshot_top_of_book();
  expect(top.best_bid.has_value(), "best bid should remain after partial fill");
  expect(*top.best_bid == 100, "best bid price should remain at 100");
  expect(top.best_bid_qty.has_value() && *top.best_bid_qty == 20, "remaining bid qty should be 20");

  auto cancel = book.process_event({
      .timestamp = 4,
      .sequence = 4,
      .type = lob::OrderType::Cancel,
      .side = lob::Side::Buy,
      .order_id = 2,
      .price = 0,
      .qty = 0,
  });
  expect(cancel.status == lob::EventStatus::Cancelled, "live order should cancel");

  auto missing_cancel = book.process_event({
      .timestamp = 5,
      .sequence = 5,
      .type = lob::OrderType::Cancel,
      .side = lob::Side::Buy,
      .order_id = 999,
      .price = 0,
      .qty = 0,
  });
  expect(missing_cancel.status == lob::EventStatus::Rejected, "missing cancel should reject");

  auto ask = book.process_event({
      .timestamp = 6,
      .sequence = 6,
      .type = lob::OrderType::Limit,
      .side = lob::Side::Sell,
      .order_id = 4,
      .price = 103,
      .qty = 25,
  });
  expect(ask.status == lob::EventStatus::Accepted, "resting ask should be accepted");

  auto market_buy = book.process_event({
      .timestamp = 7,
      .sequence = 7,
      .type = lob::OrderType::Market,
      .side = lob::Side::Buy,
      .order_id = 5,
      .price = 0,
      .qty = 25,
  });
  expect(market_buy.status == lob::EventStatus::Filled, "market buy should fully fill");
  expect(market_buy.executions.size() == 1, "market order should create one fill");
  expect(market_buy.executions.front().price == 103, "market buy should trade at resting ask price");
}

void run_determinism_check() {
  const auto events = lob::generate_workload(lob::WorkloadProfile::Balanced, 5000, 1234);
  auto direct_book = lob::make_optimized_order_book(events.size());
  auto pipeline_book = lob::make_optimized_order_book(events.size());

  for (const auto& event : events) {
    direct_book->process_event(event);
  }
  lob::run_concurrent_pipeline_benchmark(*pipeline_book, lob::WorkloadProfile::Balanced, events);

  const auto direct = direct_book->snapshot_top_of_book();
  const auto pipelined = pipeline_book->snapshot_top_of_book();

  expect(direct.best_bid == pipelined.best_bid, "pipeline and direct book should match best bid");
  expect(direct.best_ask == pipelined.best_ask, "pipeline and direct book should match best ask");
}

}  // namespace

int main() {
  {
    auto baseline = lob::make_baseline_order_book();
    run_shared_order_book_checks(*baseline);
  }

  {
    auto optimized = lob::make_optimized_order_book();
    run_shared_order_book_checks(*optimized);
  }

  run_determinism_check();

  std::cout << "all tests passed\n";
  return 0;
}
