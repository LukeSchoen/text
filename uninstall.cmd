@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROGID=TextEditor.Assoc"
set "APP_NAME=text.exe"
set "EDIT_VERB=EditWithText"
set "BACKUP_ROOT=HKCU\Software\TextEditor\Backup"
set "OPEN_EXTENSIONS=.txt .md .markdown .log .ini .json .toml .yaml .yml .xml .csv .c .h .cpp .hpp .cc .hh .cxx .hxx .cs .java .js .jsx .ts .tsx .go .rs .x"
set "EDIT_ONLY_EXTENSIONS=.bat .cmd .ps1 .sh .py .rb"
set "LEGACY_SCRIPT_EXTENSIONS=.bat .cmd .ps1 .sh .py .rb"

for %%E in (%OPEN_EXTENSIONS%) do (
    set "KEY=HKCU\Software\Classes\%%~E"
    set "CURRENT="
    set "BACKUP="
    set "DELETE_KEY="

    for /f "skip=2 tokens=1,2,*" %%A in ('reg query "!KEY!" /ve 2^>nul') do set "CURRENT=%%C"
    for /f "skip=2 tokens=1,2,*" %%A in ('reg query "%BACKUP_ROOT%\%%~E" /ve 2^>nul') do set "BACKUP=%%C"

    if /I "!CURRENT!"=="%PROGID%" (
        if /I "!BACKUP!"=="__NONE__" (
            reg delete "!KEY!" /ve /f >nul 2>nul
            set "DELETE_KEY=1"
        ) else if defined BACKUP (
            reg add "!KEY!" /ve /d "!BACKUP!" /f >nul
        ) else (
            reg delete "!KEY!" /ve /f >nul 2>nul
            set "DELETE_KEY=1"
        )
    )

    reg delete "%BACKUP_ROOT%\%%~E" /f >nul 2>nul
    reg delete "!KEY!\OpenWithProgids" /v "%PROGID%" /f >nul 2>nul
    reg delete "!KEY!\OpenWithList\%APP_NAME%" /f >nul 2>nul
    reg delete "HKCU\Software\Classes\Applications\%APP_NAME%\SupportedTypes" /v "%%~E" /f >nul 2>nul
    if defined DELETE_KEY (
        reg delete "!KEY!" /f >nul 2>nul
    )
)

for %%E in (%EDIT_ONLY_EXTENSIONS%) do (
    reg delete "HKCU\Software\Classes\SystemFileAssociations\%%~E\shell\%EDIT_VERB%" /f >nul 2>nul
)

for %%E in (%LEGACY_SCRIPT_EXTENSIONS%) do (
    set "KEY=HKCU\Software\Classes\%%~E"
    set "CURRENT="
    set "BACKUP="

    for /f "skip=2 tokens=1,2,*" %%A in ('reg query "!KEY!" /ve 2^>nul') do set "CURRENT=%%C"
    for /f "skip=2 tokens=1,2,*" %%A in ('reg query "%BACKUP_ROOT%\%%~E" /ve 2^>nul') do set "BACKUP=%%C"

    if defined BACKUP (
        if /I "!BACKUP!"=="__NONE__" (
            reg delete "!KEY!" /f >nul 2>nul
        ) else (
            reg add "!KEY!" /ve /d "!BACKUP!" /f >nul
        )
        reg delete "%BACKUP_ROOT%\%%~E" /f >nul 2>nul
    ) else (
        reg delete "!KEY!" /f >nul 2>nul
    )
)

reg delete "HKCU\Software\Classes\%PROGID%" /f >nul 2>nul
reg delete "HKCU\Software\Classes\Applications\%APP_NAME%" /f >nul 2>nul

echo Removed file associations and script edit actions managed by install.cmd
echo You may need to restart Explorer or reopen File Explorer windows to see every change immediately.
exit /b 0
