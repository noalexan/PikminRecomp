#include "rt/trace.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <string_view>

#include "rt/diag.hpp"
#include "rt/log.hpp"

namespace rt {

TraceEntry g_trace[kTraceSize];
uint64_t g_trace_seq = 0;
uint32_t g_trace_live = 0;
uint32_t g_trace_retrace = 0;

namespace {
constexpr const char* kNames[kTraceChannels] = {"hle", "dvd", "thread", "irq", "frame", "scene"};

const char* channel(Ev k) {
    unsigned i = static_cast<unsigned>(k);
    return i < kTraceChannels ? kNames[i] : "?";
}

// Appends to a fixed line buffer without libc formatting (async-signal-safe).
struct Line {
    char buf[160];
    size_t n = 0;
    void put(const char* s) {
        while (*s && n < sizeof buf - 1) buf[n++] = *s++;
    }
};

void out(int fd, const char* s, size_t n) {
    if (fd < 0) {
        diag_write(s, n);
        return;
    }
    while (n > 0) {
        ssize_t w = ::write(fd, s, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        s += w;
        n -= static_cast<size_t>(w);
    }
}
}  // namespace

void trace_live(const TraceEntry& e) {
    log("trace", "%s %s a=0x%08x b=0x%08x %s", channel(e.kind), e.name ? e.name : "", e.a, e.b, e.text);
}

uint32_t trace_parse_channels(const char* spec) {
    if (!spec) return 0;
    uint32_t bits = 0;
    std::string_view s(spec);
    while (!s.empty()) {
        size_t comma = s.find(',');
        std::string_view item = s.substr(0, comma);
        if (item == "all") bits |= (1u << kTraceChannels) - 1;
        for (int i = 0; i < kTraceChannels; ++i)
            if (item == kNames[i]) bits |= 1u << i;
        if (comma == std::string_view::npos) break;
        s.remove_prefix(comma + 1);
    }
    return bits;
}

void trace_init() { g_trace_live = trace_parse_channels(std::getenv("PIKMIN_TRACE")); }

void trace_dump_to(int fd, size_t max) {
    const uint64_t end = g_trace_seq;
    size_t count = end < kTraceSize ? static_cast<size_t>(end) : kTraceSize;
    if (max < count) count = max;
    for (uint64_t s = end - count; s < end; ++s) {
        const TraceEntry& e = g_trace[s & (kTraceSize - 1)];
        Line l;
        char num[21], hex[11];
        l.put("  ");
        l.put(fmt_dec(num, static_cast<int64_t>(e.retrace)));
        l.put(" ");
        l.put(channel(e.kind));
        l.put(" ");
        l.put(e.name ? e.name : "");
        l.put(" a=");
        l.put(fmt_hex32(hex, e.a));
        l.put(" b=");
        l.put(fmt_hex32(hex, e.b));
        if (e.text[0]) {
            l.put(" ");
            l.put(e.text);
        }
        l.buf[l.n++] = '\n';
        out(fd, l.buf, l.n);
    }
}

void trace_reset() {
    g_trace_seq = 0;
    g_trace_live = 0;
}

}  // namespace rt
