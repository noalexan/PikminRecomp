// Host time <-> guest tick conversions: exact, and no overflow over long sessions.
#include <cstdint>

#include "check.hpp"
#include "hle_test_util.hpp"
#include "rt/clock.hpp"

DEFINE_TEST_HOOKS

int main() {
    using rt::Clock;
    constexpr int64_t s = 1'000'000'000;
    CHECK(Clock::ns_to_ticks(0) == 0);
    CHECK(Clock::ns_to_ticks(s) == Clock::kTicksPerSec);
    CHECK(Clock::ns_to_ticks(s / 2) == Clock::kTicksPerSec / 2);
    // ns * 40.5 MHz passed 2^64 at ~455 s: the guest clock wrapped and every event stalled.
    CHECK(Clock::ns_to_ticks(456 * s) == 456 * Clock::kTicksPerSec);
    CHECK(Clock::ns_to_ticks(10 * 3600 * s + 123) == 10 * 3600 * Clock::kTicksPerSec + 123 * 405 / 10'000);
    CHECK(Clock::ns_to_ticks(30LL * 24 * 3600 * s) == 30ULL * 24 * 3600 * Clock::kTicksPerSec);  // a month
    CHECK(Clock::ticks_to_ns(Clock::kTicksPerSec) == s);
    CHECK(Clock::ticks_to_ns(600 * Clock::kTicksPerSec) == 600 * s);  // same overflow, the other way
    return check_result();
}
