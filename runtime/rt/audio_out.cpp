#include "rt/audio_out.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "rt/log.hpp"

#ifdef PIKMIN_GRAPHICS
#include <SDL3/SDL.h>
#endif

namespace rt {
namespace {
FILE* g_wav = nullptr;
uint32_t g_wav_rate = 0;
uint64_t g_wav_bytes = 0;
#ifdef PIKMIN_GRAPHICS
SDL_AudioStream* g_stream = nullptr;
int g_stream_rate = 0;
#endif

void put_le32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

// Rewrites the RIFF header with the current sizes, so the file is valid even if the process
// _Exit()s (the runtime never runs static destructors).
void wav_header() {
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
                     1,   0,   2,   0,   0, 0, 0, 0, 0,   0,   0,   0,   4,   0,   16,  0,   'd', 'a', 't', 'a'};
    uint32_t data = static_cast<uint32_t>(std::min<uint64_t>(g_wav_bytes, 0xFFFFFFFFu - 36));
    put_le32(h + 4, 36 + data);
    put_le32(h + 24, g_wav_rate);
    put_le32(h + 28, g_wav_rate * 4);
    put_le32(h + 40, data);
    std::fseek(g_wav, 0, SEEK_SET);
    std::fwrite(h, 1, sizeof h, g_wav);
    std::fseek(g_wav, 0, SEEK_END);
}

void wav_push(const int16_t* lr, size_t frames, uint32_t rate) {
    if (!g_wav) return;
    if (!g_wav_rate) g_wav_rate = rate;
    uint8_t buf[4096];
    size_t n = 0;
    for (size_t i = 0; i < frames * 2; ++i) {
        buf[n++] = static_cast<uint8_t>(lr[i]);
        buf[n++] = static_cast<uint8_t>(static_cast<uint16_t>(lr[i]) >> 8);
        if (n == sizeof buf) {
            std::fwrite(buf, 1, n, g_wav);
            n = 0;
        }
    }
    std::fwrite(buf, 1, n, g_wav);
    g_wav_bytes += frames * 4;
    wav_header();
    std::fflush(g_wav);
}

#ifdef PIKMIN_GRAPHICS
void open_device(uint32_t rate) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log("audio", "no audio: SDL audio init failed: %s", SDL_GetError());
        return;
    }
    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(rate)};
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!g_stream) {
        log("audio", "no audio: cannot open playback device: %s", SDL_GetError());
        return;
    }
    g_stream_rate = static_cast<int>(rate);
    SDL_ResumeAudioStreamDevice(g_stream);
    log("audio", "playback via SDL (%s), %u Hz", SDL_GetCurrentAudioDriver(), rate);
}

void device_push(const int16_t* lr, size_t frames, uint32_t rate) {
    if (!g_stream) return;
    if (static_cast<int>(rate) != g_stream_rate) {
        SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(rate)};
        SDL_SetAudioStreamFormat(g_stream, &spec, nullptr);
        g_stream_rate = static_cast<int>(rate);
    }
    // The guest produces at its own (host-clock) rate and the device consumes at its own; keep
    // the queue near the target by nudging the playback speed, and drop audio if it runs away
    // (e.g. after a hitch that let the guest catch up in a burst).
    const double queued_ms = SDL_GetAudioStreamQueued(g_stream) / 4.0 * 1000.0 / rate;
    constexpr double kTargetMs = 70.0, kMaxMs = 300.0;
    if (queued_ms > kMaxMs) SDL_ClearAudioStream(g_stream);
    const double ratio = std::clamp(1.0 + (queued_ms - kTargetMs) / kTargetMs * 0.01, 0.99, 1.01);
    SDL_SetAudioStreamFrequencyRatio(g_stream, static_cast<float>(ratio));
    SDL_PutAudioStreamData(g_stream, lr, static_cast<int>(frames * 4));
    static const bool debug = std::getenv("PIKMIN_AUDIO_DEBUG") != nullptr;
    static uint64_t pushes = 0;
    if (debug && ++pushes % 100 == 0) log("audio", "queued %.0f ms, speed ratio %.4f", queued_ms, ratio);
}
#endif
}  // namespace

static bool g_want_device = false;

void audio_out_init(bool device) {
    g_want_device = device;
    if (const char* path = std::getenv("PIKMIN_AUDIO_DUMP"); path && *path) {
        g_wav = std::fopen(path, "wb");
        if (g_wav) log("audio", "dumping output to %s", path);
        else log("audio", "cannot write %s", path);
    }
}

void audio_out_push(const int16_t* lr, size_t frames, uint32_t rate) {
#ifdef PIKMIN_GRAPHICS
    if (g_want_device) {
        g_want_device = false;  // open lazily, at the first block's rate
        open_device(rate);
    }
    device_push(lr, frames, rate);
#endif
    wav_push(lr, frames, rate);
}

}  // namespace rt
