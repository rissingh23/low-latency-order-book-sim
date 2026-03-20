#include "lob/order_book.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <thread>

#include "lob/spsc_queue.hpp"

namespace lob {
namespace {

double percentile_ns(std::vector<double> values, double percentile) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(percentile * static_cast<double>(values.size() - 1));
  return values[index];
}

RunSummary summarize(
    std::string engine_name,
    WorkloadProfile profile,
    std::size_t order_count,
    std::vector<double> latencies_ns,
    std::chrono::steady_clock::duration total_duration,
    std::uint64_t max_queue_depth,
    std::uint64_t total_fills,
    std::uint64_t total_rejects) {
  const double seconds = std::chrono::duration<double>(total_duration).count();
  return {
      .engine_name = std::move(engine_name),
      .profile = profile,
      .order_count = order_count,
      .throughput_ops_per_sec = seconds > 0.0 ? static_cast<double>(order_count) / seconds : 0.0,
      .p50_ns = percentile_ns(latencies_ns, 0.50),
      .p95_ns = percentile_ns(latencies_ns, 0.95),
      .p99_ns = percentile_ns(latencies_ns, 0.99),
      .max_queue_depth = max_queue_depth,
      .total_fills = total_fills,
      .total_rejects = total_rejects,
  };
}

struct PipelinePacket {
  OrderEvent event{};
  bool is_poison_pill{false};
  std::chrono::steady_clock::time_point enqueue_time{};
};

struct PipelineResult {
  ProcessResult result{};
  bool is_poison_pill{false};
  std::chrono::steady_clock::time_point enqueue_time{};
};

}  // namespace

RunSummary run_single_thread_benchmark(IOrderBook& book, WorkloadProfile profile, const std::vector<OrderEvent>& events) {
  book.reset();

  std::vector<double> latencies_ns;
  latencies_ns.reserve(events.size());

  std::uint64_t total_fills = 0;
  std::uint64_t total_rejects = 0;

  const auto start = std::chrono::steady_clock::now();
  for (const auto& event : events) {
    const auto event_start = std::chrono::steady_clock::now();
    const auto result = book.process_event(event);
    const auto event_end = std::chrono::steady_clock::now();

    latencies_ns.push_back(std::chrono::duration<double, std::nano>(event_end - event_start).count());
    total_fills += result.executions.size();
    total_rejects += result.status == EventStatus::Rejected ? 1 : 0;
  }
  const auto end = std::chrono::steady_clock::now();

  return summarize(book.name(), profile, events.size(), std::move(latencies_ns), end - start, 0, total_fills, total_rejects);
}

RunSummary run_concurrent_pipeline_benchmark(IOrderBook& book, WorkloadProfile profile, const std::vector<OrderEvent>& events) {
  book.reset();

  SpscQueue<PipelinePacket> ingress(1U << 16);
  SpscQueue<PipelineResult> egress(1U << 16);

  std::vector<double> latencies_ns;
  latencies_ns.reserve(events.size());

  std::atomic<bool> producer_done{false};
  std::atomic<std::uint64_t> max_queue_depth{0};
  std::uint64_t total_fills = 0;
  std::uint64_t total_rejects = 0;

  const auto start = std::chrono::steady_clock::now();

  std::thread producer([&]() {
    for (const auto& event : events) {
      PipelinePacket packet{
          .event = event,
          .is_poison_pill = false,
          .enqueue_time = std::chrono::steady_clock::now(),
      };
      while (!ingress.push(packet)) {
        std::this_thread::yield();
      }
      max_queue_depth.store(
          std::max<std::uint64_t>(max_queue_depth.load(std::memory_order_relaxed), ingress.size_approx()),
          std::memory_order_relaxed);
    }
    while (!ingress.push(PipelinePacket{
        .is_poison_pill = true,
        .enqueue_time = std::chrono::steady_clock::now(),
    })) {
      std::this_thread::yield();
    }
    producer_done.store(true, std::memory_order_release);
  });

  std::thread matcher([&]() {
    PipelinePacket packet;
    while (true) {
      if (!ingress.pop(packet)) {
        if (producer_done.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        continue;
      }

      if (packet.is_poison_pill) {
        while (!egress.push(PipelineResult{
            .is_poison_pill = true,
            .enqueue_time = packet.enqueue_time,
        })) {
          std::this_thread::yield();
        }
        break;
      }

      auto result = book.process_event(packet.event);
      while (!egress.push(PipelineResult{
          .result = std::move(result),
          .is_poison_pill = false,
          .enqueue_time = packet.enqueue_time,
      })) {
        std::this_thread::yield();
      }
    }
  });

  std::thread consumer([&]() {
    PipelineResult packet;
    while (true) {
      if (!egress.pop(packet)) {
        std::this_thread::yield();
        continue;
      }
      if (packet.is_poison_pill) {
        break;
      }
      const auto receive_time = std::chrono::steady_clock::now();
      latencies_ns.push_back(std::chrono::duration<double, std::nano>(receive_time - packet.enqueue_time).count());
      total_fills += packet.result.executions.size();
      total_rejects += packet.result.status == EventStatus::Rejected ? 1 : 0;
    }
  });

  producer.join();
  matcher.join();
  consumer.join();

  const auto end = std::chrono::steady_clock::now();
  return summarize(
      "optimized_concurrent_pipeline",
      profile,
      events.size(),
      std::move(latencies_ns),
      end - start,
      max_queue_depth.load(std::memory_order_relaxed),
      total_fills,
      total_rejects);
}

void write_benchmark_csv(const std::string& output_path, const std::vector<RunSummary>& summaries) {
  std::ofstream output(output_path);
  output << "engine,profile,orders,throughput_ops_per_sec,p50_ns,p95_ns,p99_ns,max_queue_depth,total_fills,total_rejects\n";
  output << std::fixed << std::setprecision(2);
  for (const auto& summary : summaries) {
    output << summary.engine_name << ','
           << to_string(summary.profile) << ','
           << summary.order_count << ','
           << summary.throughput_ops_per_sec << ','
           << summary.p50_ns << ','
           << summary.p95_ns << ','
           << summary.p99_ns << ','
           << summary.max_queue_depth << ','
           << summary.total_fills << ','
           << summary.total_rejects << '\n';
  }
}

}  // namespace lob
