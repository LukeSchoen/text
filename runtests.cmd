@echo off
setlocal

set "BUILD_DIR=%~dp0.build"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

if not exist "%~dp0doCommands.txt" (
  echo Missing runtime asset: %~dp0doCommands.txt
  exit /b 1
)
copy /y "%~dp0doCommands.txt" "%BUILD_DIR%\doCommands.txt" >nul
if errorlevel 1 exit /b 1

echo Building application under test...
"%~dp0cpc.exe" -o "%BUILD_DIR%\text_under_test.exe" "%~dp0main.c" "%~dp0core\core.c" -luser32 -lgdi32 -lcomdlg32 -lshell32 -luxtheme -ldwmapi -lmsimg32
if errorlevel 1 exit /b 1

echo.
echo Building black-box tests...
"%~dp0cpc.exe" -o "%BUILD_DIR%\text_tests.exe" "%~dp0text_blackbox_tests.c" -luser32 -lgdi32
if errorlevel 1 (
  echo Test build failed.
  exit /b 1
)

echo.
if "%~1"=="" (
  echo Running lean default black-box tests...
) else (
  echo Running selected black-box tests...
)
echo.
"%BUILD_DIR%\text_tests.exe" "%BUILD_DIR%\text_under_test.exe" %*
exit /b %errorlevel%
