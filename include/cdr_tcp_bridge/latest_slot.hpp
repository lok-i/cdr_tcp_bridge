#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace cdr_tcp_bridge
{

template<typename T>
class LatestSlot
{
public:
  struct Item
  {
    std::shared_ptr<T> value;
    uint64_t sequence = 0;
  };

  explicit LatestSlot(double max_rate_hz = 0.0)
  : min_period_(
      max_rate_hz > 0.0 ? std::chrono::duration<double>(1.0 / max_rate_hz) :
      std::chrono::duration<double>::zero())
  {
  }

  void offer(std::shared_ptr<T> value)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++received_;
    if (latest_) {
      ++dropped_;
    }
    latest_ = std::move(value);
    latest_sequence_ = received_;
  }

  Item take_if_due(std::chrono::steady_clock::time_point now)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!latest_) {
      return {};
    }
    if (last_taken_ != std::chrono::steady_clock::time_point{} &&
      now - last_taken_ < min_period_)
    {
      return {};
    }
    Item item{std::move(latest_), latest_sequence_};
    last_taken_ = now;
    ++taken_;
    return item;
  }

  uint64_t received() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return received_;
  }

  uint64_t dropped() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
  }

  uint64_t taken() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return taken_;
  }

private:
  mutable std::mutex mutex_;
  std::shared_ptr<T> latest_;
  uint64_t latest_sequence_ = 0;
  uint64_t received_ = 0;
  uint64_t dropped_ = 0;
  uint64_t taken_ = 0;
  std::chrono::duration<double> min_period_;
  std::chrono::steady_clock::time_point last_taken_{};
};

}  // namespace cdr_tcp_bridge
