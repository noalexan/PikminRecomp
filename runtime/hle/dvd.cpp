// DVD HLE over rt::Disc. DVDFileInfo layout: DVDCommandBlock (0x30) then startAddr, length, callback.
#include <vector>

#include "rt/clock.hpp"
#include "rt/disc.hpp"
#include "rt/guest.hpp"
#include "rt/irq.hpp"
#include "rt/log.hpp"

using rt::arg;
using rt::ret;
using rt::wr32;

namespace {
constexpr uint32_t kCbState = 0x0C, kCbOffset = 0x10, kCbLength = 0x14, kCbAddr = 0x18, kCbTransferred = 0x20,
                   kFiStart = 0x30, kFiLength = 0x34, kFiCallback = 0x38;
int g_pending = 0;

bool fill_file_info(ppc_ctx* c, int entry, uint32_t fi) {
    if (entry < 0 || static_cast<size_t>(entry) >= rt::disc()->entry_count()) return false;
    rt::FileRef f = rt::disc()->file(entry);
    if (f.is_dir) return false;
    for (uint32_t off = 0; off < 0x3C; off += 4) wr32(c, fi + off, 0);
    wr32(c, fi + kFiStart, f.offset);
    wr32(c, fi + kFiLength, f.length);
    return true;
}

void read_into(ppc_ctx* c, uint32_t fi, uint32_t addr, uint32_t len, uint32_t offset) {
    uint32_t start = rt::rd32(c, fi + kFiStart);
    rt::disc()->read(static_cast<uint64_t>(start) + offset, std::span<uint8_t>(rt::host(c, addr, len), len));
}
}  // namespace

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
    ret(c, 1);
}

void hle_DVDFastOpen(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDFastOpen");
    ret(c, fill_file_info(c, static_cast<int32_t>(arg(c, 0)), arg(c, 1)));
}

void hle_DVDConvertPathToEntrynum(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDConvertPathToEntrynum");
    ret(c, static_cast<uint32_t>(rt::disc()->path_to_entry(rt::rdstr(c, arg(c, 0)))));
}

void hle_DVDClose(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDClose");
    ret(c, 1);
}

void hle_DVDReadPrio(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDReadPrio");
    uint32_t fi = arg(c, 0), addr = arg(c, 1), len = arg(c, 2), offset = arg(c, 3);
    read_into(c, fi, addr, len, offset);
    ret(c, len);
}

void hle_DVDReadAsyncPrio(ppc_ctx* c) {
    HLE_ENTRY(c, "DVDReadAsyncPrio");
    uint32_t fi = arg(c, 0), addr = arg(c, 1), len = arg(c, 2), offset = arg(c, 3), cb = arg(c, 4);
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
        read_into(&tmp, fi, addr, len, offset);
        wr32(&tmp, fi + kCbTransferred, len);
        wr32(&tmp, fi + kCbState, 0);
        --g_pending;
        if (cb) rt::irq().post([=](ppc_ctx* ic) { rt::call_guest(ic, cb, len, fi); });
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
