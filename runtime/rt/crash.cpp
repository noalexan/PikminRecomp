#include "rt/crash.hpp"

#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#else
#include <execinfo.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#endif

#include <SDL3/SDL_messagebox.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "rt/diag.hpp"
#include "rt/guest.hpp"
#include "rt/input_record.hpp"
#include "rt/irq.hpp"
#include "rt/platform.hpp"
#include "rt/sched.hpp"
#include "rt/trace.hpp"

extern "C" void ppc_bad_access(uint32_t ea);

namespace rt {
uint32_t vi_retrace_count();

namespace {
std::atomic<bool> g_reported{false};
#ifndef _WIN32
alignas(16) char g_altstack[64 * 1024];
#endif

// Crash dialog: paths copied at startup, nothing allocated in the handler.
bool g_dialog = false;
char g_exe[4096];
char g_log[4096];

void launch_dialog(const char* reason) {
    if (!g_dialog) return;
#ifdef _WIN32
    static wchar_t cmd[3 * 4096];
    size_t n = 0;
    auto add = [&](const char* s, bool quote) {
        if (quote && n < 3 * 4096 - 1) cmd[n++] = L'"';
        // The exe and log paths were stored as UTF-8: widen them properly.
        const int w = MultiByteToWideChar(CP_UTF8, 0, s, -1, cmd + n, static_cast<int>(3 * 4096 - n - 3));
        if (w > 0) n += static_cast<size_t>(w - 1);
        if (quote && n < 3 * 4096 - 2) cmd[n++] = L'"';
        if (n < 3 * 4096 - 1) cmd[n++] = L' ';
        cmd[n] = 0;
    };
    add(g_exe, true);
    add("--crash-dialog", false);
    add(reason, true);
    add(g_log, true);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
#else
    const pid_t pid = fork();
    if (pid == 0) {
        setsid();  // outlives the crashing process
        char flag[] = "--crash-dialog";
        char* argv[] = {g_exe, flag, const_cast<char*>(reason), g_log, nullptr};
        execv(g_exe, argv);
        _exit(127);
    }
#endif
}

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

#ifdef _WIN32
const char* exception_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
        default: return "EXCEPTION";
    }
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* ep) {
    const EXCEPTION_RECORD* r = ep ? ep->ExceptionRecord : nullptr;
    CrashInfo info{r ? exception_name(r->ExceptionCode) : "EXCEPTION"};
    info.is_signal = true;
    if (r && r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        info.host_fault = reinterpret_cast<const void*>(r->ExceptionInformation[1]);
    if (r) info.pc = r->ExceptionAddress;
    crash_report(info);
    return EXCEPTION_CONTINUE_SEARCH;  // Windows Error Reporting / debugger take it from here
}

void on_abort(int) {
    crash_report(CrashInfo{"SIGABRT"});
    _exit(3);
}
}  // namespace

void crash_install() {
    SetUnhandledExceptionFilter(on_exception);
    signal(SIGABRT, on_abort);
}

#else
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

#endif

bool crash_report(const CrashInfo& info) {
    if (g_reported.exchange(true)) return false;
    put("\n=== pikmin crash: ");
    put(info.reason);
    put(" ===\nbuild ");
    put(kVersion);
    put(" ");
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
#ifdef _WIN32
    // Raw addresses with the module base: tools/resolve_bt.py maps them like Linux frames.
    void* frames[64];
    const int n = CaptureStackBackTrace(0, 64, frames, nullptr);
    put_ptr("module base 0x", GetModuleHandleW(nullptr));
    put("\n");
    if (info.pc) {
        put_ptr("  fault pc 0x", info.pc);
        put("\n");
    }
    for (int i = 0; i < n; ++i) {
        put_ptr("  0x", frames[i]);
        put("\n");
    }
#else
    void* frames[64];
    int n = backtrace(frames, 64);
    if (info.pc && n > 2) frames[2] = info.pc;  // [0] here, [1] the handler, [2] the signal frame
    backtrace_symbols_fd(frames, n, 2);
    if (log_fd() >= 0) backtrace_symbols_fd(frames, n, log_fd());
#endif
    put("recent events (last 256, oldest first; retrace channel name a b text):\n");
    trace_dump(256);
    if (!input_record_path().empty()) {
        put("input recording: ");
        put(input_record_path().c_str());
        put("\n");
    }
    put("log file ");
    put(log_path().empty() ? "(none)" : log_path().c_str());
    put("\n=== end ===\n");
    launch_dialog(info.reason);
    return true;
}

void crash_enable_dialog() {
    if (const char* off = std::getenv("PIKMIN_NO_CRASH_DIALOG"); off && *off && *off != '0') return;
    const std::string exe = plat::exe_path();
    const std::string& log = log_path();
    if (exe.empty() || exe.size() >= sizeof g_exe || log.size() >= sizeof g_log) return;
    std::memcpy(g_exe, exe.c_str(), exe.size() + 1);
    std::memcpy(g_log, log.c_str(), log.size() + 1);
    g_dialog = true;
}

int crash_dialog_main(const char* reason, const char* log) {
    std::string msg = "Pikmin Recomp crashed (";
    msg += reason;
    msg += ").\n\nA crash report was saved in:\n";
    msg += (log && *log) ? log : "(no log file)";
    msg += "\n\nPlease attach this file when you report the problem.";
    return SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pikmin Recomp crashed", msg.c_str(), nullptr) ? 0 : 1;
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
