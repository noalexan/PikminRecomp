// Minimal test helpers shared by runtime unit tests.
#pragma once
#include <cstdio>

inline int g_check_failures = 0;

#define CHECK(expr)                                                                  \
    do {                                                                             \
        if (!(expr)) {                                                               \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++g_check_failures;                                                      \
        }                                                                            \
    } while (0)

inline int check_result() {
    if (g_check_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_check_failures);
        return 1;
    }
    std::puts("ok");
    return 0;
}
