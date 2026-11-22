// Hardware register accesses from translated code (0xCC000000+): a register file, plus
// write-gather-pipe (GX FIFO) accounting. Headless mode drops FIFO data.
#pragma once
#include <cstdint>

namespace rt {
constexpr uint32_t kWgPipe = 0x0C008000u;
uint64_t mmio_wgpipe_bytes();
}  // namespace rt
