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

namespace {
uint64_t to_u64(const FILETIME& ft) {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
} // namespace

std::optional<CpuTimes> read_cpu_times() {
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user)) {
        return std::nullopt;
    }
    return CpuTimes{to_u64(idle), to_u64(kernel), to_u64(user)};
}

std::optional<double> cpu_percent_between(const CpuTimes& before, const CpuTimes& after) {
    if (after.idle < before.idle || after.kernel < before.kernel || after.user < before.user) {
        return std::nullopt;
    }
    const uint64_t idle  = after.idle - before.idle;
    const uint64_t total = (after.kernel - before.kernel) + (after.user - before.user);
    if (total == 0 || idle > total) {
        return std::nullopt;
    }
    return 100.0 * static_cast<double>(total - idle) / static_cast<double>(total);
}

CpuMonitor::CpuMonitor(std::chrono::milliseconds interval)
    : interval_(interval), worker_([this] { run(); }) {}

CpuMonitor::~CpuMonitor() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    worker_.join();
}

std::optional<double> CpuMonitor::latest() const {
    const double v = value_.load();
    if (v < 0.0) {
        return std::nullopt;
    }
    return v;
}

void CpuMonitor::run() {
    std::optional<CpuTimes> prev = read_cpu_times();
    std::unique_lock lock(mutex_);
    while (!wake_.wait_for(lock, interval_, [this] { return stop_; })) {
        std::optional<CpuTimes> now = read_cpu_times();
        if (prev && now) {
            if (auto pct = cpu_percent_between(*prev, *now)) {
                value_.store(*pct);
            }
        }
        prev = now;
    }
}

} //ending gh
