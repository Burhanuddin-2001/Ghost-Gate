// telemetry.cpp

#include "telemetry.hpp"

#include <windows.h>

namespace gh {

int get_ram_percent() {
    MEMORYSTATUSEX ms{};          // blank form, all boxes zero. Lives on the stack.
    ms.dwLength = sizeof(ms);     // form size at the top (Windows checks it)

    if (!GlobalMemoryStatusEx(&ms)) {   // BOOL: FALSE (0) = failed
        return -1;
    }
    return static_cast<int>(ms.dwMemoryLoad);  // DWORD -> int
}   // ms is freed here, at '}'. No handle, nothing to close.

} // namespace gh
