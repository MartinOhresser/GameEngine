@echo off
rem Opens the launcher: scripts\launcher.cmd (the same as gameEngine.cmd, at the root).
call "%~dp0..\gameEngine.cmd" %*
exit /b %errorlevel%
