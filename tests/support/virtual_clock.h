#pragma once

#include <chrono>
#include <mutex>

namespace modelharbor::test_support {

class VirtualClock final {
  public:
    using duration = std::chrono::steady_clock::duration;
    using time_point = std::chrono::steady_clock::time_point;

    VirtualClock() = default;
    explicit VirtualClock(time_point initial) : now_(initial) {}

    time_point now() const {
        std::scoped_lock lock(mutex_);
        return now_;
    }

    void advance(duration amount) {
        std::scoped_lock lock(mutex_);
        now_ += amount;
    }

  private:
    mutable std::mutex mutex_;
    time_point now_{};
};

} // namespace modelharbor::test_support
