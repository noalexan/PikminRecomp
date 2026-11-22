#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "check.hpp"
#include "hle_test_util.hpp"
#include "rt/clock.hpp"
#include "rt/disc.hpp"
#include "rt/irq.hpp"
#include "rt/sched.hpp"

DEFINE_TEST_HOOKS

extern "C" {
void hle_DVDOpen(ppc_ctx*);
void hle_DVDFastOpen(ppc_ctx*);
void hle_DVDConvertPathToEntrynum(ppc_ctx*);
void hle_DVDReadPrio(ppc_ctx*);
void hle_DVDReadAsyncPrio(ppc_ctx*);
void hle_DVDGetDriveStatus(ppc_ctx*);
}

static void put32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    v[off] = x >> 24; v[off + 1] = x >> 16; v[off + 2] = x >> 8; v[off + 3] = x;
}

// Minimal GCM: header, FST at 0x500 with /a.txt and /dir/B.bin.
static std::string make_disc(const char* id, uint32_t magic, size_t image_size = 0xA00) {
    std::vector<uint8_t> d(0xA00, 0);
    std::memcpy(d.data(), id, 6);
    put32(d, 0x1C, magic);
    const char names[] = "a.txt\0dir\0B.bin";
    const uint32_t fst = 0x500, count = 4, str = fst + count * 12;
    put32(d, 0x424, fst);
    put32(d, 0x428, count * 12 + sizeof names);
    // root
    put32(d, fst + 0, 0x01000000); put32(d, fst + 4, 0); put32(d, fst + 8, count);
    // a.txt
    put32(d, fst + 12, 0x00000000); put32(d, fst + 16, 0x800); put32(d, fst + 20, 5);
    // dir (parent 0, next 4)
    put32(d, fst + 24, 0x01000006); put32(d, fst + 28, 0); put32(d, fst + 32, 4);
    // B.bin
    put32(d, fst + 36, 0x0000000A); put32(d, fst + 40, 0x900); put32(d, fst + 44, 4);
    std::memcpy(&d[str], names, sizeof names);
    std::memcpy(&d[0x800], "hello", 5);
    std::memcpy(&d[0x900], "\xDE\xAD\xBE\xEF", 4);
    std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/pikmin_test_" + id + "_" +
                       std::to_string(magic) + "_" + std::to_string(image_size) + ".iso";
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(d.data(), 1, image_size, f);
    std::fclose(f);
    return path;
}

static int g_cb_calls = 0;
static uint32_t g_cb_r3 = 0, g_cb_r4 = 0;
static void read_cb(ppc_ctx* c) {
    ++g_cb_calls;
    g_cb_r3 = c->r[3];
    g_cb_r4 = c->r[4];
}

int main() {
    std::string good = make_disc("GPIP01", 0xC2339F3D);
    rt::Disc disc = rt::Disc::open(good);
    CHECK(disc.path_to_entry("a.txt") == 1);
    CHECK(disc.path_to_entry("/A.TXT") == 1);
    CHECK(disc.path_to_entry("dir/../a.txt") == 1);
    CHECK(disc.path_to_entry("./dir/b.bin") == 3);
    CHECK(disc.path_to_entry("nope") == -1);
    CHECK(disc.path_to_entry("dir") == 2);
    uint8_t buf[4];
    rt::FileRef b = disc.file(3);
    CHECK(b.offset == 0x900 && b.length == 4);
    CHECK(disc.read(b.offset, buf) == 4 && std::memcmp(buf, "\xDE\xAD\xBE\xEF", 4) == 0);

    bool threw = false;
    try {
        rt::Disc::open(make_disc("GXXE01", 0xC2339F3D));
    } catch (const rt::DiscError& e) {
        threw = std::string(e.what()).find("GXXE01") != std::string::npos;
    }
    CHECK(threw);
    threw = false;
    try {
        rt::Disc::open(make_disc("GPIP01", 0x12345678));
    } catch (const rt::DiscError&) {
        threw = true;
    }
    CHECK(threw);

    // truncated image: /dir/B.bin (0x900..0x904) lies past the end of a 0x902-byte file
    threw = false;
    try {
        rt::Disc::open(make_disc("GPIP01", 0xC2339F3D, 0x902));
    } catch (const rt::DiscError& e) {
        threw = std::string(e.what()).find("truncated") != std::string::npos;
    }
    CHECK(threw);

    // DVD HLE
    ppc_ctx c = make_ctx();
    rt::sched().init(&c, 0x817F0100u);
    rt::clock().init(rt::Clock::Mode::Fast);
    rt::irq().init(&c);
    rt::set_disc(std::make_unique<rt::Disc>(rt::Disc::open(good)));
    const uint32_t PATH = 0x80300000u, FI = 0x80300100u, DST = 0x80300200u;
    std::memcpy(&test_ram()[0x300000], "/dir/B.bin", 11);
    CHECK(call(hle_DVDOpen, &c, {PATH, FI}) == 1);
    CHECK(rt::rd32(&c, FI + 0x30) == 0x900 && rt::rd32(&c, FI + 0x34) == 4);
    CHECK(call(hle_DVDConvertPathToEntrynum, &c, {PATH}) == 3);
    CHECK(call(hle_DVDReadPrio, &c, {FI, DST, 4, 0, 2}) == 4);
    CHECK(rt::rd32(&c, DST) == 0xDEADBEEFu);

    // a read running past the end of the image reports the bytes actually read
    CHECK(call(hle_DVDReadPrio, &c, {FI, DST, 0x200, 0, 2}) == 0xA00 - 0x900);

    std::memcpy(&test_ram()[0x300000], "/missing.bin", 13);
    CHECK(call(hle_DVDOpen, &c, {PATH, FI}) == 0);

    // async read: callback only after time passes and a safe point delivers it
    CHECK(call(hle_DVDFastOpen, &c, {1, FI}) == 1);
    uint32_t cb = fake_fn(read_cb);
    CHECK(call(hle_DVDReadAsyncPrio, &c, {FI, DST, 5, 0, cb, 2}) == 1);
    CHECK(call(hle_DVDGetDriveStatus, &c, {}) == 1);
    CHECK(g_cb_calls == 0);
    rt::clock().advance_to(rt::clock().ticks() + rt::Clock::kTicksPerSec / 100);
    rt::safe_point();
    CHECK(g_cb_calls == 1 && g_cb_r3 == 5 && g_cb_r4 == FI);
    CHECK(std::memcmp(&test_ram()[0x300200], "hello", 5) == 0);
    CHECK(call(hle_DVDGetDriveStatus, &c, {}) == 0);

    return check_result();
}
