// High framerate: the game loop runs once per host frame (display refresh or target_fps) instead of
// once per PAL retrace (or two). The engine already measures its frame delta from the time base
// (System::updateSysClock -> gsys+0x290, clamped to 1/30 s) and most systems scale by it; the ones
// that step per frame are fixed in camera.cpp. VI keeps its 50 Hz retrace (callbacks, retrace
// counts, input scripts and screenshots stay in real-time units).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "enhance/enhance.hpp"
#include "rt/clock.hpp"
#include "rt/gfx.hpp"
#include "rt/guest.hpp"
#include "rt/log.hpp"
#include "rt/sched.hpp"

extern "C" {
// Original translations of the hooked functions (weak: unit tests link the runtime without them).
__attribute__((weak)) void f_80047c68_orig(ppc_ctx* c);  // DGXGraphics::waitRetrace
__attribute__((weak)) void f_80044834_orig(ppc_ctx* c);  // System::updateSysClock
__attribute__((weak)) void f_800707f0_orig(ppc_ctx* c);  // CinematicPlayer::update
__attribute__((weak)) void hle_GXSetZMode(ppc_ctx* c);
__attribute__((weak)) void hle_GXCopyDisp(ppc_ctx* c);
}

namespace rt {
uint32_t vi_retrace_count();
}

namespace enh {

Settings& settings() {
    static Settings s;
    return s;
}

namespace {
float g_orig_frames = 1.f;
}
float orig_frames() { return original_timing() ? 1.f : g_orig_frames; }

int effective_fps() {
    const Settings& s = settings();
    if (s.target_fps > 0) return s.target_fps;
    return s.display_hz > 0 ? s.display_hz : 60;
}

namespace {
double g_present_s = 0, g_pace_s = 0;  // PIKMIN_TIMING breakdown
double host_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
constexpr uint32_t kPaceQueue = 0x817F0408u;  // wait key for frame pacing (no guest memory behind it)
uint64_t g_last_frame = 0;

// Sleeps the calling guest thread (other guest threads keep running) until the next frame slot.
void pace_frame() {
    const Settings& s = settings();
    // A vsync'd window at the display rate is paced by the swap chain alone; a second timer
    // would only beat against it.
    if (s.window_vsync && (s.target_fps == 0 || s.target_fps >= s.display_hz)) return;
    const uint64_t period = rt::Clock::kTicksPerSec / static_cast<uint64_t>(effective_fps());
    uint64_t now = rt::clock().ticks();
    const uint64_t target = g_last_frame + period;
    if (g_last_frame && now < target) {
        rt::schedule_at(target, [] { rt::sched().wakeup(kPaceQueue); });
        while (rt::clock().ticks() < target) rt::sched().sleep_on(kPaceQueue);
        g_last_frame = target;
    } else {
        g_last_frame = now;  // first frame, or running behind: no catch-up burst
    }
}
// With a vsync'd window, frames are shown at multiples of the refresh period, but the moment the
// game samples the clock jitters around the vblank. Feed whole refresh periods (tracking real time
// through an error accumulator) so motion advances evenly per displayed frame.
float vsync_quantize(float dt) {
    const Settings& s = settings();
    if (!s.window_vsync || s.display_hz <= 0) return dt;
    static double real = 0, fed = 0;
    const double period = 1.0 / s.display_hz;
    real += dt;
    const double err = real - fed;
    long n = std::lround(err / period);
    if (n < 1) n = 1;
    if (n > 8 || std::fabs(err - n * period) > 0.1) {  // hitch or drift: follow the clock
        fed = real;
        return dt;
    }
    fed += n * period;
    return static_cast<float>(std::min(n * period, 1.0 / 30.0));
}
}  // namespace
}  // namespace enh

extern "C" {

// DGXGraphics::waitRetrace: originally waits for gsys+0x14 retraces since the last frame, then
// copies the EFB to the XFB. Unlocked: wait for the next host frame slot instead.
void hle_waitRetrace__11DGXGraphicsFv(ppc_ctx* c) {
    HLE_ENTRY(c, "DGXGraphics::waitRetrace");
    if (enh::original_timing() || !f_80047c68_orig || !hle_GXCopyDisp) {
        if (f_80047c68_orig) f_80047c68_orig(c);
        return;
    }
    const uint32_t self = rt::arg(c, 0);
    const double t0 = enh::host_now();
    enh::pace_frame();
    const double t1 = enh::host_now();
    c->r[3] = 1;  // GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE)
    c->r[4] = 3;
    c->r[5] = 1;
    hle_GXSetZMode(c);
    const uint32_t gsys = rt::rd32(c, enh::kGsysPtr);
    const bool keep = rt::rd32(c, gsys + 0x26c) != 0 || rt::rd8(c, gsys) != 0;
    c->r[3] = rt::rd32(c, self + 0x610);
    c->r[4] = keep ? 0 : 1;
    hle_GXCopyDisp(c);
    enh::g_pace_s += t1 - t0;
    enh::g_present_s += enh::host_now() - t1;
    rt::wr32(c, self + 0x618, rt::vi_retrace_count());
    rt::wr32(c, self + 0x61c, rt::rd32(c, gsys + 0x14));
}

// System::updateSysClock: computes the frame delta. PIKMIN_TIMING=1 logs it once per second.
void hle_updateSysClock__6SystemFv(ppc_ctx* c) {
    HLE_ENTRY(c, "System::updateSysClock");
    const uint32_t self = rt::arg(c, 0);
    if (f_80044834_orig) f_80044834_orig(c);
    if (!enh::original_timing()) {
        uint32_t u = rt::rd32(c, self + enh::kFrameTimeOff);
        float dt;
        std::memcpy(&dt, &u, 4);
        dt = enh::vsync_quantize(dt);
        // Keep the original game speed: PAL frames took `rpf` retraces at 50 Hz, but the delta is
        // clamped to 1/30 s, so at 25 fps the game ran at 0.833x real time. Same factor here.
        const uint32_t rpf = std::max<uint32_t>(1, rt::rd32(c, self + 0x14));
        const float orig_frame = 0.02f * static_cast<float>(rpf);
        enh::g_orig_frames = std::min(dt / orig_frame, 4.f);
        dt *= std::min(orig_frame, 1.f / 30.f) / orig_frame;
        std::memcpy(&u, &dt, 4);
        rt::wr32(c, self + enh::kFrameTimeOff, u);
    }
    static const bool log = std::getenv("PIKMIN_TIMING") != nullptr;
    if (!log) return;
    static uint32_t frames = 0, last_retrace = 0;
    static uint64_t last_ticks = 0;
    ++frames;
    const uint32_t r = rt::vi_retrace_count();
    if (r - last_retrace >= 50) {
        const uint64_t now = rt::clock().ticks();
        uint32_t u = rt::rd32(c, self + enh::kFrameTimeOff);
        float dt;
        std::memcpy(&dt, &u, 4);
        const double secs = static_cast<double>(now - last_ticks) / rt::Clock::kTicksPerSec;
        rt::log("timing", "retrace %u: %u frames in %.3f s (%.1f fps), frameTime %.5f, retraces/frame %u, present %.1f ms/f, pacing %.1f ms/f", r,
                frames, secs, frames / secs, dt, rt::rd32(c, self + 0x14), 1000 * enh::g_present_s / frames,
                1000 * enh::g_pace_s / frames);
        enh::g_present_s = enh::g_pace_s = 0;
        frames = 0;
        last_retrace = r;
        last_ticks = now;
    }
}

// Cinematics (intro, title, day cutscenes) advance their frame (+0x2a4) by 1 per game frame.
// Make that 1 per original frame of real time.
void hle_update__15CinematicPlayerFv(ppc_ctx* c) {
    HLE_ENTRY(c, "CinematicPlayer::update");
    const uint32_t self = rt::arg(c, 0);
    if (f_800707f0_orig) f_800707f0_orig(c);
    if (enh::original_timing() || rt::arg(c, 0) != 0) return;  // r3 = 1: finished this frame
    uint32_t u = rt::rd32(c, self + 0x2a4);
    float frame;
    std::memcpy(&frame, &u, 4);
    frame += enh::orig_frames() - 1.f;
    std::memcpy(&u, &frame, 4);
    rt::wr32(c, self + 0x2a4, u);
}

}  // extern "C"
