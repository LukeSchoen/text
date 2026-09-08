@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_runner.ps1" Core %*
exit /b %errorlevel%
