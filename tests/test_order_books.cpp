#include "lob/features.hpp"
#include "lob/order_book.hpp"
#include "lob/replay_engine.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

  const auto depth = book.snapshot_depth(4);
  expect(depth.asks.empty(), "ask depth should be empty after market buy consumes the ask");
}

void run_partial_cancel_check(lob::IOrderBook& book) {
  book.reset();

  book.process_event({
      .timestamp = 1,
      .sequence = 1,
      .type = lob::OrderType::Limit,
      .side = lob::Side::Buy,
      .order_id = 31,
      .price = 100,
      .qty = 50,
  });

  const auto cancel = book.process_event({
      .timestamp = 2,
      .sequence = 2,
      .type = lob::OrderType::Cancel,
      .side = lob::Side::Buy,
      .order_id = 31,
      .price = 0,
      .qty = 15,
  });

  expect(cancel.status == lob::EventStatus::Cancelled, "partial cancel should be accepted");
  expect(cancel.remaining_qty == 35, "partial cancel should leave remaining qty on the book");

  const auto top = book.snapshot_top_of_book();
  expect(top.best_bid.has_value() && *top.best_bid == 100, "partial cancel should keep level alive");
  expect(top.best_bid_qty.has_value() && *top.best_bid_qty == 35, "partial cancel should reduce resting quantity");
}

void run_depth_consistency_check(lob::IOrderBook& book) {
  book.reset();

  book.process_event({
      .timestamp = 1,
      .sequence = 1,
      .type = lob::OrderType::Limit,
      .side = lob::Side::Buy,
      .order_id = 11,
      .price = 101,
      .qty = 40,
  });
  book.process_event({
      .timestamp = 2,
      .sequence = 2,
      .type = lob::OrderType::Limit,
      .side = lob::Side::Buy,
      .order_id = 12,
      .price = 101,
      .qty = 35,
  });
  book.process_event({
      .timestamp = 3,
      .sequence = 3,
      .type = lob::OrderType::Limit,
      .side = lob::Side::Buy,
      .order_id = 13,
      .price = 100,
      .qty = 20,
  });

  auto depth = book.snapshot_depth(4);
  expect(depth.bids.size() == 2, "depth should include two bid levels");
  expect(depth.bids[0].price == 101 && depth.bids[0].qty == 75, "top bid level should aggregate both resting orders");
  expect(depth.bids[1].price == 100 && depth.bids[1].qty == 20, "second bid level should reflect its resting quantity");

  book.process_event({
      .timestamp = 4,
      .sequence = 4,
      .type = lob::OrderType::Limit,
      .side = lob::Side::Sell,
      .order_id = 21,
      .price = 101,
      .qty = 50,
  });

  depth = book.snapshot_depth(4);
  expect(depth.bids[0].price == 101 && depth.bids[0].qty == 25, "partial fill should reduce aggregated top bid qty");

  book.process_event({
      .timestamp = 5,
      .sequence = 5,
      .type = lob::OrderType::Cancel,
      .side = lob::Side::Buy,
      .order_id = 12,
      .price = 0,
      .qty = 0,
  });

  depth = book.snapshot_depth(4);
  expect(depth.bids[0].price == 100, "cancelled top level should disappear when depleted");
  expect(depth.bids[0].qty == 20, "next bid level should become top level with correct qty");
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

void run_intrusive_order_book_check() {
  auto intrusive = lob::make_intrusive_order_book(64);
  run_shared_order_book_checks(*intrusive);
  run_partial_cancel_check(*intrusive);
  run_depth_consistency_check(*intrusive);
}

void run_csv_loader_check() {
  const auto dataset_path = std::filesystem::temp_directory_path() / "lob_dataset_test.csv";
  {
    std::ofstream out(dataset_path);
    out << "timestamp,sequence,type,side,order_id,price,qty\n";
    out << "100,1,limit,buy,10,101,40\n";
    out << "101,2,limit,sell,11,102,25\n";
    out << "102,3,market,buy,12,0,10\n";
    out << "103,4,cancel,sell,11,0,0\n";
  }

  const auto events = lob::load_events_from_csv(dataset_path.string());
  expect(events.size() == 4, "csv loader should read all rows");
  expect(events[0].type == lob::OrderType::Limit && events[0].side == lob::Side::Buy, "first row should parse limit buy");
  expect(events[2].type == lob::OrderType::Market && events[2].price == 0, "market row should parse");
  expect(events[3].type == lob::OrderType::Cancel && events[3].qty == 0, "cancel row should parse");

  auto book = lob::make_optimized_order_book(events.size());
  for (const auto& event : events) {
    book->process_event(event);
  }

  const auto top = book->snapshot_top_of_book();
  expect(top.best_bid.has_value() && *top.best_bid == 101, "dataset replay should leave bid resting at 101");
  expect(!top.best_ask.has_value(), "dataset replay cancel should remove the ask");

  std::filesystem::remove(dataset_path);
}

void run_replay_and_feature_checks() {
  const auto events = lob::generate_workload(lob::WorkloadProfile::Balanced, 256, 777);
  auto book = lob::make_optimized_order_book(events.size());
  lob::ReplayEngine replay(3);
  lob::FeatureExtractor extractor(3);

  struct Observer final : lob::ReplayObserver {
    lob::FeatureExtractor& extractor;
    std::size_t step_count{0};
    bool saw_non_zero_mid{false};

    explicit Observer(lob::FeatureExtractor& extractor) : extractor(extractor) {}

    void on_step(const lob::ReplayStep& step) override {
      const auto features = extractor.update(step.event, step.result, step.top, step.depth);
      ++step_count;
      saw_non_zero_mid = saw_non_zero_mid || features.mid_price != 0.0;
    }
  } observer{extractor};

  replay.run(*book, events, &observer);
  expect(observer.step_count == events.size(), "replay engine should visit every event exactly once");
  expect(observer.saw_non_zero_mid, "feature extraction should eventually observe a non-zero mid price");

  auto dataset_book = lob::make_optimized_order_book(events.size());
  const auto rows = lob::build_labeled_feature_rows(*dataset_book, events, 3, 5);
  expect(!rows.empty(), "labeled feature builder should emit rows for replayed data");
}

}  // namespace

int main() {
  {
    auto baseline = lob::make_baseline_order_book();
    run_shared_order_book_checks(*baseline);
    run_partial_cancel_check(*baseline);
  }

  {
    auto optimized = lob::make_optimized_order_book();
    run_shared_order_book_checks(*optimized);
    run_partial_cancel_check(*optimized);
    run_depth_consistency_check(*optimized);
  }

  run_intrusive_order_book_check();

  run_determinism_check();
  run_csv_loader_check();
  run_replay_and_feature_checks();

  std::cout << "all tests passed\n";
  return 0;
}
