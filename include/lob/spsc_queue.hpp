#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>

namespace lob {

template <typename T>
class SpscQueue {
 public:
  explicit SpscQueue(std::size_t capacity)
      : capacity_(capacity + 1),
        storage_(std::make_unique<Storage[]>(capacity_)) {}

  bool push(const T& value) {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto next = increment(head);
    if (next == tail_.load(std::memory_order_acquire)) {
      return false;
    }

    storage_[head].engaged = true;
    storage_[head].value = value;
    head_.store(next, std::memory_order_release);
    return true;
  }

  bool pop(T& out) {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;
    }

    out = storage_[tail].value;
    storage_[tail].engaged = false;
    tail_.store(increment(tail), std::memory_order_release);
    return true;
  }

  std::size_t size_approx() const {
    const auto head = head_.load(std::memory_order_acquire);
    const auto tail = tail_.load(std::memory_order_acquire);
    if (head >= tail) {
      return head - tail;
    }
    return capacity_ - (tail - head);
  }

 private:
  struct Storage {
    T value{};
    bool engaged{false};
  };

  std::size_t increment(std::size_t value) const {
    return (value + 1) % capacity_;
  }

  const std::size_t capacity_;
  std::unique_ptr<Storage[]> storage_;
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

}  // namespace lob
