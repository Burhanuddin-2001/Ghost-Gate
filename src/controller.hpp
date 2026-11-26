// The actions GhostGate offers (status, restart, screenshot), independent of how a request arrived.

#pragma once

#include "process_manager.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gh {

struct Target {
    std::wstring name;   // process name to watch; "" = no target configured
    std::wstring path;   // executable to start on Restart; "" = restart disabled
};

// External effects are were used to test fake substitute.
struct AppServices {
    std::function<std::optional<double>()> cpu_percent;
    std::function<int()>                   ram_percent;   // -1 = unknown
    std::function<std::optional<std::vector<uint8_t>>(int max_width)> screenshot_jpeg;
};

class Controller {
public:
    Controller(Target target, const ProcessManager& processes, AppServices services);

    // {"target":{...}|null,"cpu":..,"ram":..,"uptime_s":..}
    std::string status_json() const;

    struct RestartResult {
        int         status = 200;   // HTTP-style code: 200, 400 (no target) or 500 (internal error)
        uint32_t    pid = 0;
        std::string error;
    };
    RestartResult restart();

    // JPEG bytes, or nullopt if capture failed. One capture at a time.
    std::optional<std::vector<uint8_t>> screenshot(int max_width);

    // Default width when the client does not ask for one.
    static constexpr int kDefaultWidth = 1280;
    // 0 stays 0 (full size); anything else is clamped to 320..3840.
    static int clamp_width(int requested);

private:
    Target                target_;
    const ProcessManager& processes_;
    AppServices           services_;
    std::mutex            restart_mutex_;
    std::mutex            screenshot_mutex_;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
};

} // namespace gh
