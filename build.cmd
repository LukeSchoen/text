@echo off
setlocal

if not exist cpc.exe (
  echo cpc.exe not found in %cd%
  exit /b 1
)

set "CPC_INSTALL="
for /f "tokens=1,* delims=:" %%A in ('cpc.exe -vv ^| findstr /b /c:"install:"') do (
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

cpc.exe -o text.exe main.c -luser32 -lgdi32 -lcomdlg32 -lshell32 -luxtheme -ldwmapi -lmsimg32
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
