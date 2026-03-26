#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "lob/types.hpp"

namespace lob {

class IOrderBook {
 public:
  virtual ~IOrderBook() = default;

  virtual const char* name() const = 0;
  virtual void reset() = 0;
  virtual ProcessResult process_event(const OrderEvent& event) = 0;
  virtual TopOfBook snapshot_top_of_book() const = 0;
  virtual BookDepth snapshot_depth(std::size_t levels) const = 0;
};

std::unique_ptr<IOrderBook> make_baseline_order_book();
std::unique_ptr<IOrderBook> make_optimized_order_book(std::size_t expected_orders = 1U << 20);
std::unique_ptr<IOrderBook> make_intrusive_order_book(std::size_t expected_orders = 1U << 20);

std::vector<OrderEvent> generate_workload(WorkloadProfile profile, std::size_t order_count, std::uint64_t seed);
std::vector<OrderEvent> load_events_from_csv(const std::string& csv_path, std::size_t limit = 0);
std::string dataset_label_from_path(const std::string& csv_path);

RunSummary run_single_thread_benchmark(IOrderBook& book, WorkloadProfile profile, const std::vector<OrderEvent>& events);
RunSummary run_single_thread_benchmark(IOrderBook& book, std::string_view input_label, const std::vector<OrderEvent>& events);
RunSummary run_concurrent_pipeline_benchmark(IOrderBook& book, WorkloadProfile profile, const std::vector<OrderEvent>& events);
RunSummary run_concurrent_pipeline_benchmark(IOrderBook& book, std::string_view input_label, const std::vector<OrderEvent>& events);
void write_benchmark_csv(const std::string& output_path, const std::vector<RunSummary>& summaries);
void write_replay_json(
    IOrderBook& book,
    WorkloadProfile profile,
    const std::vector<OrderEvent>& events,
    const std::string& output_path,
    std::size_t depth_levels = 20);
void write_replay_json(
    IOrderBook& book,
    std::string_view input_label,
    const std::vector<OrderEvent>& events,
    const std::string& output_path,
    std::size_t depth_levels = 20);

}  // namespace lob
