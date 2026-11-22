// printf-style formatting with arguments taken from guest registers / stack (PPC EABI varargs).
#pragma once
#include <cstdint>
#include <string>

#include "rt/guest.hpp"

namespace rt {
// Formats the guest string at fmt_ea. Integer varargs start at r[first_gpr], floats at f[first_fpr].
std::string guest_format(ppc_ctx* c, uint32_t fmt_ea, int first_gpr, int first_fpr);
}  // namespace rt
