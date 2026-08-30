@echo off
setlocal

where pwsh >nul 2>&1
if not errorlevel 1 (
  pwsh -NoProfile -File "%~dp0build-vita.ps1" %*
  exit /b %errorlevel%
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-vita.ps1" %*
exit /b %errorlevel%
