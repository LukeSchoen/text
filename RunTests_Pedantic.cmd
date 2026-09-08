@echo off
setlocal

call "%~dp0runtests.cmd" ^
  launches_new_window_class ^
  vertical_scrollbar_scrollable_for_tall_file ^
  vertical_scrollbar_release_at_end_is_100_percent ^
  vertical_scrollbar_drag_up_from_bottom_releases_stickiness ^
  pagedown_moves_vertical_scroll ^
  mousewheel_down_then_up_restores_scroll ^
  maximized_mousewheel_reaches_bottom_without_sticking ^
  horizontal_scrollbar_track_click_moves_right ^
  horizontal_scrollbar_thumb_drag_moves_right ^
  horizontal_scrollbar_uses_widest_visible_line_not_caret_line ^
  ctrl_word_navigation_insert_points ^
  basic_text_regressions ^
  ctrl_shift_word_selection_copy ^
  shift_multiline_select_delete ^
  shift_multiline_replace_typing ^
  shift_up_on_first_row_selects_to_start ^
  shift_down_on_final_row_selects_to_end ^
  alt_shift_down_insert_column ^
  type_number_lines_keeps_caret_logical_and_visual_sync ^
  enter_on_line9_does_not_place_caret_in_old_gutter ^
  large_file_scroll_end_insert_lines_updates_scrollbar ^
  alt_shift_up_insert_column_pads_short_line ^
  alt_shift_up_delete_column ^
  alt_shift_up_clear_keeps_caret_column ^
  alt_mouse_drag_box_selection_cut ^
  selectissue_md_loads ^
  selectissue_md_line10_copy ^
  selectissue_md_line10_shift_down_copy ^
  selectissue_md_select_lines_backspace ^
  paste_selectissue_text_then_backspace_lines ^
  type_selectissue_text_then_backspace_lines ^
  selectissue_md_select_three_lines_backspace ^
  double_click_word_selection_copy ^
  ctrl_c_no_selection_single_line_copy_paste ^
  save_mapped_file_leaves_no_replacement_temp ^
  duplicate_open_clean_peer_reloads_after_save ^
  duplicate_open_dirty_peer_requires_explicit_overwrite ^
  deleted_file_keeps_editor_copy_until_explicit_recreate
if errorlevel 1 exit /b %errorlevel%
call "%~dp0RunTests_Utf8.cmd"
exit /b %errorlevel%
