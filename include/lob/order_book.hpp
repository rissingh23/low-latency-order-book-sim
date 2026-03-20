#pragma once

#include <memory>
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
};

std::unique_ptr<IOrderBook> make_baseline_order_book();
std::unique_ptr<IOrderBook> make_optimized_order_book(std::size_t expected_orders = 1U << 20);

std::vector<OrderEvent> generate_workload(WorkloadProfile profile, std::size_t order_count, std::uint64_t seed);

RunSummary run_single_thread_benchmark(IOrderBook& book, WorkloadProfile profile, const std::vector<OrderEvent>& events);
RunSummary run_concurrent_pipeline_benchmark(IOrderBook& book, WorkloadProfile profile, const std::vector<OrderEvent>& events);
void write_benchmark_csv(const std::string& output_path, const std::vector<RunSummary>& summaries);

}  // namespace lob
