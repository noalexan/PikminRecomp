// Audio is stubbed for the headless boot: AI/DSP are inert and JAudio's entry points are
// overridden (recomp/overrides.toml [functions]) to these no-ops.
#include "rt/guest.hpp"

#define AUDIO_NOOPS(X)                                                                                        \
    X(AIGetStreamSampleCount) X(AIGetStreamTrigger) X(AIInit) X(AIInitDMA) X(AIRegisterDMACallback)           \
    X(AIResetStreamSampleCount) X(AISetDSPSampleRate) X(AISetStreamPlayState) X(AISetStreamSampleRate)        \
    X(AISetStreamVolLeft) X(AISetStreamVolRight) X(AIStartDMA) X(DSPAssertInt) X(DSPCheckMailFromDSP)         \
    X(DSPCheckMailToDSP) X(DSPInit) X(DSPReadMailFromDSP) X(DSPSendMailToDSP) X(__DSP_boot_task)               \
    X(__DSP_exec_task) X(__DSP_insert_task) X(__DSP_remove_task) X(Jac_Init) X(StartAudioThread)

extern "C" {

#define DEFINE_AUDIO_NOOP(name)        \
    void hle_##name(ppc_ctx* c) {      \
        HLE_ENTRY(c, #name);           \
        rt::ret(c, 0);                 \
    }
AUDIO_NOOPS(DEFINE_AUDIO_NOOP)
#undef DEFINE_AUDIO_NOOP

}  // extern "C"
