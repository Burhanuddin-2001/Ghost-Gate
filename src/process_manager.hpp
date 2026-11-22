// process_manager.hpp

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace gh {

// datatype that hold the info we care about per process.
struct ProcessInfo {
    uint32_t     pid;   // process id (the number)
    std::wstring name;  // executable name, e.g. L"UltraViewer.exe" (wide text)
};

class ProcessManager {
public:
    // Take a snapshot and return every running process.
    // Returns an empty list if the snapshot fails.
    std::vector<ProcessInfo> list() const;

    // Find every process whose exe name matches (ignores upper/lower case).
    // Returns an empty list if none is running.
    std::vector<ProcessInfo> find(const std::wstring& name) const;

    // Force-stop one process by its PID, then WAIT (up to 5 s) until it is gone.
    // Returns true only if it is truly gone, false otherwise.
    bool kill(uint32_t pid) const;

    // Start a program from its full path, in OUR session (so its window shows).
    // Returns the new PID, or 0 if it failed.
    uint32_t launch(const std::wstring& exePath) const;

    // Kill every copy of `name`, then launch `exePath` fresh.
    // If it is not running, it just launches.
    // Returns the new PID, or 0 if any kill or the launch failed.
    uint32_t restart(const std::wstring& name, const std::wstring& exePath) const;
};

} // namespace gh
