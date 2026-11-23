// Memory card (slot A) backed by files in $XDG_DATA_HOME/pikminrecomp/card_a: per save slot N,
// slotN.bin (data), slotN.stat (raw 0x6C-byte CARDStat) and slotN.name. Slot B: no card.
#include <sys/stat.h>

#include "rt/platform.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "rt/clock.hpp"
#include "rt/guest.hpp"
#include "rt/irq.hpp"
#include "rt/log.hpp"

using rt::arg;
using rt::ret;

namespace {
int g_open_files = 0;  // between a successful create/open and its close
}

namespace rt {
std::string g_card_dir;  // set by main; empty = no card inserted
// A save file is open: the game is in the middle of reading or writing it (quitting waits).
bool card_busy() { return g_open_files > 0; }
}

namespace {
constexpr int32_t kReady = 0, kNoCard = -3, kNoFile = -4, kExist = -7, kInsSpace = -9;
constexpr uint32_t kSector = 0x2000, kBlocks = 59 - 5, kMaxFiles = 127, kStatSize = 0x6C;
int32_t g_result = kReady;
uint32_t g_xferred = 0;

bool present(uint32_t chan) { return chan == 0 && !rt::g_card_dir.empty(); }
std::string path(uint32_t slot, const char* ext) {
    return rt::g_card_dir + "/slot" + std::to_string(slot) + "." + ext;
}
bool exists(uint32_t slot) {
    struct stat st{};
    return ::stat(path(slot, "bin").c_str(), &st) == 0;
}
std::vector<uint8_t> load(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
void save(const std::string& p, const void* data, size_t n) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
}
std::string name_of(uint32_t slot) {
    auto v = load(path(slot, "name"));
    return {v.begin(), v.end()};
}
int find(const std::string& name) {
    for (uint32_t s = 0; s < kMaxFiles; ++s)
        if (exists(s) && name_of(s) == name) return static_cast<int>(s);
    return -1;
}
uint32_t used_blocks() {
    uint32_t b = 0;
    for (uint32_t s = 0; s < kMaxFiles; ++s)
        if (exists(s)) b += static_cast<uint32_t>((load(path(s, "bin")).size() + kSector - 1) / kSector);
    return b;
}
int32_t done(ppc_ctx* c, int32_t result) {
    g_result = result;
    ret(c, static_cast<uint32_t>(result));
    return result;
}
// Completes an async call: result now, callback(chan, result) through the interrupt queue.
void async_done(ppc_ctx* c, uint32_t chan, uint32_t cb, int32_t result) {
    done(c, result);
    if (cb && result == kReady) rt::irq().post([=](ppc_ctx* ic) { rt::call_guest(ic, cb, chan, 0); });
}
void fill_fileinfo(ppc_ctx* c, uint32_t fi, uint32_t chan, uint32_t slot) {
    rt::wr32(c, fi + 0, chan);
    rt::wr32(c, fi + 4, slot);
    rt::wr32(c, fi + 8, 0);
    rt::wr32(c, fi + 12, static_cast<uint32_t>(load(path(slot, "bin")).size()));
    rt::wr16(c, fi + 16, 0);
}
// CARDStat banner/icon/data offsets, as the SDK computes them from iconAddr and the formats.
void update_offsets(std::vector<uint8_t>& st) {
    auto r32 = [&](size_t o) { return uint32_t(st[o]) << 24 | st[o + 1] << 16 | st[o + 2] << 8 | st[o + 3]; };
    auto w32 = [&](size_t o, uint32_t v) {
        st[o] = v >> 24; st[o + 1] = v >> 16; st[o + 2] = v >> 8; st[o + 3] = static_cast<uint8_t>(v);
    };
    uint32_t offset = r32(0x30);
    const uint8_t banner = st[0x2E] & 3;
    const uint16_t iconFmt = static_cast<uint16_t>(st[0x34] << 8 | st[0x35]);
    if (offset == 0xFFFFFFFFu) {
        for (size_t o = 0x3C; o <= 0x68; o += 4) w32(o, 0xFFFFFFFFu);
        w32(0x68, 0);
        return;
    }
    w32(0x3C, banner ? offset : 0xFFFFFFFFu);
    w32(0x40, 0xFFFFFFFFu);
    if (banner == 1) { offset += 96 * 32; w32(0x40, offset); offset += 512; }
    else if (banner == 2) { offset += 96 * 32 * 2; }
    bool ci = false;
    for (int i = 0; i < 8; ++i) {
        const int f = (iconFmt >> (i * 2)) & 3;
        w32(0x44 + 4 * i, f ? offset : 0xFFFFFFFFu);
        if (f == 1) { offset += 32 * 32; ci = true; }
        else if (f == 2) { offset += 32 * 32 * 2; }
    }
    w32(0x64, ci ? offset : 0xFFFFFFFFu);
    if (ci) offset += 512;
    w32(0x68, offset);
}
}  // namespace

extern "C" {

void hle_CARDInit(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDInit");
    if (!rt::g_card_dir.empty()) {
        rt::plat::make_dirs(rt::g_card_dir);
        rt::log("card", "memory card in slot A: %s", rt::g_card_dir.c_str());
    }
}
void hle_CARDProbe(ppc_ctx* c) { HLE_ENTRY(c, "CARDProbe"); ret(c, present(arg(c, 0))); }
void hle_CARDMount(ppc_ctx* c) { HLE_ENTRY(c, "CARDMount"); done(c, present(arg(c, 0)) ? kReady : kNoCard); }
void hle_CARDMountAsync(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDMountAsync");
    async_done(c, arg(c, 0), arg(c, 3), present(arg(c, 0)) ? kReady : kNoCard);
}
void hle_CARDUnmount(ppc_ctx* c) { HLE_ENTRY(c, "CARDUnmount"); done(c, kReady); }
void hle_CARDCheck(ppc_ctx* c) { HLE_ENTRY(c, "CARDCheck"); done(c, present(arg(c, 0)) ? kReady : kNoCard); }
void hle_CARDCheckAsync(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDCheckAsync");
    async_done(c, arg(c, 0), arg(c, 1), present(arg(c, 0)) ? kReady : kNoCard);
}
static int32_t format_card(uint32_t chan) {
    if (!present(chan)) return kNoCard;
    for (uint32_t s = 0; s < kMaxFiles; ++s)
        for (const char* ext : {"bin", "stat", "name"}) std::remove(path(s, ext).c_str());
    return kReady;
}
void hle_CARDFormat(ppc_ctx* c) { HLE_ENTRY(c, "CARDFormat"); done(c, format_card(arg(c, 0))); }
void hle_CARDFormatAsync(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDFormatAsync");
    async_done(c, arg(c, 0), arg(c, 1), format_card(arg(c, 0)));
}
void hle_CARDGetResultCode(ppc_ctx* c) { HLE_ENTRY(c, "CARDGetResultCode"); ret(c, static_cast<uint32_t>(g_result)); }
void hle_CARDGetXferredBytes(ppc_ctx* c) { HLE_ENTRY(c, "CARDGetXferredBytes"); ret(c, g_xferred); }
void hle_CARDGetSectorSize(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDGetSectorSize");
    if (!present(arg(c, 0))) { done(c, kNoCard); return; }
    rt::wr32(c, arg(c, 1), kSector);
    done(c, kReady);
}
void hle_CARDFreeBlocks(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDFreeBlocks");
    if (!present(arg(c, 0))) { done(c, kNoCard); return; }
    uint32_t files = 0;
    for (uint32_t s = 0; s < kMaxFiles; ++s) files += exists(s);
    const uint32_t used = used_blocks();
    if (arg(c, 1)) rt::wr32(c, arg(c, 1), (used < kBlocks ? kBlocks - used : 0) * kSector);
    if (arg(c, 2)) rt::wr32(c, arg(c, 2), kMaxFiles - files);
    done(c, kReady);
}
void hle_CARDCreate(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDCreate");
    const uint32_t chan = arg(c, 0), size = arg(c, 2), fi = arg(c, 3);
    if (!present(chan)) { done(c, kNoCard); return; }
    const std::string name = rt::rdstr(c, arg(c, 1), 32);
    if (find(name) >= 0) { done(c, kExist); return; }
    if (used_blocks() + (size + kSector - 1) / kSector > kBlocks) { done(c, kInsSpace); return; }
    uint32_t slot = 0;
    while (slot < kMaxFiles && exists(slot)) ++slot;
    std::vector<uint8_t> zero(size), st(kStatSize, 0);
    std::memcpy(st.data(), name.data(), std::min<size_t>(name.size(), 32));
    st[0x20] = size >> 24; st[0x21] = size >> 16; st[0x22] = size >> 8; st[0x23] = static_cast<uint8_t>(size);
    std::memcpy(&st[0x28], "GPIP01", 6);
    save(path(slot, "bin"), zero.data(), zero.size());
    save(path(slot, "stat"), st.data(), st.size());
    save(path(slot, "name"), name.data(), name.size());
    fill_fileinfo(c, fi, chan, slot);
    rt::log("card", "create '%s' (%u bytes) in slot %u", name.c_str(), size, slot);
    ++g_open_files;
    done(c, kReady);
}
void hle_CARDOpen(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDOpen");
    const uint32_t chan = arg(c, 0);
    if (!present(chan)) { done(c, kNoCard); return; }
    const int slot = find(rt::rdstr(c, arg(c, 1), 32));
    if (slot < 0) { done(c, kNoFile); return; }
    fill_fileinfo(c, arg(c, 2), chan, static_cast<uint32_t>(slot));
    ++g_open_files;
    done(c, kReady);
}
void hle_CARDFastOpen(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDFastOpen");
    const uint32_t chan = arg(c, 0), slot = arg(c, 1);
    if (!present(chan)) { done(c, kNoCard); return; }
    if (slot >= kMaxFiles || !exists(slot)) { done(c, kNoFile); return; }
    fill_fileinfo(c, arg(c, 2), chan, slot);
    ++g_open_files;
    done(c, kReady);
}
void hle_CARDClose(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDClose");
    if (g_open_files > 0) --g_open_files;
    done(c, kReady);
}
void hle_CARDRead(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDRead");
    const uint32_t fi = arg(c, 0), addr = arg(c, 1), len = arg(c, 2), off = arg(c, 3);
    const uint32_t slot = rt::rd32(c, fi + 4);
    auto data = load(path(slot, "bin"));
    if (off + len > data.size()) { done(c, kNoFile); return; }
    std::memcpy(rt::host(c, addr, len), data.data() + off, len);
    g_xferred = len;
    done(c, kReady);
}
void hle_CARDWrite(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDWrite");
    const uint32_t fi = arg(c, 0), addr = arg(c, 1), len = arg(c, 2), off = arg(c, 3);
    const uint32_t slot = rt::rd32(c, fi + 4);
    auto data = load(path(slot, "bin"));
    if (off + len > data.size()) data.resize(off + len);
    std::memcpy(data.data() + off, rt::host(c, addr, len), len);
    save(path(slot, "bin"), data.data(), data.size());
    g_xferred = len;
    rt::log("card", "write slot %u: %u bytes at %u", slot, len, off);
    done(c, kReady);
}
void hle_CARDFastDelete(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDFastDelete");
    const uint32_t slot = arg(c, 1);
    if (!present(arg(c, 0))) { done(c, kNoCard); return; }
    if (slot >= kMaxFiles || !exists(slot)) { done(c, kNoFile); return; }
    for (const char* ext : {"bin", "stat", "name"}) std::remove(path(slot, ext).c_str());
    done(c, kReady);
}
void hle_CARDRename(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDRename");
    if (!present(arg(c, 0))) { done(c, kNoCard); return; }
    const int slot = find(rt::rdstr(c, arg(c, 1), 32));
    if (slot < 0) { done(c, kNoFile); return; }
    const std::string to = rt::rdstr(c, arg(c, 2), 32);
    if (find(to) >= 0) { done(c, kExist); return; }
    save(path(static_cast<uint32_t>(slot), "name"), to.data(), to.size());
    done(c, kReady);
}
void hle_CARDGetStatus(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDGetStatus");
    const uint32_t slot = arg(c, 1), dst = arg(c, 2);
    if (!present(arg(c, 0))) { done(c, kNoCard); return; }
    if (slot >= kMaxFiles || !exists(slot)) { done(c, kNoFile); return; }
    auto st = load(path(slot, "stat"));
    st.resize(kStatSize);
    const std::string name = name_of(slot);
    std::memset(st.data(), 0, 32);
    std::memcpy(st.data(), name.data(), std::min<size_t>(name.size(), 32));
    const uint32_t len = static_cast<uint32_t>(load(path(slot, "bin")).size());
    st[0x20] = len >> 24; st[0x21] = len >> 16; st[0x22] = len >> 8; st[0x23] = static_cast<uint8_t>(len);
    std::memcpy(&st[0x28], "GPIP01", 6);  // every file here is Pikmin's (repairs saves whose code was wiped)
    update_offsets(st);
    std::memcpy(rt::host(c, dst, kStatSize), st.data(), kStatSize);
    done(c, kReady);
}
void hle_CARDSetStatus(ppc_ctx* c) {
    HLE_ENTRY(c, "CARDSetStatus");
    const uint32_t slot = arg(c, 1), src = arg(c, 2);
    if (!present(arg(c, 0))) { done(c, kNoCard); return; }
    if (slot >= kMaxFiles || !exists(slot)) { done(c, kNoFile); return; }
    // Like the SDK, only the banner/icon/comment fields come from the caller (CARDStat 0x2E-0x3B);
    // name, length and game code stay the file's own. Pikmin passes a stat it filled itself, so
    // taking all of it wiped the game code and the game no longer recognised its save.
    std::vector<uint8_t> st = load(path(slot, "stat"));
    st.resize(kStatSize);
    std::memcpy(&st[0x2E], rt::host(c, src + 0x2E, 0x3C - 0x2E), 0x3C - 0x2E);
    std::memcpy(&st[0x28], "GPIP01", 6);
    const uint32_t now = static_cast<uint32_t>(rt::clock().ticks() / rt::Clock::kTicksPerSec);
    st[0x24] = now >> 24; st[0x25] = now >> 16; st[0x26] = now >> 8; st[0x27] = static_cast<uint8_t>(now);
    update_offsets(st);
    save(path(slot, "stat"), st.data(), st.size());
    done(c, kReady);
}

}  // extern "C"
