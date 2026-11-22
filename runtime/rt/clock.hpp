// Guest time base (40.5 MHz, epoch 2000-01-01) in real or fast (virtual) mode, plus timed events.
#pragma once
#include <cstdint>
#include <functional>

namespace rt {

class Clock {
public:
    enum class Mode { Real, Fast };
    static constexpr uint64_t kTicksPerSec = 40'500'000;

    void init(Mode mode);
    Mode mode() const { return mode_; }
    // Current guest time. In fast mode each read also advances time slightly, so guest code
    // that polls the clock always makes progress.
    uint64_t ticks();
    // Fast mode: jump forward. Real mode: sleep until the host reaches `t`.
    void advance_to(uint64_t t);

private:
    Mode mode_ = Mode::Real;
    uint64_t base_ = 0;     // ticks at init
    int64_t host_ns0_ = 0;  // host steady clock at init
    uint64_t virt_ = 0;     // fast-mode counter
};

Clock& clock();

// Run fn (on the host, at a safe point or while idle) once guest time reaches tick.
void schedule_at(uint64_t tick, std::function<void()> fn);
// Earliest pending event tick, or UINT64_MAX.
uint64_t next_event_tick();
// Runs every event whose tick has passed.
void run_due_events();
void clear_events();

}  // namespace rt
