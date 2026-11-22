// Host audio sink for the AI DMA stream: an SDL3 audio stream (windowed runs) and/or a WAV dump
// ($PIKMIN_AUDIO_DUMP=path, works in every mode). Without either, samples are dropped.
#pragma once
#include <cstddef>
#include <cstdint>

namespace rt {

// device: open the default playback device (needs SDL; skipped for headless/fast runs).
void audio_out_init(bool device);
// Interleaved host-order L,R frames at `rate` Hz.
void audio_out_push(const int16_t* lr, size_t frames, uint32_t rate);

}  // namespace rt
