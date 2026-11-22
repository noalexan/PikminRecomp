// Runtime logging to stderr: "[channel] message".
#pragma once

namespace rt {
void log(const char* channel, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
// Logs only the first time `key` is seen.
void log_once(const char* key, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
// Ends the process from anywhere, including a guest fiber: flushes stdio and skips static
// destructors (which would unmap the fiber stack we may be running on).
[[noreturn]] void exit_process(int code);
}  // namespace rt
