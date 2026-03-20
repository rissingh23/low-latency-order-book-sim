#include "lob/order_book.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

lob::WorkloadProfile parse_profile(const std::string& value) {
  if (value == "balanced") {
    return lob::WorkloadProfile::Balanced;
  }
  if (value == "cancel-heavy" || value == "cancel_heavy") {
    return lob::WorkloadProfile::CancelHeavy;
  }
  if (value == "bursty") {
    return lob::WorkloadProfile::Bursty;
  }
  throw std::runtime_error("unknown profile: " + value);
}

std::string arg_value(int argc, char** argv, const std::string& flag, const std::string& fallback) {
  for (int i = 1; i < argc - 1; ++i) {
    if (argv[i] == flag) {
      return argv[i + 1];
    }
  }
  return fallback;
}

bool has_flag(int argc, char** argv, const std::string& flag) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == flag) {
      return true;
    }
  }
  return false;
}

void print_summary(const lob::RunSummary& summary) {
  std::cout << summary.engine_name << " | profile=" << lob::to_string(summary.profile)
            << " | orders=" << summary.order_count
            << " | throughput=" << summary.throughput_ops_per_sec
            << " ops/s | p50=" << summary.p50_ns
            << " ns | p95=" << summary.p95_ns
            << " ns | p99=" << summary.p99_ns
            << " ns | max_queue_depth=" << summary.max_queue_depth << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string mode = arg_value(argc, argv, "--mode", "benchmark");
    const auto profile = parse_profile(arg_value(argc, argv, "--profile", "balanced"));
    const std::size_t order_count = static_cast<std::size_t>(std::stoull(arg_value(argc, argv, "--orders", "200000")));
    const std::uint64_t seed = std::stoull(arg_value(argc, argv, "--seed", "42"));
    const std::string output = arg_value(argc, argv, "--output", "results/benchmark_results.csv");

    const auto events = lob::generate_workload(profile, order_count, seed);

    if (mode == "simulate") {
      auto book = lob::make_optimized_order_book(events.size());
      std::size_t printed = 0;
      for (const auto& event : events) {
        const auto result = book->process_event(event);
        if (printed < 15) {
          std::cout << "#" << event.sequence
                    << " " << lob::to_string(event.type)
                    << " " << lob::to_string(event.side)
                    << " id=" << event.order_id
                    << " qty=" << event.qty
                    << " status=" << static_cast<int>(result.status)
                    << " fills=" << result.executions.size()
                    << '\n';
          ++printed;
        }
      }
      const auto top = book->snapshot_top_of_book();
      std::cout << "top_of_book: bid="
                << (top.best_bid ? std::to_string(*top.best_bid) : "NA")
                << " ask="
                << (top.best_ask ? std::to_string(*top.best_ask) : "NA")
                << '\n';
      return 0;
    }

    std::vector<lob::RunSummary> summaries;

    auto baseline = lob::make_baseline_order_book();
    summaries.push_back(lob::run_single_thread_benchmark(*baseline, profile, events));

    auto optimized = lob::make_optimized_order_book(events.size());
    summaries.push_back(lob::run_single_thread_benchmark(*optimized, profile, events));

    auto optimized_pipeline = lob::make_optimized_order_book(events.size());
    summaries.push_back(lob::run_concurrent_pipeline_benchmark(*optimized_pipeline, profile, events));

    std::filesystem::create_directories(std::filesystem::path(output).parent_path());
    lob::write_benchmark_csv(output, summaries);

    for (const auto& summary : summaries) {
      print_summary(summary);
    }
    std::cout << "wrote results to " << output << '\n';
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << '\n';
    return 1;
  }
}
