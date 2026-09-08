@echo off
setlocal
set "BUILD_DIR=%~dp0.build"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
copy /y "%~dp0doCommands.txt" "%BUILD_DIR%\doCommands.txt" >nul
if errorlevel 1 exit /b 1
"%~dp0cpc.exe" -o "%BUILD_DIR%\text_under_test.exe" "%~dp0main.c" -luser32 -lgdi32 -lcomdlg32 -lshell32 -luxtheme -ldwmapi -lmsimg32
if errorlevel 1 exit /b 1
"%~dp0cpc.exe" -o "%BUILD_DIR%\text_utf8_tests.exe" "%~dp0text_utf8_tests.c" -luser32 -lgdi32 -lcomdlg32 -lshell32 -luxtheme -ldwmapi -lmsimg32
if errorlevel 1 exit /b 1
"%BUILD_DIR%\text_utf8_tests.exe"
if errorlevel 1 exit /b 1
"%~dp0cpc.exe" -o "%BUILD_DIR%\paste_corruption_repro.exe" "%~dp0paste_corruption_repro.c" -luser32 -lgdi32
if errorlevel 1 exit /b 1
pushd "%~dp0"
"%BUILD_DIR%\paste_corruption_repro.exe" "%BUILD_DIR%\text_under_test.exe"
set "TEST_RESULT=%errorlevel%"
popd
exit /b %TEST_RESULT%
