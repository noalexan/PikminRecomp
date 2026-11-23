// Crash reports: fatal signals, ppc_* fatal hooks and the watchdog all write one report (to stderr
// and the run log) with the guest context, a host backtrace and the recent-events ring.
#pragma once
#include <cstdint>

namespace rt {

// Alternate signal stack for the calling (game) thread and handlers for SIGSEGV, SIGBUS, SIGILL,
// SIGFPE, SIGABRT. Each handler writes the report, then re-raises with the default action (core).
void crash_install();

struct CrashInfo {
    const char* reason;                  // "SIGSEGV", "ppc_bad_access", "watchdog: ..."
    bool is_signal = false;
    const void* host_fault = nullptr;    // signals: si_addr
    bool has_guest_addr = false;
    uint32_t guest_addr = 0;             // ppc_bad_access / ppc_unimpl address
    void* pc = nullptr;                  // signals: faulting host PC (first backtrace frame)
};

// Async-signal-safe. Writes the report once per process; later calls return false at once.
bool crash_report(const CrashInfo& info);

// --crash-test KIND[:RETRACE] (tests only). KIND: segv, bad_access, abort, double_fault, hang.
bool crash_test_arm(const char* spec);
void crash_test_check(uint32_t retrace);  // called by VI at every retrace

}  // namespace rt
