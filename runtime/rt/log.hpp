// Runtime logging to stderr: "[channel] message".
#pragma once

namespace rt {
void log(const char* channel, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
// Logs only the first time `key` is seen.
void log_once(const char* key, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
}  // namespace rt
