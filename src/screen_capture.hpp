// Captures the screen on all monitors and encodes it as JPEG in memory.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gh {

struct Screenshot {
    std::vector<uint8_t> jpeg;   // complete JPEG bytes variable
    int width  = 0;              // size
    int height = 0;
};

// max_width:  0 = full size.
// quality:   1 = 100%
// On failure return types nullopt and, if `error` is given, a short reason.
std::optional<Screenshot> capture_screen_jpeg(int max_width = 0, float quality = 0.95f,
                                              std::string* error = nullptr);

} // ending namespace gh
