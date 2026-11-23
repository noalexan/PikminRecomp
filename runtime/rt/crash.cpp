#include "rt/crash.hpp"

#include <execinfo.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "rt/diag.hpp"
#include "rt/guest.hpp"
#include "rt/irq.hpp"
#include "rt/sched.hpp"
#include "rt/trace.hpp"

extern "C" void ppc_bad_access(uint32_t ea);

namespace rt {
uint32_t vi_retrace_count();

namespace {
std::atomic<bool> g_reported{false};
alignas(16) char g_altstack[64 * 1024];

enum class Test { None, Segv, BadAccess, Abort, DoubleFault, Hang };
Test g_test = Test::None;
uint32_t g_test_at = 30;

void put(const char* s) { diag_write(s); }
void put_hex(const char* label, uint32_t v) {
    char b[11];
    put(label);
    put(fmt_hex32(b, v));
}
void put_ptr(const char* label, const void* p) {
    const uint64_t v = reinterpret_cast<uintptr_t>(p);
    char b[11];
    put(label);
    put(fmt_hex32(b, static_cast<uint32_t>(v >> 32)) + 2);  // label carries the "0x"
    put(fmt_hex32(b, static_cast<uint32_t>(v)) + 2);
}
void put_dec(const char* label, int64_t v) {
    char b[21];
    put(label);
    put(fmt_dec(b, v));
}

const char* signal_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGILL: return "SIGILL";
        case SIGFPE: return "SIGFPE";
        case SIGABRT: return "SIGABRT";
        default: return "signal";
    }
}

void on_signal(int sig, siginfo_t* si, void* uc_v) {
    CrashInfo info{signal_name(sig)};
    info.is_signal = sig != SIGABRT;
    if (info.is_signal) info.host_fault = si ? si->si_addr : nullptr;
#if defined(__x86_64__)
    if (auto* uc = static_cast<ucontext_t*>(uc_v)) info.pc = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RIP]);
#else
    (void)uc_v;
#endif
    crash_report(info);
    raise(sig);  // SA_RESETHAND restored the default action: terminate (and dump core)
}
}  // namespace

void crash_install() {
    void* prime[2];
    backtrace(prime, 2);  // loads libgcc now: the unwinder must not be dlopen'ed inside a handler
    stack_t ss{};
    ss.ss_sp = g_altstack;
    ss.ss_size = sizeof g_altstack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &sa, nullptr);
}

bool crash_report(const CrashInfo& info) {
    if (g_reported.exchange(true)) return false;
    put("\n=== pikmin crash: ");
    put(info.reason);
    put(" ===\nbuild ");
    put(kBuildHash);
    put(" (");
    put(kBuildType);
    put(")\n");
    if (info.is_signal) {
        put_ptr("fault address 0x", info.host_fault);
        put("\n");
    }
    if (info.has_guest_addr) {
        put_hex("guest address ", info.guest_addr);
        put("\n");
    }
    if (g_test == Test::DoubleFault) {
        volatile int* null = nullptr;
        *null = 1;  // --crash-test double_fault: the report path itself faults
    }
    put_dec("retrace ", vi_retrace_count());
    put("\nlast HLE ");
    put(current_function.name ? current_function.name : "");
    put_hex(" lr ", current_function.lr);
    put("\n");
    const GuestThread* t = sched().current();
    const bool in_irq = irq().in_callback();
    if (t) {
        put_hex("guest thread ", t->ea);
        put_dec(" prio ", t->prio);
        put("\n");
    } else {
        put("guest thread none\n");
    }
    put("in IRQ callback: ");
    put(in_irq ? "yes\n" : "no\n");
    const ppc_ctx* c = in_irq ? irq().ctx() : (t ? t->ctx : nullptr);
    if (c) {
        put_hex("lr ", c->lr);
        put_hex(" r1 ", c->r[1]);
        put_hex(" r3 ", c->r[3]);
        put_hex(" r4 ", c->r[4]);
        put_hex(" r5 ", c->r[5]);
        put("\n");
    }
    put("backtrace:\n");
    void* frames[64];
    int n = backtrace(frames, 64);
    if (info.pc && n > 2) frames[2] = info.pc;  // [0] here, [1] the handler, [2] the signal frame
    backtrace_symbols_fd(frames, n, 2);
    if (log_fd() >= 0) backtrace_symbols_fd(frames, n, log_fd());
    put("recent events (last 256, oldest first; retrace channel name a b text):\n");
    trace_dump(256);
    put("log file ");
    put(log_path().empty() ? "(none)" : log_path().c_str());
    put("\n=== end ===\n");
    return true;
}

bool crash_test_arm(const char* spec) {
    std::string_view s(spec);
    const size_t colon = s.find(':');
    std::string_view kind = s.substr(0, colon);
    if (colon != std::string_view::npos) g_test_at = static_cast<uint32_t>(std::atoi(spec + colon + 1));
    if (kind == "segv") g_test = Test::Segv;
    else if (kind == "bad_access") g_test = Test::BadAccess;
    else if (kind == "abort") g_test = Test::Abort;
    else if (kind == "double_fault") g_test = Test::DoubleFault;
    else if (kind == "hang") g_test = Test::Hang;
    else return false;
    return true;
}

void crash_test_check(uint32_t retrace) {
    if (g_test == Test::None || retrace != g_test_at) return;
    switch (g_test) {
        case Test::Segv:
        case Test::DoubleFault: {
            volatile int* null = nullptr;
            *null = 1;
            break;
        }
        case Test::BadAccess: ppc_bad_access(0x7ffffff0u); break;
        case Test::Abort: std::abort();
        case Test::Hang:
            for (volatile bool spin = true; spin;) {
            }
            break;
        case Test::None: break;
    }
}

}  // namespace rt
