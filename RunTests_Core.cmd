@echo off
setlocal
if not exist "%~dp0.build" mkdir "%~dp0.build"
"%~dp0cpc.exe" -o "%~dp0.build\core_tests.exe" "%~dp0core\core_tests.c" "%~dp0core\core.c"
if errorlevel 1 exit /b 1
if not "%~1"=="" (
  "%~dp0.build\core_tests.exe" "%~1"
) else if exist "%~dp0test.txt" (
  "%~dp0.build\core_tests.exe" "%~dp0test.txt"
) else (
  "%~dp0.build\core_tests.exe"
)
exit /b %errorlevel%
