// Headless CARD: no memory card inserted.
#include "rt/guest.hpp"

namespace {
constexpr uint32_t kNoCard = static_cast<uint32_t>(-3);  // CARD_RESULT_NOCARD
}

#define CARD_NOCARD(X)                                                                                         \
    X(CARDCheck) X(CARDCheckAsync) X(CARDClose) X(CARDCreate) X(CARDFastDelete) X(CARDFastOpen) X(CARDFormat) \
    X(CARDFormatAsync) X(CARDFreeBlocks) X(CARDGetResultCode) X(CARDGetSectorSize) X(CARDGetStatus)          \
    X(CARDMount) X(CARDMountAsync) X(CARDOpen) X(CARDRead) X(CARDRename) X(CARDSetStatus) X(CARDUnmount)     \
    X(CARDWrite)

extern "C" {

#define DEFINE_NOCARD(name)                \
    void hle_##name(ppc_ctx* c) {          \
        HLE_ENTRY(c, #name);               \
        rt::ret(c, kNoCard);               \
    }
CARD_NOCARD(DEFINE_NOCARD)
#undef DEFINE_NOCARD

void hle_CARDInit(ppc_ctx* c) { HLE_ENTRY(c, "CARDInit"); }

void hle_CARDProbe(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDProbe");
    rt::ret(c, 0);
}

void hle_CARDGetXferredBytes(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDGetXferredBytes");
    rt::ret(c, 0);
}

}  // extern "C"
