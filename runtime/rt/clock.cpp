#include "rt/clock.hpp"

#include <time.h>

#include <chrono>
#include <map>
#include <thread>

namespace rt {
namespace {
constexpr int64_t kUnixTo2000 = 946684800;  // seconds from 1970-01-01 to 2000-01-01

int64_t host_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::multimap<uint64_t, std::function<void()>>& events() {
    static std::multimap<uint64_t, std::function<void()>> e;
    return e;
}
}  // namespace

Clock& clock() {
    static Clock c;
    return c;
}

void Clock::init(Mode mode) {
    mode_ = mode;
    int64_t unix_s = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    base_ = static_cast<uint64_t>(unix_s - kUnixTo2000) * kTicksPerSec;
    host_ns0_ = host_ns();
    virt_ = base_;
    clear_events();
}

uint64_t Clock::ticks() {
    if (mode_ == Mode::Fast) {
        virt_ += 40;  // ~1 us per read
        return virt_;
    }
    const int64_t ns = host_ns() - host_ns0_;
    return base_ + ns_to_ticks(static_cast<uint64_t>(ns > 0 ? ns : 0));
}

uint64_t Clock::coarse_ticks() {
    if (mode_ == Mode::Fast) return ticks();
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
    const int64_t ns = static_cast<int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec - host_ns0_;
    return base_ + ns_to_ticks(static_cast<uint64_t>(ns > 0 ? ns : 0));
}

void Clock::advance_to(uint64_t t) {
    if (mode_ == Mode::Fast) {
        if (t > virt_) virt_ = t;
        return;
    }
    uint64_t now = ticks();
    if (t > now) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(ticks_to_ns(t - now)));
    }
}

void schedule_at(uint64_t tick, std::function<void()> fn) { events().emplace(tick, std::move(fn)); }

uint64_t next_event_tick() { return events().empty() ? UINT64_MAX : events().begin()->first; }

void run_due_events() {
    // Called at every safe point: skip the precise (possibly slow, e.g. HPET) clock read while the
    // next event is certainly in the future. Fast mode keeps reading ticks() (that advances virtual time).
    if (clock().mode() == Clock::Mode::Real &&
        (events().empty() || clock().coarse_ticks() + Clock::kCoarseLag < events().begin()->first)) {
        return;
    }
    uint64_t now = clock().ticks();
    while (!events().empty() && events().begin()->first <= now) {
        auto fn = std::move(events().begin()->second);
        events().erase(events().begin());
        fn();
    }
}

void clear_events() { events().clear(); }

}  // namespace rt
