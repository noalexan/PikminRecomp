// dsp.a: the audio DSP as an HLE device. The DSP runs JAudio's microcode, the Zelda-family ucode
// that Dolphin HLEs (hle/dsp/zelda.cpp, ported from Dolphin). CPU->DSP mails are handled
// synchronously (the DSP "consumes" them instantly); DSP->CPU mails queue up and raise the DSP
// interrupt, which runs JAudio's own __DSPHandler on the interrupt context.
#include <deque>
#include <memory>
#include <utility>

#include "hle/dsp/zelda.h"
#include "rt/guest.hpp"
#include "rt/irq.hpp"
#include "rt/log.hpp"

using rt::arg;
using rt::ret;

namespace rt {
uint8_t* aram_data();
uint32_t aram_size();
}  // namespace rt

namespace {
// JAudio's DSP interrupt handler (jaudio.o osdsp_task.c, build.map). The SDK's DSPInit would
// install it with __OSSetInterruptHandler; the HLE calls it directly.
constexpr uint32_t kDSPHandler = 0x80008920u;
constexpr uint32_t kInterruptDSP = 7;  // __OS_INTERRUPT_DSP_DSP
// Pikmin PAL's ucode CRC: selects the Zelda HLE flags (SYNC_PER_FRAME | NO_CMD_0D).
constexpr uint32_t kPikminPalUcodeCrc = 0x267FD05Au;

ppc_ctx* g_ctx = nullptr;  // guest memory for the Core::System shim

class Mailbox final : public DSP::HLE::MailSink {
public:
    void PushMail(u32 mail, bool interrupt) override {
        queue_.emplace_back(mail, interrupt);
        if (queue_.size() == 1 && interrupt) raise();
    }
    bool has_mail() const { return !queue_.empty(); }
    u32 peek() const { return queue_.empty() ? last_ : queue_.front().first; }
    u32 read() {
        if (queue_.empty()) return last_ & 0x7FFFFFFFu;
        last_ = queue_.front().first;
        queue_.pop_front();
        if (!queue_.empty() && queue_.front().second) raise();
        return last_;
    }
    void clear() { queue_.clear(); }

private:
    static void raise() {
        rt::irq().post([](ppc_ctx* ic) { rt::call_guest(ic, kDSPHandler, kInterruptDSP, 0); });
    }
    std::deque<std::pair<u32, bool>> queue_;
    u32 last_ = 0;
};

Core::System g_system;
Mailbox g_mailbox;
std::unique_ptr<DSP::HLE::ZeldaUCode> g_ucode;
}  // namespace

// ---- dolphin_shim.h services ----
void zelda_log(const char* level, const char* fmt) {
    char key[160];
    std::snprintf(key, sizeof key, "zelda:%s:%s", level, fmt);
    rt::log_once(key, "dsp hle %s: %s", level, fmt);
}

namespace Core {
u8* Memory::GetPointerForRange(u32 addr, size_t size) const {
    return rt::host(g_ctx, addr, static_cast<uint32_t>(size));
}
void Memory::CopyFromEmuSwapped(u16* dst, u32 addr, size_t size) const {
    const u8* src = rt::host(g_ctx, addr, static_cast<uint32_t>(size));
    for (size_t i = 0; i < size / 2; ++i) dst[i] = static_cast<u16>(src[2 * i] << 8 | src[2 * i + 1]);
}
void Memory::CopyToEmuSwapped(u32 addr, const u16* src, size_t size) const {
    u8* dst = rt::host(g_ctx, addr, static_cast<uint32_t>(size));
    for (size_t i = 0; i < size / 2; ++i) {
        dst[2 * i] = static_cast<u8>(src[i] >> 8);
        dst[2 * i + 1] = static_cast<u8>(src[i]);
    }
}
u32 DSPManager::GetARAMSize() const { return rt::aram_size(); }
u8* DSPManager::GetARAMPtr() const { return rt::aram_data(); }
}  // namespace Core

extern "C" {

void hle_DSPInit(ppc_ctx* c) {
    HLE_ENTRY(c, "DSPInit");
    g_ctx = c;
}

// The SDK boots a task by mailing its IRAM/DRAM location to the DSP ROM. The only task Pikmin
// boots is JAudio's (DspBoot): start the Zelda HLE, which announces itself with DSP_INIT.
void hle___DSP_boot_task(ppc_ctx* c) {
    HLE_ENTRY(c, "__DSP_boot_task");
    g_ctx = c;
    g_mailbox.clear();
    g_ucode = std::make_unique<DSP::HLE::ZeldaUCode>(g_system, g_mailbox, kPikminPalUcodeCrc);
    rt::log("dsp", "booted audio ucode (Zelda HLE), task 0x%08x", arg(c, 0));
    g_ucode->Initialize();
}

void hle_DSPCheckMailToDSP(ppc_ctx* c) {
    HLE_ENTRY(c, "DSPCheckMailToDSP");
    ret(c, 0);  // mails to the DSP are consumed immediately
}

void hle_DSPCheckMailFromDSP(ppc_ctx* c) {
    HLE_ENTRY(c, "DSPCheckMailFromDSP");
    ret(c, g_mailbox.has_mail() ? 1 : 0);
}

void hle_DSPReadMailFromDSP(ppc_ctx* c) {
    HLE_ENTRY(c, "DSPReadMailFromDSP");
    ret(c, g_mailbox.read());
}

void hle_DSPSendMailToDSP(ppc_ctx* c) {
    HLE_ENTRY(c, "DSPSendMailToDSP");
    g_ctx = c;
    uint32_t mail = arg(c, 0);
    if (!g_ucode) {
        rt::log_once("dsp:nomcode", "mail 0x%08x to the DSP before any ucode was booted", mail);
        return;
    }
    g_ucode->HandleMail(mail);
    if (g_ucode->Halted()) rt::log_once("dsp:halted", "Zelda HLE halted (after mail 0x%08x)", mail);
}

void hle_DSPAssertInt(ppc_ctx* c) { HLE_ENTRY(c, "DSPAssertInt"); }

// Other DSP tasks (ucode switching) are never used by Pikmin.
void hle___DSP_exec_task(ppc_ctx* c) {
    HLE_ENTRY(c, "__DSP_exec_task");
    rt::log_once("dsp:exec", "__DSP_exec_task(0x%08x, 0x%08x) ignored", arg(c, 0), arg(c, 1));
}
void hle___DSP_insert_task(ppc_ctx* c) {
    HLE_ENTRY(c, "__DSP_insert_task");
    rt::log_once("dsp:insert", "__DSP_insert_task(0x%08x) ignored", arg(c, 0));
}
void hle___DSP_remove_task(ppc_ctx* c) { HLE_ENTRY(c, "__DSP_remove_task"); }

}  // extern "C"
