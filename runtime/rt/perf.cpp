#include "rt/perf.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "rt/log.hpp"
#include "rt/trace.hpp"

namespace rt {
extern uint32_t g_trace_retrace;

namespace {
std::deque<FrameRecord> g_hist;
double g_last = -1, g_target_ms = 0;
uint64_t g_seq = 0;                 // trace sequence at the previous frame
std::optional<Drop> g_run, g_last_drop;  // drop run in progress, last finished drop
int g_drops = 0;

// The most notable event since the previous frame: the largest DVD read (named after the last file
// opened), else the last scene change, else a thread switch.
std::string drop_cause(uint64_t from, uint64_t to) {
    if (to - from > kTraceSize) from = to - kTraceSize;
    const TraceEntry* read = nullptr;
    const TraceEntry* scene = nullptr;
    bool switched = false;
    for (uint64_t s = from; s < to; ++s) {
        const TraceEntry& e = g_trace[s & (kTraceSize - 1)];
        if (e.seq != s) continue;
        if (e.kind == Ev::Dvd && e.name && e.name[0] == 'r' && (!read || e.b > read->b)) read = &e;
        if (e.kind == Ev::Scene) scene = &e;
        if (e.kind == Ev::Thread) switched = true;
    }
    char buf[128];
    if (read) {
        const char* path = nullptr;  // the newest "open" at or before the read, anywhere in the ring
        const uint64_t lo = read->seq >= kTraceSize ? read->seq - kTraceSize + 1 : 0;
        for (uint64_t s = read->seq + 1; s-- > lo;) {
            const TraceEntry& e = g_trace[s & (kTraceSize - 1)];
            if (e.seq == s && e.kind == Ev::Dvd && e.name && e.name[0] == 'o' && e.text[0]) {
                path = e.text;
                break;
            }
        }
        const double mb = read->b / 1e6;
        if (path) std::snprintf(buf, sizeof buf, "dvd read %s (%.1f MB)", path, mb);
        else std::snprintf(buf, sizeof buf, "dvd read at 0x%08x (%.1f MB)", read->a, mb);
        return buf;
    }
    if (scene && scene->name) return std::string("scene ") + scene->name;
    return switched ? "thread switch" : "";
}

void finish_run() {
    if (!g_run) return;
    g_last_drop = g_run;
    ++g_drops;
    log("perf", "drop %.1f ms at retrace %u%s%s", g_run->ms, g_run->retrace, g_run->cause.empty() ? "" : ": ",
        g_run->cause.c_str());
    g_run.reset();
}
}  // namespace

void perf_frame(double now_s, double present_s, double pace_s, double target_s) {
    const uint64_t seq = g_trace_seq;
    g_target_ms = target_s * 1000.0;
    if (g_last < 0) {
        g_last = now_s;
        g_seq = seq;
        return;
    }
    const double total = (now_s - g_last) * 1000.0;
    g_last = now_s;
    // A drop is a frame over twice the larger of the target and the recent average (a machine that
    // runs steadily below the target isn't dropping frames).
    const double recent = perf_ms();
    const bool drop = g_hist.size() >= 10 && total > 2.0 * std::max(g_target_ms, recent);  // not while starting
    const double present = present_s * 1000.0, pace = pace_s * 1000.0;
    g_hist.push_back({now_s, total, std::max(0.0, total - present - pace), present, pace, g_trace_retrace, drop});
    while (!g_hist.empty() && now_s - g_hist.front().t > 4.0) g_hist.pop_front();
    if (drop) {
        if (!g_run || total > g_run->ms) {
            std::string cause = drop_cause(g_seq, seq);
            if (g_run && cause.empty()) cause = g_run->cause;
            g_run = Drop{total, g_trace_retrace, cause};
        }
    } else {
        finish_run();
    }
    g_seq = seq;
}

const std::deque<FrameRecord>& perf_history() { return g_hist; }

double perf_ms() {
    if (g_hist.empty()) return 0;
    const double from = g_hist.back().t - 0.5;
    double sum = 0;
    int n = 0;
    for (auto it = g_hist.rbegin(); it != g_hist.rend() && it->t > from; ++it) sum += it->total_ms, ++n;
    return n ? sum / n : 0;
}

double perf_fps() {
    const double ms = perf_ms();
    return ms > 0 ? 1000.0 / ms : 0;
}

double perf_one_percent_low() {
    if (g_hist.empty()) return 0;
    std::vector<double> v;
    v.reserve(g_hist.size());
    for (const auto& f : g_hist) v.push_back(f.total_ms);
    const size_t k = std::max<size_t>(1, v.size() / 100);
    std::partial_sort(v.begin(), v.begin() + static_cast<long>(k), v.end(), std::greater<>());
    double sum = 0;
    for (size_t i = 0; i < k; ++i) sum += v[i];
    return sum > 0 ? 1000.0 * static_cast<double>(k) / sum : 0;
}

double perf_target_ms() { return g_target_ms; }
std::optional<Drop> perf_last_drop() { return g_last_drop; }
int perf_drop_count() { return g_drops; }

void perf_reset() {
    g_hist.clear();
    g_last = -1;
    g_seq = g_trace_seq;
    g_run.reset();
    g_last_drop.reset();
    g_drops = 0;
}

}  // namespace rt
