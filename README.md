# GhostGate (In-Development)

Personal remote access gateway for Windows, using WebRTC DataChannels and UDP hole punching to give remote process control and monitoring from any network without any subscriptions.

## The problem I am trying to fix with it
Remote desktop apps (UltraViewer, TeamViewer) crash on an unattended PC.
- I lose all remote access.
- I cannot restart the app from far away.
- After restart, the app shows a NEW random ID and password ON the screen.
- So I must also SEE the screen to read the new login.

GhostGate fixes this: a second, independent control channel to the PC.

## What it will do (final features)
1. Show if the target app (e.g. UltraViewer.exe) is Running or Dead.
2. Show live CPU % and RAM % of the PC.
3. Button: Restart App (kill it, then launch it in the real desktop so its window shows).
4. Button: Grab Screen (take a picture of the desktop, send to phone).
5. All of this from my phone, even on 4G/5G, far from home.
6. Protected by a shared password. No password = dropped.

## Working Principles
- RAII + smart pointers for all Windows handles and memory. No leaks.
- Zero hosting cost (goal). STUN servers discover each peer's public address and ICE connectivity checks handle the hole-punching.
- Launch GUI apps in the active desktop (this avoid Session 0 service isolation).
- (P2P over internet), traffic is encrypted: WebRTC DataChannels use DTLS end-to-end.

## Tech stack
- Language: C++20
- Build (now): build.bat (finds Visual Studio with vswhere, then runs cl)
- Build (later): CMake (>= 3.20) + vcpkg (package manager)
- OS: Windows 10/11 64-bit
- Process/system: Win32 APIs (windows.h, tlhelp32.h, wtsapi32.h, psapi.h)
- Screen: Desktop Duplication API (DXGI), fallback GDI BitBlt
- Image compress: libwebp or stb_image_write (in memory, no temp files)
- Networking P2P: libdatachannel (WebRTC DataChannels + WebSockets)
- JSON: nlohmann/json
- Frontend: one HTML page + JS + CSS

## What works now

```
ghostgate.exe                          → report if a named process is RUNNING or DEAD
ghostgate.exe kill <PID>               → kill that one process
ghostgate.exe launch <path>            → start a program, print its PID
ghostgate.exe restart <name> <path>    → kill all copies, launch fresh
ghostgate.exe ram                      → print RAM % in use
ghostgate.exe screenshot <jpg> [width] → save the whole screen as a JPEG file
```

## How to build (now)
Requirements:
- Windows 10 or 11, 64-bit.
- Visual Studio 2019 (16.11) or newer, any edition, with the
  "Desktop development with C++" workload.

Steps:
1. Open a terminal in the project folder.
2. Run `.\build.bat`
3. It builds `build\GhostGate.exe` and runs it once.

Note: build.bat uses vswhere.exe to find the newest Visual Studio with the C++
compiler. If it finds none, it prints an error and stops.

## Build order (phases)
1. C++ memory, RAII, smart pointers.
2. ProcessManager: list, kill, launch apps.
3. Telemetry: read CPU and RAM. (CPU is pending)
4. ScreenCapture: grab screen to memory, compress. (Currently here.)
5. Local web dashboard: control from phone on same Wi-Fi.
6. P2P over internet: STUN / WebRTC.
