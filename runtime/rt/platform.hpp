// The few OS services the runtime needs, for POSIX (platform_posix.cpp) and Windows
// (platform_win32.cpp). Paths are UTF-8 everywhere.
#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>

namespace rt::plat {

int open_read(const std::string& path);   // binary; -1 on failure (errno set)
int open_write(const std::string& path);  // binary, created or truncated; -1 on failure
int64_t pread(int fd, void* buf, size_t n, uint64_t offset);  // bytes read, -1 on error
int64_t file_size(int fd);                // -1 on error
bool make_dir(const std::string& path);   // one level; true if it now exists
bool make_dirs(const std::string& path);  // every level
bool is_dir(const std::string& path);
int pid();
long tid();  // the calling thread (for CPU usage)
std::tm local_time(std::time_t t);
std::string exe_path();  // empty if unknown

void* dl_open(const char* name);  // nullptr if missing
void* dl_sym(void* lib, const char* name);

// Per-user base directories: config, data (saves, caches), state (logs).
// POSIX: $XDG_*_HOME or ~/.config, ~/.local/share, ~/.local/state.
// Windows: %APPDATA% (config, data), %LOCALAPPDATA% (state).
enum class UserDir { Config, Data, State };
std::string user_dir(UserDir d);  // without the "pikminrecomp" part; empty if unknown

}  // namespace rt::plat
