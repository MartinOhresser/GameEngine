@echo off
rem Opens the launcher: scripts\launcher.cmd
rem
rem Needs Visual Studio (or the Build Tools, with "Desktop development with C++") and CMake.
rem The first time it builds the launcher, which takes a few minutes.
setlocal
cd /d "%~dp0.."

where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake was not found. Install it from https://cmake.org/download/ ^(tick "Add CMake to the PATH"^) and try again.
  exit /b 1
)

cmake -S . -B build\platform -DGE_BUILD_PLATFORM=ON -DGE_BUILD_TESTS=OFF -DGE_BUILD_EXAMPLES=OFF
if errorlevel 1 (
  echo The launcher could not be prepared. Is Visual Studio ^(or its Build Tools with "Desktop development with C++"^) installed?
  exit /b 1
)
cmake --build build\platform --config Release --target gelauncher
if errorlevel 1 exit /b 1

build\platform\platform\sdl\Release\gelauncher.exe %*
exit /b %errorlevel%
