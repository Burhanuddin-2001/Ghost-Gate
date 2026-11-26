#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include "app.hpp"
#include "controller.hpp"
#include "http_server.hpp"
#include "process_manager.hpp"
#include "screen_capture.hpp"
#include "telemetry.hpp"
#include "util.hpp"

namespace {

// Set by Ctrl+C / console close, so `serve` can shut down cleanly.
std::atomic<bool>       g_stop{false};
std::mutex              g_stop_mutex;
std::condition_variable g_stop_cond;

BOOL WINAPI on_console_event(DWORD) {
    {
        std::lock_guard lock(g_stop_mutex);
        g_stop = true;
    }
    g_stop_cond.notify_all();
    return TRUE;
}

std::wstring exe_directory() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return L".";
        if (n < path.size()) { path.resize(n); break; }
        path.resize(path.size() * 2);
    }
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

// serve [--port <n>] [--bind <ipv4>] [--target <name.exe>] [--target-path <exe>] [--web-dir <dir>]
int run_serve(int argc, wchar_t* argv[]) {
    std::string  bind = "127.0.0.1";   // no password, only PC by default
    uint16_t     port = 8080;
    std::wstring target_name, target_path, web_dir;

    for (int i = 2; i < argc; i += 2) {
        const std::wstring flag = argv[i];
        if (i + 1 >= argc) {
            std::wcout << L"Missing value for " << flag << L".\n";
            return 2;
        }
        const std::wstring value = argv[i + 1];
        if (flag == L"--port") {
            wchar_t* end = nullptr;
            const unsigned long n = std::wcstoul(value.c_str(), &end, 10);
            if (value.empty() || *end != L'\0' || n < 1 || n > 65535) {
                std::wcout << L"--port must be a number from 1 to 65535.\n";
                return 2;
            }
            port = static_cast<uint16_t>(n);
        } else if (flag == L"--bind") {
            bind = gh::to_utf8(value);
        } else if (flag == L"--target") {
            target_name = value;
        } else if (flag == L"--target-path") {
            target_path = value;
        } else if (flag == L"--web-dir") {
            web_dir = value;
        } else {
            std::wcout << L"Unknown option " << flag << L".\n";
            return 2;
        }
    }
    if (target_name.empty() && !target_path.empty()) {
        target_name = target_path.substr(target_path.find_last_of(L"\\/") + 1);   // file name part
    }
    if (web_dir.empty()) {
        web_dir = exe_directory() + L"\\web";
    }

    std::string error;
    auto files = gh::load_static_files(web_dir, error);
    if (!files) {
        std::wcout << L"Error: " << gh::from_utf8(error) << L"\n";
        return 2;
    }

    gh::ProcessManager processes;
    gh::CpuMonitor cpu;

    gh::AppServices services;
    services.cpu_percent = [&cpu] { return cpu.latest(); };
    services.ram_percent = [] { return gh::get_ram_percent(); };
    services.screenshot_jpeg = [](int max_width) -> std::optional<std::vector<uint8_t>> {
        std::string why;
        auto shot = gh::capture_screen_jpeg(max_width, 0.7f, &why);
        if (!shot) {
            std::fprintf(stderr, "screenshot failed: %s\n", why.c_str());
            return std::nullopt;
        }
        return std::move(shot->jpeg);
    };

    gh::Controller controller(gh::Target{target_name, target_path}, processes, std::move(services));
    gh::App app(std::move(*files), controller);

    gh::http::Server::Options options;
    options.bind_address = bind;
    options.port = port;
    gh::http::Server server([&app](const gh::http::Request& r) { return app.handle(r); }, options);
    if (!server.start(error)) {
        std::wcout << L"Error: " << gh::from_utf8(error) << L"\n";
        return 1;
    }

    const std::wstring port_text = std::to_wstring(server.port());
    if (bind == "0.0.0.0") {
        std::wcout << L"Dashboard: http://127.0.0.1:" << port_text << L"/ on this PC,\n"
                   << L"           http://<this PC's IP>:" << port_text << L"/ from the same Wi-Fi.\n"
                   << L"Warning: there is no password yet. Anyone on this network can use it.\n";
    } else {
        std::wcout << L"Dashboard: http://" << gh::from_utf8(bind) << L":" << port_text << L"/\n";
    }
    std::wcout << (target_name.empty() ? L"Target: none (use --target-path to enable Restart)"
                                       : L"Target: " + target_name) << L"\n";
    std::wcout << L"Press Ctrl+C to stop.\n";

    SetConsoleCtrlHandler(on_console_event, TRUE);
    {
        std::unique_lock lock(g_stop_mutex);
        g_stop_cond.wait(lock, [] { return g_stop.load(); });
    }
    std::wcout << L"Stopping...\n";
    server.stop();
    return 0;
}

} // namespace

// wmain = main that takes wide text arguments (Windows names are wide).
int wmain(int argc, wchar_t* argv[]) {
    // Capture real pixels on high-DPI screens instead of a scaled-down image.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (argc >= 2 && std::wstring(argv[1]) == L"serve") {
        return run_serve(argc, argv);
    }

    gh::ProcessManager pm;

    // screenshot <file.jpg> [max-width]
    if ((argc == 3 || argc == 4) && std::wstring(argv[1]) == L"screenshot") {
        int max_width = (argc == 4) ? std::stoi(argv[3]) : 0;
        std::string why;
        auto shot = gh::capture_screen_jpeg(max_width, 0.8f, &why);
        if (!shot) {
            std::wcout << L"Screenshot failed: " << std::wstring(why.begin(), why.end()) << L"\n";
            return 1;
        }
        std::ofstream file(argv[2], std::ios::binary);
        file.write(reinterpret_cast<const char*>(shot->jpeg.data()),
                   static_cast<std::streamsize>(shot->jpeg.size()));
        if (!file) {
            std::wcout << L"Could not write " << argv[2] << L".\n";
            return 1;
        }
        std::wcout << L"Saved " << shot->width << L"x" << shot->height << L" screenshot ("
                   << shot->jpeg.size() << L" bytes) to " << argv[2] << L".\n";
        return 0;
    }

    if (argc == 3) {
        std::wstring cmd = argv[1];

        if (cmd == L"kill") {
            uint32_t pid = static_cast<uint32_t>(std::stoul(argv[2]));
            if (pm.kill(pid)) {
                std::wcout << L"Killed PID " << pid << L".\n";
            } else {
                std::wcout << L"Could not kill PID " << pid << L".\n";
            }
            return 0;
        }

        if (cmd == L"launch") {
            uint32_t pid = pm.launch(argv[2]);
            if (pid != 0) {
                std::wcout << L"Launched. New PID " << pid << L".\n";
            } else {
                std::wcout << L"Could not launch " << argv[2] << L".\n";
            }
            return 0;
        }
    }

    if (argc == 2 && std::wstring(argv[1]) == L"ram") {
        int ram = gh::get_ram_percent();
        if (ram != -1) {
            std::wcout << L"RAM in use: " << ram << L" %\n";
        } else {
            std::wcout << L"Could not read RAM.\n";
        }
        return 0;
    }

    if (argc == 2 && std::wstring(argv[1]) == L"cpu") {
        // CPU % = busy time between two samples, one second apart.
        const auto before = gh::read_cpu_times();
        Sleep(1000);
        const auto after = gh::read_cpu_times();
        const auto pct = (before && after) ? gh::cpu_percent_between(*before, *after) : std::nullopt;
        if (pct) {
            wchar_t text[64];
            std::swprintf(text, 64, L"CPU in use: %.1f %%\n", *pct);
            std::wcout << text;
        } else {
            std::wcout << L"Could not read CPU.\n";
        }
        return 0;
    }

    if (argc == 4 && std::wstring(argv[1]) == L"restart") {
        uint32_t pid = pm.restart(argv[2], argv[3]);
        if (pid != 0) {
            std::wcout << L"Restarted. New PID " << pid << L".\n";
        } else {
            std::wcout << L"Restart failed.\n";
        }
        return 0;
    }

    // For manually testing (Change this name to test other programs)
    const std::wstring target = L"explorer.exe";

    std::vector<gh::ProcessInfo> found = pm.find(target);

    if (found.empty()) {
        std::wcout << target << L" is DEAD (not running).\n";
    } else {
        std::wcout << target << L" is RUNNING. Copies: " << found.size() << L"\n";
        for (const gh::ProcessInfo& p : found) {
            std::wcout << L"  PID " << p.pid << L"\n";
        }
    }
    return 0;
}
