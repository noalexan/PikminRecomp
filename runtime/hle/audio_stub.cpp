// Audio is stubbed for the headless boot: AI/DSP are inert, JAudio's start-up is skipped, and every
// JAudio function the game calls directly is overridden (recomp/overrides.toml [functions]) to a
// silent no-op returning 0 ("not playing", "no event").
#include "rt/guest.hpp"

#define AUDIO_NOOPS(X)                                                                                        \
    X(AIGetStreamSampleCount) X(AIGetStreamTrigger) X(AIInit) X(AIInitDMA) X(AIRegisterDMACallback)           \
    X(AIResetStreamSampleCount) X(AISetDSPSampleRate) X(AISetStreamPlayState) X(AISetStreamSampleRate)        \
    X(AISetStreamVolLeft) X(AISetStreamVolRight) X(AIStartDMA) X(DSPAssertInt) X(DSPCheckMailFromDSP)         \
    X(DSPCheckMailToDSP) X(DSPInit) X(DSPReadMailFromDSP) X(DSPSendMailToDSP) X(__DSP_boot_task)               \
    X(__DSP_exec_task) X(__DSP_insert_task) X(__DSP_remove_task) X(Jac_Init) X(StartAudioThread) X(Jac_Start) \
    X(Jac_AddDVDBuffer) X(Jac_BackDVDBuffer) X(Jac_CheckFreeEvents) X(Jac_CreateEvent) X(Jac_DemoFrame) X(Jac_DestroyEvent) X(Jac_EnterBossMode) X(Jac_ExitBossMode) X(Jac_FinishDemo) X(Jac_FinishPartsFindDemo) X(Jac_FinishTextDemo) X(Jac_Freeze) X(Jac_Freeze_Precall) X(Jac_GetActiveEvents) X(Jac_Gsync) X(Jac_InitAllEvent) X(Jac_Orima_Formation) X(Jac_Orima_Walk) X(Jac_OutputMode) X(Jac_Piki_Number) X(Jac_PlayEventAction) X(Jac_PlayOrimaSe) X(Jac_PlaySystemSe) X(Jac_SceneExit) X(Jac_SceneSetup) X(Jac_SetBGMVolume) X(Jac_SetDemoOnyons) X(Jac_SetDemoPartsCount) X(Jac_SetDemoPartsID) X(Jac_SetSEVolume) X(Jac_StartDemo) X(Jac_StartPartsFindDemo) X(Jac_StartTextDemo) X(Jac_StopEventAction) X(Jac_StopOrimaSe) X(Jac_StopSe) X(Jac_StopSystemSe) X(Jac_StreamMovieGetPicture) X(Jac_StreamMovieInit) X(Jac_StreamMovieStop) X(Jac_StreamMovieUpdate) X(Jac_UpdateCamera) X(Jac_UpdateEventPosition)

extern "C" {

#define DEFINE_AUDIO_NOOP(name)        \
    void hle_##name(ppc_ctx* c) {      \
        HLE_ENTRY(c, #name);           \
        rt::ret(c, 0);                 \
    }
AUDIO_NOOPS(DEFINE_AUDIO_NOOP)
#undef DEFINE_AUDIO_NOOP

}  // extern "C"
