@echo off
setlocal

if not exist clang.exe (
  echo clang.exe not found in %cd%
  exit /b 1
)

clang.exe -std=c11 -O2 -municode -Wall -Wextra -x c main.c -luser32 -lgdi32 -lcomdlg32 -lshell32 -luxtheme -ldwmapi -lmsimg32 -o text.exe
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
