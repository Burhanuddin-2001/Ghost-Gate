#include "process_manager.hpp"
#include "win_handle.hpp"   // our RAII UniqueHandle

#include <windows.h>
#include <tlhelp32.h>       // CreateToolhelp32Snapshot, Process32First/Next

namespace gh {

std::vector<ProcessInfo> ProcessManager::list() const {
    std::vector<ProcessInfo> result;

    // 1. Take the group photo of all processes.
    //    TH32CS_SNAPPROCESS = "include processes in the snapshot".
    HANDLE rawSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (rawSnap == INVALID_HANDLE_VALUE) {
        return result;   // No Snapshot = empty list
    }

    // 2. Wrap the ticket at once, so it always auto-closes (RAII).
    UniqueHandle snap(rawSnap);

    // 3. Prepare an empty card. Windows REQUIRES we set dwSize first.
    PROCESSENTRY32W entry{};            // {} zero-fills it
    entry.dwSize = sizeof(entry);       // telling how big is the card.

    // 4. Look at the first face. If none, stop.
    if (!Process32FirstW(snap.get(), &entry)) {
        return result;
    }

    // 5. Flip through every face until there are no more.
    do {
        ProcessInfo info;
        info.pid  = entry.th32ProcessID;     // the PID number
        info.name = entry.szExeFile;         // the exe name (wide text)
        result.push_back(info);              // add it to our list
    } while (Process32NextW(snap.get(), &entry));

    return result;   // snap dies here -> handle auto-closed
}

std::vector<ProcessInfo> ProcessManager::find(const std::wstring& name) const {
    std::vector<ProcessInfo> matches;
    for (const ProcessInfo& p : list()) {
        // _wcsicmp = compare wide text, ignore case. Returns 0 when equal.
        if (_wcsicmp(p.name.c_str(), name.c_str()) == 0) {
            matches.push_back(p);
        }
    }
    return matches;
}

bool ProcessManager::kill(uint32_t pid) const {
    // 1. Ask Windows for a ticket with 2 small rights: stop it AND wait on it.
    //    '|' joins the rights. OpenProcess returns NULL when it fails.
    UniqueHandle proc(OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid));
    if (!proc) {
        return false;   // no such PID, or access denied
    }

    // 2. Start the shutdown. Exit code 1 = "it was killed, not a normal exit".
    if (!TerminateProcess(proc.get(), 1)) {
        return false;
    }

    // 3. Wait until it is truly gone (max 5 s).
    //    WAIT_OBJECT_0 = it ended. WAIT_TIMEOUT = still closing after 5 s.
    return WaitForSingleObject(proc.get(), 5000) == WAIT_OBJECT_0;
}   // proc dies here -> ticket auto-closed

uint32_t ProcessManager::launch(const std::wstring& exePath) const {
    // 1. CreateProcessW may WRITE into the command line, so it needs our own
    //    changeable copy. Quotes keep paths with spaces in one piece.
    std::wstring cmdLine = L"\"" + exePath + L"\"";

    // 2. Two forms Windows wants. {} zero-fills them.
    STARTUPINFOW si{};          // IN:  how to start (window look, etc.)
    si.cb = sizeof(si);         // Windows REQUIRES the size, like dwSize before
    PROCESS_INFORMATION pi{};   // OUT: Windows fills in the 2 tickets + PID

    // 3. Start it.
    BOOL ok = CreateProcessW(
        nullptr,          // app name: none, it is in the command line
        cmdLine.data(),   // command line (changeable buffer)
        nullptr, nullptr, // default security for process and thread
        FALSE,            // do not pass our handles to the child
        0,                // no special flags
        nullptr,          // use our environment variables
        nullptr,          // use our current folder
        &si, &pi);
    if (!ok) {
        return 0;         // failed: bad path, no rights, ...
    }

    // 4. Wrap BOTH tickets at once, so neither can leak.
    UniqueHandle process(pi.hProcess);
    UniqueHandle thread(pi.hThread);

    return pi.dwProcessId;
}   // thread and process die here -> both tickets auto-closed.
    // The PROGRAM keeps running. Closing a ticket does not stop it.

uint32_t ProcessManager::restart(const std::wstring& name,
                                 const std::wstring& exePath) const {
    // 1. Kill every copy. Empty list -> this loop runs 0 times.
    for (const ProcessInfo& p : find(name)) {
        if (!kill(p.pid)) {
            return 0;   // one copy will not die -> do NOT launch a second one
        }
    }

    // 2. All old copies are gone (or there were none). Start fresh.
    return launch(exePath);
}

} //ending gh namespace block
