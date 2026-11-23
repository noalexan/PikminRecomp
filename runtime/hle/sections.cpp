// Scene changes for diagnostics: each game section's init is hooked to leave a "scene" breadcrumb
// (always logged, and recorded in the recent-events ring) before running the original.
#include "rt/guest.hpp"
#include "rt/log.hpp"

#define PIKMIN_SECTION_HOOK(mangled, addr, label)                  \
    extern "C" __attribute__((weak)) void f_##addr##_orig(ppc_ctx*); \
    extern "C" void hle_##mangled(ppc_ctx* c) {                    \
        HLE_ENTRY(c, label);                                       \
        ::rt::trace(::rt::Ev::Scene, label);                       \
        ::rt::log("scene", "%s", label);                           \
        if (f_##addr##_orig) f_##addr##_orig(c);                   \
    }

PIKMIN_SECTION_HOOK(init__13TitlesSectionFv, 8005f7b8, "TitlesSection::init")
PIKMIN_SECTION_HOOK(init__14NinLogoSectionFv, 80061134, "NinLogoSection::init")
PIKMIN_SECTION_HOOK(init__15GameExitSectionFv, 8005b66c, "GameExitSection::init")
PIKMIN_SECTION_HOOK(init__16GameSetupSectionFv, 80054c7c, "GameSetupSection::init")
PIKMIN_SECTION_HOOK(init__16IntroGameSectionFv, 8005b57c, "IntroGameSection::init")
PIKMIN_SECTION_HOOK(init__16MapSelectSectionFv, 800567b4, "MapSelectSection::init")
PIKMIN_SECTION_HOOK(init__16MovSampleSectionFv, 800781cc, "MovSampleSection::init")
PIKMIN_SECTION_HOOK(init__16OnePlayerSectionFv, 80053104, "OnePlayerSection::init")
PIKMIN_SECTION_HOOK(init__17CardSelectSectionFv, 80055420, "CardSelectSection::init")
PIKMIN_SECTION_HOOK(init__18GameCreditsSectionFv, 8019a064, "GameCreditsSection::init")
PIKMIN_SECTION_HOOK(init__18NewPikiGameSectionFv, 8005a9c8, "NewPikiGameSection::init")
PIKMIN_SECTION_HOOK(init__21GameStageClearSectionFv, 80199fcc, "GameStageClearSection::init")
PIKMIN_SECTION_HOOK(init__22GameCourseClearSectionFv, 80199f18, "GameCourseClearSection::init")
PIKMIN_SECTION_HOOK(initStage__15GameCoreSectionFv, 8010ebd0, "GameCoreSection::initStage")
