#include "controller.hpp"
#include "util.hpp"

#include <algorithm>
#include <cstdio>

namespace gh {

Controller::Controller(Target target, const ProcessManager& processes, AppServices services)
    : target_(std::move(target)), processes_(processes), services_(std::move(services)) {}

std::string Controller::status_json() const {
    std::string target = "null";
    if (!target_.name.empty()) {
        const auto found = processes_.find(target_.name);
        std::string pids = "[";
        for (size_t i = 0; i < found.size(); ++i) {
            if (i) pids += ",";
            pids += std::to_string(found[i].pid);
        }
        pids += "]";
        target = "{\"name\":" + json_string(to_utf8(target_.name)) +
                 ",\"running\":" + (found.empty() ? "false" : "true") +
                 ",\"pids\":" + pids +
                 ",\"can_restart\":" + (target_.path.empty() ? "false" : "true") + "}";
    }

    const auto cpu = services_.cpu_percent ? services_.cpu_percent() : std::nullopt;
    const int ram = services_.ram_percent ? services_.ram_percent() : -1;
    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started_).count();

    std::string cpu_text = "null";
    if (cpu) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", *cpu);
        cpu_text = buf;
    }
    return "{\"target\":" + target +
           ",\"cpu\":" + cpu_text +
           ",\"ram\":" + (ram >= 0 ? std::to_string(ram) : "null") +
           ",\"uptime_s\":" + std::to_string(uptime) + "}";
}

Controller::RestartResult Controller::restart() {
    if (target_.name.empty() || target_.path.empty()) {
        return {400, 0, "no restart target configured (start with --target-path)"};
    }
    std::lock_guard lock(restart_mutex_);
    const uint32_t pid = processes_.restart(target_.name, target_.path);
    if (pid == 0) {
        return {500, 0, "restart failed (access denied, or the program could not start)"};
    }
    return {200, pid, {}};
}

std::optional<std::vector<uint8_t>> Controller::screenshot(int max_width) {
    if (!services_.screenshot_jpeg) {
        return std::nullopt;
    }
    std::lock_guard lock(screenshot_mutex_);
    return services_.screenshot_jpeg(clamp_width(max_width));
}

int Controller::clamp_width(int requested) {
    return requested == 0 ? 0 : std::clamp(requested, 320, 3840);
}

} // namespace gh
