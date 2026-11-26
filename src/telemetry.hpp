#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace gh {

// Returns RAM in use, 0 to 100.
// Returns -1 if Windows could not fill in the form.
// (Not 0: 0 % is a real answer, so it cannot also mean "failed".)
int get_ram_percent();

// Raw counters from GetSystemTimes, in 100 ns units.
struct CpuTimes {
    uint64_t idle   = 0; 
    uint64_t kernel = 0;   // also includes idle time
    uint64_t user   = 0;
};

// Busy percentage between two samples, 0..100.
// Returns nullopt if no time has passed (or the counters went backwards).
std::optional<double> cpu_percent_between(const CpuTimes& before, const CpuTimes& after);

std::optional<CpuTimes> read_cpu_times();

// Samples CPU usage on a background thread so requests never block on it.
class CpuMonitor {
public:
    explicit CpuMonitor(std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    ~CpuMonitor();
    CpuMonitor(const CpuMonitor&) = delete;
    CpuMonitor& operator=(const CpuMonitor&) = delete;

    // Latest busy percentage; nullopt until the first interval has passed.
    std::optional<double> latest() const;

private:
    void run();

    std::chrono::milliseconds interval_;
    std::atomic<double>       value_{-1.0};
    std::mutex                mutex_;
    std::condition_variable   wake_;
    bool                      stop_ = false;
    std::thread               worker_;   // declared last: starts after the rest is ready
};

} // ending namespace gh
