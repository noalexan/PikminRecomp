// Debug channels (HIO host I/O, EXI UART): no devices; UART output goes to stderr.
#include <cstdio>

#include "rt/guest.hpp"

using rt::arg;
using rt::ret;

#define HIO_NONE(X) X(HIOInit) X(HIOEnumDevices) X(HIOReadMailbox) X(HIOWrite) X(HIOWriteMailbox)

extern "C" {

#define DEFINE_HIO(name)               \
    void hle_##name(ppc_ctx* c) {      \
        HLE_ENTRY(c, #name);           \
        ret(c, 0);                     \
    }
HIO_NONE(DEFINE_HIO)
#undef DEFINE_HIO

void hle_InitializeUART(ppc_ctx* c) {
    HLE_ENTRY(c, "InitializeUART");
    ret(c, 0);
}

void hle_WriteUARTN(ppc_ctx* c) {
    HLE_ENTRY(c, "WriteUARTN");
    uint32_t buf = arg(c, 0), len = arg(c, 1);
    std::fwrite(rt::host(c, buf, len), 1, len, stderr);
    std::fflush(stderr);
    ret(c, 0);
}

void hle_ReadUARTN(ppc_ctx* c) {
    HLE_ENTRY(c, "ReadUARTN");
    ret(c, 0);
}

}  // extern "C"
