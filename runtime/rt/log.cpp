#include "rt/log.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

#include "rt/diag.hpp"
#include "rt/guest.hpp"

namespace rt {

CurrentFunction current_function;

// One write per line (stderr and the log file), so lines never interleave and a crash loses nothing
// buffered.
static void vlog_to(bool to_stderr, const char* channel, const char* fmt, va_list ap) {
    char buf[2048];
    int n = std::snprintf(buf, sizeof buf, "[%s] ", channel);
    if (n < 0) return;
    size_t len = std::min(static_cast<size_t>(n), sizeof buf - 2);
    int m = std::vsnprintf(buf + len, sizeof buf - 1 - len, fmt, ap);
    if (m > 0) len = std::min(len + static_cast<size_t>(m), sizeof buf - 2);
    buf[len++] = '\n';
    std::fflush(stderr);  // keep ordering with any remaining stdio writers
    if (to_stderr) diag_write(buf, len);
    else diag_write_file(buf, len);
}

void log_raw(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    size_t len = std::min(static_cast<size_t>(n), sizeof buf - 2);
    buf[len++] = '\n';
    std::fflush(stderr);
    diag_write(buf, len);
}

static void vlog(const char* channel, const char* fmt, va_list ap) { vlog_to(true, channel, fmt, ap); }

void log_file_only(const char* channel, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog_to(false, channel, fmt, ap);
    va_end(ap);
}

void log(const char* channel, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(channel, fmt, ap);
    va_end(ap);
}

void log_once(const char* key, const char* fmt, ...) {
    static std::set<std::string> seen;
    if (!seen.insert(key).second) return;
    va_list ap;
    va_start(ap, fmt);
    vlog("once", fmt, ap);
    va_end(ap);
}

void exit_process(int code) {
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(code);
}

}  // namespace rt
