// MinGW-w64 ships windows.h; code written for MSVC includes Windows.h (case-sensitive on Linux).
// include_next: on a case-insensitive file system <windows.h> would find this very file again.
#pragma once
#include_next <windows.h>
