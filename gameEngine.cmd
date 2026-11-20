@echo off
rem Builds the engine and opens the launcher (Windows):  gameEngine.cmd [arguments of gelauncher]
rem
rem   gameEngine.cmd                       build (the first time takes a few minutes), then open
rem   gameEngine.cmd --headless --frames 5 any argument goes to the launcher
rem   gameEngine.cmd --build-only          build and stop
rem
rem Needs CMake and Visual Studio (or its Build Tools, with "Desktop development with C++").
rem A plain prompt is enough: CMake finds Visual Studio by itself. The build goes to build\launcher.
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "build_only=0"
set "launcher_args="
:parse
if "%~1"=="" goto parsed
if /i "%~1"=="--build-only" (
  set "build_only=1"
) else (
  set launcher_args=!launcher_args! %1
)
shift
goto parse
:parsed

where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake was not found. Install it from https://cmake.org/download/ ^(tick "Add CMake to the PATH"^) and try again.
  exit /b 1
)

rem MSBuild compiles the files of a project one by one unless it is told to use every core.
cmake -S . -B build\launcher --log-level=WARNING -DGE_BUILD_PLATFORM=ON -DGE_BUILD_TOOLS=ON -DGE_BUILD_TESTS=OFF -DGE_BUILD_EXAMPLES=OFF "-DCMAKE_VS_GLOBALS=UseMultiToolTask=true;EnforceProcessCountAcrossBuilds=true"
if errorlevel 1 (
  echo The build could not be prepared. Is Visual Studio ^(or its Build Tools with "Desktop development with C++"^) installed?
  exit /b 1
)
cmake --build build\launcher --config Release --target gelauncher --parallel
if errorlevel 1 exit /b 1

rem Visual Studio puts the program in a Release folder, other generators next to the libraries.
set "launcher="
for %%f in (build\launcher\platform\sdl\Release\gelauncher.exe build\launcher\platform\sdl\gelauncher.exe) do (
  if not defined launcher if exist "%%f" set "launcher=%%f"
)
if not defined launcher (
  echo The build worked but gelauncher.exe was not found in build\launcher\platform\sdl.
  exit /b 1
)

if "%build_only%"=="1" (
  echo Built: !launcher!
  exit /b 0
)
"!launcher!" !launcher_args!
exit /b %errorlevel%
