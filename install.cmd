@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "EXE=%SCRIPT_DIR%text.exe"
set "APP_NAME=text.exe"
set "PROGID=TextEditor.Assoc"
set "EDIT_VERB=EditWithText"
set "BACKUP_ROOT=HKCU\Software\TextEditor\Backup"
set "OPEN_EXTENSIONS=.txt .md .markdown .log .ini .json .toml .yaml .yml .xml .csv .c .h .cpp .hpp .cc .x"
set "EDIT_ONLY_EXTENSIONS=.bat .cmd .ps1 .sh .py .rb"
set "WINDOWS_EDIT_PROGIDS=batfile cmdfile"

if not exist "%EXE%" (
    echo text.exe not found next to install.cmd
    exit /b 1
)

echo Adding Text to the Start menu and Windows search...
powershell.exe -NoProfile -NonInteractive -Command "$ErrorActionPreference = 'Stop'; $programs = [Environment]::GetFolderPath('Programs'); if (-not $programs) { throw 'Cannot locate the Start menu Programs folder.' }; [IO.Directory]::CreateDirectory($programs) | Out-Null; $shell = New-Object -ComObject WScript.Shell; $shortcut = $shell.CreateShortcut((Join-Path $programs 'Text.lnk')); $shortcut.TargetPath = $env:EXE; $shortcut.Arguments = ''; $shortcut.WorkingDirectory = $env:SCRIPT_DIR; $shortcut.IconLocation = $env:EXE + ',0'; $shortcut.Description = 'Text - Fast text editor'; $shortcut.Save(); $legacyPath = Join-Path $programs 'Notepad (Text).lnk'; if (Test-Path -LiteralPath $legacyPath) { $legacy = $shell.CreateShortcut($legacyPath); if ($legacy.TargetPath -ieq $env:EXE) { Remove-Item -LiteralPath $legacyPath -Force } }"
if errorlevel 1 (
    echo Failed to create the Text Start menu shortcuts.
    exit /b 1
)

echo Registering Text application...
reg add "HKCU\Software\Classes\%PROGID%" /ve /d "Text" /f >nul
reg add "HKCU\Software\Classes\%PROGID%\DefaultIcon" /ve /d "\"%EXE%\",0" /f >nul
reg add "HKCU\Software\Classes\%PROGID%\shell\open\command" /ve /d "\"%EXE%\" \"%%1\"" /f >nul
reg add "HKCU\Software\Classes\Applications\%APP_NAME%" /v "FriendlyAppName" /d "Text" /f >nul
reg add "HKCU\Software\Classes\Applications\%APP_NAME%\DefaultIcon" /ve /d "\"%EXE%\",0" /f >nul
reg add "HKCU\Software\Classes\Applications\%APP_NAME%\shell\open\command" /ve /d "\"%EXE%\" \"%%1\"" /f >nul
reg add "HKCU\Software\Classes\Applications\%APP_NAME%\Capabilities" /v "ApplicationName" /d "Text" /f >nul
reg add "HKCU\Software\Classes\Applications\%APP_NAME%\Capabilities" /v "ApplicationDescription" /d "Fast text editor" /f >nul
reg add "HKCU\Software\RegisteredApplications" /v "Text" /d "Software\Classes\Applications\%APP_NAME%\Capabilities" /f >nul

for %%E in (%OPEN_EXTENSIONS%) do (
    echo Registering %%~E...
    set "KEY=HKCU\Software\Classes\%%~E"
    set "EXPLORER_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\%%~E"
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
    reg add "!EXPLORER_KEY!\OpenWithProgids" /v "%PROGID%" /t REG_NONE /d "" /f >nul
    reg add "!EXPLORER_KEY!\OpenWithList" /v "%APP_NAME%" /d "%APP_NAME%" /f >nul
    call :PromoteExplorerOpenWith "%%~E"
    reg add "HKCU\Software\Classes\Applications\%APP_NAME%\SupportedTypes" /v "%%~E" /t REG_NONE /d "" /f >nul
    reg add "HKCU\Software\Classes\Applications\%APP_NAME%\Capabilities\FileAssociations" /v "%%~E" /d "%PROGID%" /f >nul
)

for %%E in (%EDIT_ONLY_EXTENSIONS%) do (
    echo Registering edit action for %%~E...
    reg add "HKCU\Software\Classes\SystemFileAssociations\%%~E\shell\%EDIT_VERB%" /ve /d "Edit with Text" /f >nul
    reg add "HKCU\Software\Classes\SystemFileAssociations\%%~E\shell\%EDIT_VERB%" /v "Icon" /d "\"%EXE%\",0" /f >nul
    reg add "HKCU\Software\Classes\SystemFileAssociations\%%~E\shell\%EDIT_VERB%\command" /ve /d "\"%EXE%\" \"%%1\"" /f >nul
)

for %%P in (%WINDOWS_EDIT_PROGIDS%) do (
    echo Registering default Edit command for %%~P...
    call :SetDefaultEditCommand "%%~P"
)

echo Installed default file associations for document files.
echo Added Text to the Start menu. Search for Text to launch it.
echo Windows search may take a moment to update; Windows controls result ranking.
echo Added an "Edit with Text" context-menu action for script files.
echo Made Text the default right-click Edit command for .bat and .cmd files.
echo On modern Windows, fully forcing the default app may still require one user confirmation in Default Apps or the Open With dialog.
echo You may need to restart Explorer or reopen File Explorer windows to see every change immediately.
exit /b 0

:PromoteExplorerOpenWith
set "PROMOTE_EXT=%~1"
set "OPENWITH_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\%PROMOTE_EXT%\OpenWithList"
set "APP_LETTER="

for %%L in (a b c d e f g h i j k l m n o p q r s t u v w x y z) do (
    set "OPENWITH_VALUE="
    for /f "skip=2 tokens=1,2,*" %%A in ('reg query "%OPENWITH_KEY%" /v %%L 2^>nul') do set "OPENWITH_VALUE=%%C"
    if /I "!OPENWITH_VALUE!"=="%APP_NAME%" set "APP_LETTER=%%L"
)

if not defined APP_LETTER (
    for %%L in (a b c d e f g h i j k l m n o p q r s t u v w x y z) do (
        if not defined APP_LETTER (
            reg query "%OPENWITH_KEY%" /v %%L >nul 2>nul
            if errorlevel 1 set "APP_LETTER=%%L"
        )
    )
)

if not defined APP_LETTER set "APP_LETTER=z"
reg add "%OPENWITH_KEY%" /v "%APP_LETTER%" /d "%APP_NAME%" /f >nul
reg add "%OPENWITH_KEY%" /v "MRUList" /d "%APP_LETTER%" /f >nul
exit /b 0

:SetDefaultEditCommand
set "EDIT_PROGID=%~1"
set "EDIT_KEY=HKCU\Software\Classes\%EDIT_PROGID%\shell\edit"
set "EDIT_COMMAND_KEY=%EDIT_KEY%\command"
set "EDIT_BACKUP_KEY=%BACKUP_ROOT%\DefaultEdit\%EDIT_PROGID%"
set "TEXT_EDIT_COMMAND=\"%EXE%\" \"%%1\""
set "CURRENT_EDIT_COMMAND="
set "CURRENT_EDIT_ICON="
set "HAD_EDIT_COMMAND="
set "HAD_EDIT_ICON="

for /f "skip=2 tokens=1,2,*" %%A in ('reg query "%EDIT_COMMAND_KEY%" /ve 2^>nul') do (
    set "CURRENT_EDIT_COMMAND=%%C"
    set "HAD_EDIT_COMMAND=1"
)
for /f "skip=2 tokens=1,2,*" %%A in ('reg query "%EDIT_KEY%" /v "Icon" 2^>nul') do (
    set "CURRENT_EDIT_ICON=%%C"
    set "HAD_EDIT_ICON=1"
)

if not defined HAD_EDIT_COMMAND (
    reg add "%EDIT_BACKUP_KEY%" /ve /d "__NONE__" /f >nul
) else if /I not "!CURRENT_EDIT_COMMAND!"=="!TEXT_EDIT_COMMAND!" (
    reg add "%EDIT_BACKUP_KEY%" /ve /d "!CURRENT_EDIT_COMMAND!" /f >nul
)

if not defined HAD_EDIT_ICON (
    reg add "%EDIT_BACKUP_KEY%" /v "Icon" /d "__NONE__" /f >nul
) else if /I not "!CURRENT_EDIT_ICON!"=="\"%EXE%\",0" (
    reg add "%EDIT_BACKUP_KEY%" /v "Icon" /d "!CURRENT_EDIT_ICON!" /f >nul
)

reg add "%EDIT_KEY%" /v "Icon" /d "\"%EXE%\",0" /f >nul
reg add "%EDIT_COMMAND_KEY%" /ve /d "\"%EXE%\" \"%%1\"" /f >nul
exit /b 0
