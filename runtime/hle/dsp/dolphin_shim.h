// Minimal stand-ins for the Dolphin types/services the ported Zelda ucode HLE (zelda.cpp, from
// Dolphin, GPL-2.0-or-later) uses: integer typedefs, BitField, byte swapping, logging, and a
// Core::System that exposes guest main RAM and ARAM.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

// Logging: the HLE's diagnostics go to the runtime log once per format string.
void zelda_log(const char* level, const char* fmt);
#define PanicAlertFmt(fmt, ...) zelda_log("panic", fmt)
#define ERROR_LOG_FMT(cat, fmt, ...) zelda_log("error", fmt)
#define WARN_LOG_FMT(cat, fmt, ...) zelda_log("warn", fmt)
#define NOTICE_LOG_FMT(cat, fmt, ...) zelda_log("notice", fmt)
#define INFO_LOG_FMT(cat, fmt, ...) ((void)0)
#define DEBUG_LOG_FMT(cat, fmt, ...) ((void)0)

// Bits [position, position+bits) of a storage word, Dolphin's BitField subset.
template <size_t position, size_t bits, typename T>
struct BitField {
    T storage;
    static constexpr T mask() { return static_cast<T>(((1ull << bits) - 1) << position); }
    operator T() const { return static_cast<T>((storage & mask()) >> position); }
    T Value() const { return *this; }
    BitField& operator=(T v) {
        storage = static_cast<T>((storage & ~mask()) | ((v << position) & mask()));
        return *this;
    }
};

namespace Common {
inline u16 swap16(u16 v) { return static_cast<u16>((v >> 8) | (v << 8)); }
inline u32 swap32(u32 v) { return __builtin_bswap32(v); }
template <typename T>
inline T FromBigEndian(T v) {
    if constexpr (sizeof(T) == 1) return v;
    else if constexpr (sizeof(T) == 2) return static_cast<T>(swap16(static_cast<u16>(v)));
    else return static_cast<T>(swap32(static_cast<u32>(v)));
}
}  // namespace Common

namespace Core {
// Guest main RAM, big-endian. "Swapped" copies convert 16-bit words to/from host order.
class Memory {
public:
    u8* GetPointerForRange(u32 addr, size_t size) const;
    void CopyFromEmuSwapped(u16* dst, u32 addr, size_t size) const;
    void CopyToEmuSwapped(u32 addr, const u16* src, size_t size) const;
    template <typename T>
    void CopyFromEmuSwapped(T* dst, u32 addr, size_t size) const {
        CopyFromEmuSwapped(reinterpret_cast<u16*>(dst), addr, size);
    }
    template <typename T>
    void CopyToEmuSwapped(u32 addr, const T* src, size_t size) const {
        CopyToEmuSwapped(addr, reinterpret_cast<const u16*>(src), size);
    }
};

class DSPManager {
public:
    u32 GetARAMSize() const;
    u8* GetARAMPtr() const;
};

class System {
public:
    Memory& GetMemory() { return m_memory; }
    const Memory& GetMemory() const { return m_memory; }
    DSPManager& GetDSP() { return m_dsp; }
    const DSPManager& GetDSP() const { return m_dsp; }
    bool IsWii() const { return false; }

private:
    Memory m_memory;
    DSPManager m_dsp;
};
}  // namespace Core
