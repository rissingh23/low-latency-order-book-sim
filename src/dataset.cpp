#include "lob/order_book.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace lob {
namespace {

std::string trim(std::string value) {
  const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  bool in_quotes = false;

  for (char ch : line) {
    if (ch == '"') {
      in_quotes = !in_quotes;
      continue;
    }
    if (ch == ',' && !in_quotes) {
      fields.push_back(trim(current));
      current.clear();
      continue;
    }
    current.push_back(ch);
  }

  fields.push_back(trim(current));
  return fields;
}

std::string require_field(
    const std::unordered_map<std::string, std::size_t>& header_index,
    const std::vector<std::string>& fields,
    const std::string& name,
    std::size_t line_number) {
  const auto it = header_index.find(name);
  if (it == header_index.end() || it->second >= fields.size()) {
    throw std::runtime_error("missing required column '" + name + "' on line " + std::to_string(line_number));
  }
  return fields[it->second];
}

std::string optional_field(
    const std::unordered_map<std::string, std::size_t>& header_index,
    const std::vector<std::string>& fields,
    const std::string& name) {
  const auto it = header_index.find(name);
  if (it == header_index.end() || it->second >= fields.size()) {
    return {};
  }
  return fields[it->second];
}

OrderType parse_type(const std::string& raw, std::size_t line_number) {
  const auto value = lower(trim(raw));
  if (value == "limit" || value == "l" || value == "add") {
    return OrderType::Limit;
  }
  if (value == "market" || value == "m" || value == "trade") {
    return OrderType::Market;
  }
  if (value == "cancel" || value == "c" || value == "delete") {
    return OrderType::Cancel;
  }
  throw std::runtime_error("unknown order type '" + raw + "' on line " + std::to_string(line_number));
}

Side parse_side(const std::string& raw, std::size_t line_number) {
  const auto value = lower(trim(raw));
  if (value == "buy" || value == "b" || value == "bid" || value == "1") {
    return Side::Buy;
  }
  if (value == "sell" || value == "s" || value == "ask" || value == "-1") {
    return Side::Sell;
  }
  throw std::runtime_error("unknown side '" + raw + "' on line " + std::to_string(line_number));
}

template <typename T>
T parse_integer(const std::string& raw, const std::string& field_name, std::size_t line_number) {
  try {
    if constexpr (std::is_same_v<T, std::uint64_t>) {
      return static_cast<T>(std::stoull(raw));
    } else if constexpr (std::is_same_v<T, std::uint32_t>) {
      return static_cast<T>(std::stoul(raw));
    } else {
      return static_cast<T>(std::stoll(raw));
    }
  } catch (const std::exception&) {
    throw std::runtime_error("invalid " + field_name + " value '" + raw + "' on line " + std::to_string(line_number));
  }
}

}  // namespace

std::vector<OrderEvent> load_events_from_csv(const std::string& csv_path, std::size_t limit) {
  std::ifstream input(csv_path);
  if (!input) {
    throw std::runtime_error("unable to open dataset: " + csv_path);
  }

  std::string header_line;
  if (!std::getline(input, header_line)) {
    throw std::runtime_error("dataset is empty: " + csv_path);
  }

  const auto header_fields = split_csv_line(header_line);
  std::unordered_map<std::string, std::size_t> header_index;
  header_index.reserve(header_fields.size());
  for (std::size_t i = 0; i < header_fields.size(); ++i) {
    header_index.emplace(lower(header_fields[i]), i);
  }

  if (!header_index.contains("type") || !header_index.contains("side") || !header_index.contains("order_id")) {
    throw std::runtime_error(
        "dataset must contain at least type, side, and order_id columns: " + csv_path);
  }

  std::vector<OrderEvent> events;
  events.reserve(limit == 0 ? 1U << 15 : limit);

  std::string line;
  std::size_t line_number = 1;
  std::uint64_t next_sequence = 1;

  while (std::getline(input, line)) {
    ++line_number;
    if (trim(line).empty()) {
      continue;
    }

    const auto fields = split_csv_line(line);
    OrderEvent event;
    event.type = parse_type(require_field(header_index, fields, "type", line_number), line_number);
    event.side = parse_side(require_field(header_index, fields, "side", line_number), line_number);
    event.order_id = parse_integer<OrderId>(
        require_field(header_index, fields, "order_id", line_number),
        "order_id",
        line_number);

    const auto sequence_value = optional_field(header_index, fields, "sequence");
    event.sequence = sequence_value.empty()
        ? next_sequence
        : parse_integer<std::uint64_t>(sequence_value, "sequence", line_number);

    const auto timestamp_value = optional_field(header_index, fields, "timestamp");
    event.timestamp = timestamp_value.empty()
        ? event.sequence
        : parse_integer<Timestamp>(timestamp_value, "timestamp", line_number);

    const auto price_value = optional_field(header_index, fields, "price");
    event.price = price_value.empty() ? 0 : parse_integer<Price>(price_value, "price", line_number);

    const auto qty_value = optional_field(header_index, fields, "qty");
    event.qty = qty_value.empty() ? 0 : parse_integer<Qty>(qty_value, "qty", line_number);

    events.push_back(event);
    next_sequence = event.sequence + 1;
    if (limit != 0 && events.size() >= limit) {
      break;
    }
  }

  if (events.empty()) {
    throw std::runtime_error("dataset has no events: " + csv_path);
  }

  return events;
}

std::string dataset_label_from_path(const std::string& csv_path) {
  const auto stem = std::filesystem::path(csv_path).stem().string();
  return stem.empty() ? "dataset" : stem;
}

}  // namespace lob
