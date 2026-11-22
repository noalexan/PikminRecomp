#include "rt/format.hpp"

#include <cstdio>
#include <cstring>
#include <string_view>

namespace rt {
namespace {

// PPC EABI varargs: GPRs r3..r10 then the overflow area at r1+8; doubles in f1..f8.
// 64-bit integers occupy an odd/even register pair (r3:r4, r5:r6, ...) or an 8-aligned stack slot.
class VarArgs {
public:
    VarArgs(ppc_ctx* c, int gpr, int fpr) : c_(c), gpr_(gpr), fpr_(fpr), stack_(c->r[1] + 8) {}

    uint32_t u32() {
        if (gpr_ <= 10) return c_->r[gpr_++];
        uint32_t v = rd32(c_, stack_);
        stack_ += 4;
        return v;
    }

    uint64_t u64() {
        if (gpr_ % 2 == 0) ++gpr_;
        if (gpr_ <= 9) {
            uint64_t v = (static_cast<uint64_t>(c_->r[gpr_]) << 32) | c_->r[gpr_ + 1];
            gpr_ += 2;
            return v;
        }
        gpr_ = 11;
        stack_ = (stack_ + 7) & ~7u;
        uint64_t v = (static_cast<uint64_t>(rd32(c_, stack_)) << 32) | rd32(c_, stack_ + 4);
        stack_ += 8;
        return v;
    }

    double f64() {
        if (fpr_ <= 8) return c_->f[fpr_++].ps0;
        stack_ = (stack_ + 7) & ~7u;
        uint64_t bits = (static_cast<uint64_t>(rd32(c_, stack_)) << 32) | rd32(c_, stack_ + 4);
        stack_ += 8;
        double d;
        std::memcpy(&d, &bits, 8);
        return d;
    }

private:
    ppc_ctx* c_;
    int gpr_, fpr_;
    uint32_t stack_;
};

}  // namespace

std::string guest_format(ppc_ctx* c, uint32_t fmt_ea, int first_gpr, int first_fpr) {
    const std::string fmt = rdstr(c, fmt_ea);
    VarArgs va(c, first_gpr, first_fpr);
    std::string out;
    char buf[512];

    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') {
            out.push_back(fmt[i]);
            continue;
        }
        std::string spec = "%";
        size_t j = i + 1;
        while (j < fmt.size() && std::string_view("-+ #0").find(fmt[j]) != std::string_view::npos) spec += fmt[j++];
        auto star_or_digits = [&](bool allow_dot) {
            if (allow_dot) {
                if (j >= fmt.size() || fmt[j] != '.') return;
                spec += fmt[j++];
            }
            if (j < fmt.size() && fmt[j] == '*') {
                spec += std::to_string(static_cast<int32_t>(va.u32()));
                ++j;
            } else {
                while (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') spec += fmt[j++];
            }
        };
        star_or_digits(false);
        star_or_digits(true);
        int longs = 0;
        while (j < fmt.size() && (fmt[j] == 'l' || fmt[j] == 'h')) {
            if (fmt[j] == 'l') ++longs;
            ++j;
        }
        if (j >= fmt.size()) {
            out += spec;
            break;
        }
        const char conv = fmt[j];
        i = j;
        switch (conv) {
        case 'd':
        case 'i':
            if (longs >= 2) {
                std::snprintf(buf, sizeof buf, (spec + "lld").c_str(), static_cast<long long>(va.u64()));
            } else {
                std::snprintf(buf, sizeof buf, (spec + "d").c_str(), static_cast<int32_t>(va.u32()));
            }
            break;
        case 'u':
        case 'x':
        case 'X':
        case 'o':
            if (longs >= 2) {
                std::snprintf(buf, sizeof buf, (spec + "ll" + conv).c_str(), static_cast<unsigned long long>(va.u64()));
            } else {
                std::snprintf(buf, sizeof buf, (spec + conv).c_str(), va.u32());
            }
            break;
        case 'c':
            std::snprintf(buf, sizeof buf, (spec + "c").c_str(), static_cast<int>(va.u32() & 0xFF));
            break;
        case 'p':
            std::snprintf(buf, sizeof buf, "0x%08x", va.u32());
            break;
        case 's': {
            uint32_t ea = va.u32();
            std::string s = ea ? rdstr(c, ea) : std::string("(null)");
            std::snprintf(buf, sizeof buf, (spec + "s").c_str(), s.c_str());
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            std::snprintf(buf, sizeof buf, (spec + conv).c_str(), va.f64());
            break;
        case '%':
            std::snprintf(buf, sizeof buf, "%%");
            break;
        default:  // unknown conversion: emit verbatim
            std::snprintf(buf, sizeof buf, "%s%c", spec.c_str(), conv);
            break;
        }
        out += buf;
    }
    return out;
}

}  // namespace rt
