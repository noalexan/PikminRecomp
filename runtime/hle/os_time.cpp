// OS time and interrupt-state HLE.
#include "rt/clock.hpp"
#include "rt/guest.hpp"
#include "rt/irq.hpp"

using rt::arg;
using rt::ret;

namespace {
// Days since 1970-01-01 -> civil date (Howard Hinnant's algorithm).
void civil_from_days(int64_t z, int* y, int* m, int* d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    *m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    *y = static_cast<int>(yy + (*m <= 2));
}
}  // namespace

extern "C" {

void hle_OSGetTime(ppc_ctx* c) {
    HLE_ENTRY(c, "OSGetTime");
    rt::ret64(c, rt::clock().ticks());
}

void hle_OSGetTick(ppc_ctx* c) {
    HLE_ENTRY(c, "OSGetTick");
    ret(c, static_cast<uint32_t>(rt::clock().ticks()));
}

void hle_OSTicksToCalendarTime(ppc_ctx* c) {
    HLE_ENTRY(c, "OSTicksToCalendarTime");
    int64_t ticks = static_cast<int64_t>((static_cast<uint64_t>(arg(c, 0)) << 32) | arg(c, 1));
    uint32_t cal = arg(c, 2);
    const int64_t tps = static_cast<int64_t>(rt::Clock::kTicksPerSec);
    int64_t secs = ticks / tps, rem = ticks % tps;
    if (rem < 0) { rem += tps; --secs; }
    int64_t days = secs / 86400, sod = secs % 86400;
    if (sod < 0) { sod += 86400; --days; }
    int64_t unix_days = days + 10957;  // 1970 -> 2000
    int y, m, d;
    civil_from_days(unix_days, &y, &m, &d);
    int64_t jan1 = unix_days - (d - 1);
    static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    int yday = cum[m - 1] + d - 1 + (leap && m > 2 ? 1 : 0);
    (void)jan1;
    int wday = static_cast<int>(((unix_days % 7) + 7 + 4) % 7);  // 1970-01-01 was Thursday (4)
    uint32_t f[10] = {
        static_cast<uint32_t>(sod % 60), static_cast<uint32_t>(sod / 60 % 60), static_cast<uint32_t>(sod / 3600),
        static_cast<uint32_t>(d), static_cast<uint32_t>(m - 1), static_cast<uint32_t>(y),
        static_cast<uint32_t>(wday), static_cast<uint32_t>(yday),
        static_cast<uint32_t>(rem * 1000 / tps), static_cast<uint32_t>(rem * 1000000 / tps % 1000),
    };
    for (int i = 0; i < 10; ++i) rt::wr32(c, cal + 4 * i, f[i]);
}

void hle_OSDisableInterrupts(ppc_ctx* c) {
    HLE_ENTRY(c, "OSDisableInterrupts");
    ret(c, rt::irq().disable());
}

void hle_OSEnableInterrupts(ppc_ctx* c) {
    HLE_ENTRY(c, "OSEnableInterrupts");
    bool prev = rt::irq().enabled();
    rt::irq().restore(true);
    ret(c, prev);
}

void hle_OSRestoreInterrupts(ppc_ctx* c) {
    HLE_ENTRY(c, "OSRestoreInterrupts");
    bool prev = rt::irq().enabled();
    rt::irq().restore(arg(c, 0) != 0);
    ret(c, prev);
}

}  // extern "C"
