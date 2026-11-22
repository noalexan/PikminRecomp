// Modern camera: the right stick orbits the camera around Olimar (yaw) and tilts it (pitch); the
// GameCube C-stick (swarming) moves to modifier + right stick. Also makes the camera's per-frame
// smoothing framerate independent.
//
// PcamCamera (plugPikiNakata pcamcamera.cpp) keeps a polar position around its target:
//   +0xc8 yaw (radians, wrapped to [0, 2pi)), elevation from getCurrentAngle() (degrees),
//   +0x9c mode (1 = "attention": L recentres behind Olimar), +0x20 controllable flag.
// Its smoothing factors are fractions per frame, tuned for the original frame rate.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "enhance/enhance.hpp"
#include "rt/guest.hpp"
#include "rt/log.hpp"

namespace rt {
uint32_t vi_retrace_count();
}

extern "C" {
__attribute__((weak)) void f_80120bf0_orig(ppc_ctx* c);  // PcamCamera::control(Controller&)
__attribute__((weak)) void f_801218bc_orig(ppc_ctx* c);  // PcamCamera::getCurrentAngle()
__attribute__((weak)) void f_80121994_orig(ppc_ctx* c);  // PcamCamera::getCurrentHomingSpeed()
__attribute__((weak)) void f_80120ffc_orig(ppc_ctx* c);  // PcamCamera::makePosture()
}

namespace enh {
namespace {
float g_stick_x = 0.f, g_stick_y = 0.f;  // camera stick, [-1, 1], up positive
float g_pitch = 0.f;                     // degrees added to the camera elevation
constexpr float kYawSpeed = 2.6f;        // rad/s at full deflection
constexpr float kPitchSpeed = 70.f;      // deg/s at full deflection
constexpr float kMinElevation = 8.f, kMaxElevation = 82.f;
constexpr float kRefFrame = 1.f / 30.f;  // fallback frame time

float rdf(ppc_ctx* c, uint32_t ea) {
    uint32_t u = rt::rd32(c, ea);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
void wrf(ppc_ctx* c, uint32_t ea, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    rt::wr32(c, ea, u);
}

float frame_time(ppc_ctx* c) {
    const uint32_t gsys = rt::rd32(c, kGsysPtr);
    return gsys ? rdf(c, gsys + kFrameTimeOff) : kRefFrame;
}

// A per-frame lerp fraction tuned for the original frame, converted for `n` original frames.
float rescale_fraction(float f, float n) {
    if (f <= 0.f || f >= 1.f) return f;
    return 1.f - std::pow(1.f - f, n);
}

float shape(float v) {  // dead zone + quadratic response
    const float a = std::fabs(v);
    if (a < 0.12f) return 0.f;
    const float t = (a - 0.12f) / 0.88f;
    return std::copysign(t * t, v);
}

uint32_t params(ppc_ctx* c, uint32_t cam) {  // PcamCameraParameters values
    uint32_t a = rt::rd32(c, cam + 0xd0);
    uint32_t b = a ? rt::rd32(c, a + 0x24) : 0;
    uint32_t d = b ? rt::rd32(c, b + 4) : 0;
    return d ? rt::rd32(c, d) : 0;
}
}  // namespace

void route_right_stick(ppc_ctx* c, uint32_t status, bool swarm_modifier) {
    if (!settings().modern_camera) return;
    const auto sx = static_cast<int8_t>(rt::rd8(c, status + 4)), sy = static_cast<int8_t>(rt::rd8(c, status + 5));
    if (swarm_modifier) {
        g_stick_x = g_stick_y = 0.f;  // the guest sees the C-stick: swarm
        return;
    }
    g_stick_x = std::clamp(sx / 100.f, -1.f, 1.f);
    g_stick_y = std::clamp(sy / 100.f, -1.f, 1.f);
    rt::wr8(c, status + 4, 0);
    rt::wr8(c, status + 5, 0);
}

}  // namespace enh

extern "C" {

void hle_control__10PcamCameraFR10Controller(ppc_ctx* c) {
    HLE_ENTRY(c, "PcamCamera::control");
    const uint32_t cam = rt::arg(c, 0);
    if (f_80120bf0_orig) f_80120bf0_orig(c);
    static const bool trace = std::getenv("PIKMIN_TIMING") != nullptr;
    if (trace) {  // speed checks: camera target (follows Olimar) once per 25 retraces
        static uint32_t last = 0;
        const uint32_t r = rt::vi_retrace_count();
        if (r / 25 != last / 25) {
            rt::log("camtrace", "retrace %u target %.2f %.2f %.2f yaw %.3f", r, enh::rdf(c, cam + 0x14),
                    enh::rdf(c, cam + 0x18), enh::rdf(c, cam + 0x1c), enh::rdf(c, cam + 0xc8));
            last = r;
        }
    }
    if (!enh::settings().modern_camera || rt::rd8(c, cam + 0x20) == 0) return;
    const float dt = enh::frame_time(c);
    if (rt::rd32(c, cam + 0x9c) == 1) {  // recentring behind Olimar (L): also level the tilt
        enh::g_pitch *= std::exp(-dt * 6.f);
        return;
    }
    const float x = enh::shape(enh::g_stick_x), y = enh::shape(enh::g_stick_y);
    if (x != 0.f) {
        float yaw = enh::rdf(c, cam + 0xc8) - x * enh::kYawSpeed * dt;
        const float two_pi = 6.2831853f;
        yaw = std::fmod(yaw, two_pi);
        if (yaw < 0.f) yaw += two_pi;
        enh::wrf(c, cam + 0xc8, yaw);
    }
    // Stick up lowers the camera (look up toward the horizon), down raises it (more top-down).
    enh::g_pitch -= y * enh::kPitchSpeed * dt;
    enh::g_pitch = std::clamp(enh::g_pitch, -60.f, 60.f);
}

// Elevation in degrees: original value plus the player's tilt, kept in a sane range.
void hle_getCurrentAngle__10PcamCameraFv(ppc_ctx* c) {
    HLE_ENTRY(c, "PcamCamera::getCurrentAngle");
    const uint32_t cam = rt::arg(c, 0);
    if (f_801218bc_orig) f_801218bc_orig(c);
    if (!enh::settings().modern_camera || enh::g_pitch == 0.f || rt::rd8(c, cam + 0x20) == 0) return;
    const float base = static_cast<float>(c->f[1].ps0);
    const float lo = std::min(base, enh::kMinElevation), hi = std::max(base, enh::kMaxElevation);
    const float v = std::clamp(base + enh::g_pitch, lo, hi);
    enh::g_pitch = v - base;  // don't wind up beyond the limits
    c->f[1].ps0 = c->f[1].ps1 = static_cast<double>(v);
}

// Fraction of the remaining distance the camera closes per frame.
void hle_getCurrentHomingSpeed__10PcamCameraFv(ppc_ctx* c) {
    HLE_ENTRY(c, "PcamCamera::getCurrentHomingSpeed");
    if (f_80121994_orig) f_80121994_orig(c);
    if (enh::original_timing()) return;
    const float f = enh::rescale_fraction(static_cast<float>(c->f[1].ps0), enh::orig_frames());
    c->f[1].ps0 = c->f[1].ps1 = static_cast<double>(f);
}

// makePosture also lerps the FOV (param 0xac) and, while recentring, the yaw (fraction 0xb8,
// step limit 0xd8 per frame): scale those parameters for this frame's length around the call.
void hle_makePosture__10PcamCameraFv(ppc_ctx* c) {
    HLE_ENTRY(c, "PcamCamera::makePosture");
    const uint32_t p = enh::params(c, rt::arg(c, 0));
    if (!p || !f_80120ffc_orig || enh::original_timing()) {
        if (f_80120ffc_orig) f_80120ffc_orig(c);
        return;
    }
    const float n = enh::orig_frames();
    const float fov = enh::rdf(c, p + 0xac), turn = enh::rdf(c, p + 0xb8), step = enh::rdf(c, p + 0xd8);
    enh::wrf(c, p + 0xac, enh::rescale_fraction(fov, n));
    enh::wrf(c, p + 0xb8, enh::rescale_fraction(turn, n));
    enh::wrf(c, p + 0xd8, step * n);
    f_80120ffc_orig(c);
    enh::wrf(c, p + 0xac, fov);
    enh::wrf(c, p + 0xb8, turn);
    enh::wrf(c, p + 0xd8, step);
}

}  // extern "C"
