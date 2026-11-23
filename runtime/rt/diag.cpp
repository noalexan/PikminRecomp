#include "rt/diag.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

namespace rt {
namespace {
std::string g_path;
int g_fd = -1;

void write_all(int fd, const char* s, size_t n) {
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

bool mkdirs(const std::string& dir) {
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/') {
            std::string p = dir.substr(0, i);
            if (::mkdir(p.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
    }
    struct stat st{};
    return ::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void rotate(const std::string& dir, int keep) {
    struct Entry {
        std::string name;
        time_t mtime;
    };
    std::vector<Entry> logs;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            std::string n = e->d_name;
            if (n.rfind("pikmin-", 0) != 0 || n.size() < 4 || n.compare(n.size() - 4, 4, ".log") != 0) continue;
            struct stat st{};
            if (::stat((dir + "/" + n).c_str(), &st) == 0) logs.push_back({n, st.st_mtime});
        }
        ::closedir(d);
    }
    std::sort(logs.begin(), logs.end(), [](const Entry& a, const Entry& b) {
        return a.mtime != b.mtime ? a.mtime < b.mtime : a.name < b.name;
    });
    const size_t max_old = keep > 1 ? static_cast<size_t>(keep - 1) : 0;
    for (size_t i = 0; i + max_old < logs.size(); ++i) ::unlink((dir + "/" + logs[i].name).c_str());
}
}  // namespace

std::string log_dir() {
    const char* xdg = std::getenv("XDG_STATE_HOME");
    std::string base;
    if (xdg && *xdg) base = xdg;
    else if (const char* home = std::getenv("HOME"); home && *home) base = std::string(home) + "/.local/state";
    else return {};
    return base + "/pikminrecomp/logs";
}

bool log_open(const std::string& dir, int keep) {
    if (dir.empty() || !mkdirs(dir)) {
        std::fprintf(stderr, "[log] cannot create log directory %s; logging to stderr only\n", dir.c_str());
        return false;
    }
    rotate(dir, keep);
    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    std::string path = dir + "/pikmin-" + stamp + "-" + std::to_string(::getpid()) + ".log";
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        std::fprintf(stderr, "[log] cannot open %s; logging to stderr only\n", path.c_str());
        return false;
    }
    if (g_fd >= 0) ::close(g_fd);
    g_fd = fd;
    g_path = path;
    return true;
}

const std::string& log_path() { return g_path; }
int log_fd() { return g_fd; }

void diag_write(const char* s, size_t n) {
    write_all(2, s, n);
    if (g_fd >= 0) write_all(g_fd, s, n);
}
void diag_write(const char* s) { diag_write(s, std::strlen(s)); }
void diag_write_file(const char* s, size_t n) {
    if (g_fd >= 0) write_all(g_fd, s, n);
}

char* fmt_hex32(char out[11], uint32_t v) {
    static const char kDigits[] = "0123456789abcdef";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 8; ++i) out[2 + i] = kDigits[(v >> (28 - 4 * i)) & 0xf];
    out[10] = '\0';
    return out;
}

char* fmt_dec(char out[21], int64_t v) {
    char tmp[21];
    int n = 0;
    uint64_t u = v < 0 ? 0 - static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
    do {
        tmp[n++] = static_cast<char>('0' + u % 10);
        u /= 10;
    } while (u);
    int o = 0;
    if (v < 0) out[o++] = '-';
    while (n) out[o++] = tmp[--n];
    out[o] = '\0';
    return out;
}

}  // namespace rt

namespace rt {
#ifndef PIKMIN_BUILD_TYPE
#define PIKMIN_BUILD_TYPE "Debug"
#endif
extern const char* const kBuildType = PIKMIN_BUILD_TYPE;
}  // namespace rt
