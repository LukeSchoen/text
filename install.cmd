@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "EXE=%SCRIPT_DIR%text.exe"
set "APP_NAME=text.exe"
set "PROGID=TextEditor.Assoc"
set "EDIT_VERB=EditWithText"
set "BACKUP_ROOT=HKCU\Software\TextEditor\Backup"
set "OPEN_EXTENSIONS=.txt .md .markdown .log .ini .json .toml .yaml .yml .xml .csv .c .h .cpp .hpp .cc .hh .cxx .hxx .cs .java .js .jsx .ts .tsx .go .rs .x"
set "EDIT_ONLY_EXTENSIONS=.bat .cmd .ps1 .sh .py .rb"

if not exist "%EXE%" (
    echo text.exe not found next to install.cmd
    exit /b 1
)

reg add "HKCU\Software\Classes\%PROGID%" /ve /d "Text" /f >nul
reg add "HKCU\Software\Classes\%PROGID%\DefaultIcon" /ve /d "\"%EXE%\",0" /f >nul
reg add "HKCU\Software\Classes\%PROGID%\shell\open\command" /ve /d "\"%EXE%\" \"%%1\"" /f >nul
reg add "HKCU\Software\Classes\Applications\%APP_NAME%" /v "FriendlyAppName" /d "Text" /f >nul
reg add "HKCU\Software\Classes\Applications\%APP_NAME%\DefaultIcon" /ve /d "\"%EXE%\",0" /f >nul
reg add "HKCU\Software\Classes\Applications\%APP_NAME%\shell\open\command" /ve /d "\"%EXE%\" \"%%1\"" /f >nul

for %%E in (%OPEN_EXTENSIONS%) do (
    set "KEY=HKCU\Software\Classes\%%~E"
    set "CURRENT="
    for /f "skip=2 tokens=1,2,*" %%A in ('reg query "!KEY!" /ve 2^>nul') do set "CURRENT=%%C"
    if not defined CURRENT (
        reg add "%BACKUP_ROOT%\%%~E" /ve /d "__NONE__" /f >nul
    ) else if /I not "!CURRENT!"=="%PROGID%" (
        reg add "%BACKUP_ROOT%\%%~E" /ve /d "!CURRENT!" /f >nul
    )
    reg add "!KEY!" /ve /d "%PROGID%" /f >nul
    reg add "!KEY!\OpenWithProgids" /v "%PROGID%" /t REG_NONE /d "" /f >nul
    reg add "!KEY!\OpenWithList\%APP_NAME%" /f >nul
    reg add "HKCU\Software\Classes\Applications\%APP_NAME%\SupportedTypes" /v "%%~E" /t REG_NONE /d "" /f >nul
)

for %%E in (%EDIT_ONLY_EXTENSIONS%) do (
    reg add "HKCU\Software\Classes\SystemFileAssociations\%%~E\shell\%EDIT_VERB%" /ve /d "Edit with Text" /f >nul
    reg add "HKCU\Software\Classes\SystemFileAssociations\%%~E\shell\%EDIT_VERB%" /v "Icon" /d "\"%EXE%\",0" /f >nul
    reg add "HKCU\Software\Classes\SystemFileAssociations\%%~E\shell\%EDIT_VERB%\command" /ve /d "\"%EXE%\" \"%%1\"" /f >nul
)

echo Installed default file associations for document files.
echo Added an "Edit with Text" context-menu action for script files.
echo On modern Windows, fully forcing the default app may still require one user confirmation in Default Apps or the Open With dialog.
echo You may need to restart Explorer or reopen File Explorer windows to see every change immediately.
exit /b 0
