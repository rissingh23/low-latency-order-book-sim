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
    std::string input_label,
    WorkloadProfile profile,
    std::size_t order_count,
    std::vector<double> service_latencies_ns,
    std::vector<double> end_to_end_latencies_ns,
    std::vector<double> queue_delays_ns,
    std::chrono::steady_clock::duration total_duration,
    std::uint64_t max_queue_depth,
    std::uint64_t total_fills,
    std::uint64_t total_rejects) {
  const double seconds = std::chrono::duration<double>(total_duration).count();
  return {
      .engine_name = std::move(engine_name),
      .input_label = std::move(input_label),
      .profile = profile,
      .order_count = order_count,
      .throughput_ops_per_sec = seconds > 0.0 ? static_cast<double>(order_count) / seconds : 0.0,
      .service_p50_ns = percentile_ns(service_latencies_ns, 0.50),
      .service_p95_ns = percentile_ns(service_latencies_ns, 0.95),
      .service_p99_ns = percentile_ns(service_latencies_ns, 0.99),
      .end_to_end_p50_ns = percentile_ns(end_to_end_latencies_ns, 0.50),
      .end_to_end_p95_ns = percentile_ns(end_to_end_latencies_ns, 0.95),
      .end_to_end_p99_ns = percentile_ns(end_to_end_latencies_ns, 0.99),
      .queue_delay_p50_ns = percentile_ns(queue_delays_ns, 0.50),
      .queue_delay_p95_ns = percentile_ns(queue_delays_ns, 0.95),
      .queue_delay_p99_ns = percentile_ns(queue_delays_ns, 0.99),
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
  std::chrono::steady_clock::time_point match_start_time{};
  std::chrono::steady_clock::time_point match_end_time{};
};

}  // namespace

RunSummary run_single_thread_benchmark(IOrderBook& book, WorkloadProfile profile, const std::vector<OrderEvent>& events) {
  return run_single_thread_benchmark(book, to_string(profile), events);
}

RunSummary run_single_thread_benchmark(IOrderBook& book, std::string_view input_label, const std::vector<OrderEvent>& events) {
  book.reset();

  std::vector<double> service_latencies_ns;
  std::vector<double> end_to_end_latencies_ns;
  std::vector<double> queue_delays_ns;
  service_latencies_ns.reserve(events.size());
  end_to_end_latencies_ns.reserve(events.size());
  queue_delays_ns.reserve(events.size());

  std::uint64_t total_fills = 0;
  std::uint64_t total_rejects = 0;

  const auto start = std::chrono::steady_clock::now();
  for (const auto& event : events) {
    const auto event_start = std::chrono::steady_clock::now();
    const auto result = book.process_event(event);
    const auto event_end = std::chrono::steady_clock::now();

    const double service_ns = std::chrono::duration<double, std::nano>(event_end - event_start).count();
    service_latencies_ns.push_back(service_ns);
    end_to_end_latencies_ns.push_back(service_ns);
    queue_delays_ns.push_back(0.0);
    total_fills += result.executions.size();
    total_rejects += result.status == EventStatus::Rejected ? 1 : 0;
  }
  const auto end = std::chrono::steady_clock::now();

  return summarize(
      book.name(),
      std::string(input_label),
      WorkloadProfile::Balanced,
      events.size(),
      std::move(service_latencies_ns),
      std::move(end_to_end_latencies_ns),
      std::move(queue_delays_ns),
      end - start,
      0,
      total_fills,
      total_rejects);
}

RunSummary run_concurrent_pipeline_benchmark(IOrderBook& book, WorkloadProfile profile, const std::vector<OrderEvent>& events) {
  return run_concurrent_pipeline_benchmark(book, to_string(profile), events);
}

RunSummary run_concurrent_pipeline_benchmark(IOrderBook& book, std::string_view input_label, const std::vector<OrderEvent>& events) {
  book.reset();

  SpscQueue<PipelinePacket> ingress(1U << 16);
  SpscQueue<PipelineResult> egress(1U << 16);

  std::vector<double> service_latencies_ns;
  std::vector<double> end_to_end_latencies_ns;
  std::vector<double> queue_delays_ns;
  service_latencies_ns.reserve(events.size());
  end_to_end_latencies_ns.reserve(events.size());
  queue_delays_ns.reserve(events.size());

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
      bool processed_any = false;
      while (ingress.pop(packet)) {
        processed_any = true;
        if (packet.is_poison_pill) {
          while (!egress.push(PipelineResult{
              .is_poison_pill = true,
              .enqueue_time = packet.enqueue_time,
          })) {
            std::this_thread::yield();
          }
          return;
        }

        const auto match_start = std::chrono::steady_clock::now();
        auto result = book.process_event(packet.event);
        const auto match_end = std::chrono::steady_clock::now();
        while (!egress.push(PipelineResult{
            .result = std::move(result),
            .is_poison_pill = false,
            .enqueue_time = packet.enqueue_time,
            .match_start_time = match_start,
            .match_end_time = match_end,
        })) {
          std::this_thread::yield();
        }
      }

      if (!processed_any) {
        std::this_thread::yield();
      }
    } 
  });

  std::thread consumer([&]() {
    PipelineResult packet;
    while (true) {
      bool processed_any = false;
      while (egress.pop(packet)) {
        processed_any = true;
        if (packet.is_poison_pill) {
          return;
        }
        const auto receive_time = std::chrono::steady_clock::now();
        const double service_ns = std::chrono::duration<double, std::nano>(packet.match_end_time - packet.match_start_time).count();
        const double end_to_end_ns = std::chrono::duration<double, std::nano>(receive_time - packet.enqueue_time).count();
        const double queue_delay_ns = std::max(0.0, end_to_end_ns - service_ns);
        service_latencies_ns.push_back(service_ns);
        end_to_end_latencies_ns.push_back(end_to_end_ns);
        queue_delays_ns.push_back(queue_delay_ns);
        total_fills += packet.result.executions.size();
        total_rejects += packet.result.status == EventStatus::Rejected ? 1 : 0;
      }

      if (!processed_any) {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  matcher.join();
  consumer.join();

  const auto end = std::chrono::steady_clock::now();
  return summarize(
      "optimized_concurrent_pipeline",
      std::string(input_label),
      WorkloadProfile::Balanced,
      events.size(),
      std::move(service_latencies_ns),
      std::move(end_to_end_latencies_ns),
      std::move(queue_delays_ns),
      end - start,
      max_queue_depth.load(std::memory_order_relaxed),
      total_fills,
      total_rejects);
}

void write_benchmark_csv(const std::string& output_path, const std::vector<RunSummary>& summaries) {
  std::ofstream output(output_path);
  output << "engine,profile,orders,throughput_ops_per_sec,service_p50_ns,service_p95_ns,service_p99_ns,end_to_end_p50_ns,end_to_end_p95_ns,end_to_end_p99_ns,queue_delay_p50_ns,queue_delay_p95_ns,queue_delay_p99_ns,max_queue_depth,total_fills,total_rejects\n";
  output << std::fixed << std::setprecision(2);
  for (const auto& summary : summaries) {
    output << summary.engine_name << ','
           << summary.input_label << ','
           << summary.order_count << ','
           << summary.throughput_ops_per_sec << ','
           << summary.service_p50_ns << ','
           << summary.service_p95_ns << ','
           << summary.service_p99_ns << ','
           << summary.end_to_end_p50_ns << ','
           << summary.end_to_end_p95_ns << ','
           << summary.end_to_end_p99_ns << ','
           << summary.queue_delay_p50_ns << ','
           << summary.queue_delay_p95_ns << ','
           << summary.queue_delay_p99_ns << ','
           << summary.max_queue_depth << ','
           << summary.total_fills << ','
           << summary.total_rejects << '\n';
  }
}

}  // namespace lob
