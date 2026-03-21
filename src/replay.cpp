#include "lob/order_book.hpp"

#include <fstream>
#include <sstream>

namespace lob {
namespace {

std::string json_escape(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (const char ch : input) {
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        out += ch;
        break;
    }
  }
  return out;
}

void write_top(std::ostream& out, const TopOfBook& top) {
  out << "{";
  out << "\"best_bid\":";
  if (top.best_bid) {
    out << *top.best_bid;
  } else {
    out << "null";
  }
  out << ",\"best_bid_qty\":";
  if (top.best_bid_qty) {
    out << *top.best_bid_qty;
  } else {
    out << "null";
  }
  out << ",\"best_ask\":";
  if (top.best_ask) {
    out << *top.best_ask;
  } else {
    out << "null";
  }
  out << ",\"best_ask_qty\":";
  if (top.best_ask_qty) {
    out << *top.best_ask_qty;
  } else {
    out << "null";
  }
  out << "}";
}

void write_depth_side(std::ostream& out, const std::vector<DepthLevel>& levels) {
  out << "[";
  for (std::size_t i = 0; i < levels.size(); ++i) {
    if (i != 0) {
      out << ",";
    }
    out << "{\"price\":" << levels[i].price << ",\"qty\":" << levels[i].qty << "}";
  }
  out << "]";
}

void write_depth(std::ostream& out, const BookDepth& depth) {
  out << "{";
  out << "\"bids\":";
  write_depth_side(out, depth.bids);
  out << ",\"asks\":";
  write_depth_side(out, depth.asks);
  out << "}";
}

void write_executions(std::ostream& out, const std::vector<Execution>& executions) {
  out << "[";
  for (std::size_t i = 0; i < executions.size(); ++i) {
    if (i != 0) {
      out << ",";
    }
    const auto& execution = executions[i];
    out << "{"
        << "\"resting_order_id\":" << execution.resting_order_id
        << ",\"aggressive_order_id\":" << execution.aggressive_order_id
        << ",\"price\":" << execution.price
        << ",\"qty\":" << execution.qty
        << ",\"timestamp\":" << execution.timestamp
        << ",\"aggressive_is_buy\":" << (execution.aggressive_is_buy ? "true" : "false")
        << "}";
  }
  out << "]";
}

}  // namespace

void write_replay_json(
    IOrderBook& book,
    WorkloadProfile profile,
    const std::vector<OrderEvent>& events,
    const std::string& output_path,
    std::size_t depth_levels) {
  write_replay_json(book, to_string(profile), events, output_path, depth_levels);
}

void write_replay_json(
    IOrderBook& book,
    std::string_view input_label,
    const std::vector<OrderEvent>& events,
    const std::string& output_path,
    std::size_t depth_levels) {
  book.reset();

  std::ofstream out(output_path);
  out << "{";
  out << "\"engine\":\"" << book.name() << "\",";
  out << "\"profile\":\"" << json_escape(std::string(input_label)) << "\",";
  out << "\"order_count\":" << events.size() << ",";
  out << "\"depth_levels\":" << depth_levels << ",";
  out << "\"steps\":[";

  for (std::size_t i = 0; i < events.size(); ++i) {
    if (i != 0) {
      out << ",";
    }

    const auto& event = events[i];
    const auto result = book.process_event(event);
    const auto top = book.snapshot_top_of_book();
    const auto depth = book.snapshot_depth(depth_levels);

    out << "{";
    out << "\"sequence\":" << event.sequence << ",";
    out << "\"timestamp\":" << event.timestamp << ",";
    out << "\"event\":{"
        << "\"type\":\"" << to_string(event.type) << "\","
        << "\"side\":\"" << to_string(event.side) << "\","
        << "\"order_id\":" << event.order_id << ","
        << "\"price\":" << event.price << ","
        << "\"qty\":" << event.qty
        << "},";
    out << "\"result\":{"
        << "\"status\":" << static_cast<int>(result.status) << ","
        << "\"filled_qty\":" << result.filled_qty << ","
        << "\"remaining_qty\":" << result.remaining_qty << ","
        << "\"message\":\"" << json_escape(result.message) << "\","
        << "\"executions\":";
    write_executions(out, result.executions);
    out << "},";
    out << "\"top\":";
    write_top(out, top);
    out << ",";
    out << "\"depth\":";
    write_depth(out, depth);
    out << "}";
  }

  out << "]";
  out << "}";
}

}  // namespace lob
