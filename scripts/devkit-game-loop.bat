@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0devkit-game-loop.ps1" %*
exit /b %errorlevel%
