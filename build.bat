@echo off
setlocal
cd /d "%~dp0"

rem vswhere.exe comes with every Visual Studio 2017 or newer, always in this folder.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio not found.
    echo Install Visual Studio with the "Desktop development with C++" workload.
    exit /b 1
)

rem Ask vswhere for the newest Visual Studio that has the C++ compiler.
set "VSPATH="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo ERROR: Visual Studio has no C++ compiler.
    echo Add the "Desktop development with C++" workload in Visual Studio Installer.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist build mkdir build
cl /nologo /std:c++20 /EHsc /W4 src\main.cpp src\process_manager.cpp src\telemetry.cpp src\screen_capture.cpp /Fe:build\ghostgate.exe /Fo:build\ /link windowscodecs.lib ole32.lib oleaut32.lib gdi32.lib user32.lib
if errorlevel 1 (
    echo --- build FAILED, see errors above ---
    exit /b 1
)
echo --- build OK, running ---
build\ghostgate.exe
