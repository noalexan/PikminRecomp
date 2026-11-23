#include <string>

#include "check.hpp"
#include "rt/perf.hpp"
#include "rt/sysinfo.hpp"
#include "rt/trace.hpp"

int main() {
    const double t240 = 1.0 / 240;
    double now = 100.0;
    auto frame = [&](double dt) {
        now += dt;
        rt::perf_frame(now, 0.0, 0.0, t240);
    };
    // fps_and_low: 240 fps with one 20 ms frame in 100 (the slowest 1%).
    rt::perf_reset();
    for (int i = 0; i < 960; ++i) frame(i % 100 == 50 ? 0.020 : t240);
    CHECK(rt::perf_fps() > 200 && rt::perf_fps() < 245);
    CHECK(rt::perf_one_percent_low() < 60);
    // history_window: only the last 4 s are kept.
    CHECK(!rt::perf_history().empty() && now - rt::perf_history().front().t <= 4.0 + 1e-9);

    // drop_cause_dvd: the largest DVD read since the previous frame, with the last opened path.
    rt::perf_reset();
    rt::trace_reset();
    for (int i = 0; i < 20; ++i) frame(t240);  // warm-up: no drops while starting
    rt::trace(rt::Ev::Dvd, "open", 7, 0, "dataDir/x.arc");
    rt::trace(rt::Ev::Dvd, "read", 0, 2'000'000);
    frame(0.040);
    frame(t240);
    auto d = rt::perf_last_drop();
    CHECK(d && d->cause.find("dvd read dataDir/x.arc") != std::string::npos && d->cause.find("2.0 MB") != std::string::npos);

    // long_stall_one_drop: consecutive slow frames are one drop, with the longest frame.
    rt::perf_reset();
    for (int i = 0; i < 20; ++i) frame(t240);
    for (int i = 0; i < 5; ++i) frame(0.400);
    frame(t240);
    CHECK(rt::perf_drop_count() == 1 && rt::perf_last_drop() && rt::perf_last_drop()->ms > 399.9);

    // steady_slow_is_not_a_drop: a machine that can't reach the target (20 ms frames at 240 fps) has
    // no drops; a 100 ms hitch in it does.
    rt::perf_reset();
    for (int i = 0; i < 200; ++i) frame(0.020);
    CHECK(rt::perf_drop_count() == 0);
    frame(0.100);
    frame(0.020);
    CHECK(rt::perf_drop_count() == 1);

    // cpu_parse / cpu_parse_missing
    CHECK(rt::cpu_ticks_from_stat("1234 (my (weird) prog) S 1 2 3 4 5 6 7 8 9 10 250 50 0 0") == 300);
    CHECK(rt::cpu_ticks_from_stat("") < 0 && rt::cpu_ticks_from_stat("12 (x) S 1 2") < 0);
    // pick_gpu_source
    using rt::GpuSource;
    CHECK(rt::pick_gpu_source(0x10de, true, true) == GpuSource::Nvml);
    CHECK(rt::pick_gpu_source(0x1002, true, true) == GpuSource::AmdSysfs);
    CHECK(rt::pick_gpu_source(0x8086, true, true) == GpuSource::None);
    CHECK(rt::pick_gpu_source(0x10de, false, true) == GpuSource::None);
    return check_result();
}
