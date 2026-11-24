// currently just the ram data.
#pragma once

namespace gh {

// Returns RAM in use, 0 to 100.
// Returns -1 if Windows could not fill in the form.
// (Not 0: 0 % is a real answer, so it cannot also mean "failed".)
int get_ram_percent();

} // ending namespace gh
