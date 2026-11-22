#include "rt/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <set>
#include <string>

#include "rt/guest.hpp"

namespace rt {

CurrentFunction current_function;

static void vlog(const char* channel, const char* fmt, va_list ap) {
    std::fprintf(stderr, "[%s] ", channel);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
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

}  // namespace rt
