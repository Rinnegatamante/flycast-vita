@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0capture-vita-profile.ps1" %*
exit /b %errorlevel%
