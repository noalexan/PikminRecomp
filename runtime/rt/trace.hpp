// Recent-events ring: cheap breadcrumbs (HLE calls, DVD, thread switches, IRQs, frames, scene
// changes) kept in memory and dumped into crash reports. PIKMIN_TRACE=hle,dvd,... (or "all") also
// prints the chosen channels live through rt::log.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rt {

enum class Ev : uint8_t { Hle, Dvd, Thread, Irq, Frame, Scene };
inline constexpr int kTraceChannels = 6;
inline constexpr size_t kTraceSize = 4096;  // power of two
inline constexpr size_t kTraceText = 40;

struct TraceEntry {
    uint64_t seq;
    uint32_t retrace;
    uint32_t a, b;
    Ev kind;
    const char* name;  // static string
    char text[kTraceText];
};

extern TraceEntry g_trace[kTraceSize];
extern uint64_t g_trace_seq;
extern uint32_t g_trace_live;     // channel bits printed live
extern uint32_t g_trace_retrace;  // retrace count, set by VI

void trace_live(const TraceEntry& e);

inline void trace(Ev kind, const char* name, uint32_t a = 0, uint32_t b = 0, const char* text = nullptr) {
    TraceEntry& e = g_trace[g_trace_seq & (kTraceSize - 1)];
    e.seq = g_trace_seq++;
    e.retrace = g_trace_retrace;
    e.a = a;
    e.b = b;
    e.kind = kind;
    e.name = name;
    if (text) {
        size_t n = std::strlen(text);
        if (n > kTraceText - 1) n = kTraceText - 1;
        std::memcpy(e.text, text, n);
        e.text[n] = '\0';
    } else {
        e.text[0] = '\0';
    }
    if (g_trace_live & (1u << static_cast<unsigned>(kind))) [[unlikely]] trace_live(e);
}

// Comma-separated channel names ("hle,dvd,thread,irq,frame,scene" or "all") -> bitmask.
uint32_t trace_parse_channels(const char* spec);
// Reads PIKMIN_TRACE.
void trace_init();
// Async-signal-safe: writes the last `max` entries, oldest first. fd < 0 means stderr + log
// (rt::diag_write).
void trace_dump_to(int fd, size_t max);
inline void trace_dump(size_t max) { trace_dump_to(-1, max); }
void trace_reset();  // tests

}  // namespace rt
