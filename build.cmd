@echo off
setlocal

set "BUILD_MODE=%~1"
if not "%~2"=="" goto usage
if "%BUILD_MODE%"=="" goto validate
if /i "%BUILD_MODE%"=="repro" goto validate
goto usage

:validate
if not exist "%~dp0cpc.exe" (
  echo cpc.exe not found in %~dp0
  exit /b 1
)

set "CPC_INSTALL="
for /f "tokens=1,* delims=:" %%A in ('^""%~dp0cpc.exe" -vv ^| findstr /b /c:"install:"^"') do (
  set "CPC_INSTALL=%%B"
)
for /f "tokens=* delims= " %%A in ("%CPC_INSTALL%") do set "CPC_INSTALL=%%A"

if "%CPC_INSTALL%"=="" (
  echo Could not detect cpc install path from "cpc.exe -vv".
  echo Ensure cpc reports a valid install tree with include and lib directories.
  exit /b 1
)

set "CPC_WINAPI_H=%CPC_INSTALL%\include\winapi\windows.h"
if not exist "%CPC_WINAPI_H%" (
  echo Missing WinAPI headers for cpc: "%CPC_WINAPI_H%"
  echo Your current cpc install is missing include\winapi.
  echo This build expects a complete cpc runtime tree ^(headers + import libs^).
  exit /b 1
)

set "CPC_LIB_OK="
if exist "%CPC_INSTALL%\lib\user32.def" set "CPC_LIB_OK=1"
if exist "%CPC_INSTALL%\lib\libuser32.a" set "CPC_LIB_OK=1"
if exist "%CPC_INSTALL%\lib\user32.lib" set "CPC_LIB_OK=1"
if "%CPC_LIB_OK%"=="" (
  echo Could not find user32 import library in "%CPC_INSTALL%\lib".
  echo Expected one of: user32.def, libuser32.a, user32.lib
  exit /b 1
)

if /i "%BUILD_MODE%"=="repro" goto build_repro

"%~dp0cpc.exe" -o "%~dp0text.exe" "%~dp0main.c" "%~dp0core\core.c" -luser32 -lgdi32 -lcomdlg32 -lshell32 -luxtheme -ldwmapi -lmsimg32
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0embed_icon.ps1" "%~dp0text.exe" "%~dp0document.png"
if errorlevel 1 (
  echo Icon embedding failed.
  exit /b 1
)

echo Build succeeded: text.exe
exit /b 0

:build_repro
set "REPRO_DIR=%~dp0.build\repro"
if not exist "%REPRO_DIR%" mkdir "%REPRO_DIR%"

echo Building repro executable...
"%~dp0cpc.exe" -DREPRO_BUILD -o "%REPRO_DIR%\textRepro.exe" "%~dp0main.c" "%~dp0core\core.c" "%~dp0repro_logger.c" -luser32 -lgdi32 -lcomdlg32 -lshell32 -luxtheme -ldwmapi -lmsimg32
if errorlevel 1 (
  echo Repro build failed.
  exit /b 1
)

copy /y "%~dp0doCommands.txt" "%REPRO_DIR%\doCommands.txt" >nul
if errorlevel 1 (
  echo Could not copy doCommands.txt into the repro build.
  exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0embed_icon.ps1" "%REPRO_DIR%\textRepro.exe" "%~dp0document.png"
if errorlevel 1 (
  echo Icon embedding failed.
  exit /b 1
)

if not exist "%REPRO_DIR%\repro_flags.txt" (
  > "%REPRO_DIR%\repro_flags.txt" echo # one per line: wheel vscroll size scrollbar input edit semantic all
  >> "%REPRO_DIR%\repro_flags.txt" echo semantic
)

echo Built: .build\repro\textRepro.exe
echo Configure channels in: .build\repro\repro_flags.txt
echo Log output: .build\repro\log.txt
echo Close screenshot: .build\repro\close_screenshot.bmp
exit /b 0

:usage
echo Usage: build.cmd [repro]
exit /b 2
