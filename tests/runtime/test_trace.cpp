#include <unistd.h>

#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "check.hpp"
#include "rt/trace.hpp"

namespace {
// Dumps through a pipe and returns the text.
std::string dump(int max) {
    int fds[2];
    if (pipe(fds) != 0) return {};
    std::string out;
    std::thread reader([&] {
        char buf[4096];
        ssize_t n;
        while ((n = read(fds[0], buf, sizeof buf)) > 0) out.append(buf, static_cast<size_t>(n));
    });
    rt::trace_dump_to(fds[1], max);
    close(fds[1]);
    reader.join();
    close(fds[0]);
    return out;
}
std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> v;
    size_t p = 0, q;
    while ((q = s.find('\n', p)) != std::string::npos) {
        v.push_back(s.substr(p, q - p));
        p = q + 1;
    }
    return v;
}
}  // namespace

int main() {
    // ring_wraps_oldest_first
    rt::trace_reset();
    for (uint32_t i = 0; i < 5000; ++i) rt::trace(rt::Ev::Hle, "OSFoo", i);
    auto l = lines(dump(rt::kTraceSize));
    CHECK(l.size() == rt::kTraceSize);
    CHECK(!l.empty() && l.front().find("a=0x00000388") != std::string::npos);  // 5000 - 4096 = 904
    CHECK(!l.empty() && l.back().find("a=0x00001387") != std::string::npos);
    CHECK(!l.empty() && l.back().find(" hle OSFoo ") != std::string::npos);
    auto last3 = lines(dump(3));
    CHECK(last3.size() == 3 && last3[2] == l.back());

    // ring_copies_text
    rt::trace_reset();
    char path[] = "dvd/path.bin";
    rt::trace(rt::Ev::Dvd, "open", 7, 0, path);
    std::memset(path, 'x', sizeof path - 1);
    auto d = dump(10);
    CHECK(d.find("dvd open a=0x00000007") != std::string::npos);
    CHECK(d.find("dvd/path.bin") != std::string::npos);
    CHECK(lines(d).size() == 1);

    // long text is truncated, not overflowed
    rt::trace_reset();
    std::string longtext(200, 'q');
    rt::trace(rt::Ev::Scene, "init", 0, 0, longtext.c_str());
    CHECK(dump(1).find(std::string(rt::kTraceText - 1, 'q')) != std::string::npos);

    // parse_channels
    using rt::Ev;
    auto bit = [](Ev e) { return 1u << static_cast<unsigned>(e); };
    CHECK(rt::trace_parse_channels("hle,dvd") == (bit(Ev::Hle) | bit(Ev::Dvd)));
    CHECK(rt::trace_parse_channels("all") == 0x3fu);
    CHECK(rt::trace_parse_channels("bogus,irq") == bit(Ev::Irq));
    CHECK(rt::trace_parse_channels("") == 0);
    CHECK(rt::trace_parse_channels(nullptr) == 0);
    CHECK(rt::trace_parse_channels("frame,scene,thread") == (bit(Ev::Frame) | bit(Ev::Scene) | bit(Ev::Thread)));
    return check_result();
}
