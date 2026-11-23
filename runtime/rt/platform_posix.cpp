#ifndef _WIN32
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>

#include "rt/platform.hpp"

namespace rt::plat {

int open_read(const std::string& p) { return ::open(p.c_str(), O_RDONLY | O_CLOEXEC); }
int open_write(const std::string& p) { return ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644); }
int64_t pread(int fd, void* buf, size_t n, uint64_t off) { return ::pread(fd, buf, n, static_cast<off_t>(off)); }

int64_t file_size(int fd) {
    struct stat st{};
    return ::fstat(fd, &st) == 0 ? static_cast<int64_t>(st.st_size) : -1;
}

bool is_dir(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool make_dir(const std::string& p) { return ::mkdir(p.c_str(), 0755) == 0 || errno == EEXIST ? is_dir(p) : false; }
bool make_dirs(const std::string& dir) {
    for (size_t i = 1; i <= dir.size(); ++i)
        if (i == dir.size() || dir[i] == '/') make_dir(dir.substr(0, i));
    return is_dir(dir);
}

int pid() { return static_cast<int>(::getpid()); }
long tid() { return static_cast<long>(::syscall(SYS_gettid)); }
std::tm local_time(std::time_t t) {
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm;
}
std::string exe_path() {
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
}

void* dl_open(const char* name) { return ::dlopen(name, RTLD_NOW | RTLD_LOCAL); }
void* dl_sym(void* lib, const char* name) { return lib ? ::dlsym(lib, name) : nullptr; }

std::string user_dir(UserDir d) {
    const char* var = d == UserDir::Config ? "XDG_CONFIG_HOME" : d == UserDir::Data ? "XDG_DATA_HOME" : "XDG_STATE_HOME";
    if (const char* x = std::getenv(var); x && *x) return x;
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return std::string(home) + (d == UserDir::Config ? "/.config" : d == UserDir::Data ? "/.local/share" : "/.local/state");
}

}  // namespace rt::plat
#endif
