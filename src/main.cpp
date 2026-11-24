#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>
#include "process_manager.hpp"
#include "screen_capture.hpp"
#include "telemetry.hpp"

// wmain = main that takes wide text arguments (Windows names are wide).
int wmain(int argc, wchar_t* argv[]) {
    // Capture real pixels on high-DPI screens instead of a scaled-down image.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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
