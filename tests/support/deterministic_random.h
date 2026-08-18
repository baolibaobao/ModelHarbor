#pragma once

#include <cstdint>
#include <limits>

namespace modelharbor::test_support {

class DeterministicRandom final {
  public:
    explicit DeterministicRandom(std::uint64_t seed = 0x4d6f64656c486172ULL) : state_(seed) {}

    std::uint64_t next() {
        state_ ^= state_ << 7;
        state_ ^= state_ >> 9;
        state_ ^= state_ << 8;
        return state_;
    }

    std::uint64_t uniform(std::uint64_t upperExclusive) {
        if (upperExclusive == 0)
            return 0;
        return next() % upperExclusive;
    }

  private:
    std::uint64_t state_;
};

} // namespace modelharbor::test_support
