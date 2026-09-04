@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "EXE=%SCRIPT_DIR%text.exe"
set "PROGID=TextEditor.Assoc"
set "APP_NAME=text.exe"
set "EDIT_VERB=EditWithText"
set "BACKUP_ROOT=HKCU\Software\TextEditor\Backup"
set "OPEN_EXTENSIONS=.txt .md .markdown .log .ini .json .toml .yaml .yml .xml .csv .c .h .cpp .hpp .cc .x"
set "EDIT_ONLY_EXTENSIONS=.bat .cmd .ps1 .sh .py .rb"
set "LEGACY_SCRIPT_EXTENSIONS=.bat .cmd .ps1 .sh .py .rb"
set "WINDOWS_EDIT_PROGIDS=batfile cmdfile"

echo Removing document file associations...
for %%E in (%OPEN_EXTENSIONS%) do (
    echo Removing %%~E...
    set "KEY=HKCU\Software\Classes\%%~E"
    set "EXPLORER_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\%%~E"
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
    reg delete "!EXPLORER_KEY!\OpenWithProgids" /v "%PROGID%" /f >nul 2>nul
    reg delete "!EXPLORER_KEY!\OpenWithList" /v "%APP_NAME%" /f >nul 2>nul
    call :RemoveExplorerOpenWith "%%~E"
    reg delete "HKCU\Software\Classes\Applications\%APP_NAME%\SupportedTypes" /v "%%~E" /f >nul 2>nul
    reg delete "HKCU\Software\Classes\Applications\%APP_NAME%\Capabilities\FileAssociations" /v "%%~E" /f >nul 2>nul
    if defined DELETE_KEY (
        reg delete "!KEY!" /f >nul 2>nul
    )
)

for %%E in (%EDIT_ONLY_EXTENSIONS%) do (
    echo Removing edit action for %%~E...
    reg delete "HKCU\Software\Classes\SystemFileAssociations\%%~E\shell\%EDIT_VERB%" /f >nul 2>nul
)

for %%P in (%WINDOWS_EDIT_PROGIDS%) do (
    echo Restoring default Edit command for %%~P...
    call :RemoveDefaultEditCommand "%%~P"
)

for %%E in (%LEGACY_SCRIPT_EXTENSIONS%) do (
    echo Removing legacy association backup for %%~E...
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

echo Removing Text application registration...
reg delete "HKCU\Software\Classes\%PROGID%" /f >nul 2>nul
reg delete "HKCU\Software\Classes\Applications\%APP_NAME%" /f >nul 2>nul
reg delete "HKCU\Software\RegisteredApplications" /v "Text" /f >nul 2>nul

echo Removed file associations and script edit actions managed by install.cmd
echo You may need to restart Explorer or reopen File Explorer windows to see every change immediately.
exit /b 0

:RemoveExplorerOpenWith
set "REMOVE_EXT=%~1"
set "OPENWITH_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\%REMOVE_EXT%\OpenWithList"

for %%L in (a b c d e f g h i j k l m n o p q r s t u v w x y z) do (
    set "OPENWITH_VALUE="
    for /f "skip=2 tokens=1,2,*" %%A in ('reg query "%OPENWITH_KEY%" /v %%L 2^>nul') do set "OPENWITH_VALUE=%%C"
    if /I "!OPENWITH_VALUE!"=="%APP_NAME%" (
        set "OPENWITH_MRU="
        for /f "skip=2 tokens=1,2,*" %%A in ('reg query "%OPENWITH_KEY%" /v MRUList 2^>nul') do set "OPENWITH_MRU=%%C"
        reg delete "%OPENWITH_KEY%" /v %%L /f >nul 2>nul
        if /I "!OPENWITH_MRU!"=="%%L" reg delete "%OPENWITH_KEY%" /v MRUList /f >nul 2>nul
    )
)
exit /b 0

:RemoveDefaultEditCommand
set "EDIT_PROGID=%~1"
set "EDIT_KEY=HKCU\Software\Classes\%EDIT_PROGID%\shell\edit"
set "EDIT_COMMAND_KEY=%EDIT_KEY%\command"
set "EDIT_BACKUP_KEY=%BACKUP_ROOT%\DefaultEdit\%EDIT_PROGID%"
set "TEXT_EDIT_COMMAND=\"%EXE%\" \"%%1\""
set "TEXT_EDIT_ICON=\"%EXE%\",0"
set "CURRENT_EDIT_COMMAND="
set "CURRENT_EDIT_ICON="
set "BACKUP_EDIT_COMMAND="
set "BACKUP_EDIT_ICON="

for /f "skip=2 tokens=1,2,*" %%A in ('reg query "%EDIT_COMMAND_KEY%" /ve 2^>nul') do set "CURRENT_EDIT_COMMAND=%%C"
for /f "skip=2 tokens=1,2,*" %%A in ('reg query "%EDIT_KEY%" /v "Icon" 2^>nul') do set "CURRENT_EDIT_ICON=%%C"
for /f "skip=2 tokens=1,2,*" %%A in ('reg query "%EDIT_BACKUP_KEY%" /ve 2^>nul') do set "BACKUP_EDIT_COMMAND=%%C"
for /f "skip=2 tokens=1,2,*" %%A in ('reg query "%EDIT_BACKUP_KEY%" /v "Icon" 2^>nul') do set "BACKUP_EDIT_ICON=%%C"

if /I "!CURRENT_EDIT_COMMAND!"=="!TEXT_EDIT_COMMAND!" (
    if /I "!BACKUP_EDIT_COMMAND!"=="__NONE__" (
        reg delete "%EDIT_COMMAND_KEY%" /f >nul 2>nul
    ) else if defined BACKUP_EDIT_COMMAND (
        reg add "%EDIT_COMMAND_KEY%" /ve /d "!BACKUP_EDIT_COMMAND!" /f >nul
    ) else (
        reg delete "%EDIT_COMMAND_KEY%" /f >nul 2>nul
    )
)

if /I "!CURRENT_EDIT_ICON!"=="!TEXT_EDIT_ICON!" (
    if /I "!BACKUP_EDIT_ICON!"=="__NONE__" (
        reg delete "%EDIT_KEY%" /v "Icon" /f >nul 2>nul
    ) else if defined BACKUP_EDIT_ICON (
        reg add "%EDIT_KEY%" /v "Icon" /d "!BACKUP_EDIT_ICON!" /f >nul
    ) else (
        reg delete "%EDIT_KEY%" /v "Icon" /f >nul 2>nul
    )
)

reg delete "%EDIT_BACKUP_KEY%" /f >nul 2>nul
exit /b 0
