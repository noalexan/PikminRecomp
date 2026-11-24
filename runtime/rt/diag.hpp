// Diagnostics output: the per-run log file and an async-signal-safe writer to stderr + log.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace rt {

// $XDG_STATE_HOME/pikminrecomp/logs, or ~/.local/state/pikminrecomp/logs.
std::string log_dir();
// Creates dir, removes the oldest pikmin-*.log so at most keep-1 remain, and opens
// pikmin-YYYYmmdd-HHMMSS-<pid>.log. On failure warns once on stderr and returns false.
bool log_open(const std::string& dir, int keep = 10);
const std::string& log_path();  // empty if no log file
int log_fd();                   // -1 if no log file

// Async-signal-safe: write() to stderr and the log file, retrying short writes.
void diag_write(const char* s, size_t n);
void diag_write(const char* s);
// Same, log file only (nothing on stderr).
void diag_write_file(const char* s, size_t n);

// Async-signal-safe formatters; return out.
char* fmt_hex32(char out[11], uint32_t v);  // "0x%08x"
char* fmt_dec(char out[21], int64_t v);

extern const char* const kBuildHash;
extern const char* const kVersion;  // VERSION file, e.g. "0.2.4"
extern const char* const kBuildType;

}  // namespace rt
