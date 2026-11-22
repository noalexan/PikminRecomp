// OSReport / OSPanic: guest debug output.
#include <cstdio>
#include <cstdlib>

#include "rt/format.hpp"
#include "rt/guest.hpp"

extern "C" void hle_OSReport(ppc_ctx* c) {
    HLE_ENTRY(c, "OSReport");
    std::string s = rt::guest_format(c, rt::arg(c, 0), 4, 1);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

extern "C" void hle_OSPanic(ppc_ctx* c) {
    HLE_ENTRY(c, "OSPanic");
    std::string file = rt::rdstr(c, rt::arg(c, 0));
    uint32_t line = rt::arg(c, 1);
    std::string msg = rt::guest_format(c, rt::arg(c, 2), 6, 1);
    std::fprintf(stderr, "OSPanic at %s:%u: %s\n", file.c_str(), line, msg.c_str());
    std::abort();
}
