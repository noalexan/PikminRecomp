// AI disc-audio streaming (DTK): JAudio's StreamMain still references it, but Pikmin has no
// streamed tracks (stream0x.adp are not on the disc), so these are inert. The AI DMA path is in
// ai.cpp and the DSP in dsp.cpp.
#include "rt/guest.hpp"

#define AUDIO_NOOPS(X)                                                                                 \
    X(AIGetStreamSampleCount) X(AIGetStreamTrigger) X(AIResetStreamSampleCount) X(AISetStreamPlayState) \
    X(AISetStreamSampleRate) X(AISetStreamVolLeft) X(AISetStreamVolRight) X(AIGetStreamPlayState)        \
    X(AIGetStreamSampleRate) X(AIGetStreamVolLeft) X(AIGetStreamVolRight)

extern "C" {

#define DEFINE_AUDIO_NOOP(name)   \
    void hle_##name(ppc_ctx* c) { \
        HLE_ENTRY(c, #name);      \
        rt::ret(c, 0);            \
    }
AUDIO_NOOPS(DEFINE_AUDIO_NOOP)
#undef DEFINE_AUDIO_NOOP

}  // extern "C"
