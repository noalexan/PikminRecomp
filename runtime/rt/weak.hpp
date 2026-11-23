// Default definitions tests may replace: weak on ELF. COFF (Windows) has no equivalent override,
// so they are ordinary definitions there (tests run on Linux only).
#pragma once
#ifdef _WIN32
#define PIKMIN_WEAK
#else
#define PIKMIN_WEAK __attribute__((weak))
#endif
