// telemetry.hpp
// Reads numbers about the PC
// Step 1: RAM % in use.

#pragma once

namespace gh {

// Returns RAM in use, 0 to 100.
// Returns -1 if Windows could not fill in the form.
// (Not 0: 0 % is a real answer, so it cannot also mean "failed".)
int get_ram_percent();

} // namespace gh
