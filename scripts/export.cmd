@echo off
rem Makes the zip of a game for Windows: scripts\export.cmd games\doodle_jump\game.json
rem
rem Needs Visual Studio (or the Build Tools, with "Desktop development with C++") and CMake.
rem It builds gectl the first time, then runs `gectl export` with the arguments given here:
rem     scripts\export.cmd games\doodle_jump\game.json --out C:\Envois
rem The zip is in dist\ unless --out says otherwise.
setlocal
cd /d "%~dp0.."

if "%~1"=="" (
  echo Usage: scripts\export.cmd games\NAME\game.json [--out DIR] [--skip-tests]
  exit /b 2
)

where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake was not found. Install it from https://cmake.org/download/ ^(tick "Add CMake to the PATH"^) and try again.
  exit /b 1
)

cmake -S . -B build\tools -DGE_BUILD_TOOLS=ON -DGE_BUILD_TESTS=OFF -DGE_BUILD_EXAMPLES=OFF
if errorlevel 1 (
  echo The tools could not be prepared. Is Visual Studio ^(or its Build Tools with "Desktop development with C++"^) installed?
  exit /b 1
)
cmake --build build\tools --config Release --target gectl
if errorlevel 1 exit /b 1

build\tools\tools\Release\gectl.exe export %*
exit /b %errorlevel%
