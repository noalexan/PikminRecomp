// ai.a: the audio interface DMA. A clock event per DMA block plays the block the guest set up with
// AIInitDMA (big-endian R,L stereo frames) and raises the AI interrupt, whose callback (JAudio's
// AudioSync) queues the next block. Disc audio streaming (AISetStream*) is unused by Pikmin.
#include <cstdlib>
#include <vector>

#include "hle/dsp/zelda.h"

#include "rt/audio_out.hpp"
#include "rt/clock.hpp"
#include "rt/guest.hpp"
#include "rt/irq.hpp"
#include "rt/log.hpp"

using rt::arg;
using rt::ret;

namespace {
ppc_ctx* g_ctx = nullptr;
uint32_t g_addr = 0, g_len = 0;  // the block AIInitDMA set up (played next)
uint32_t g_callback = 0;
uint32_t g_dsp_rate_sel = 0;  // AI_SAMPLERATE_32KHZ = 0, AI_SAMPLERATE_48KHZ = 1
bool g_running = false;
const bool g_debug = std::getenv("PIKMIN_AUDIO_DEBUG") != nullptr;
uint64_t g_due = 0;           // tick the current block started
uint64_t g_blocks = 0;

// The GameCube's "32 kHz" DSP rate is 32029 Hz (JAudio's JAC_DAC_RATE says 32028.5).
uint32_t rate_hz() { return g_dsp_rate_sel ? 48000u : 32029u; }

void block() {
    if (!g_running) return;
    const uint32_t addr = g_addr, frames = g_len / 4, rate = rate_hz();
    if (frames && g_ctx) {
        static std::vector<int16_t> lr;
        lr.resize(frames * 2);
        const uint8_t* p = rt::host(g_ctx, addr, frames * 4);
        for (uint32_t i = 0; i < frames; ++i, p += 4) {
            lr[2 * i + 1] = static_cast<int16_t>(p[0] << 8 | p[1]);  // right first
            lr[2 * i] = static_cast<int16_t>(p[2] << 8 | p[3]);
        }
        rt::audio_out_push(lr.data(), frames, rate);
    }
    ++g_blocks;
    if (g_debug && g_blocks % 100 == 0) {
        const auto& st = DSP::HLE::g_zelda_stats;
        rt::log("ai", "block %llu: dsp frames %llu, voices flagged %llu enabled %llu mixed %llu",
                (unsigned long long)g_blocks, (unsigned long long)st.frames, (unsigned long long)st.flagged,
                (unsigned long long)st.enabled, (unsigned long long)st.voices);
    }
    if (uint32_t cb = g_callback) rt::irq().post([=](ppc_ctx* ic) { rt::call_guest(ic, cb); });
    // Next block: exactly one block later, unless the guest fell far behind (a stall): then resync
    // instead of firing a burst of catch-up interrupts.
    const uint64_t len_ticks = uint64_t(frames ? frames : 32) * rt::Clock::kTicksPerSec / rate;
    const uint64_t now = rt::clock().ticks();
    g_due += len_ticks;
    if (g_due + rt::Clock::kTicksPerSec / 10 < now) g_due = now;
    rt::schedule_at(g_due, block);
}
}  // namespace

namespace rt {
uint64_t ai_blocks_played() { return g_blocks; }
}  // namespace rt

extern "C" {

void hle_AIInit(ppc_ctx* c) {
    HLE_ENTRY(c, "AIInit");
    g_ctx = c;
}

void hle_AIInitDMA(ppc_ctx* c) {
    HLE_ENTRY(c, "AIInitDMA");
    g_ctx = c;
    g_addr = arg(c, 0) & ~31u;
    g_len = arg(c, 1) & ~31u;
}

void hle_AIStartDMA(ppc_ctx* c) {
    HLE_ENTRY(c, "AIStartDMA");
    g_ctx = c;
    if (g_running) return;
    g_running = true;
    g_due = rt::clock().ticks();
    rt::schedule_at(g_due, block);
}

void hle_AIRegisterDMACallback(ppc_ctx* c) {
    HLE_ENTRY(c, "AIRegisterDMACallback");
    uint32_t prev = g_callback;
    g_callback = arg(c, 0);
    ret(c, prev);
}

void hle_AISetDSPSampleRate(ppc_ctx* c) {
    HLE_ENTRY(c, "AISetDSPSampleRate");
    g_dsp_rate_sel = arg(c, 0) ? 1 : 0;
}

void hle_AIGetDSPSampleRate(ppc_ctx* c) {
    HLE_ENTRY(c, "AIGetDSPSampleRate");
    ret(c, g_dsp_rate_sel);
}

}  // extern "C"
