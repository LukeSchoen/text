@echo off
setlocal

set "LOOK_DIR=%~dp0.build\look"
if not exist "%LOOK_DIR%" mkdir "%LOOK_DIR%"
set "TEXT_LOOK_DIR=%LOOK_DIR%"

if /i "%~1"=="pedantic" (
  call "%~dp0runtests.cmd" ^
    look_tall_caret_three_lines ^
    alt_shift_down_insert_column ^
    alt_shift_up_insert_column_pads_short_line ^
    alt_shift_up_delete_column ^
    alt_shift_up_clear_keeps_caret_column ^
    box_cut_undo_redo_multi_line ^
    alt_mouse_drag_box_selection_cut
) else if "%~1"=="" (
  call "%~dp0runtests.cmd" look_tall_caret_three_lines look_ctrl_d_command_popup
) else (
  echo Usage: runTests_Look.cmd [pedantic]
  exit /b 2
)
exit /b %errorlevel%
