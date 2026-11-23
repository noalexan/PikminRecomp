// Frame-time records for the debug HUD: fps, 1% low, a 4 s history and frame drops with their likely
// cause (from the recent-events ring). Host steady-clock times only: never the guest clock, which
// advances --fast virtual time when read.
#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace rt {

struct FrameRecord {
    double t;  // host time at the end of the frame (s)
    double total_ms, game_ms, present_ms, pace_ms;
    uint32_t retrace;
    bool drop;
};
struct Drop {
    double ms;
    uint32_t retrace;
    std::string cause;
};

// One call per game frame. A frame longer than 2x the larger of target_s and the average of the last
// 0.5 s is a drop; consecutive drop frames
// count as one, logged as "[perf] drop 38.1 ms at retrace N: <cause>" once the run ends.
void perf_frame(double now_s, double present_s, double pace_s, double target_s);
const std::deque<FrameRecord>& perf_history();  // last 4 s
double perf_fps();  // over the last 0.5 s
double perf_ms();
double perf_one_percent_low();  // fps of the slowest 1% of frames over 4 s
double perf_target_ms();
std::optional<Drop> perf_last_drop();
int perf_drop_count();
void perf_reset();  // tests

}  // namespace rt
