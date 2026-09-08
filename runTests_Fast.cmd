@echo off
setlocal

set "BUILD_DIR=%~dp0.build"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo Stopping stale fast test process...
taskkill /f /im text_fast_tests.exe >nul 2>nul

echo Building fast backend test harness...
"%~dp0cpc.exe" -o "%BUILD_DIR%\text_fast_tests.exe" "%~dp0text_fast_tests.c" "%~dp0core\core.c" -luser32 -lgdi32 -lcomdlg32 -lshell32 -luxtheme -ldwmapi -lmsimg32
if errorlevel 1 exit /b 1

echo.
echo Running fast backend tests with a generated 300 MiB sparse fixture...
"%BUILD_DIR%\text_fast_tests.exe"
if errorlevel 1 exit /b 1

echo.
echo Fast backend tests complete.
exit /b 0
