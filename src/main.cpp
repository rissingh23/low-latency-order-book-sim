#include "lob/order_book.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
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

void print_usage() {
  std::cout << "modes: benchmark | simulate | export-replay | export-dashboard\n";
  std::cout << "synthetic: --profile balanced|cancel_heavy|bursty --orders N --seed S\n";
  std::cout << "dataset: --dataset path/to/events.csv [--dataset-limit N]\n";
}

void print_summary(const lob::RunSummary& summary) {
  std::cout << summary.engine_name << " | input=" << summary.input_label
            << " | orders=" << summary.order_count
            << " | throughput=" << summary.throughput_ops_per_sec
            << " ops/s | service_p50=" << summary.service_p50_ns
            << " ns | service_p99=" << summary.service_p99_ns
            << " ns | e2e_p50=" << summary.end_to_end_p50_ns
            << " ns | e2e_p99=" << summary.end_to_end_p99_ns
            << " ns | queue_p50=" << summary.queue_delay_p50_ns
            << " ns | queue_p99=" << summary.queue_delay_p99_ns
            << " ns | max_queue_depth=" << summary.max_queue_depth << '\n';
}

double pct_delta(double baseline, double candidate) {
  if (baseline == 0.0) {
    return 0.0;
  }
  return ((candidate - baseline) / baseline) * 100.0;
}

void write_comparison_report(const std::filesystem::path& output_path, const std::vector<lob::RunSummary>& summaries) {
  if (summaries.size() < 3) {
    return;
  }

  const auto& baseline = summaries[0];
  const auto& optimized = summaries[1];
  const auto& pipeline = summaries[2];

  std::ofstream out(output_path);
  out << "# Benchmark Comparison\n\n";
  out << "- Input: " << baseline.input_label << '\n';
  out << "- Orders: " << baseline.order_count << "\n\n";

  out << "## Clear Optimizations\n\n";
  out << "1. Baseline -> optimized matcher: direct cancel lookup, cached per-level quantity, and pooled allocators for resting-order storage.\n";
  out << "2. Optimized matcher -> pipeline: batched queue draining to reduce handoff overhead.\n\n";

  out << "## Measured Deltas\n\n";
  out << std::fixed << std::setprecision(2);
  out << "- Optimized throughput vs baseline: " << pct_delta(baseline.throughput_ops_per_sec, optimized.throughput_ops_per_sec) << "%\n";
  out << "- Optimized service p50 vs baseline: " << pct_delta(baseline.service_p50_ns, optimized.service_p50_ns) << "%\n";
  out << "- Optimized service p99 vs baseline: " << pct_delta(baseline.service_p99_ns, optimized.service_p99_ns) << "%\n";
  out << "- Pipeline throughput vs optimized: " << pct_delta(optimized.throughput_ops_per_sec, pipeline.throughput_ops_per_sec) << "%\n";
  out << "- Pipeline service p50 vs optimized: " << pct_delta(optimized.service_p50_ns, pipeline.service_p50_ns) << "%\n";
  out << "- Pipeline queue p50: " << pipeline.queue_delay_p50_ns << " ns\n";
  out << "- Pipeline max queue depth: " << pipeline.max_queue_depth << "\n\n";

  out << "## Interpretation\n\n";
  out << "- If optimized service latency drops, the matcher itself got faster.\n";
  out << "- If pipeline service latency stays close to optimized but end-to-end latency rises, queueing is dominating rather than matching.\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string mode = arg_value(argc, argv, "--mode", "benchmark");
    const std::string dataset_path = arg_value(argc, argv, "--dataset", "");
    const auto profile = parse_profile(arg_value(argc, argv, "--profile", "balanced"));
    const std::size_t order_count = static_cast<std::size_t>(std::stoull(arg_value(argc, argv, "--orders", "200000")));
    const std::size_t dataset_limit = static_cast<std::size_t>(std::stoull(arg_value(argc, argv, "--dataset-limit", "0")));
    const std::uint64_t seed = std::stoull(arg_value(argc, argv, "--seed", "42"));
    const std::string output = arg_value(argc, argv, "--output", "results/benchmark_results.csv");
    const std::size_t depth_levels = static_cast<std::size_t>(std::stoull(arg_value(argc, argv, "--depth", "20")));
    const bool using_dataset = !dataset_path.empty();
    const auto input_label = using_dataset ? lob::dataset_label_from_path(dataset_path) : std::string(lob::to_string(profile));
    const auto events = using_dataset
        ? lob::load_events_from_csv(dataset_path, dataset_limit)
        : lob::generate_workload(profile, order_count, seed);

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

    if (mode == "export-replay") {
      auto book = lob::make_optimized_order_book(events.size());
      std::filesystem::create_directories(std::filesystem::path(output).parent_path());
      lob::write_replay_json(*book, input_label, events, output, depth_levels);
      std::cout << "wrote replay to " << output << '\n';
      return 0;
    }

    if (mode == "export-dashboard") {
      const auto output_path = std::filesystem::path(output);
      std::filesystem::create_directories(output_path.parent_path());

      auto baseline = lob::make_baseline_order_book();
      auto optimized = lob::make_optimized_order_book(events.size());
      auto optimized_pipeline = lob::make_optimized_order_book(events.size());

      std::vector<lob::RunSummary> summaries;
      summaries.push_back(lob::run_single_thread_benchmark(*baseline, input_label, events));
      summaries.push_back(lob::run_single_thread_benchmark(*optimized, input_label, events));
      summaries.push_back(lob::run_concurrent_pipeline_benchmark(*optimized_pipeline, input_label, events));
      lob::write_benchmark_csv(output, summaries);

      auto replay_baseline = lob::make_baseline_order_book();
      auto replay_optimized = lob::make_optimized_order_book(events.size());
      lob::write_replay_json(
          *replay_baseline,
          input_label,
          events,
          (output_path.parent_path() / ("replay_baseline_" + input_label + ".json")).string(),
          depth_levels);
      lob::write_replay_json(
          *replay_optimized,
          input_label,
          events,
          (output_path.parent_path() / ("replay_optimized_" + input_label + ".json")).string(),
          depth_levels);

      for (const auto& summary : summaries) {
        print_summary(summary);
      }
      write_comparison_report(output_path.parent_path() / (input_label + "_comparison.md"), summaries);
      std::cout << "wrote dashboard data to " << output_path.parent_path().string() << '\n';
      return 0;
    }

    if (has_flag(argc, argv, "--help")) {
      print_usage();
      return 0;
    }

    std::vector<lob::RunSummary> summaries;

    auto baseline = lob::make_baseline_order_book();
    summaries.push_back(lob::run_single_thread_benchmark(*baseline, input_label, events));

    auto optimized = lob::make_optimized_order_book(events.size());
    summaries.push_back(lob::run_single_thread_benchmark(*optimized, input_label, events));

    auto optimized_pipeline = lob::make_optimized_order_book(events.size());
    summaries.push_back(lob::run_concurrent_pipeline_benchmark(*optimized_pipeline, input_label, events));

    std::filesystem::create_directories(std::filesystem::path(output).parent_path());
    lob::write_benchmark_csv(output, summaries);

    for (const auto& summary : summaries) {
      print_summary(summary);
    }
    write_comparison_report(std::filesystem::path(output).parent_path() / (input_label + "_comparison.md"), summaries);
    std::cout << "wrote results to " << output << '\n';
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << '\n';
    return 1;
  }
}
