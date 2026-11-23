#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "rt/diag.hpp"
#include "rt/log.hpp"

namespace {
std::vector<std::string> list(const std::string& dir) {
    std::vector<std::string> out;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') out.push_back(e->d_name);
        closedir(d);
    }
    return out;
}
bool has(const std::vector<std::string>& v, const std::string& s) {
    for (auto& x : v) if (x == s) return true;
    return false;
}
std::string slurp(const std::string& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace

int main() {
    // fmt_helpers
    char hb[11], db[21];
    CHECK(std::strcmp(rt::fmt_hex32(hb, 0x8001234au), "0x8001234a") == 0);
    CHECK(std::strcmp(rt::fmt_hex32(hb, 0), "0x00000000") == 0);
    CHECK(std::strcmp(rt::fmt_dec(db, -12), "-12") == 0);
    CHECK(std::strcmp(rt::fmt_dec(db, 0), "0") == 0);
    CHECK(std::strcmp(rt::fmt_dec(db, 1234567890123), "1234567890123") == 0);

    // log_dir_unwritable
    CHECK(!rt::log_open("/proc/nonexistent/x"));
    CHECK(rt::log_fd() == -1);
    CHECK(rt::log_path().empty());
    rt::log("test", "still works");

    // rotation_keeps_ten
    char tmpl[] = "/tmp/pikmin_diag_XXXXXX";
    std::string dir = mkdtemp(tmpl);
    for (int i = 1; i <= 12; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "%s/pikmin-202601%02d-000000-1.log", dir.c_str(), i);
        std::FILE* f = std::fopen(name, "w");
        std::fclose(f);
        utimbuf t{1000L * i, 1000L * i};
        utime(name, &t);
    }
    std::fclose(std::fopen((dir + "/other.txt").c_str(), "w"));
    CHECK(rt::log_open(dir, 10));
    auto files = list(dir);
    int logs = 0;
    for (auto& f : files) if (f.rfind("pikmin-", 0) == 0) ++logs;
    CHECK(logs == 10);
    CHECK(!has(files, "pikmin-20260101-000000-1.log"));
    CHECK(!has(files, "pikmin-20260102-000000-1.log"));
    CHECK(!has(files, "pikmin-20260103-000000-1.log"));
    CHECK(has(files, "pikmin-20260104-000000-1.log"));
    CHECK(has(files, "other.txt"));
    CHECK(rt::log_path().find("-" + std::to_string(getpid()) + ".log") != std::string::npos);
    CHECK(rt::log_fd() >= 0);

    // log_line_reaches_file
    rt::log("test", "hello %d", 42);
    CHECK(slurp(rt::log_path()).find("[test] hello 42\n") != std::string::npos);
    rt::diag_write("raw\n");
    CHECK(slurp(rt::log_path()).find("raw\n") != std::string::npos);

    std::system(("rm -rf " + dir).c_str());
    return check_result();
}
