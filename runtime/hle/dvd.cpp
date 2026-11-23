// DVD HLE over rt::Disc. DVDFileInfo layout: DVDCommandBlock (0x30) then startAddr, length, callback.
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include "rt/clock.hpp"
#include "rt/disc.hpp"
#include "rt/guest.hpp"
#include "rt/irq.hpp"
#include "rt/log.hpp"
#include "rt/sched.hpp"

using rt::arg;
using rt::ret;
using rt::wr32;

namespace {
constexpr uint32_t kCbState = 0x0C, kCbOffset = 0x10, kCbLength = 0x14, kCbAddr = 0x18, kCbTransferred = 0x20,
                   kFiStart = 0x30, kFiLength = 0x34, kFiCallback = 0x38;
int g_pending = 0;

// Overrides, keyed by entry and by the file's disc offset (what a DVDFileInfo carries).
std::map<int, std::vector<uint8_t>> g_overrides;
std::map<uint32_t, int> g_override_by_offset;

const std::vector<uint8_t>* override_at(uint32_t start) {
    auto it = g_override_by_offset.find(start);
    return it == g_override_by_offset.end() ? nullptr : &g_overrides[it->second];
}

bool fill_file_info(ppc_ctx* c, int entry, uint32_t fi) {
    if (entry < 0 || static_cast<size_t>(entry) >= rt::disc()->entry_count()) return false;
    rt::FileRef f = rt::disc()->file(entry);
    if (f.is_dir) return false;
    for (uint32_t off = 0; off < 0x3C; off += 4) wr32(c, fi + off, 0);
    wr32(c, fi + kFiStart, f.offset);
    const auto* o = rt::file_override(entry);
    wr32(c, fi + kFiLength, o ? static_cast<uint32_t>(o->size()) : f.length);
    return true;
}

uint32_t read_into(ppc_ctx* c, uint32_t fi, uint32_t addr, uint32_t len, uint32_t offset) {
    uint32_t start = rt::rd32(c, fi + kFiStart);
    if (const auto* o = override_at(start)) {
        const size_t n = offset < o->size() ? std::min<size_t>(len, o->size() - offset) : 0;
        if (n) std::memcpy(rt::host(c, addr, static_cast<uint32_t>(n)), o->data() + offset, n);
        return static_cast<uint32_t>(n);
    }
    size_t n = rt::disc()->read(static_cast<uint64_t>(start) + offset, std::span<uint8_t>(rt::host(c, addr, len), len));
    if (n != len) rt::log("dvd", "short read: %zu of %u bytes at disc offset 0x%x", n, len, start + offset);
    return static_cast<uint32_t>(n);
}
// Sleeps the current guest thread until a clock event ~0.5 ms later (no-op outside a thread).
void block_briefly(ppc_ctx* c) {
    if (!rt::sched().current() || rt::irq().in_callback()) return;
    static uint32_t seq = 0;
    const uint32_t key = 0x817F0800u + 4 * (seq++ & 0xFF);  // wait keys only; never dereferenced
    rt::schedule_at(rt::clock().ticks() + rt::Clock::kTicksPerSec / 2000, [key] { rt::sched().wakeup(key); });
    rt::sched().sleep_on(key);
    (void)c;
}
}  // namespace

namespace rt {
void set_file_override(int entry, std::vector<uint8_t> bytes) {
    g_overrides[entry] = std::move(bytes);
    g_override_by_offset[disc()->file(entry).offset] = entry;
}
const std::vector<uint8_t>* file_override(int entry) {
    auto it = g_overrides.find(entry);
    return it == g_overrides.end() ? nullptr : &it->second;
}
void clear_file_overrides() {
    g_overrides.clear();
    g_override_by_offset.clear();
}
std::vector<uint8_t> read_entry(int entry) {
    if (!disc() || entry < 0 || static_cast<size_t>(entry) >= disc()->entry_count()) return {};
    const FileRef f = disc()->file(entry);
    if (f.is_dir) return {};
    std::vector<uint8_t> out(f.length);
    out.resize(disc()->read(f.offset, out));
    return out;
}
}  // namespace rt

extern "C" {

void hle_DVDInit(ppc_ctx* c) { HLE_ENTRY(c, "DVDInit"); }

void hle_DVDOpen(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDOpen");
    std::string path = rt::rdstr(c, arg(c, 0));
    int entry = rt::disc()->path_to_entry(path);
    if (entry < 0 || !fill_file_info(c, entry, arg(c, 1))) {
        rt::log("dvd", "not found %s", path.c_str());
        ret(c, 0);
        return;
    }
    rt::log("dvd", "open %s", path.c_str());
    rt::trace(rt::Ev::Dvd, "open", static_cast<uint32_t>(entry), 0, path.c_str());
    ret(c, 1);
}

void hle_DVDFastOpen(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDFastOpen");
    rt::trace(rt::Ev::Dvd, "fastopen", arg(c, 0));
    ret(c, fill_file_info(c, static_cast<int32_t>(arg(c, 0)), arg(c, 1)));
}

void hle_DVDConvertPathToEntrynum(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDConvertPathToEntrynum");
    std::string path = rt::rdstr(c, arg(c, 0));
    int entry = rt::disc()->path_to_entry(path);
    if (entry < 0) rt::log("dvd", "not found %s", path.c_str());
    ret(c, static_cast<uint32_t>(entry));
}

void hle_DVDClose(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDClose");
    ret(c, 1);
}

void hle_DVDReadPrio(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDReadPrio");
    uint32_t fi = arg(c, 0), addr = arg(c, 1), len = arg(c, 2), offset = arg(c, 3);
    rt::trace(rt::Ev::Dvd, "read", offset, len);
    uint32_t n = read_into(c, fi, addr, len, offset);
    // A synchronous read blocks the calling thread on the GameCube, letting lower-priority threads
    // run meanwhile. JAudio depends on that: its DVD thread (higher priority than the game) runs a
    // scene's "BGM loaded" callback only after the game thread has recorded the scene's BGM.
    block_briefly(c);
    ret(c, n);
}

void hle_DVDReadAsyncPrio(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDReadAsyncPrio");
    uint32_t fi = arg(c, 0), addr = arg(c, 1), len = arg(c, 2), offset = arg(c, 3), cb = arg(c, 4);
    rt::trace(rt::Ev::Dvd, "read_async", offset, len);
    wr32(c, fi + kCbState, 1);
    wr32(c, fi + kCbOffset, offset);
    wr32(c, fi + kCbLength, len);
    wr32(c, fi + kCbAddr, addr);
    wr32(c, fi + kFiCallback, cb);
    ++g_pending;
    uint8_t* mem = c->mem;
    rt::schedule_at(rt::clock().ticks() + rt::Clock::kTicksPerSec / 1000, [=] {
        ppc_ctx tmp{};
        tmp.mem = mem;
        uint32_t n = read_into(&tmp, fi, addr, len, offset);
        wr32(&tmp, fi + kCbTransferred, n);
        wr32(&tmp, fi + kCbState, 0);
        --g_pending;
        if (cb) rt::irq().post([=](ppc_ctx* ic) { rt::call_guest(ic, cb, n, fi); });
    });
    ret(c, 1);
}

void hle_DVDGetDriveStatus(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDGetDriveStatus");
    ret(c, g_pending > 0 ? 1 : 0);
}

void hle_DVDGetCurrentDiskID(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDGetCurrentDiskID");
    ret(c, 0x80000000u);
}

// Disc audio streaming is stubbed together with audio.
void hle_DVDPrepareStreamAsync(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDPrepareStreamAsync");
    uint32_t fi = arg(c, 0), cb = arg(c, 3);
    if (cb) rt::irq().post([=](ppc_ctx* ic) { rt::call_guest(ic, cb, 0, fi); });
    ret(c, 1);
}

void hle_DVDCancelStream(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDCancelStream");
    ret(c, 0);
}

}  // extern "C"
