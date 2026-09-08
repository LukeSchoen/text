#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wctype.h>
#include <direct.h>
#include "repro_logger.h"
#include "core/core.h"

/******************************************************************************
 * Core Types
 ******************************************************************************/

typedef uint64_t u64;

/******************************************************************************
 * Application Constants
 ******************************************************************************/

#define APP_CLASS_NAME L"TextSuiteFastText"
#define GUTTER_CHARS 1
#define EDIT_LINE_NUMBER_LEFT_PADDING 16
#define EDIT_LINE_NUMBER_RIGHT_PADDING 8
#define EDIT_TEXT_LEFT_PADDING 8
#define MIN_WINDOW_WIDTH 200
#define MIN_WINDOW_HEIGHT 60
#define TAB_WIDTH 2
#define MAX_PAINT_COLS 8192
#define LINE_DISCOVERY_BUDGET (256u * 1024u)
#define LINE_DISCOVERY_ASYNC_CHUNK (64u * 1024u)
#define LINE_DISCOVERY_ASYNC_SLICE_MS 10
#define LINECOUNT_TIMER_ID 1
#define FILEWATCH_TIMER_ID 2
#define FILEWATCH_TIMER_MS 1000
#define LINECOUNT_ASYNC_FILE_BYTES (8u * 1024u * 1024u)
#define THEME_BG RGB(35, 31, 24)
#define THEME_FG RGB(155, 155, 155)
#define THEME_GUTTER_BG THEME_BG
#define THEME_GUTTER_FG RGB(122, 104, 88)
#define THEME_TEXT RGB(220, 220, 220)
#define THEME_CMD_POPUP_BG RGB(48, 43, 35)
#define THEME_GUTTER_ACTIVE_LINE_BG RGB(68, 58, 48)
#define THEME_SELECTION_BG RGB(0, 120, 215)
#define THEME_SELECTION_TEXT RGB(255, 255, 255)
#define FONT_SIZE_MIN 10
#define FONT_SIZE_MAX 48
#define FONT_SIZE_DEFAULT 20
#define IDC_FIND_COUNT 5001
#define WM_APP_RESTORE_EDITOR_FOCUS (WM_APP + 1)
#define IDC_CMD_EDIT 5101
#define IDC_CMD_LIST 5102
#define IDC_FIND_EDIT 5201
#define IDC_FIND_LIST 5202
#define IDC_GOTO_EDIT 5301
#define IDC_GOTO_LABEL 5302
#define MAX_DO_COMMANDS 128
#define MAX_FIND_RESULTS 512
#define CMD_POPUP_CLASS_NAME L"TextSuiteCommandPopup"
#define FIND_POPUP_CLASS_NAME L"TextSuiteFindPopup"
#define GOTO_POPUP_CLASS_NAME L"TextSuiteGotoPopup"
#ifndef edt1
#define edt1 0x0480
#endif
#ifndef stc2
#define stc2 0x0441
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

/******************************************************************************
 * Core Data Structures
 ******************************************************************************/



typedef struct LineIndex
{
  u64 *starts;
  int64_t *block_add;
  u64 count, cap, scanned_to;
  u64 block_cap;
  bool eof;
} LineIndex;

typedef struct MappedFile
{
  HANDLE file;
  HANDLE map;
  const char *data;
  u64 len;
} MappedFile;

typedef struct FileStamp
{
  bool exists;
  DWORD volume_serial;
  DWORD file_index_high;
  DWORD file_index_low;
  FILETIME last_write_time;
  u64 size;
} FileStamp;

typedef enum FileProbeKind
{
  FILE_PROBE_EXISTS,
  FILE_PROBE_MISSING,
  FILE_PROBE_ERROR
} FileProbeKind;

typedef struct FileProbe
{
  FileProbeKind kind;
  FileStamp stamp;
  DWORD error;
} FileProbe;

typedef enum DiskState
{
  DISK_STATE_SAME,
  DISK_STATE_CHANGED,
  DISK_STATE_MISSING,
  DISK_STATE_INACCESSIBLE
} DiskState;

typedef struct PreparedSave
{
  WCHAR temp_path[MAX_PATH + 80];
  FileStamp stamp;
  bool ready;
} PreparedSave;

typedef struct Document
{
  Core rope;
  u64 len;
  bool dirty;
  bool stamp_valid;
  bool disk_change_prompted;
  bool observed_stamp_valid;
  DiskState disk_state;
  DWORD disk_error;
  WCHAR path[MAX_PATH];
  WCHAR path_key[MAX_PATH];
  FileStamp stamp;
  FileStamp observed_stamp;
  MappedFile mf;
  LineIndex lines;
} Document;

typedef struct DoCommandEntry
{
  WCHAR first[64];
  WCHAR second[64];
  WCHAR display[144];
} DoCommandEntry;

typedef struct FindResult
{
  u64 off;
  u64 len;
  u64 line;
  WCHAR preview[160];
} FindResult;

typedef struct App
{
  HWND hwnd;
  Document doc;
  HFONT font;
  int font_size;
  HBRUSH bg_brush;
  HBRUSH cmd_bg_brush;
  int char_w;
  int line_h;
  int gutter_w;
  int rows;
  int cols;
  int last_client_w;
  int last_client_h;
  bool handling_main_wm_size;
  u64 tl, fc, cl, cc, co, sa, sv;
  bool bsa;
  u64 bal, bac, bcl, bcc, bdc;
  bool vsb;
  HWND find_hwnd;
  WCHAR fq[256];
  bool swm;
  bool sbxm;
  bool swdm;
  u64 wdas, wdae;
  int ch;
  bool clm;
  bool lcp;
  bool suppress_tab_char_once;
  WCHAR pending_high_surrogate;
  HWND cmd_hwnd;
  HWND cmd_edit;
  HWND cmd_list;
  WCHAR cmd_query[128];
  int cmd_match_indices[MAX_DO_COMMANDS];
  int cmd_match_count;
  DoCommandEntry cmd_entries[MAX_DO_COMMANDS];
  int cmd_entry_count;
  bool cmd_restore_pending;
  u64 cmd_saved_co;
  u64 cmd_saved_sa;
  u64 cmd_saved_sv;
  HWND find_edit;
  HWND find_list;
  HWND find_count_label;
  int find_selected_result;
  int find_result_count;
  FindResult find_results[MAX_FIND_RESULTS];
  HWND goto_hwnd;
  HWND goto_edit;
  HWND goto_label;
  WCHAR goto_query[32];
  int wheel_delta_remainder;
  int zoom_wheel_delta_remainder;
  bool skip_keep_caret_visible_once;
} App;

typedef Core SpanRef;

typedef enum UndoOpKind
{
  UNDO_OP_INSERT,
  UNDO_OP_DELETE
} UndoOpKind;

typedef struct UndoOp
{
  UndoOpKind kind;
  u64 off, len;
  SpanRef *spans;
  size_t span_count;
} UndoOp;

typedef struct UndoTxn
{
  UndoOp *ops;
  size_t count;
  size_t cap;
  u64 before_caret_off, before_sel_anchor, before_sel_active;
  u64 after_caret_off, after_sel_anchor, after_sel_active;
  bool before_dirty;
  bool after_dirty;
} UndoTxn;

typedef struct UndoStack
{
  UndoTxn *items;
  size_t count;
  size_t cap;
} UndoStack;



/******************************************************************************
 * Global State
 ******************************************************************************/

static App g_app;

static WNDPROC g_command_edit_wndproc;

static WNDPROC g_find_popup_edit_wndproc;

static WNDPROC g_goto_popup_edit_wndproc;

static UndoStack g_undo_stack;

static UndoStack g_redo_stack;

static UndoTxn g_txn;

static int g_txn_depth;

static bool g_history_replaying;

static void repro_capture_ui_state(const App *app, ReproUiState *state);
static void repro_begin_app_event(const App *app, const char *kind, unsigned long a, unsigned long b);
static void repro_end_app_event(const App *app);
static u64 repro_collect_deleted_preview(const SpanRef *spans, size_t span_count, char *buf, u64 cap);

typedef enum PreferredAppMode
{
  APP_MODE_DEFAULT,
  APP_MODE_ALLOW_DARK,
  APP_MODE_FORCE_DARK,
  APP_MODE_FORCE_LIGHT,
  APP_MODE_MAX
} PreferredAppMode;

typedef PreferredAppMode (WINAPI *SetPreferredAppModeFn)(PreferredAppMode app_mode);

typedef BOOL (WINAPI *AllowDarkModeForWindowFn)(HWND hwnd, BOOL allow);

typedef VOID (WINAPI *FlushMenuThemesFn)(VOID);

static SetPreferredAppModeFn g_set_preferred_app_mode;

static AllowDarkModeForWindowFn g_allow_dark_mode_for_window;

static FlushMenuThemesFn g_flush_menu_themes;

/******************************************************************************
 * Forward Declarations
 ******************************************************************************/

/* Forward declarations by domain. */

/* Core helpers. */
static int digit_count_u64(u64 value);

static u64 min_u64(u64 a, u64 b);

static u64 max_u64(u64 a, u64 b);

static bool is_word_byte(unsigned char c);

static bool is_space_byte(unsigned char c);

static int wcs_copy_trunc(wchar_t *dst, size_t dst_count, const wchar_t *src);

static int wcs_cat_trunc(wchar_t *dst, size_t dst_count, const wchar_t *src);

static int wcs_copy_n_trunc(wchar_t *dst, size_t dst_count, const wchar_t *src, size_t max_chars);

static int wfmt(wchar_t *dst, size_t dst_count, const wchar_t *fmt, ...);

static ULONGLONG monotonic_tick_ms(void);

/* Document primitives. */
static int doc_copy_utf8_z(Document *doc, char **out_utf8);

static bool doc_set_empty(Document *doc);

static bool doc_check_disk_state(Document *doc);

static bool doc_insert_bytes(Document *doc, u64 off, const char *text, u64 n, const char **out_add_ptr);


static bool doc_delete_range(Document *doc, u64 off, u64 n);


/* App-facing edit operations. */
static bool app_doc_insert(App *app, u64 off, const char *text, u64 n);

static bool app_doc_delete(App *app, u64 off, u64 n);

/* Undo/history helpers. */
static bool doc_capture_spans(Document *doc, u64 off, u64 len, SpanRef **out_spans, size_t *out_count);

static bool txn_record_insert(Document *doc, u64 off, u64 len);

static bool txn_record_delete(u64 off, u64 len, SpanRef *spans, size_t span_count);

/* Caret, selection, and repaint. */
static bool has_stream_selection(const App *app);

static bool has_box_selection(const App *app);

static void clear_stream_selection(App *app);

static void clear_box_selection(App *app);

static void sync_caret_from_offsets(App *app);

static void keep_caret_visible(App *app);

static void update_title(App *app);

static void update_scrollbars(App *app);

static void position_caret(App *app);

static void request_repaint(App *app, BOOL erase);

static void keep_visible_and_repaint(App *app);

static void set_caret_line_col(App *app, u64 line, u64 col);

static void apply_caret_line_metrics(App *app);

static void apply_scroll_limits_and_count(App *app);

static void apply_scroll_limits_and_position(App *app);

static void preserve_caret_row_for_scroll(App *app, u64 old_top_line);

static void apply_font_size(App *app, int new_size);

static bool sync_path_utf8_on_change(App *app);

static bool app_load_path(App *app, const WCHAR *path);

static bool app_save_to_path(App *app, const WCHAR *path, bool save_as);

static bool app_save_to_path_approved(App *app, const WCHAR *path, bool save_as,
                                      const FileProbe *approved_probe);

static bool app_check_external_change(App *app, bool interactive);

static bool app_confirm_replace_document(App *app);

static void close_find_popup(App *app);

static void open_find_popup(App *app);

static void close_goto_popup(App *app);

static void open_goto_popup(App *app);

static bool goto_line_from_popup(App *app);

static void box_selection_bounds(const App *app, u64 *top, u64 *bottom, u64 *left, u64 *right);

static bool doc_get_byte(Document *doc, u64 off, unsigned char *out);

static void doc_discover_for_view(Document *doc, u64 first_line, u64 rows);

static void force_focus_window(HWND hwnd);

/* Command popup helpers. */
static void close_command_popup(App *app);

static void open_command_popup(App *app);

/* Command/edit helpers used by input handling before their definitions. */
static void open_dialog(App *app);

static bool launch_new_window(App *app);

static bool save_dialog(App *app);

static bool save_as_dialog(App *app);

static void copy_box_selection_to_clipboard(App *app);

static void copy_selection_or_current_line_to_clipboard(App *app);

static void copy_current_line_to_clipboard(App *app);

static bool current_line_range(App *app, u64 *out_start, u64 *out_end);

static bool set_clipboard_utf8_text(HWND hwnd, const char *utf8, size_t utf8_len);

static bool run_codex_do_command(App *app);

static void paste_clipboard(App *app);

static bool seed_find_query_from_selection(App *app);

static bool find_next(App *app, const WCHAR *query);

static bool move_caret_line_by_swap(App *app, bool move_down);

static void move_caret_word_left(App *app);

static void move_caret_left(App *app);

static void move_caret_word_right(App *app);

static void move_caret_right(App *app);
static bool doc_get_byte(Document *doc, u64 off, unsigned char *out);
static u64 doc_line_first_nonblank_col(Document *doc, u64 line);
static u64 doc_line_visual_width(Document *doc, u64 start, u64 end);
static u64 doc_line_visual_col_from_byte_col(Document *doc, u64 line, u64 byte_col);
static u64 doc_line_byte_col_from_visual_col(Document *doc, u64 line, u64 visual_col);


/******************************************************************************
 * Core Helpers
 ******************************************************************************/
static u64 min_u64(u64 a, u64 b) { return a < b ? a : b; }

static u64 max_u64(u64 a, u64 b) { return a > b ? a : b; }

static int digit_count_u64(u64 value)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
  return (int)strlen(buf);
}

static bool is_word_byte(unsigned char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

static bool is_space_byte(unsigned char c)
{
  return c == ' ' || c == '\t';
}

static int wcs_copy_trunc(wchar_t *dst, size_t dst_count, const wchar_t *src)
{
  size_t i = 0;
  if (!dst || dst_count == 0) return 22;
  if (!src)
  {
    dst[0] = 0;
    return 22;
  }
  while (i + 1 < dst_count && src[i])
  {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
  return src[i] ? 80 : 0;
}

static int wcs_cat_trunc(wchar_t *dst, size_t dst_count, const wchar_t *src)
{
  size_t used = 0;
  if (!dst || dst_count == 0) return 22;
  while (used < dst_count && dst[used]) used++;
  if (used == dst_count) return 80;
  return wcs_copy_trunc(dst + used, dst_count - used, src);
}

static int wcs_copy_n_trunc(wchar_t *dst, size_t dst_count, const wchar_t *src, size_t max_chars)
{
  size_t i = 0;
  if (!dst || dst_count == 0) return 22;
  if (!src)
  {
    dst[0] = 0;
    return 22;
  }
  while (i + 1 < dst_count && i < max_chars && src[i])
  {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
  return (i == max_chars || src[i] == 0) ? 0 : 80;
}

static int wfmt(wchar_t *dst, size_t dst_count, const wchar_t *fmt, ...)
{
  int rc;
  va_list args;
  if (!dst || dst_count == 0 || !fmt) return -1;
  va_start(args, fmt);
  rc = _vsnwprintf_s(dst, dst_count, _TRUNCATE, fmt, args);
  va_end(args);
  dst[dst_count - 1] = 0;
  return rc;
}

static ULONGLONG monotonic_tick_ms(void)
{
  static ULONGLONG (WINAPI *get_tick_count64)(void);
  static int initialized;
  if (!initialized)
  {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (k32) get_tick_count64 = (ULONGLONG (WINAPI *)(void))GetProcAddress(k32, "GetTickCount64");
    initialized = 1;
  }
  if (get_tick_count64) return get_tick_count64();
  return (ULONGLONG)GetTickCount();
}



/******************************************************************************
 * Document Storage And Shared Rope
 ******************************************************************************/
static void mapped_close(MappedFile *mf)
{
  if (mf->data) UnmapViewOfFile(mf->data);
  if (mf->map) CloseHandle(mf->map);
  if (mf->file && mf->file != INVALID_HANDLE_VALUE) CloseHandle(mf->file);
  memset(mf, 0, sizeof(*mf));
}

static void file_stamp_clear(FileStamp *stamp)
{
  memset(stamp, 0, sizeof(*stamp));
}

static bool file_stamp_from_handle(HANDLE f, FileStamp *out)
{
  BY_HANDLE_FILE_INFORMATION info;
  LARGE_INTEGER sz;
  file_stamp_clear(out);
  if (f == INVALID_HANDLE_VALUE) return false;
  if (!GetFileInformationByHandle(f, &info)) return false;
  if (!GetFileSizeEx(f, &sz) || sz.QuadPart < 0) return false;
  out->exists = true;
  out->volume_serial = info.dwVolumeSerialNumber;
  out->file_index_high = info.nFileIndexHigh;
  out->file_index_low = info.nFileIndexLow;
  out->last_write_time = info.ftLastWriteTime;
  out->size = (u64)sz.QuadPart;
  return true;
}

static FileProbe file_probe_path(const WCHAR *path)
{
  FileProbe probe;
  HANDLE f;
  memset(&probe, 0, sizeof(probe));
  f = CreateFileW(path, FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE)
  {
    probe.error = GetLastError();
    if (probe.error == ERROR_FILE_NOT_FOUND || probe.error == ERROR_PATH_NOT_FOUND)
      probe.kind = FILE_PROBE_MISSING;
    else
      probe.kind = FILE_PROBE_ERROR;
    return probe;
  }
  if (file_stamp_from_handle(f, &probe.stamp))
    probe.kind = FILE_PROBE_EXISTS;
  else
  {
    probe.kind = FILE_PROBE_ERROR;
    probe.error = GetLastError();
  }
  CloseHandle(f);
  return probe;
}

static bool file_stamp_equal(const FileStamp *a, const FileStamp *b)
{
  if (a->exists != b->exists) return false;
  if (!a->exists) return true;
  return a->volume_serial == b->volume_serial &&
         a->file_index_high == b->file_index_high &&
         a->file_index_low == b->file_index_low &&
         CompareFileTime(&a->last_write_time, &b->last_write_time) == 0 &&
         a->size == b->size;
}

static bool file_stamp_same_identity(const FileStamp *a, const FileStamp *b)
{
  return a->exists && b->exists &&
         a->volume_serial == b->volume_serial &&
         a->file_index_high == b->file_index_high &&
         a->file_index_low == b->file_index_low;
}

static bool file_probe_equal(const FileProbe *a, const FileProbe *b)
{
  if (a->kind != b->kind) return false;
  if (a->kind == FILE_PROBE_EXISTS) return file_stamp_equal(&a->stamp, &b->stamp);
  if (a->kind == FILE_PROBE_ERROR) return a->error == b->error;
  return true;
}

static void canonicalize_path_key(const WCHAR *path, WCHAR *out, size_t out_count)
{
  typedef DWORD (WINAPI *GetFinalPathNameByHandleWFn)(HANDLE, LPWSTR, DWORD, DWORD);
  WCHAR final_path[MAX_PATH];
  GetFinalPathNameByHandleWFn get_final_path;
  HANDLE f;
  DWORD n;
  size_t i;
  if (!out || out_count == 0) return;
  out[0] = 0;
  if (!path || !path[0]) return;
  n = GetFullPathNameW(path, (DWORD)out_count, out, NULL);
  if (n == 0 || n >= out_count)
    lstrcpynW(out, path, (int)out_count);
  get_final_path = (GetFinalPathNameByHandleWFn)GetProcAddress(
      GetModuleHandleW(L"kernel32.dll"), "GetFinalPathNameByHandleW");
  f = CreateFileW(path, FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (get_final_path && f != INVALID_HANDLE_VALUE)
  {
    n = get_final_path(f, final_path, (DWORD)_countof(final_path), 0);
    if (n > 0 && n < _countof(final_path))
    {
      if (wcsncmp(final_path, L"\\\\?\\UNC\\", 8) == 0)
        wfmt(out, out_count, L"\\\\%ls", final_path + 8);
      else if (wcsncmp(final_path, L"\\\\?\\", 4) == 0)
        lstrcpynW(out, final_path + 4, (int)out_count);
      else
        lstrcpynW(out, final_path, (int)out_count);
    }
  }
  if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
  for (i = 0; out[i]; ++i)
  {
    if (out[i] == L'/') out[i] = L'\\';
    out[i] = towlower(out[i]);
  }
}

static void doc_clear_disk_tracking(Document *doc)
{
  doc->disk_state = DISK_STATE_SAME;
  doc->disk_change_prompted = false;
  doc->observed_stamp_valid = false;
  doc->disk_error = ERROR_SUCCESS;
  file_stamp_clear(&doc->observed_stamp);
}

static void doc_set_disk_baseline(Document *doc, const WCHAR *path,
                                  const WCHAR *path_key, const FileStamp *stamp)
{
  lstrcpynW(doc->path, path ? path : L"", MAX_PATH);
  if (path_key && path_key[0])
    lstrcpynW(doc->path_key, path_key, MAX_PATH);
  else
    canonicalize_path_key(path, doc->path_key, _countof(doc->path_key));
  if (stamp)
  {
    doc->stamp = *stamp;
    doc->stamp_valid = true;
  }
  else
  {
    file_stamp_clear(&doc->stamp);
    doc->stamp_valid = false;
  }
  doc_clear_disk_tracking(doc);
}

static bool doc_check_disk_state(Document *doc)
{
  FileProbe probe;
  DiskState next_state;
  bool new_revision;
  if (!doc->path[0])
  {
    doc_clear_disk_tracking(doc);
    return false;
  }
  if (!doc->stamp_valid)
  {
    new_revision = doc->disk_state != DISK_STATE_INACCESSIBLE ||
                   doc->disk_error != ERROR_INVALID_DATA;
    doc->disk_state = DISK_STATE_INACCESSIBLE;
    doc->disk_error = ERROR_INVALID_DATA;
    doc->observed_stamp_valid = false;
    if (new_revision) doc->disk_change_prompted = false;
    return true;
  }
  probe = file_probe_path(doc->path);
  if (probe.kind == FILE_PROBE_ERROR)
    next_state = DISK_STATE_INACCESSIBLE;
  else if (probe.kind == FILE_PROBE_MISSING)
    next_state = DISK_STATE_MISSING;
  else if (file_stamp_equal(&doc->stamp, &probe.stamp))
    next_state = DISK_STATE_SAME;
  else
    next_state = DISK_STATE_CHANGED;

  new_revision = next_state != doc->disk_state;
  if (!new_revision && next_state == DISK_STATE_CHANGED)
    new_revision = !doc->observed_stamp_valid ||
                   !file_stamp_equal(&doc->observed_stamp, &probe.stamp);
  if (!new_revision && next_state == DISK_STATE_INACCESSIBLE)
    new_revision = doc->disk_error != probe.error;

  doc->disk_state = next_state;
  doc->disk_error = next_state == DISK_STATE_INACCESSIBLE ? probe.error : ERROR_SUCCESS;
  doc->observed_stamp_valid = next_state == DISK_STATE_CHANGED;
  if (doc->observed_stamp_valid)
    doc->observed_stamp = probe.stamp;
  else
    file_stamp_clear(&doc->observed_stamp);
  if (next_state == DISK_STATE_SAME)
    doc->disk_change_prompted = false;
  else if (new_revision)
    doc->disk_change_prompted = false;
  return next_state != DISK_STATE_SAME;
}

static FileProbe doc_observed_probe(const Document *doc)
{
  FileProbe probe;
  memset(&probe, 0, sizeof(probe));
  if (doc->disk_state == DISK_STATE_CHANGED && doc->observed_stamp_valid)
  {
    probe.kind = FILE_PROBE_EXISTS;
    probe.stamp = doc->observed_stamp;
  }
  else if (doc->disk_state == DISK_STATE_MISSING)
    probe.kind = FILE_PROBE_MISSING;
  else if (doc->disk_state == DISK_STATE_INACCESSIBLE)
  {
    probe.kind = FILE_PROBE_ERROR;
    probe.error = doc->disk_error;
  }
  else
  {
    probe.kind = FILE_PROBE_EXISTS;
    probe.stamp = doc->stamp;
  }
  return probe;
}

static void line_index_free(LineIndex *li)
{
  free(li->starts);
  free(li->block_add);
  memset(li, 0, sizeof(*li));
}

enum
{
  LINE_INDEX_BLOCK_SHIFT = 10,
  LINE_INDEX_BLOCK_SIZE = 1 << LINE_INDEX_BLOCK_SHIFT
};

static u64 line_index_blocks_for_count(u64 count)
{
  return (count + (u64)LINE_INDEX_BLOCK_SIZE - 1) >> LINE_INDEX_BLOCK_SHIFT;
}

static bool line_index_ensure_block_capacity(LineIndex *li, u64 need_count)
{
  u64 need_blocks = line_index_blocks_for_count(need_count);
  if (need_blocks <= li->block_cap) return true;
  {
    u64 new_cap = li->block_cap ? li->block_cap : 4;
    int64_t *new_add;
    while (new_cap < need_blocks) new_cap *= 2;
    new_add = (int64_t *)realloc(li->block_add, (size_t)(new_cap * sizeof(int64_t)));
    if (!new_add) return false;
    for (u64 i = li->block_cap; i < new_cap; ++i) new_add[i] = 0;
    li->block_add = new_add;
    li->block_cap = new_cap;
  }
  return true;
}

static u64 line_index_start_at(const LineIndex *li, u64 line)
{
  u64 block = line >> LINE_INDEX_BLOCK_SHIFT;
  int64_t add = (block < li->block_cap) ? li->block_add[block] : 0;
  if (add >= 0) return li->starts[line] + (u64)add;
  return li->starts[line] - (u64)(-add);
}

static bool line_index_push_start(LineIndex *li, u64 off)
{
  u64 line = li->count;
  u64 block = line >> LINE_INDEX_BLOCK_SHIFT;
  if (li->count == li->cap)
  {
    u64 new_cap = li->cap ? li->cap * 2 : 4096;
    u64 *new_starts = (u64 *)realloc(li->starts, (size_t)(new_cap * sizeof(u64)));
    if (!new_starts) return false;
    li->starts = new_starts;
    li->cap = new_cap;
  }
  if (!line_index_ensure_block_capacity(li, li->count + 1)) return false;
  {
    int64_t add = li->block_add[block];
    if (add >= 0) li->starts[li->count++] = off - (u64)add;
    else li->starts[li->count++] = off + (u64)(-add);
  }
  return true;
}

static void doc_reset_lines(Document *doc)
{
  line_index_free(&doc->lines);
  line_index_push_start(&doc->lines, 0);
}

static void line_index_invalidate_from(LineIndex *li, u64 off)
{
  u64 keep = 1;
  if (li->count == 0)
  {
    line_index_push_start(li, 0);
    li->scanned_to = 0;
    li->eof = false;
    return;
  }
  {
    u64 hi = li->count;
    while (keep < hi) {
      u64 mid = keep + (hi - keep) / 2;
      if (line_index_start_at(li, mid) <= off) keep = mid + 1;
      else hi = mid;
    }
  }
  li->count = keep;
  {
    u64 keep_blocks = line_index_blocks_for_count(keep);
    for (u64 b = keep_blocks; b < li->block_cap; ++b) li->block_add[b] = 0;
  }
  if (li->scanned_to >= off) li->scanned_to = off;
  li->eof = false;
}

static u64 line_index_first_after(const LineIndex *li, u64 off)
{
  u64 lo = 1;
  u64 hi = li->count;
  while (lo < hi)
  {
    u64 mid = lo + (hi - lo) / 2;
    if (line_index_start_at(li, mid) > off) hi = mid;
    else lo = mid + 1;
  }
  return lo;
}

static void line_index_add_from(LineIndex *li, u64 start, int64_t delta)
{
  if (delta == 0 || start >= li->count) return;
  {
    u64 first_block = start >> LINE_INDEX_BLOCK_SHIFT;
    u64 first_block_end = min_u64(li->count, ((first_block + 1) << LINE_INDEX_BLOCK_SHIFT));
    for (u64 i = start; i < first_block_end; ++i)
    {
      if (delta >= 0) li->starts[i] += (u64)delta;
      else li->starts[i] -= (u64)(-delta);
    }
    for (u64 b = first_block + 1; (b << LINE_INDEX_BLOCK_SHIFT) < li->count; ++b)
      li->block_add[b] += delta;
  }
}

static void line_index_adjust_insert(LineIndex *li, u64 off, u64 n)
{
  u64 first = line_index_first_after(li, off);
  line_index_add_from(li, first, (int64_t)n);
  if (li->scanned_to > off) li->scanned_to += n;
}

static void line_index_adjust_delete(LineIndex *li, u64 off, u64 n)
{
  u64 first = line_index_first_after(li, off);
  line_index_add_from(li, first, -(int64_t)n);
  if (li->scanned_to > off + n) li->scanned_to -= n;
  else if (li->scanned_to > off)
  {
    li->scanned_to = off;
    li->eof = false;
  }
}

static void spans_free(SpanRef *spans, size_t count)
{
  for (size_t i = 0; i < count; ++i) core_dispose(&spans[i]);
  free(spans);
}

static void mapped_release(void *user, const void *data, uint64_t len)
{
  MappedFile *mf = (MappedFile *)user;
  (void)data; (void)len;
  mapped_close(mf);
  free(mf);
}

static void doc_dispose_contents(Document *doc)
{
  core_dispose(&doc->rope);
  mapped_close(&doc->mf);
  line_index_free(&doc->lines);
}

static void doc_clear(Document *doc)
{
  doc_dispose_contents(doc);
  memset(doc, 0, sizeof(*doc));
  doc_reset_lines(doc);
}

static bool doc_set_empty(Document *doc)
{
  doc_clear(doc);
  return true;
}

static bool doc_load_mapped(Document *doc, const WCHAR *path)
{
  Document loaded;
  HANDLE f;
  LARGE_INTEGER sz;
  FileStamp stamp;
  MappedFile *owner;
  memset(&loaded, 0, sizeof(loaded));
  /* Retain this read lease with delete sharing for the mapped document's
     lifetime. Other readers and atomic ReplaceFile saves remain compatible,
     while in-place writers cannot mutate BACKING_ORIGINAL under our pieces. */
  f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
  if (f == INVALID_HANDLE_VALUE) return false;
  if (!GetFileSizeEx(f, &sz) || sz.QuadPart < 0)
  {
    CloseHandle(f);
    return false;
  }
  if (!file_stamp_from_handle(f, &stamp))
  {
    CloseHandle(f);
    return false;
  }
  loaded.mf.file = f;
  loaded.mf.len = (u64)sz.QuadPart;
  loaded.len = loaded.mf.len;
  doc_set_disk_baseline(&loaded, path, NULL, &stamp);
  if (loaded.len != 0)
  {
    loaded.mf.map = CreateFileMappingW(f, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!loaded.mf.map)
    {
      doc_dispose_contents(&loaded);
      return false;
    }
    loaded.mf.data = (const char *)MapViewOfFile(loaded.mf.map, FILE_MAP_READ, 0, 0, 0);
    if (!loaded.mf.data)
    {
      doc_dispose_contents(&loaded);
      return false;
    }
    owner = (MappedFile *)malloc(sizeof(*owner));
    if (!owner)
    {
      doc_dispose_contents(&loaded);
      return false;
    }
    *owner = loaded.mf;
    if (!core_from_memory(&loaded.rope, owner->data, loaded.len, mapped_release, owner))
    {
      free(owner);
      doc_dispose_contents(&loaded);
      return false;
    }
    memset(&loaded.mf, 0, sizeof(loaded.mf));
  }

  doc_reset_lines(&loaded);
  doc_dispose_contents(doc);
  *doc = loaded;
  return true;
}

typedef void (*SpanFn)(const char *data, u64 len, void *user);
typedef struct SpanAdapter { SpanFn fn; void *user; } SpanAdapter;
static int doc_span_adapter(void *user, const char *data, uint64_t len)
{
  SpanAdapter *a = (SpanAdapter *)user;
  a->fn(data, len, a->user);
  return 1;
}
static void doc_read_range(Document *doc, u64 off, u64 len, SpanFn fn, void *user)
{
  SpanAdapter a = {fn, user};
  if (off >= doc->len || len == 0) return;
  len = min_u64(len, doc->len - off);
  core_runs(&doc->rope, off, len, doc_span_adapter, &a);
}

typedef struct CopyCtx
{
  char *dst;
  u64 at;
} CopyCtx;

static void copy_span(const char *data, u64 len, void *user)
{
  CopyCtx *ctx = (CopyCtx *)user;
  memcpy(ctx->dst + ctx->at, data, (size_t)len);
  ctx->at += len;
}

typedef struct SearchCtx
{
  char *buf;
  u64 n;
} SearchCtx;

static void append_span(const char *data, u64 len, void *user)
{
  SearchCtx *ctx = (SearchCtx *)user;
  memcpy(ctx->buf + ctx->n, data, (size_t)len);
  ctx->n += len;
}



static bool bytes_have_newline(const char *s, u64 n)
{
  for (u64 i = 0; i < n; ++i)
  {
    if (s[i] == '\n' || s[i] == '\r') return true;
  }
  return false;
}

static bool doc_insert_bytes(Document *doc, u64 off, const char *text, u64 n, const char **out_add_ptr)
{
  CoreRun run;
  if (n == 0) return true;
  if (off > doc->len) off = doc->len;
  if (!core_insert(&doc->rope, off, text, n)) return false;
  doc->len = core_len(&doc->rope);
  doc->dirty = true;
  if (out_add_ptr) {
    core_run(&doc->rope, off, &run);
    *out_add_ptr = run.data;
  }
  if (n > 4096 || bytes_have_newline(text, n))
    line_index_invalidate_from(&doc->lines, off);
  else
    line_index_adjust_insert(&doc->lines, off, n);
  return true;
}

static bool doc_delete_range(Document *doc, u64 off, u64 n)
{
  char small[4096];
  bool touches_newline = true;
  if (off >= doc->len || n == 0) return true;
  n = min_u64(n, doc->len - off);
  if (n <= sizeof(small)) {
    core_read(&doc->rope, off, n, small);
    touches_newline = bytes_have_newline(small, n);
  }
  if (!core_replace(&doc->rope, off, n, NULL)) return false;
  doc->len = core_len(&doc->rope);
  doc->dirty = true;
  if (touches_newline) line_index_invalidate_from(&doc->lines, off);
  else line_index_adjust_delete(&doc->lines, off, n);
  return true;
}

static bool app_doc_insert(App *app, u64 off, const char *text, u64 n)
{
  Core before = {0};
  bool was_dirty = app->doc.dirty;
  const char *add_ptr = NULL;
  if (!n) return true;
  off = min_u64(off, app->doc.len);
  core_clone(&before, &app->doc.rope);
  if (!doc_insert_bytes(&app->doc, off, text, n, &add_ptr)) {
    core_dispose(&before);
    return false;
  }
  if (!txn_record_insert(&app->doc, off, n)) {
    core_clone(&app->doc.rope, &before);
    app->doc.len = core_len(&before);
    app->doc.dirty = was_dirty;
    line_index_invalidate_from(&app->doc.lines, 0);
    core_dispose(&before);
    return false;
  }
  repro_note_insert((unsigned long long)off, (unsigned long long)n, add_ptr ? add_ptr : text, (unsigned long long)n);
  core_dispose(&before);
  return true;
}

static bool app_doc_delete(App *app, u64 off, u64 n)
{
  Core before = {0};
  bool was_dirty = app->doc.dirty;
  SpanRef *spans = NULL;
  size_t span_count = 0;
  u64 del_len = 0;
  char preview[32];
  u64 preview_len = 0;
  if (off >= app->doc.len || n == 0) return true;
  del_len = min_u64(n, app->doc.len - off);
  if (g_txn_depth > 0 && !g_history_replaying)
  {
    if (!doc_capture_spans(&app->doc, off, del_len, &spans, &span_count)) return false;
  }
  if (spans && span_count > 0)
  {
    preview_len = repro_collect_deleted_preview(spans, span_count, preview, (u64)sizeof(preview));
  }
  core_clone(&before, &app->doc.rope);
  if (!doc_delete_range(&app->doc, off, del_len))
  {
    spans_free(spans, span_count);
    core_dispose(&before);
    return false;
  }
  if (!txn_record_delete(off, del_len, spans, span_count)) {
    core_clone(&app->doc.rope, &before);
    app->doc.len = core_len(&before);
    app->doc.dirty = was_dirty;
    line_index_invalidate_from(&app->doc.lines, 0);
    core_dispose(&before);
    return false;
  }
  repro_note_delete((unsigned long long)off, (unsigned long long)del_len, preview, (unsigned long long)preview_len);
  core_dispose(&before);
  return true;
}

static void flat_release(void *user, const void *data, uint64_t len)
{
  (void)user; (void)len;
  free((void *)data);
}
static bool doc_flatten_to_one_add_piece(Document *doc)
{
  Core flat = {0};
  char *data = NULL;
  if (doc->len > SIZE_MAX) return false;
  if (doc->len) {
    data = (char *)malloc((size_t)doc->len);
    if (!data) return false;
    if (!core_read(&doc->rope, 0, doc->len, data) ||
        !core_from_memory(&flat, data, doc->len, flat_release, NULL)) {
      free(data);
      return false;
    }
  }
  core_dispose(&doc->rope);
  doc->rope = flat; /* transfer ownership */
  mapped_close(&doc->mf);
  doc_reset_lines(doc);
  return true;
}

static bool write_all(HANDLE f, const char *data, u64 len)
{
  u64 written_total = 0;
  while (written_total < len)
  {
    DWORD chunk = (DWORD)min_u64(len - written_total, 1u << 24);
    DWORD wrote = 0;
    if (!WriteFile(f, data + written_total, chunk, &wrote, NULL)) return false;
    if (wrote == 0) return false;
    written_total += wrote;
  }
  return true;
}

static int write_rope_run(void *user, const char *data, uint64_t len)
{
  return write_all((HANDLE)user, data, len);
}
static bool doc_write_pieces(Document *doc, HANDLE f)
{
  return core_runs(&doc->rope, 0, doc->len, write_rope_run, f) != 0;
}

static bool make_save_temp_path(const WCHAR *path, WCHAR *out, size_t out_count)
{
  DWORD pid = GetCurrentProcessId();
  DWORD tick = GetTickCount();
  for (int i = 0; i < 100; ++i)
  {
    if (wfmt(out, out_count, L"%ls.%lu.%lu.%d.tmp", path, (unsigned long)pid,
             (unsigned long)tick, i) < 0)
      return false;
    if (GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES)
      return true;
  }
  return false;
}

static bool make_save_backup_path(const WCHAR *path, WCHAR *out, size_t out_count)
{
  DWORD pid = GetCurrentProcessId();
  DWORD tick = GetTickCount();
  for (int i = 0; i < 100; ++i)
  {
    if (wfmt(out, out_count, L"%ls~text-save-%lu-%lu-%d.tmp", path,
             (unsigned long)pid, (unsigned long)tick, i) < 0)
      return false;
    if (GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES)
      return true;
  }
  return false;
}

static bool delete_replaced_backup(const WCHAR *path, DWORD *error_out)
{
  typedef BOOL (WINAPI *SetFileInformationByHandleFn)(HANDLE, int, LPVOID, DWORD);
  typedef struct FileDispositionInfoExCompat
  {
    DWORD flags;
  } FileDispositionInfoExCompat;
  enum
  {
    FILE_DISPOSITION_INFO_EX_CLASS = 21,
    FILE_DISPOSITION_DELETE_COMPAT = 0x00000001,
    FILE_DISPOSITION_POSIX_SEMANTICS_COMPAT = 0x00000002
  };
  SetFileInformationByHandleFn set_file_information;
  FileDispositionInfoExCompat disposition;
  HANDLE f;
  DWORD error = ERROR_SUCCESS;

  if (error_out) *error_out = ERROR_SUCCESS;
  if (!path || !path[0]) return true;
  if (DeleteFileW(path)) return true;
  error = GetLastError();
  if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return true;

  /* A peer editor may still have the replaced file mapped. POSIX disposition
     removes the backup name while those existing mappings keep their bytes. */
  set_file_information = (SetFileInformationByHandleFn)GetProcAddress(
      GetModuleHandleW(L"kernel32.dll"), "SetFileInformationByHandle");
  f = CreateFileW(path, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f != INVALID_HANDLE_VALUE)
  {
    disposition.flags = FILE_DISPOSITION_DELETE_COMPAT |
                        FILE_DISPOSITION_POSIX_SEMANTICS_COMPAT;
    if (set_file_information &&
        set_file_information(f, FILE_DISPOSITION_INFO_EX_CLASS,
                             &disposition, (DWORD)sizeof(disposition)))
    {
      CloseHandle(f);
      return true;
    }
    error = GetLastError();
    CloseHandle(f);
  }
  else
    error = GetLastError();
  if (error_out) *error_out = error;
  return false;
}

static bool doc_prepare_save(Document *doc, const WCHAR *path, PreparedSave *prepared)
{
  FileProbe probe;
  HANDLE f;
  memset(prepared, 0, sizeof(*prepared));
  if (!path || !path[0]) return false;
  if (!make_save_temp_path(path, prepared->temp_path, _countof(prepared->temp_path))) return false;
  f = CreateFileW(prepared->temp_path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                  FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return false;
  if (!doc_write_pieces(doc, f) || !FlushFileBuffers(f))
  {
    CloseHandle(f);
    DeleteFileW(prepared->temp_path);
    return false;
  }
  CloseHandle(f);
  probe = file_probe_path(prepared->temp_path);
  if (probe.kind != FILE_PROBE_EXISTS)
  {
    DeleteFileW(prepared->temp_path);
    return false;
  }
  prepared->stamp = probe.stamp;
  prepared->ready = true;
  return true;
}

static void doc_discard_prepared_save(PreparedSave *prepared)
{
  if (prepared->ready) DeleteFileW(prepared->temp_path);
  prepared->ready = false;
  prepared->temp_path[0] = 0;
}

static u64 save_mutex_hash(const WCHAR *path_key)
{
  u64 hash = UINT64_C(1469598103934665603);
  const WCHAR *at = path_key;
  while (at && *at)
  {
    hash ^= (u64)(unsigned short)*at++;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static HANDLE acquire_save_mutex(const WCHAR *path_key)
{
  WCHAR name[96];
  u64 hash = save_mutex_hash(path_key);
  HANDLE mutex;
  DWORD wait_result;
  wsprintfW(name, L"Local\\TextSuiteFastTextSave_%08lX%08lX",
            (DWORD)(hash >> 32), (DWORD)hash);
  mutex = CreateMutexW(NULL, FALSE, name);
  if (!mutex) return NULL;
  wait_result = WaitForSingleObject(mutex, 30000);
  if (wait_result != WAIT_OBJECT_0 && wait_result != WAIT_ABANDONED)
  {
    CloseHandle(mutex);
    return NULL;
  }
  return mutex;
}

static void release_save_mutex(HANDLE mutex)
{
  if (!mutex) return;
  ReleaseMutex(mutex);
  CloseHandle(mutex);
}

static bool doc_commit_prepared_save(Document *doc, const WCHAR *path,
                                     const WCHAR *path_key, PreparedSave *prepared,
                                     DWORD *cleanup_error)
{
  typedef BOOL (WINAPI *ReplaceFileWFn)(LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPVOID, LPVOID);
  WCHAR backup_path[MAX_PATH + 80];
  FileStamp saved_stamp;
  ReplaceFileWFn replace_file;
  bool has_backup = false;
  if (cleanup_error) *cleanup_error = ERROR_SUCCESS;
  if (!prepared->ready) return false;
  backup_path[0] = 0;
  saved_stamp = prepared->stamp;
  if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
  {
    if (!MoveFileExW(prepared->temp_path, path, MOVEFILE_WRITE_THROUGH)) return false;
  }
  else
  {
    if (!make_save_backup_path(path, backup_path, _countof(backup_path)))
    {
      SetLastError(ERROR_CANNOT_MAKE);
      return false;
    }
    replace_file = (ReplaceFileWFn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ReplaceFileW");
    if (!replace_file || !replace_file(path, prepared->temp_path, backup_path,
                                       REPLACEFILE_WRITE_THROUGH, NULL, NULL))
      return false;
    has_backup = true;
  }
  prepared->ready = false;
  prepared->temp_path[0] = 0;
  /* The disk commit already succeeded. Flattening is a best-effort cleanup if
     allocation pressure prevents rebuilding the one-piece saved baseline. */
  doc_flatten_to_one_add_piece(doc);
  if (has_backup && !delete_replaced_backup(backup_path, cleanup_error) &&
      cleanup_error && *cleanup_error == ERROR_SUCCESS)
    *cleanup_error = ERROR_ACCESS_DENIED;
  doc->dirty = false;
  doc_set_disk_baseline(doc, path, path_key, &saved_stamp);
  /* Never adopt a later writer's stamp as ours. A writer that won immediately
     after the rename is instead observed as a fresh external revision. */
  doc_check_disk_state(doc);
  return true;
}



/******************************************************************************
 * Line Index
 ******************************************************************************/
static void scan_for_lines(Document *doc, u64 target)
{
  LineIndex *li = &doc->lines;
  u64 budget = LINE_DISCOVERY_BUDGET;
  while (!li->eof && li->count <= target && budget) {
    CoreRun run;
    core_run(&doc->rope, li->scanned_to, &run);
    u64 take = min_u64(run.len, budget);
    for (u64 i = 0; i < take; ++i) {
      ++li->scanned_to;
      --budget;
      if (run.data[i] == '\n') {
        if (!line_index_push_start(li, li->scanned_to)) return;
        if (li->count > target) break;
      }
    }
    li->eof = li->scanned_to == doc->len;
  }
}

static void doc_ensure_line(Document *doc, u64 line)
{
  while (!doc->lines.eof && doc->lines.count <= line)
  {
    u64 before = doc->lines.scanned_to;
    scan_for_lines(doc, line);
    if (doc->lines.scanned_to == before) break;
  }
}

static void doc_discover_for_view(Document *doc, u64 first_line, u64 rows)
{
  scan_for_lines(doc, first_line + rows + 4);
}

static u64 doc_known_line_count(Document *doc)
{
  return doc->lines.eof ? doc->lines.count : doc->lines.count + 1000;
}

static u64 doc_line_start(Document *doc, u64 line)
{
  doc_ensure_line(doc, line);
  if (line >= doc->lines.count) return doc->len;
  return line_index_start_at(&doc->lines, line);
}

static u64 doc_line_length_clamped(Document *doc, u64 start, u64 limit)
{
  if (start >= doc->len) return doc->len;
  u64 end = start + min_u64(limit, doc->len - start);
  while (start < end) {
    CoreRun run;
    core_run(&doc->rope, start, &run);
    u64 take = min_u64(run.len, end - start);
    for (u64 i = 0; i < take; ++i)
      if (run.data[i] == '\r' || run.data[i] == '\n') return start + i;
    start += take;
  }
  return end;
}

/* Decode one Unicode scalar. Invalid bytes remain individually addressable and
   render as replacement characters; never consume an unrelated following byte. */
static unsigned utf8_decode(const unsigned char *s, unsigned n, uint32_t *cp)
{
  unsigned need;
  uint32_t value;
  *cp = 0xfffd;
  if (!n) return 0;
  if (s[0] < 0x80) { *cp = s[0]; return 1; }
  if (s[0] >= 0xc2 && s[0] <= 0xdf) { need = 2; value = s[0] & 31; }
  else if (s[0] >= 0xe0 && s[0] <= 0xef) { need = 3; value = s[0] & 15; }
  else if (s[0] >= 0xf0 && s[0] <= 0xf4) { need = 4; value = s[0] & 7; }
  else return 1;
  if (n < need) return 1;
  for (unsigned i = 1; i < need; ++i)
  {
    if ((s[i] & 0xc0) != 0x80) return 1;
    value = (value << 6) | (s[i] & 63);
  }
  if ((need == 3 && value < 0x800) || (need == 4 && value < 0x10000) ||
      value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 1;
  *cp = value;
  return need;
}

typedef struct Utf8Cursor
{
  Document *doc;
  const char *data;
  u64 available, off, end;
} Utf8Cursor;
static Utf8Cursor utf8_cursor(Document *doc, u64 off, u64 end)
{
  Utf8Cursor c = {doc, NULL, 0, off, min_u64(end, doc->len)};
  return c;
}
static unsigned utf8_cursor_byte(Utf8Cursor *c, unsigned char *out)
{
  if (c->off >= c->end) return 0;
  if (!c->available) {
    CoreRun run;
    if (!core_run(&c->doc->rope, c->off, &run) || !run.len) return 0;
    c->data = run.data;
    c->available = run.len;
  }
  *out = (unsigned char)*c->data++;
  --c->available;
  ++c->off;
  return 1;
}

static unsigned utf8_cursor_next(Utf8Cursor *c, uint32_t *cp)
{
  unsigned char bytes[4];
  Utf8Cursor look = *c;
  unsigned count = utf8_cursor_byte(&look, bytes);
  unsigned used;
  if (!count) return 0;
  if (bytes[0] < 0x80) { *cp = bytes[0]; *c = look; return 1; }
  while (count < 4 && utf8_cursor_byte(&look, bytes + count)) count++;
  used = utf8_decode(bytes, count, cp);
  for (unsigned i = 0; i < used; ++i) utf8_cursor_byte(c, bytes);
  return used;
}

static u64 doc_next_character(Document *doc, u64 off)
{
  Utf8Cursor c = utf8_cursor(doc, off, doc->len);
  uint32_t cp;
  utf8_cursor_next(&c, &cp);
  return c.off;
}

static u64 doc_previous_character(Document *doc, u64 off)
{
  unsigned char bytes[4];
  u64 start = off > 4 ? off - 4 : 0;
  CopyCtx ctx = {(char *)bytes, 0};
  uint32_t cp;
  if (!off) return 0;
  doc_read_range(doc, start, off - start, copy_span, &ctx);
  for (unsigned i = 0; i < ctx.at; ++i)
    if (utf8_decode(bytes + i, (unsigned)ctx.at - i, &cp) == ctx.at - i)
      return start + i;
  return off - 1;
}

static u64 utf8_text_columns(const char *text, u64 len)
{
  u64 cols = 0, off = 0;
  while (off < len)
  {
    uint32_t cp;
    off += utf8_decode((const unsigned char *)text + off, (unsigned)min_u64(4, len - off), &cp);
    cols += cp == '\t' ? TAB_WIDTH : 1;
  }
  return cols;
}

static u64 visual_advance_for_byte(unsigned char c, u64 visual_col)
{
  (void)visual_col;
  if (c == '\t') return (u64)TAB_WIDTH;
  return 1;
}

static u64 doc_line_visual_width(Document *doc, u64 start, u64 end)
{
  u64 col = 0;
  Utf8Cursor c = utf8_cursor(doc, start, end);
  uint32_t cp;
  while (utf8_cursor_next(&c, &cp))
  {
    if (cp == '\r' || cp == '\n') break;
    col += cp == '\t' ? TAB_WIDTH : 1;
  }
  return col;
}

static u64 doc_line_visual_col_from_byte_col(Document *doc, u64 line, u64 byte_col)
{
  u64 start = doc_line_start(doc, line);
  u64 end = doc_line_length_clamped(doc, start, UINT32_MAX);
  u64 max_byte_col = end - start;
  if (byte_col > max_byte_col) byte_col = max_byte_col;
  return doc_line_visual_width(doc, start, start + byte_col);
}

static u64 doc_line_byte_col_from_visual_col(Document *doc, u64 line, u64 visual_col)
{
  u64 start = doc_line_start(doc, line);
  u64 col = 0;
  Utf8Cursor c = utf8_cursor(doc, start, doc->len);
  while (col < visual_col)
  {
    u64 before = c.off;
    uint32_t cp;
    if (!utf8_cursor_next(&c, &cp)) break;
    if (cp == '\r' || cp == '\n') return before - start;
    u64 width = cp == '\t' ? TAB_WIDTH : 1;
    if (col + width > visual_col && visual_col - col < col + width - visual_col)
      return before - start;
    col += width;
  }
  return c.off - start;
}

static u64 doc_line_col_clamped(Document *doc, u64 line, u64 col)
{
  u64 start = doc_line_start(doc, line);
  u64 byte_col = doc_line_byte_col_from_visual_col(doc, line, col);
  return doc_line_visual_width(doc, start, start + byte_col);
}

static u64 doc_line_first_nonblank_col(Document *doc, u64 line)
{
  u64 start = doc_line_start(doc, line);
  u64 end = doc_line_length_clamped(doc, start, UINT32_MAX);
  u64 byte_col = 0;
  u64 visual_col = 0;
  while (start + byte_col < end)
  {
    unsigned char ch = 0;
    if (!doc_get_byte(doc, start + byte_col, &ch)) break;
    if (ch != ' ' && ch != '\t' && ch != '\r') break;
    visual_col += visual_advance_for_byte(ch, visual_col);
    ++byte_col;
  }
  return visual_col;
}

static void doc_discover_all_lines(Document *doc)
{
  while (!doc->lines.eof)
  {
    u64 before = doc->lines.scanned_to;
    u64 base = doc->lines.count > 0 ? doc->lines.count - 1 : 0;
    doc_discover_for_view(doc, base, 4096);
    if (doc->lines.scanned_to == before) break;
  }
}

static void doc_offset_to_line_col(Document *doc, u64 off, u64 *out_line, u64 *out_col)
{
  u64 lo = 0;
  u64 hi;
  u64 line = 0;
  if (off > doc->len) off = doc->len;
  doc_discover_all_lines(doc);
  if (doc->lines.count == 0)
  {
    *out_line = 0;
    *out_col = 0;
    return;
  }
  hi = doc->lines.count;
  while (lo < hi)
  {
    u64 mid = lo + (hi - lo) / 2;
    if (line_index_start_at(&doc->lines, mid) <= off)
    {
      line = mid;
      lo = mid + 1;
    }
    else
      hi = mid;
  }
  *out_line = line;
  *out_col = doc_line_visual_col_from_byte_col(doc, line, off - line_index_start_at(&doc->lines, line));
}



/******************************************************************************
 * Undo / Redo
 ******************************************************************************/
static void undo_op_free(UndoOp *op)
{
  if (!op) return;
  spans_free(op->spans, op->span_count);
  memset(op, 0, sizeof(*op));
}

static void undo_txn_free(UndoTxn *txn)
{
  if (!txn) return;
  for (size_t i = 0; i < txn->count; ++i) undo_op_free(&txn->ops[i]);
  free(txn->ops);
  memset(txn, 0, sizeof(*txn));
}

static void undo_stack_clear(UndoStack *stack)
{
  if (!stack) return;
  for (size_t i = 0; i < stack->count; ++i) undo_txn_free(&stack->items[i]);
  free(stack->items);
  memset(stack, 0, sizeof(*stack));
}

static bool undo_stack_push(UndoStack *stack, const UndoTxn *txn)
{
  if (stack->count == stack->cap)
  {
    size_t new_cap = stack->cap ? stack->cap * 2 : 16;
    UndoTxn *new_items = (UndoTxn *)realloc(stack->items, new_cap *sizeof(UndoTxn));
    if (!new_items) return false;
    stack->items = new_items;
    stack->cap = new_cap;
  }
  stack->items[stack->count++] = *txn;
  return true;
}

static bool undo_txn_push_op(UndoTxn *txn, const UndoOp *op)
{
  if (txn->count == txn->cap)
  {
    size_t new_cap = txn->cap ? txn->cap * 2 : 8;
    UndoOp *new_ops = (UndoOp *)realloc(txn->ops, new_cap *sizeof(UndoOp));
    if (!new_ops) return false;
    txn->ops = new_ops;
    txn->cap = new_cap;
  }
  txn->ops[txn->count++] = *op;
  return true;
}

static bool begin_edit_txn(App *app)
{
  if (g_history_replaying) return true;
  if (g_txn_depth == 0)
  {
    undo_txn_free(&g_txn);
    memset(&g_txn, 0, sizeof(g_txn));
    g_txn.before_caret_off = app->co;
    g_txn.before_sel_anchor = app->sa;
    g_txn.before_sel_active = app->sv;
    g_txn.before_dirty = app->doc.dirty;
    undo_stack_clear(&g_redo_stack);
  }
  g_txn_depth++;
  return true;
}

static void end_edit_txn(App *app)
{
  if (g_history_replaying || g_txn_depth <= 0) return;
  g_txn_depth--;
  if (g_txn_depth != 0) return;
  if (g_txn.count == 0)
  {
    undo_txn_free(&g_txn);
    return;
  }
  g_txn.after_caret_off = app->co;
  g_txn.after_sel_anchor = app->sa;
  g_txn.after_sel_active = app->sv;
  g_txn.after_dirty = app->doc.dirty;
  if (!undo_stack_push(&g_undo_stack, &g_txn))
  {
    undo_txn_free(&g_txn);
    return;
  }
  memset(&g_txn, 0, sizeof(g_txn));
}

static void clear_history(void)
{
  undo_txn_free(&g_txn);
  g_txn_depth = 0;
  undo_stack_clear(&g_undo_stack);
  undo_stack_clear(&g_redo_stack);
}

static bool doc_capture_spans(Document *doc, u64 off, u64 len, SpanRef **out_spans, size_t *out_count)
{
  SpanRef *spans;
  *out_spans = NULL; *out_count = 0;
  if (off >= doc->len || !len) return true;
  len = min_u64(len, doc->len - off);
  spans = (SpanRef *)calloc(1, sizeof(*spans));
  if (!spans) return false;
  if (!core_slice(spans, &doc->rope, off, len)) { free(spans); return false; }
  *out_spans = spans; *out_count = 1;
  return true;
}

static bool txn_record_insert(Document *doc, u64 off, u64 len)
{
  UndoOp op = {0};
  if (g_history_replaying || g_txn_depth <= 0 || !len) return true;
  op.kind = UNDO_OP_INSERT; op.off = off; op.len = len;
  if (!doc_capture_spans(doc, off, len, &op.spans, &op.span_count)) return false;
  if (!undo_txn_push_op(&g_txn, &op)) { undo_op_free(&op); return false; }
  return true;
}

static bool txn_record_delete(u64 off, u64 len, SpanRef *spans, size_t span_count)
{
  UndoOp op = {0};
  if (g_history_replaying || g_txn_depth <= 0 || !len) {
    spans_free(spans, span_count); return true;
  }
  op.kind = UNDO_OP_DELETE; op.off = off; op.len = len;
  op.spans = spans; op.span_count = span_count;
  if (!undo_txn_push_op(&g_txn, &op)) { undo_op_free(&op); return false; }
  return true;
}

static void apply_after_state(App *app, const UndoTxn *txn, bool after)
{
  if (after)
  {
    app->co = min_u64(txn->after_caret_off, app->doc.len);
    app->sa = min_u64(txn->after_sel_anchor, app->doc.len);
    app->sv = min_u64(txn->after_sel_active, app->doc.len);
    app->doc.dirty = txn->after_dirty;
  }
  else
  {
    app->co = min_u64(txn->before_caret_off, app->doc.len);
    app->sa = min_u64(txn->before_sel_anchor, app->doc.len);
    app->sv = min_u64(txn->before_sel_active, app->doc.len);
    app->doc.dirty = txn->before_dirty;
  }
  sync_caret_from_offsets(app);
  keep_caret_visible(app);
  update_title(app);
  request_repaint(app, FALSE);
}

/* Build the entire replay privately, then commit once. Even a multi-operation
   undo leaves both the document and history unchanged on allocation failure. */
static bool replay_txn(App *app, const UndoTxn *txn, bool reverse)
{
  Core next = {0};
  u64 first_changed = app->doc.len;
  core_clone(&next, &app->doc.rope);
  for (size_t i = 0; i < txn->count; ++i) {
    const UndoOp *op = &txn->ops[reverse ? txn->count - 1 - i : i];
    bool insert = (op->kind == UNDO_OP_INSERT) != reverse;
    const Core *range = NULL;
    if (insert) {
      if (op->span_count != 1) { core_dispose(&next); return false; }
      range = &op->spans[0];
    }
    if (!core_replace(&next, op->off, insert ? 0 : op->len, range)) {
      core_dispose(&next);
      return false;
    }
    first_changed = min_u64(first_changed, op->off);
  }
  core_dispose(&app->doc.rope);
  app->doc.rope = next; /* transfer ownership */
  app->doc.len = core_len(&next);
  line_index_invalidate_from(&app->doc.lines, first_changed);
  return true;
}

static void perform_history(App *app, bool reverse)
{
  UndoStack *source = reverse ? &g_undo_stack : &g_redo_stack;
  UndoStack *target = reverse ? &g_redo_stack : &g_undo_stack;
  if (g_txn_depth > 0) end_edit_txn(app);
  if (!source->count) return;
  UndoTxn txn = source->items[source->count - 1];
  /* Reserve the destination before editing; ownership moves only on success. */
  if (!undo_stack_push(target, &txn)) return;
  g_history_replaying = true;
  if (replay_txn(app, &txn, reverse)) {
    --source->count;
    apply_after_state(app, &txn, !reverse);
  } else --target->count;
  g_history_replaying = false;
}

static void perform_undo(App *app) { perform_history(app, true); }
static void perform_redo(App *app) { perform_history(app, false); }

/******************************************************************************
 * Caret Utilities
 ******************************************************************************/
static int desired_caret_height(const App *app)
{
  if (app->bsa)
  {
    u64 top = app->bal < app->bcl ? app->bal : app->bcl;
    u64 bottom = app->bal > app->bcl ? app->bal : app->bcl;
    u64 span = bottom - top + 1;
    if (span > (u64)INT_MAX) span = (u64)INT_MAX;
    return (int)span * app->line_h;
  }
  return app->line_h;
}

static void ensure_caret_shape(App *app)
{
  int target_h;
  if (GetFocus() != app->hwnd) return;
  target_h = desired_caret_height(app);
  if (target_h < 1) target_h = 1;
  if (app->ch == target_h) return;
  if (app->ch > 0)
  {
    HideCaret(app->hwnd);
    DestroyCaret();
  }
  CreateCaret(app->hwnd, NULL, 2, target_h);
  ShowCaret(app->hwnd);
  app->ch = target_h;
}



/******************************************************************************
 * Rendering
 ******************************************************************************/
/* One scalar per editor grid cell; UTF-16 pairs are formed only at GDI output. */
static int line_to_wide_visible(Document *doc, u64 start, u64 end, u64 fc, int max_cols, uint32_t *out)
{
  Utf8Cursor c = utf8_cursor(doc, start, end);
  u64 col = 0;
  int n = 0;
  uint32_t cp;
  while (n < max_cols && utf8_cursor_next(&c, &cp))
  {
    if (cp == '\r' || cp == '\n') break;
    unsigned width = cp == '\t' ? TAB_WIDTH : 1;
    for (unsigned i = 0; i < width; ++i, ++col)
      if (col >= fc && n < max_cols) out[n++] = cp < 32 ? ' ' : cp;
  }
  return n;
}

static void draw_text_cells(HDC dc, int x, int y, const uint32_t *cells, int n, int char_w)
{
  WCHAR text[MAX_PAINT_COLS * 2];
  int advances[MAX_PAINT_COLS * 2];
  int count = 0;
  for (int i = 0; i < n; ++i)
  {
    uint32_t cp = cells[i];
    if (cp > 0xffff)
    {
      cp -= 0x10000;
      text[count] = (WCHAR)(0xd800 + (cp >> 10));
      advances[count++] = 0;
      text[count] = (WCHAR)(0xdc00 + (cp & 1023));
    }
    else text[count] = (WCHAR)cp;
    advances[count++] = char_w;
  }
  ExtTextOutW(dc, x, y, 0, NULL, text, count, advances);
}

static void paint_editor(App *app, HDC dc)
{
  RECT rc;
  GetClientRect(app->hwnd, &rc);
  FillRect(dc, &rc, app->bg_brush);
  RECT gutter = rc;
  gutter.right = app->gutter_w;
  HBRUSH gutter_bg = CreateSolidBrush(THEME_GUTTER_BG);
  FillRect(dc, &gutter, gutter_bg);
  DeleteObject(gutter_bg);
  SelectObject(dc, app->font);
  SetBkMode(dc, TRANSPARENT);
  int max_cols = app->cols + 2;
  if (max_cols > MAX_PAINT_COLS - 1) max_cols = MAX_PAINT_COLS - 1;
  uint32_t text_buf[MAX_PAINT_COLS];
  WCHAR num_buf[32];
  int text_left = app->gutter_w + EDIT_TEXT_LEFT_PADDING;
  HBRUSH active_line_brush = CreateSolidBrush(THEME_GUTTER_ACTIVE_LINE_BG);
  HBRUSH selection_brush = CreateSolidBrush(THEME_SELECTION_BG);
  doc_discover_for_view(&app->doc, app->tl, (u64)app->rows);
  for (int row = 0; row < app->rows; ++row)
  {
    u64 line = app->tl + (u64)row;
    int y = row * app->line_h;
    if (line < app->doc.lines.count)
    {
      if (line == app->cl && active_line_brush)
      {
        RECT line_rc;
        line_rc.left = 0;
        line_rc.top = y;
        line_rc.right = app->gutter_w - EDIT_LINE_NUMBER_RIGHT_PADDING;
        line_rc.bottom = y + app->line_h;
        FillRect(dc, &line_rc, active_line_brush);
      }
      SetTextColor(dc, THEME_GUTTER_FG);
      int gutter_chars = digit_count_u64(doc_known_line_count(&app->doc));
      if (gutter_chars < 1) gutter_chars = 1;
      wfmt(num_buf, _countof(num_buf), L"%*llu", gutter_chars, (unsigned long long)(line + 1));
      TextOutW(dc, EDIT_LINE_NUMBER_LEFT_PADDING, y, num_buf, lstrlenW(num_buf));
      u64 start = line_index_start_at(&app->doc.lines, line);
      u64 end = app->doc.len;
      if (line + 1 < app->doc.lines.count)
      {
        end = line_index_start_at(&app->doc.lines, line + 1);
        while (end > start)
        {
          char c = 0;
          CopyCtx ctx = { &c, 0 };
          doc_read_range(&app->doc, end - 1, 1, copy_span, &ctx);
          if (c != '\n' && c != '\r') break;
          --end;
        }
      }
      int n = line_to_wide_visible(&app->doc, start, end, app->fc, max_cols, text_buf);
      SetTextColor(dc, THEME_TEXT);
      if (n > 0) draw_text_cells(dc, text_left, y, text_buf, n, app->char_w);
      if (has_stream_selection(app))
      {
        u64 sel_start = min_u64(app->sa, app->sv);
        u64 sel_end = max_u64(app->sa, app->sv);
        u64 draw_start = max_u64(sel_start, start);
        u64 draw_end = min_u64(sel_end, end);
        if (draw_end > draw_start)
        {
          u64 col_start = doc_line_visual_width(&app->doc, start, draw_start);
          u64 col_end = doc_line_visual_width(&app->doc, start, draw_end);
          if (col_end > app->fc)
          {
            int vis_start = (int)(col_start > app->fc ? col_start - app->fc : 0);
            int vis_end = (int)(col_end - app->fc);
            if (vis_start < 0) vis_start = 0;
            if (vis_end > n) vis_end = n;
            if (vis_end > vis_start)
            {
              RECT sel_rc;
              sel_rc.left = text_left + vis_start * app->char_w;
              sel_rc.right = text_left + vis_end * app->char_w;
              sel_rc.top = y;
              sel_rc.bottom = y + app->line_h;
              FillRect(dc, &sel_rc, selection_brush);
              SetTextColor(dc, THEME_SELECTION_TEXT);
              draw_text_cells(dc, text_left + vis_start *app->char_w, y, text_buf + vis_start, vis_end - vis_start, app->char_w);
              SetTextColor(dc, THEME_TEXT);
            }
          }
        }
      }
      if (has_box_selection(app))
      {
        u64 top, bottom, left, right;
        box_selection_bounds(app, &top, &bottom, &left, &right);
        if (line >= top && line <= bottom && right > left)
        {
          int vis_start = 0;
          int vis_end = 0;
          if (right > app->fc)
          {
            vis_start = (int)(left > app->fc ? left - app->fc : 0);
            vis_end = (int)(right - app->fc);
            if (vis_start < 0) vis_start = 0;
            if (vis_end > max_cols) vis_end = max_cols;
            if (vis_end > vis_start)
            {
              RECT sel_rc;
              sel_rc.left = text_left + vis_start * app->char_w;
              sel_rc.right = text_left + vis_end * app->char_w;
              sel_rc.top = y;
              sel_rc.bottom = y + app->line_h;
              FillRect(dc, &sel_rc, selection_brush);
              if (vis_start < n)
              {
                int text_vis_end = vis_end;
                if (text_vis_end > n) text_vis_end = n;
                if (text_vis_end > vis_start)
                {
                  SetTextColor(dc, THEME_SELECTION_TEXT);
                  draw_text_cells(dc, text_left + vis_start *app->char_w, y, text_buf + vis_start, text_vis_end - vis_start, app->char_w);
                  SetTextColor(dc, THEME_TEXT);
                }
              }
            }
          }
        }
      }
    }
  }
  if (active_line_brush) DeleteObject(active_line_brush);
  if (selection_brush) DeleteObject(selection_brush);
}



/******************************************************************************
 * View / Caret / Scroll
 ******************************************************************************/
static void update_title(App *app)
{
  WCHAR title[MAX_PATH + 64];
  const WCHAR *name = app->doc.path[0] ? app->doc.path : L"Untitled";
  WCHAR prefix[8] = L"";
  if (sync_path_utf8_on_change(app)) wcs_cat_trunc(prefix, _countof(prefix), L"*");
  if (app->doc.disk_state != DISK_STATE_SAME) wcs_cat_trunc(prefix, _countof(prefix), L"!");
  if (prefix[0]) wcs_cat_trunc(prefix, _countof(prefix), L" ");
  wsprintfW(title, L"%s%s - text", prefix, name);
  SetWindowTextW(app->hwnd, title);
}

static void update_scrollbars(App *app)
{
  LONG_PTR style;
  bool has_vscroll;
  bool has_hscroll;
  u64 tl_before = app->tl;
  u64 known_lines = app->lcp
                    ? (app->doc.lines.count ? app->doc.lines.count : 1)
                    : doc_known_line_count(&app->doc);
  u64 page = (u64)(app->rows > 0 ? app->rows : 1);
  u64 max_top = known_lines > page ? known_lines - page : 0;
  if (app->vsb)
  {
    if (!app->doc.lines.eof)
    {
      // Explicit bottom intent should snap to true EOF, not a provisional lazy-line estimate.
      doc_discover_all_lines(&app->doc);
      known_lines = doc_known_line_count(&app->doc);
      max_top = known_lines > page ? known_lines - page : 0;
    }
    app->tl = max_top;
  }
  else
  {
    doc_discover_for_view(&app->doc, app->tl, (u64)app->rows);
    known_lines = doc_known_line_count(&app->doc);
    max_top = known_lines > page ? known_lines - page : 0;
  }
  if (app->tl > max_top) app->tl = max_top;
  {
    RECT rc;
    int digits = digit_count_u64(known_lines ? known_lines : 1);
    if (digits < GUTTER_CHARS) digits = GUTTER_CHARS;
    app->gutter_w = (app->char_w *digits) + EDIT_LINE_NUMBER_LEFT_PADDING + EDIT_LINE_NUMBER_RIGHT_PADDING;
    GetClientRect(app->hwnd, &rc);
    app->cols = (rc.right - rc.left - app->gutter_w - EDIT_TEXT_LEFT_PADDING) / app->char_w;
    if (app->cols < 1) app->cols = 1;
  }
  SCROLLINFO si;
  style = GetWindowLongPtrW(app->hwnd, GWL_STYLE);
  has_vscroll = (style & WS_VSCROLL) != 0;
  has_hscroll = (style & WS_HSCROLL) != 0;
  if (app->lcp)
  {
    if (has_vscroll) ShowScrollBar(app->hwnd, SB_VERT, FALSE);
  }
  else
  {
    bool need_vscroll = known_lines > page;
    if (need_vscroll != has_vscroll)
      ShowScrollBar(app->hwnd, SB_VERT, need_vscroll);
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (int)min_u64(known_lines ? known_lines - 1 : 0, INT32_MAX);
    si.nPage = (UINT)page;
    si.nPos = (int)min_u64(app->tl, INT32_MAX);
    SetScrollInfo(app->hwnd, SB_VERT, &si, TRUE);
  }
  {
    int page_cols = app->cols > 0 ? app->cols : 1;
    u64 probe_rows = (u64)(app->rows > 0 ? app->rows : 1) + 1;
    u64 max_line_cols = 1;
    u64 first_visible_line = app->tl;
    /* Probe one extra row so h-scroll visibility remains stable when toggling
     the bar itself changes viewport height by a row. */
    u64 last_visible_line = app->tl + probe_rows;
    for (u64 line = first_visible_line; line < last_visible_line && line < app->doc.lines.count; ++line)
    {
      u64 start = line_index_start_at(&app->doc.lines, line);
      u64 end = (line + 1 < app->doc.lines.count) ? line_index_start_at(&app->doc.lines, line + 1) : app->doc.len;
      while (end > start)
      {
        char c = 0;
        CopyCtx ctx = { &c, 0 };
        doc_read_range(&app->doc, end - 1, 1, copy_span, &ctx);
        if (c != '\n' && c != '\r') break;
        --end;
      }
      if (end > start)
      {
        u64 width = doc_line_visual_width(&app->doc, start, end);
        if (width > max_line_cols) max_line_cols = width;
      }
    }
    {
      bool need_hscroll = max_line_cols > (u64)page_cols;
      if (need_hscroll != has_hscroll)
        ShowScrollBar(app->hwnd, SB_HORZ, need_hscroll);
      if (!need_hscroll) app->fc = 0;
      memset(&si, 0, sizeof(si));
      si.cbSize = sizeof(si);
      si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
      si.nMin = 0;
      si.nMax = (int)min_u64(max_line_cols ? max_line_cols - 1 : 0, INT32_MAX);
      si.nPage = (UINT)page_cols;
      si.nPos = (int)min_u64(app->fc, INT32_MAX);
      SetScrollInfo(app->hwnd, SB_HORZ, &si, TRUE);
    }
  }
  repro_log_scrollbar((unsigned long long)tl_before, (unsigned long long)app->tl,
                      (unsigned long long)known_lines, (unsigned long long)page,
                      (unsigned long long)max_top, app->doc.lines.eof, app->vsb);
}

static bool doc_discover_step(Document *doc, u64 budget)
{
  LineIndex *li = &doc->lines;
  while (!li->eof && budget) {
    CoreRun run;
    core_run(&doc->rope, li->scanned_to, &run);
    u64 take = min_u64(run.len, budget);
    for (u64 i = 0; i < take; ++i) {
      ++li->scanned_to;
      if (run.data[i] == '\n' && !line_index_push_start(li, li->scanned_to)) return false;
    }
    budget -= take;
    li->eof = li->scanned_to == doc->len;
  }
  return li->eof;
}

static void ensure_honest_vertical_scroll_range(App *app)
{
  if (app->lcp) return;
  if (!app->doc.lines.eof)
    doc_discover_all_lines(&app->doc);
  update_scrollbars(app);
}

static bool should_defer_line_count(const Document *doc)
{
  return doc->len >= LINECOUNT_ASYNC_FILE_BYTES;
}

static void apply_scroll_limits_and_count(App *app)
{
  if (!app->hwnd || !app->lcp) return;
  SetTimer(app->hwnd, LINECOUNT_TIMER_ID, 1, NULL);
}

static void apply_scroll_limits_and_position(App *app)
{
  if (!app->hwnd) return;
  KillTimer(app->hwnd, LINECOUNT_TIMER_ID);
}

static void apply_caret_line_metrics(App *app)
{
  apply_scroll_limits_and_position(app);
  if (should_defer_line_count(&app->doc))
  {
    app->lcp = true;
    app->tl = 0;
    app->vsb = false;
    apply_scroll_limits_and_count(app);
  }
  else
  {
    app->lcp = false;
    doc_discover_all_lines(&app->doc);
  }
}

static void preserve_caret_row_for_scroll(App *app, u64 old_top_line)
{
  u64 bottom_visible;
  (void)old_top_line;
  if (app->rows <= 0)
  {
    set_caret_line_col(app, app->tl, app->cc);
    return;
  }
  bottom_visible = app->tl + (u64)app->rows - 1;
  if (app->cl < app->tl)
    set_caret_line_col(app, app->tl, app->cc);
  else if (app->cl > bottom_visible)
    set_caret_line_col(app, bottom_visible, app->cc);
}

static void keep_caret_visible(App *app)
{
  if (app->cl < app->tl) app->tl = app->cl;
  if (app->cl >= app->tl + (u64)app->rows)
    app->tl = app->cl - (u64)app->rows + 1;
  if (app->cc < app->fc) app->fc = app->cc;
  if (app->cc >= app->fc + (u64)app->cols)
    app->fc = app->cc - (u64)app->cols + 1;
}

static void clamp_caret_to_horizontal_view(App *app)
{
  u64 last_visible_col;
  if (app->cols <= 0) return;
  last_visible_col = app->fc + (u64)app->cols - 1;
  if (app->cc < app->fc)
    set_caret_line_col(app, app->cl, app->fc);
  else if (app->cc > last_visible_col)
    set_caret_line_col(app, app->cl, last_visible_col);
}

static void position_caret(App *app)
{
  if (GetFocus() != app->hwnd) return;
  ensure_caret_shape(app);
  update_scrollbars(app);
  if (app->skip_keep_caret_visible_once)
    app->skip_keep_caret_visible_once = false;
  else
    keep_caret_visible(app);
  u64 visual_line = app->cl;
  if (app->bsa)
    visual_line = app->bal < app->bcl ? app->bal : app->bcl;
  int row = (int)(visual_line - app->tl);
  int col;
  if (app->cc < app->fc)
    col = 0;
  else if (app->cc >= app->fc + (u64)app->cols)
    col = app->cols > 0 ? app->cols - 1 : 0;
  else
    col = (int)(app->cc - app->fc);
  int x = app->gutter_w + EDIT_TEXT_LEFT_PADDING + col * app->char_w;
  int y = row * app->line_h;
  SetCaretPos(x, y);
  update_scrollbars(app);
}

static void request_repaint(App *app, BOOL erase)
{
  InvalidateRect(app->hwnd, NULL, erase);
  position_caret(app);
}

static void force_focus_window(HWND hwnd)
{
  HWND foreground;
  DWORD current_tid;
  DWORD target_tid;
  DWORD foreground_tid;
  BOOL attached_to_target = FALSE;
  BOOL attached_to_foreground = FALSE;
  if (!hwnd || !IsWindow(hwnd)) return;
  current_tid = GetCurrentThreadId();
  target_tid = GetWindowThreadProcessId(hwnd, NULL);
  foreground = GetForegroundWindow();
  foreground_tid = foreground ? GetWindowThreadProcessId(foreground, NULL) : 0;
  if (target_tid && target_tid != current_tid)
    attached_to_target = AttachThreadInput(current_tid, target_tid, TRUE);
  if (foreground_tid && foreground_tid != current_tid && foreground_tid != target_tid)
    attached_to_foreground = AttachThreadInput(current_tid, foreground_tid, TRUE);
  ShowWindow(hwnd, SW_SHOW);
  BringWindowToTop(hwnd);
  SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  SetForegroundWindow(hwnd);
  SetActiveWindow(hwnd);
  SetFocus(hwnd);
  if (attached_to_foreground) AttachThreadInput(current_tid, foreground_tid, FALSE);
  if (attached_to_target) AttachThreadInput(current_tid, target_tid, FALSE);
}

static void keep_visible_and_repaint(App *app)
{
  keep_caret_visible(app);
  request_repaint(app, FALSE);
}

static void set_caret_line_col(App *app, u64 line, u64 col)
{
  doc_ensure_line(&app->doc, line);
  {
    u64 max_line = app->doc.lines.count ? app->doc.lines.count - 1 : 0;
    if (line > max_line) line = max_line;
  }
  app->cl = line;
  app->cc = doc_line_col_clamped(&app->doc, line, col);
  app->co = doc_line_start(&app->doc, app->cl) +
            doc_line_byte_col_from_visual_col(&app->doc, app->cl, app->cc);
  if (app->co > app->doc.len) app->co = app->doc.len;
}

static bool has_stream_selection(const App *app)
{
  return app->sa != app->sv;
}

static bool has_box_selection(const App *app)
{
  return app->bsa;
}

static void clear_box_selection(App *app)
{
  app->bsa = false;
}

static void clear_stream_selection(App *app)
{
  app->sa = app->co;
  app->sv = app->co;
  clear_box_selection(app);
}

static void collapse_stream_selection_to_caret(App *app)
{
  if (!app) return;
  app->sa = app->co;
  app->sv = app->co;
}

static void sync_caret_from_offsets(App *app)
{
  doc_offset_to_line_col(&app->doc, app->co, &app->cl, &app->cc);
}

static bool delete_selection(App *app)
{
  if (!has_stream_selection(app)) return false;
  u64 start = min_u64(app->sa, app->sv);
  u64 end = max_u64(app->sa, app->sv);
  if (end > start)
  {
    app_doc_delete(app, start, end - start);
    app->co = start;
    sync_caret_from_offsets(app);
  }
  clear_stream_selection(app);
  return true;
}

static u64 line_count_known(Document *doc)
{
  doc_discover_for_view(doc, doc->lines.count ? doc->lines.count - 1 : 0, 4096);
  return doc->lines.count ? doc->lines.count : 1;
}

static void set_caret_from_xy(App *app, int x, int y)
{
  u64 row = (u64)(y > 0 ? y : 0) / (u64)app->line_h;
  u64 line = app->tl + row;
  int text_left = app->gutter_w + EDIT_TEXT_LEFT_PADDING;
  int text_x = x > text_left ? x - text_left : 0;
  u64 col = app->fc + (u64)(text_x / app->char_w);
  set_caret_line_col(app, line, col);
}

static void set_caret_from_drag_xy(App *app, int x, int y)
{
  u64 row = (u64)(y > 0 ? y : 0) / (u64)app->line_h;
  u64 line = app->tl + row;
  int text_left = app->gutter_w + EDIT_TEXT_LEFT_PADDING;
  u64 col;
  if (x < text_left)
  {
    u64 cols_left = (u64)((text_left - x + app->char_w - 1) / app->char_w);
    col = app->fc > cols_left ? app->fc - cols_left : 0;
  }
  else
  {
    int text_x = x - text_left;
    col = app->fc + (u64)(text_x / app->char_w);
  }
  set_caret_line_col(app, line, col);
}

static void box_selection_bounds(const App *app, u64 *top, u64 *bottom, u64 *left, u64 *right)
{
  u64 a_line = app->bal;
  u64 c_line = app->bcl;
  u64 a_col = app->bac;
  u64 c_col = app->bcc;
  *top = a_line < c_line ? a_line : c_line;
  *bottom = a_line < c_line ? c_line : a_line;
  *left = a_col < c_col ? a_col : c_col;
  *right = a_col < c_col ? c_col : a_col;
}

static bool app_insert_spaces(App *app, u64 off, u64 count)
{
  if (count == 0) return true;
  char buf[64];
  memset(buf, ' ', sizeof(buf));
  while (count > 0)
  {
    u64 chunk = min_u64(count, (u64)sizeof(buf));
    if (!app_doc_insert(app, off, buf, chunk)) return false;
    off += chunk;
    count -= chunk;
  }
  return true;
}

static u64 app_remove_line_indent(App *app, u64 line)
{
  char prefix[2] = {0, 0};
  CopyCtx ctx = {prefix, 0};
  u64 line_off = doc_line_start(&app->doc, line);
  u64 remove = 0;
  if (line_off >= app->doc.len) return 0;
  doc_read_range(&app->doc, line_off, min_u64(2, app->doc.len - line_off), copy_span, &ctx);
  if (prefix[0] == '\t') remove = 1;
  else
  {
    if (prefix[0] == ' ') remove++;
    if (prefix[0] == ' ' && prefix[1] == ' ') remove++;
  }
  if (remove > 0 && app_doc_delete(app, line_off, remove)) return remove;
  return 0;
}

static u64 app_remove_spaces_at_column(App *app, u64 line, u64 col)
{
  u64 line_off = doc_line_start(&app->doc, line);
  u64 line_end = doc_line_length_clamped(&app->doc, line_off, UINT32_MAX);
  u64 line_len = line_end - line_off;
  u64 visual_len = doc_line_visual_width(&app->doc, line_off, line_end);
  if (col >= visual_len) return 0;

  char probe[2] = {0, 0};
  CopyCtx ctx = {probe, 0};
  u64 remove = 0;
  u64 byte_col = doc_line_byte_col_from_visual_col(&app->doc, line, col);
  u64 at = line_off + byte_col;
  if (byte_col >= line_len) return 0;
  doc_read_range(&app->doc, at, min_u64(2, line_len - byte_col), copy_span, &ctx);
  if (probe[0] == '\t') remove = 1;
  else
  {
    if (probe[0] == ' ') remove++;
    if (probe[0] == ' ' && probe[1] == ' ') remove++;
  }
  if (remove > 0 && app_doc_delete(app, at, remove)) return remove;
  return 0;
}

static u64 app_remove_spaces_near_caret(App *app, u64 *caret_io)
{
  u64 caret = *caret_io;
  u64 removed = 0;
  for (int stage = 0; stage < 2; ++stage)
  {
    bool removed_this_stage = false;
    if (caret > 0)
    {
      char left = 0;
      CopyCtx left_ctx = {&left, 0};
      doc_read_range(&app->doc, caret - 1, 1, copy_span, &left_ctx);
      if (left == ' ' || left == '\t')
      {
        if (app_doc_delete(app, caret - 1, 1))
        {
          caret--;
          removed++;
          removed_this_stage = true;
        }
      }
    }
    if (!removed_this_stage && caret < app->doc.len)
    {
      char right = 0;
      CopyCtx right_ctx = {&right, 0};
      doc_read_range(&app->doc, caret, 1, copy_span, &right_ctx);
      if (right == ' ' || right == '\t')
      {
        if (app_doc_delete(app, caret, 1))
          removed++;
      }
    }
  }
  *caret_io = caret;
  return removed;
}

static bool apply_basic_edit(App *app, const char *insert_text, u64 insert_len, bool delete_backspace, bool delete_forward)
{
  if (!has_box_selection(app)) return false;
  u64 top, bottom, left, right;
  bool changed = false;
  box_selection_bounds(app, &top, &bottom, &left, &right);
  begin_edit_txn(app);
  for (u64 line = bottom + 1; line-- > top;)
  {
    u64 ls = doc_line_start(&app->doc, line);
    u64 le = doc_line_length_clamped(&app->doc, ls, UINT32_MAX);
    u64 ll = le - ls;
    u64 vl = doc_line_visual_width(&app->doc, ls, le);
    u64 del_col_start = left;
    u64 del_col_end = right;
    if (delete_backspace && left == right)
    {
      if (left == 0) continue;
      del_col_start = left - 1;
      del_col_end = left;
    }
    else if (delete_forward && left == right)
    {
      del_col_start = left;
      del_col_end = left + 1;
    }
    if (del_col_start < vl)
    {
      u64 del_start_col = doc_line_byte_col_from_visual_col(&app->doc, line, del_col_start);
      u64 del_end_col = doc_line_byte_col_from_visual_col(&app->doc, line, min_u64(del_col_end, vl));
      if (del_end_col > del_start_col)
      {
        app_doc_delete(app, ls + del_start_col, del_end_col - del_start_col);
        le = doc_line_length_clamped(&app->doc, ls, UINT32_MAX);
        ll = le - ls;
        vl = doc_line_visual_width(&app->doc, ls, le);
        changed = true;
      }
    }
    if (insert_len > 0)
    {
      u64 ins_col = left;
      if (vl < ins_col)
      {
        if (!app_insert_spaces(app, ls + ll, ins_col - vl))
          continue;
        le = doc_line_length_clamped(&app->doc, ls, UINT32_MAX);
        ll = le - ls;
        vl = doc_line_visual_width(&app->doc, ls, le);
        changed = true;
      }
      {
        u64 ins_byte_col = doc_line_byte_col_from_visual_col(&app->doc, line, ins_col);
        if (app_doc_insert(app, ls + ins_byte_col, insert_text, insert_len)) changed = true;
      }
    }
  }
  if (changed)
  {
    update_title(app);
    doc_ensure_line(&app->doc, bottom);
    app->bac = left + utf8_text_columns(insert_text, insert_len);
    app->bcc = left + utf8_text_columns(insert_text, insert_len);
    app->bdc = left + utf8_text_columns(insert_text, insert_len);
    set_caret_line_col(app, app->bcl, app->bdc);
  }
  end_edit_txn(app);
  return changed;
}

static bool apply_box_paste_multiline(App *app, const char *insert_text, u64 insert_len)
{
  u64 top, bottom, left, right;
  u64 seg_count = 1;
  const char **seg_ptrs = NULL;
  u64 *seg_lens = NULL;
  bool changed = false;
  if (!has_box_selection(app) || !insert_text || insert_len == 0) return false;
  box_selection_bounds(app, &top, &bottom, &left, &right);
  for (u64 i = 0; i < insert_len; ++i)
    if (insert_text[i] == '\n')
      seg_count++;
  if (seg_count <= 1) return apply_basic_edit(app, insert_text, insert_len, false, false);

  seg_ptrs = (const char **)malloc(sizeof(*seg_ptrs) * (size_t)seg_count);
  seg_lens = (u64 *)malloc(sizeof(*seg_lens) * (size_t)seg_count);
  if (!seg_ptrs || !seg_lens)
  {
    free(seg_ptrs);
    free(seg_lens);
    return false;
  }

  {
    u64 seg_i = 0;
    u64 start = 0;
    for (u64 i = 0; i <= insert_len; ++i)
    {
      if (i == insert_len || insert_text[i] == '\n')
      {
        u64 len = i - start;
        if (len > 0 && insert_text[start + len - 1] == '\r') len--;
        seg_ptrs[seg_i] = insert_text + start;
        seg_lens[seg_i] = len;
        seg_i++;
        start = i + 1;
      }
    }
  }

  begin_edit_txn(app);
  for (u64 line = bottom + 1; line-- > top;)
  {
    u64 ls = doc_line_start(&app->doc, line);
    u64 le = doc_line_length_clamped(&app->doc, ls, UINT32_MAX);
    u64 ll = le - ls;
    u64 vl = doc_line_visual_width(&app->doc, ls, le);
    u64 del_col_start = left;
    u64 del_col_end = right;
    u64 seg_index = line - top;
    if (seg_index >= seg_count) seg_index = seg_count - 1;
    if (del_col_start < vl)
    {
      u64 del_start_col = doc_line_byte_col_from_visual_col(&app->doc, line, del_col_start);
      u64 del_end_col = doc_line_byte_col_from_visual_col(&app->doc, line, min_u64(del_col_end, vl));
      if (del_end_col > del_start_col)
      {
        app_doc_delete(app, ls + del_start_col, del_end_col - del_start_col);
        le = doc_line_length_clamped(&app->doc, ls, UINT32_MAX);
        ll = le - ls;
        vl = doc_line_visual_width(&app->doc, ls, le);
        changed = true;
      }
    }
    if (seg_lens[seg_index] > 0)
    {
      if (vl < left)
      {
        if (!app_insert_spaces(app, ls + ll, left - vl))
          continue;
        le = doc_line_length_clamped(&app->doc, ls, UINT32_MAX);
        vl = doc_line_visual_width(&app->doc, ls, le);
        changed = true;
      }
      {
        u64 ins_byte_col = doc_line_byte_col_from_visual_col(&app->doc, line, left);
        if (app_doc_insert(app, ls + ins_byte_col, seg_ptrs[seg_index], seg_lens[seg_index]))
          changed = true;
      }
    }
  }
  if (changed)
  {
    u64 active_seg = app->bcl >= top ? app->bcl - top : 0;
    if (active_seg >= seg_count) active_seg = seg_count - 1;
    update_title(app);
    doc_ensure_line(&app->doc, bottom);
    app->bac = left + utf8_text_columns(seg_ptrs[0], seg_lens[0]);
    app->bcc = left + utf8_text_columns(seg_ptrs[active_seg], seg_lens[active_seg]);
    app->bdc = app->bcc;
    set_caret_line_col(app, app->bcl, app->bdc);
  }
  end_edit_txn(app);
  free(seg_ptrs);
  free(seg_lens);
  return changed;
}

static bool extend_box_selection(App *app, WPARAM key)
{
  u64 line;
  u64 col;
  u64 desired_col;
  u64 max_line;
  bool moving_vertical = (key == VK_UP || key == VK_DOWN);
  doc_ensure_line(&app->doc, app->doc.lines.count ? app->doc.lines.count - 1 : 0);
  max_line = app->doc.lines.count ? app->doc.lines.count - 1 : 0;
  if (has_box_selection(app))
  {
    line = app->bcl;
    col = app->bcc;
    desired_col = app->bdc;
  }
  else
  {
    line = app->cl;
    col = app->cc;
    desired_col = app->cc;
    app->bsa = true;
    app->bal = app->cl;
    app->bac = app->cc;
    collapse_stream_selection_to_caret(app);
  }
  switch (key)
  {
  case VK_UP:
    if (line > 0) --line;
    break;
  case VK_DOWN:
    if (line < max_line) ++line;
    break;
  case VK_LEFT:
    if (col > 0) --col;
    desired_col = col;
    break;
  case VK_RIGHT:
    ++col;
    desired_col = col;
    break;
  default:
    return false;
  }
  if (moving_vertical)
  {
    set_caret_line_col(app, line, desired_col);
    app->bcc = desired_col;
  }
  else
  {
    set_caret_line_col(app, line, col);
    app->bcc = col;
  }
  app->bcl = app->cl;
  app->bdc = desired_col;
  collapse_stream_selection_to_caret(app);
  if (app->bal == app->bcl &&
      app->bac == app->bcc)
    clear_box_selection(app);
  return true;
}

static char *trim_ascii(char *s)
{
  char *end;
  while (*s == ' ' || *s == '\t') ++s;
  end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    *--end = 0;
  return s;
}

static void command_popup_update_matches(App *app)
{
  int prev_sel = LB_ERR;
  int i;
  if (!app->cmd_list) return;
  prev_sel = (int)SendMessageW(app->cmd_list, LB_GETCURSEL, 0, 0);
  app->cmd_match_count = 0;
  SendMessageW(app->cmd_list, LB_RESETCONTENT, 0, 0);
  for (i = 0; i < app->cmd_entry_count; ++i)
  {
    const WCHAR *first = app->cmd_entries[i].first;
    size_t qlen = wcslen(app->cmd_query);
    if (qlen != 0 && _wcsnicmp(first, app->cmd_query, qlen) != 0) continue;
    if (app->cmd_match_count < MAX_DO_COMMANDS)
    {
      app->cmd_match_indices[app->cmd_match_count] = i;
      SendMessageW(app->cmd_list, LB_ADDSTRING, 0, (LPARAM)app->cmd_entries[i].display);
      app->cmd_match_count++;
    }
  }
  if (app->cmd_match_count > 0)
  {
    int sel = prev_sel;
    if (sel < 0 || sel >= app->cmd_match_count) sel = 0;
    SendMessageW(app->cmd_list, LB_SETCURSEL, (WPARAM)sel, 0);
  }
  InvalidateRect(app->cmd_edit, NULL, TRUE);
}

static bool load_do_commands(App *app)
{
  FILE *f = NULL;
  char line[512];
  WCHAR exe_path[MAX_PATH];
  WCHAR cmd_path[MAX_PATH];
  WCHAR *slash = NULL;
  DWORD exe_len = 0;
  app->cmd_entry_count = 0;
  exe_len = GetModuleFileNameW(NULL, exe_path, (DWORD)_countof(exe_path));
  if (exe_len == 0 || exe_len >= (DWORD)_countof(exe_path)) return false;
  wcs_copy_trunc(cmd_path, _countof(cmd_path), exe_path);
  slash = wcsrchr(cmd_path, L'\\');
  if (!slash) slash = wcsrchr(cmd_path, L'/');
  if (!slash) return false;
  slash[1] = 0;
  wcs_cat_trunc(cmd_path, _countof(cmd_path), L"doCommands.txt");
  if (_wfopen_s(&f, cmd_path, L"rb") != 0) f = NULL;
  if (!f) return false;
  while (fgets(line, (int)sizeof(line), f))
  {
    char *first;
    char *second;
    char *third;
    DoCommandEntry *entry;
    if (app->cmd_entry_count >= MAX_DO_COMMANDS) break;
    first = trim_ascii(line);
    if (!*first) continue;
    second = strchr(first, ':');
    if (!second) continue;
    *second++ = 0;
    third = strchr(second, ':');
    if (third) *third = 0;
    first = trim_ascii(first);
    second = trim_ascii(second);
    if (!*first) continue;
    entry = &app->cmd_entries[app->cmd_entry_count];
    MultiByteToWideChar(CP_UTF8, 0, first, -1, entry->first, (int)_countof(entry->first));
    MultiByteToWideChar(CP_UTF8, 0, second, -1, entry->second, (int)_countof(entry->second));
    if (entry->second[0])
      wfmt(entry->display, _countof(entry->display), L"%ls (%ls)", entry->first, entry->second);
    else
      wfmt(entry->display, _countof(entry->display), L"%ls", entry->first);
    app->cmd_entry_count++;
  }
  fclose(f);
  return app->cmd_entry_count > 0;
}

static void command_popup_move_selection(App *app, int delta)
{
  int sel;
  if (!app->cmd_list || app->cmd_match_count <= 0) return;
  sel = (int)SendMessageW(app->cmd_list, LB_GETCURSEL, 0, 0);
  if (sel == LB_ERR) sel = 0;
  sel += delta;
  if (sel < 0) sel = app->cmd_match_count - 1;
  if (sel >= app->cmd_match_count) sel = 0;
  SendMessageW(app->cmd_list, LB_SETCURSEL, (WPARAM)sel, 0);
  InvalidateRect(app->cmd_edit, NULL, TRUE);
}

static bool command_popup_get_completion(App *app, WCHAR *out, size_t out_count)
{
  int sel;
  int idx;
  if (!out || out_count == 0) return false;
  out[0] = 0;
  if (!app->cmd_list || app->cmd_match_count <= 0) return false;
  sel = (int)SendMessageW(app->cmd_list, LB_GETCURSEL, 0, 0);
  if (sel == LB_ERR) sel = 0;
  if (sel < 0 || sel >= app->cmd_match_count) return false;
  idx = app->cmd_match_indices[sel];
  wcs_copy_n_trunc(out, out_count, app->cmd_entries[idx].first, (size_t)-1);
  return true;
}

static void command_edit_delete_previous_word(HWND hwnd)
{
  DWORD start = 0;
  DWORD end = 0;
  WCHAR text[128];
  int len;
  int pos;
  int cut;
  SendMessageW(hwnd, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
  if (start != end)
  {
    SendMessageW(hwnd, WM_CLEAR, 0, 0);
    return;
  }
  len = GetWindowTextW(hwnd, text, (int)_countof(text));
  if (len <= 0 || start == 0) return;
  if (start > (DWORD)len) start = (DWORD)len;
  pos = (int)start;
  cut = pos;
  while (cut > 0 && iswspace((wint_t)text[cut - 1])) cut--;
  while (cut > 0 && !iswspace((wint_t)text[cut - 1])) cut--;
  if (cut == pos) return;
  SendMessageW(hwnd, EM_SETSEL, (WPARAM)cut, (LPARAM)pos);
  SendMessageW(hwnd, WM_CLEAR, 0, 0);
}

static bool is_ctrl_backspace_char_message(WPARAM wp)
{
  return wp == 0x7F;
}

static LRESULT CALLBACK command_edit_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  App *app = &g_app;
  if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
  if (msg == WM_KEYDOWN)
  {
    if ((GetKeyState(VK_CONTROL) & 0x8000) && wp == VK_BACK)
    {
      command_edit_delete_previous_word(hwnd);
      return 0;
    }
    if (wp == VK_UP) { command_popup_move_selection(app, -1); return 0; }
    if (wp == VK_DOWN) { command_popup_move_selection(app, 1); return 0; }
    if (wp == VK_RETURN) { close_command_popup(app); return 0; }
    if (wp == VK_TAB) { close_command_popup(app); return 0; }
    if (wp == VK_ESCAPE) { close_command_popup(app); return 0; }
  }
  if (msg == WM_CHAR)
  {
    if (((GetKeyState(VK_CONTROL) & 0x8000) && wp == VK_BACK) || is_ctrl_backspace_char_message(wp)) return 0;
    if (wp == VK_RETURN || wp == VK_TAB || wp == VK_ESCAPE) return 0;
  }
  if (msg == WM_PAINT && app->cmd_edit == hwnd)
  {
    LRESULT r = CallWindowProcW(g_command_edit_wndproc, hwnd, msg, wp, lp);
    {
      WCHAR typed[128];
      WCHAR completion[64];
      size_t typed_len;
      size_t comp_len;
      if (GetWindowTextW(hwnd, typed, (int)_countof(typed)) <= 0) return r;
      if (!command_popup_get_completion(app, completion, _countof(completion))) return r;
      typed_len = wcslen(typed);
      comp_len = wcslen(completion);
      if (typed_len < comp_len && _wcsnicmp(completion, typed, typed_len) == 0)
      {
        HDC dc = GetDC(hwnd);
        if (dc)
        {
          HFONT font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
          HFONT old_font = NULL;
          RECT rc;
          SIZE sz = { 0, 0 };
          DWORD margins = (DWORD)SendMessageW(hwnd, EM_GETMARGINS, 0, 0);
          int left = LOWORD(margins);
          if (font) old_font = (HFONT)SelectObject(dc, font);
          GetClientRect(hwnd, &rc);
          GetTextExtentPoint32W(dc, typed, (int)typed_len, &sz);
          SetBkMode(dc, TRANSPARENT);
          SetTextColor(dc, RGB(135, 135, 135));
          TextOutW(dc, left + 1 + sz.cx, (rc.bottom - rc.top - app->line_h) / 2,
                   completion + typed_len, (int)(comp_len - typed_len));
          if (old_font) SelectObject(dc, old_font);
          ReleaseDC(hwnd, dc);
        }
      }
    }
    return r;
  }
  return CallWindowProcW(g_command_edit_wndproc, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK command_popup_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  App *app = &g_app;
  switch (msg)
  {
  case WM_ERASEBKGND:
  {
    RECT rc;
    HDC dc = (HDC)wp;
    if (dc && GetClientRect(hwnd, &rc))
    {
      FillRect(dc, &rc, app->cmd_bg_brush ? app->cmd_bg_brush : (app->bg_brush ? app->bg_brush : (HBRUSH)(COLOR_WINDOW + 1)));
      return 1;
    }
    break;
  }
  case WM_CREATE:
  {
    app->cmd_edit = CreateWindowExW(0, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                    8, 8, 344, 28, hwnd, (HMENU)(INT_PTR)IDC_CMD_EDIT, GetModuleHandleW(NULL), NULL);
    app->cmd_list = CreateWindowExW(0, L"LISTBOX", L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
                                    8, 42, 344, 120, hwnd, (HMENU)(INT_PTR)IDC_CMD_LIST, GetModuleHandleW(NULL), NULL);
    if (app->font)
    {
      SendMessageW(app->cmd_edit, WM_SETFONT, (WPARAM)app->font, TRUE);
      SendMessageW(app->cmd_list, WM_SETFONT, (WPARAM)app->font, TRUE);
    }
    g_command_edit_wndproc = (WNDPROC)SetWindowLongPtrW(app->cmd_edit, GWLP_WNDPROC, (LONG_PTR)command_edit_wndproc);
    SetFocus(app->cmd_edit);
    return 0;
  }
  case WM_SIZE:
  {
    int w = LOWORD(lp);
    int h = HIWORD(lp);
    if (app->cmd_edit) MoveWindow(app->cmd_edit, 8, 8, w - 16, 28, TRUE);
    if (app->cmd_list) MoveWindow(app->cmd_list, 8, 42, w - 16, h - 50, TRUE);
    return 0;
  }
  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    RECT rc;
    BeginPaint(hwnd, &ps);
    if (GetClientRect(hwnd, &rc))
      FillRect(ps.hdc, &rc, app->cmd_bg_brush ? app->cmd_bg_brush : (app->bg_brush ? app->bg_brush : (HBRUSH)(COLOR_WINDOW + 1)));
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_ACTIVATE:
    if (LOWORD(wp) == WA_INACTIVE)
    {
      DestroyWindow(hwnd);
      return 0;
    }
    break;
  case WM_COMMAND:
    if (LOWORD(wp) == IDC_CMD_EDIT && HIWORD(wp) == EN_CHANGE)
    {
      GetWindowTextW(app->cmd_edit, app->cmd_query, (int)_countof(app->cmd_query));
      command_popup_update_matches(app);
      return 0;
    }
    if (LOWORD(wp) == IDC_CMD_LIST && HIWORD(wp) == LBN_DBLCLK)
    {
      close_command_popup(app);
      return 0;
    }
    break;
  case WM_CTLCOLORLISTBOX:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORSTATIC:
  {
    HDC dc = (HDC)wp;
    SetTextColor(dc, THEME_TEXT);
    SetBkColor(dc, THEME_CMD_POPUP_BG);
    return (LRESULT)(app->cmd_bg_brush ? app->cmd_bg_brush : app->bg_brush);
  }
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    if (app->cmd_hwnd == hwnd)
    {
      HWND owner = GetWindow(hwnd, GW_OWNER);
      if (!owner) owner = app->hwnd;
      app->cmd_hwnd = NULL;
      app->cmd_edit = NULL;
      app->cmd_list = NULL;
      if (owner && IsWindow(owner))
      {
        PostMessageW(owner, WM_APP_RESTORE_EDITOR_FOCUS, 0, 0);
      }
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static void close_command_popup(App *app)
{
  if (app->cmd_hwnd && IsWindow(app->cmd_hwnd))
    DestroyWindow(app->cmd_hwnd);
}

static void open_command_popup(App *app)
{
  RECT rc = { 0 };
  POINT p = { 0 };
  int x = 40;
  int y = 40;
  int w = 360;
  int h = 180;
  if (app->cmd_hwnd && IsWindow(app->cmd_hwnd))
  {
    SetForegroundWindow(app->cmd_hwnd);
    SetFocus(app->cmd_edit);
    return;
  }
  if (!load_do_commands(app))
  {
    MessageBoxW(app->hwnd, L"Could not load doCommands.txt.", L"text", MB_ICONERROR | MB_OK);
    return;
  }
  app->cmd_query[0] = 0;
  app->cmd_restore_pending = true;
  app->cmd_saved_co = app->co;
  app->cmd_saved_sa = app->sa;
  app->cmd_saved_sv = app->sv;
  if (GetCaretPos(&p))
  {
    ClientToScreen(app->hwnd, &p);
    x = p.x - 9;
    y = p.y - 8;
  }
  else if (GetClientRect(app->hwnd, &rc))
  {
    p.x = app->gutter_w + EDIT_TEXT_LEFT_PADDING + (int)((app->cc - app->fc) * (u64)app->char_w);
    p.y = (int)((app->cl - app->tl) * (u64)app->line_h);
    if (p.x < 0) p.x = 0;
    if (p.y < 0) p.y = 0;
    ClientToScreen(app->hwnd, &p);
    x = p.x;
    y = p.y;
  }
  app->cmd_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CMD_POPUP_CLASS_NAME, L"do",
                                  WS_POPUP,
                                  x, y, w, h, app->hwnd, NULL, GetModuleHandleW(NULL), NULL);
  if (!app->cmd_hwnd)
  {
    app->cmd_restore_pending = false;
    return;
  }
  ShowWindow(app->cmd_hwnd, SW_SHOW);
  UpdateWindow(app->cmd_hwnd);
  command_popup_update_matches(app);
}

static void find_result_preview(Document *doc, u64 line, WCHAR *out, size_t out_count)
{
  u64 start;
  u64 end;
  u64 len;
  char buf[120];
  CopyCtx ctx = { buf, 0 };
  int wlen;
  if (!out || out_count == 0)
    return;
  out[0] = 0;
  if (!doc || out_count < 2) return;
  start = doc_line_start(doc, line);
  end = (line + 1 < doc->lines.count) ? doc_line_start(doc, line + 1) : doc->len;
  while (end > start)
  {
    char tail[2] = { 0, 0 };
    CopyCtx tail_ctx = { tail, 0 };
    doc_read_range(doc, end - 1, 1, copy_span, &tail_ctx);
    if (tail[0] == '\n' || tail[0] == '\r') end--;
    else break;
  }
  len = min_u64(end - start, (u64)(sizeof(buf) - 1));
  memset(buf, 0, sizeof(buf));
  if (len > 0) doc_read_range(doc, start, len, copy_span, &ctx);
  buf[ctx.at] = 0;
  wlen = MultiByteToWideChar(CP_UTF8, 0, buf, -1, out, (int)out_count);
  if (wlen <= 0)
    out[0] = 0;
}

static void find_popup_select_result(App *app, int index)
{
  FindResult *result;
  if (!app || index < 0 || index >= app->find_result_count) return;
  result = &app->find_results[index];
  app->find_selected_result = index;
  app->sa = result->off;
  app->sv = min_u64(result->off + result->len, app->doc.len);
  app->co = app->sv;
  doc_offset_to_line_col(&app->doc, app->co, &app->cl, &app->cc);
  keep_visible_and_repaint(app);
  if (app->find_list)
  {
    SendMessageW(app->find_list, LB_SETCURSEL, (WPARAM)index, 0);
    SendMessageW(app->find_list, LB_SETTOPINDEX, (WPARAM)index, 0);
  }
}

static void find_popup_activate_selected(App *app)
{
  int sel;
  if (!app || app->find_result_count <= 0) return;
  sel = (int)SendMessageW(app->find_list, LB_GETCURSEL, 0, 0);
  if (sel == LB_ERR) sel = 0;
  find_popup_select_result(app, sel);
  if (sel + 1 < app->find_result_count)
  {
    SendMessageW(app->find_list, LB_SETCURSEL, (WPARAM)(sel + 1), 0);
    SendMessageW(app->find_list, LB_SETTOPINDEX, (WPARAM)(sel + 1), 0);
  }
}

static void find_popup_update_matches(App *app)
{
  int qbytes;
  char *q = NULL;
  char *hay = NULL;
  u64 qlen;
  u64 count = 0;
  u64 i = 0;
  WCHAR count_text[96];
  if (!app || !app->find_list || !app->find_count_label) return;
  app->find_result_count = 0;
  app->find_selected_result = -1;
  SendMessageW(app->find_list, LB_RESETCONTENT, 0, 0);
  qbytes = WideCharToMultiByte(CP_UTF8, 0, app->fq, -1, NULL, 0, NULL, NULL);
  if (qbytes <= 1 || app->doc.len == 0)
  {
    SetWindowTextW(app->find_count_label, L"0 entities found");
    InvalidateRect(app->find_list, NULL, TRUE);
    return;
  }
  q = (char *)malloc((size_t)qbytes);
  hay = (char *)malloc((size_t)app->doc.len);
  if (!q || !hay)
  {
    free(q);
    free(hay);
    SetWindowTextW(app->find_count_label, L"0 entities found");
    return;
  }
  WideCharToMultiByte(CP_UTF8, 0, app->fq, -1, q, qbytes, NULL, NULL);
  qlen = (u64)qbytes - 1;
  {
    SearchCtx sctx = { hay, 0 };
    doc_read_range(&app->doc, 0, app->doc.len, append_span, &sctx);
  }
  while (i + qlen <= app->doc.len)
  {
    if (memcmp(hay + i, q, (size_t)qlen) == 0)
    {
      u64 line = 0;
      u64 col = 0;
      count++;
      if (app->find_result_count < MAX_FIND_RESULTS)
      {
        FindResult *result = &app->find_results[app->find_result_count];
        result->off = i;
        result->len = qlen;
        doc_offset_to_line_col(&app->doc, i, &line, &col);
        result->line = line;
        find_result_preview(&app->doc, line, result->preview, _countof(result->preview));
        SendMessageW(app->find_list, LB_ADDSTRING, 0, (LPARAM)result->preview);
        app->find_result_count++;
      }
      i += qlen;
    }
    else
      i++;
  }
  wfmt(count_text, _countof(count_text), L"%llu entities found", (unsigned long long)count);
  SetWindowTextW(app->find_count_label, count_text);
  if (app->find_result_count > 0)
  {
    SendMessageW(app->find_list, LB_SETCURSEL, 0, 0);
    find_popup_select_result(app, 0);
    SendMessageW(app->find_list, LB_SETCURSEL, 1 < app->find_result_count ? 1 : 0, 0);
  }
  free(q);
  free(hay);
  InvalidateRect(app->find_list, NULL, TRUE);
}

static LRESULT CALLBACK find_popup_edit_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  App *app = &g_app;
  if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
  if (msg == WM_KEYDOWN)
  {
    if ((GetKeyState(VK_CONTROL) & 0x8000) && wp == VK_BACK)
    {
      command_edit_delete_previous_word(hwnd);
      return 0;
    }
    if (wp == VK_UP)
    {
      if (app->find_list) SendMessageW(app->find_list, WM_KEYDOWN, VK_UP, 0);
      return 0;
    }
    if (wp == VK_DOWN)
    {
      if (app->find_list) SendMessageW(app->find_list, WM_KEYDOWN, VK_DOWN, 0);
      return 0;
    }
    if (wp == VK_RETURN)
    {
      find_popup_activate_selected(app);
      return 0;
    }
    if (wp == VK_ESCAPE)
    {
      close_find_popup(app);
      return 0;
    }
  }
  if (msg == WM_CHAR)
  {
    if (((GetKeyState(VK_CONTROL) & 0x8000) && wp == VK_BACK) || is_ctrl_backspace_char_message(wp)) return 0;
    if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
  }
  return CallWindowProcW(g_find_popup_edit_wndproc, hwnd, msg, wp, lp);
}

static bool parse_line_number(const WCHAR *text, u64 *out_line)
{
  u64 value = 0;
  bool saw_digit = false;
  const WCHAR *p = text;
  if (!text || !out_line) return false;
  while (*p && iswspace((wint_t)*p)) ++p;
  while (*p >= L'0' && *p <= L'9')
  {
    u64 digit = (u64)(*p - L'0');
    saw_digit = true;
    if (value > (UINT64_MAX - digit) / 10)
      value = UINT64_MAX;
    else
      value = value * 10 + digit;
    ++p;
  }
  while (*p && iswspace((wint_t)*p)) ++p;
  if (!saw_digit || *p != 0 || value == 0) return false;
  *out_line = value - 1;
  return true;
}

static bool goto_line_from_popup(App *app)
{
  u64 line = 0;
  if (!app || !app->goto_edit) return false;
  GetWindowTextW(app->goto_edit, app->goto_query, (int)_countof(app->goto_query));
  if (!parse_line_number(app->goto_query, &line))
  {
    if (app->goto_label) SetWindowTextW(app->goto_label, L"Enter a positive line number");
    return false;
  }
  set_caret_line_col(app, line, 0);
  clear_stream_selection(app);
  clear_box_selection(app);
  app->vsb = false;
  close_goto_popup(app);
  keep_visible_and_repaint(app);
  return true;
}

static LRESULT CALLBACK goto_popup_edit_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  App *app = &g_app;
  if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
  if (msg == WM_KEYDOWN)
  {
    if ((GetKeyState(VK_CONTROL) & 0x8000) && wp == VK_BACK)
    {
      command_edit_delete_previous_word(hwnd);
      return 0;
    }
    if (wp == VK_RETURN)
    {
      goto_line_from_popup(app);
      return 0;
    }
    if (wp == VK_ESCAPE)
    {
      close_goto_popup(app);
      return 0;
    }
  }
  if (msg == WM_CHAR)
  {
    if (((GetKeyState(VK_CONTROL) & 0x8000) && wp == VK_BACK) || is_ctrl_backspace_char_message(wp)) return 0;
    if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
  }
  return CallWindowProcW(g_goto_popup_edit_wndproc, hwnd, msg, wp, lp);
}



/******************************************************************************
 * Keyboard / Character Input
 ******************************************************************************/
static void handle_key(App *app, WPARAM vk)
{
  app->vsb = false;
  bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
  u64 old_off = app->co;
  bool moved = false;
  if (vk == VK_ESCAPE)
  {
    if (shift)
    {
      DestroyWindow(app->hwnd);
      return;
    }
    if (app->find_hwnd && IsWindow(app->find_hwnd))
      DestroyWindow(app->find_hwnd);
    else if (app->goto_hwnd && IsWindow(app->goto_hwnd))
      DestroyWindow(app->goto_hwnd);
    else
      PostMessageW(app->hwnd, WM_CLOSE, 0, 0);
    return;
  }
  if (ctrl && vk == 'O') { open_dialog(app); return; }
  if (ctrl && !shift && !alt && vk == 'N') { launch_new_window(app); return; }
  if (ctrl && !shift && vk == 'S') { save_dialog(app); return; }
  if (ctrl && shift && vk == 'S') { save_as_dialog(app); return; }
  if (ctrl && vk == 'C')
  {
    if (has_box_selection(app))
    {
      copy_box_selection_to_clipboard(app);
      app->clm = false;
    }
    else if (has_stream_selection(app))
    {
      copy_selection_or_current_line_to_clipboard(app);
      app->clm = false;
    }
    else
    {
      copy_current_line_to_clipboard(app);
      app->clm = true;
    }
    return;
  }
  if (ctrl && vk == 'X')
  {
    if (has_box_selection(app))
    {
      begin_edit_txn(app);
      copy_box_selection_to_clipboard(app);
      app->clm = false;
      if (apply_basic_edit(app, NULL, 0, false, false))
      {
        update_title(app);
        request_repaint(app, FALSE);
      }
      end_edit_txn(app);
      return;
    }
    if (has_stream_selection(app))
    {
      begin_edit_txn(app);
      copy_selection_or_current_line_to_clipboard(app);
      app->clm = false;
      if (delete_selection(app))
      {
        update_title(app);
        request_repaint(app, FALSE);
      }
    }
    else
    {
      u64 start = 0;
      u64 end = 0;
      u64 old_col = app->cc;
      if (current_line_range(app, &start, &end))
      {
        copy_current_line_to_clipboard(app);
        app->clm = true;
        begin_edit_txn(app);
        app_doc_delete(app, start, end - start);
        app->co = start;
        app->sa = app->co;
        app->sv = app->co;
        sync_caret_from_offsets(app);
        set_caret_line_col(app, app->cl, old_col);
        keep_caret_visible(app);
        update_title(app);
        request_repaint(app, FALSE);
      }
    }
    end_edit_txn(app);
    return;
  }
  if (ctrl && vk == 'A')
  {
    app->sa = 0;
    app->sv = app->doc.len;
    app->co = app->doc.len;
    sync_caret_from_offsets(app);
    keep_visible_and_repaint(app);
    return;
  }
  if (ctrl && !shift && vk == 'Z') { perform_undo(app); return; }
  if ((ctrl && !shift && vk == 'Y') || (ctrl && shift && vk == 'Z')) { perform_redo(app); return; }
  if (ctrl && !shift && !alt && vk == 'D') { open_command_popup(app); return; }
  if (ctrl && alt && vk == 'D') { run_codex_do_command(app); return; }
  if (ctrl && shift && vk == 'D')
  {
    if (app->doc.dirty)
    {
      if (!save_dialog(app)) return;
    }
    if (run_codex_do_command(app))
      PostMessageW(app->hwnd, WM_CLOSE, 0, 0);
    return;
  }
  if (ctrl && (vk == VK_OEM_MINUS || vk == VK_SUBTRACT)) { apply_font_size(app, app->font_size - 2); return; }
  if (ctrl && (vk == VK_OEM_PLUS || vk == VK_ADD)) { apply_font_size(app, app->font_size + 2); return; }
  if (ctrl && (vk == '0' || vk == VK_NUMPAD0)) { apply_font_size(app, FONT_SIZE_DEFAULT); return; }
  if (ctrl && vk == 'V') { paste_clipboard(app); return; }
  if (ctrl && vk == 'F')
  {
    seed_find_query_from_selection(app);
    open_find_popup(app);
    return;
  }
  if (ctrl && vk == 'G')
  {
    open_goto_popup(app);
    return;
  }
  if (vk == VK_F3) { find_next(app, app->fq); return; }
  if (alt && !shift && (vk == VK_UP || vk == VK_DOWN))
  {
    if (move_caret_line_by_swap(app, vk == VK_DOWN))
      update_title(app);
    keep_visible_and_repaint(app);
    return;
  }
  if (alt && shift &&
      (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT))
  {
    extend_box_selection(app, vk);
    keep_visible_and_repaint(app);
    return;
  }
  if (vk == VK_TAB && !has_stream_selection(app))
  {
    if (has_box_selection(app))
    {
      u64 top, bottom, left, right;
      u64 line;
      u64 removed_anchor = 0;
      u64 removed_active = 0;
      bool changed = false;
      box_selection_bounds(app, &top, &bottom, &left, &right);
      (void)left;
      (void)right;
      begin_edit_txn(app);
      if (!shift)
      {
        for (line = 0; line <= bottom - top; ++line)
        {
          u64 line_off = doc_line_start(&app->doc, top + line);
          u64 line_end = doc_line_length_clamped(&app->doc, line_off, UINT32_MAX);
          u64 line_len = line_end - line_off;
          u64 visual_len = doc_line_visual_width(&app->doc, line_off, line_end);
          if (visual_len < left)
          {
            if (!app_insert_spaces(app, line_off + line_len, left - visual_len))
              continue;
            line_end = doc_line_length_clamped(&app->doc, line_off, UINT32_MAX);
            line_len = line_end - line_off;
          }
          {
            u64 byte_col = doc_line_byte_col_from_visual_col(&app->doc, top + line, left);
            if (app_doc_insert(app, line_off + byte_col, "  ", 2))
              changed = true;
          }
        }
        if (changed)
        {
          app->bac += 2;
          app->bcc += 2;
          app->bdc += 2;
        }
      }
      else
      {
        u64 remove_col = left >= 2 ? left - 2 : left;
        for (line = 0; line <= bottom - top; ++line)
        {
          u64 current_line = top + line;
          u64 removed = app_remove_spaces_at_column(app, current_line, remove_col);
          if (current_line == app->bal) removed_anchor = removed;
          if (current_line == app->bcl) removed_active = removed;
          if (removed > 0) changed = true;
        }
        if (changed)
        {
          app->bac -= min_u64(app->bac, removed_anchor);
          app->bcc -= min_u64(app->bcc, removed_active);
          app->bdc -= min_u64(app->bdc, removed_active);
        }
      }
      if (changed)
      {
        update_title(app);
        set_caret_line_col(app, app->bcl, app->bcc);
      }
      end_edit_txn(app);
      app->suppress_tab_char_once = true;
      keep_visible_and_repaint(app);
      return;
    }
    if (!shift)
    {
      if (apply_basic_edit(app, "  ", 2, false, false))
      {
        app->suppress_tab_char_once = true;
        keep_visible_and_repaint(app);
      }
      return;
    }
    else
    {
      u64 caret = app->co;
      u64 removed = 0;
      begin_edit_txn(app);
      removed = app_remove_spaces_near_caret(app, &caret);
      if (removed == 0)
      {
        u64 removed_indent = app_remove_line_indent(app, app->cl);
        if (removed_indent > 0)
        {
          u64 line_start = doc_line_start(&app->doc, app->cl);
          u64 col = caret >= line_start ? caret - line_start : 0;
          caret -= min_u64(col, removed_indent);
          removed = removed_indent;
        }
      }
      if (removed > 0)
      {
        app->co = caret;
        clear_stream_selection(app);
        sync_caret_from_offsets(app);
        update_title(app);
        keep_visible_and_repaint(app);
      }
      end_edit_txn(app);
      app->suppress_tab_char_once = true;
      return;
    }
  }
  if (vk == VK_TAB && has_stream_selection(app))
  {
    u64 sel_start = min_u64(app->sa, app->sv);
    u64 sel_end = max_u64(app->sa, app->sv);
    u64 start_line = 0;
    u64 end_line = 0;
    u64 ignored_col = 0;
    doc_offset_to_line_col(&app->doc, sel_start, &start_line, &ignored_col);
    doc_offset_to_line_col(&app->doc, sel_end, &end_line, &ignored_col);
    u64 end_line_start = doc_line_start(&app->doc, end_line);
    bool include_end_line = (sel_end > end_line_start) || (sel_start == sel_end);
    u64 line_count = include_end_line ? (end_line - start_line + 1) : (end_line - start_line);
    u64 line;
    u64 delta_total = 0;
    bool changed = false;

    if (line_count == 0) return;

    begin_edit_txn(app);
    if (!shift)
    {
      for (line = 0; line < line_count; ++line)
      {
        u64 line_off = doc_line_start(&app->doc, start_line + line);
        if (app_doc_insert(app, line_off, "  ", 2))
        {
          delta_total += 2;
          changed = true;
        }
      }
      if (changed)
      {
        app->sa = sel_start + 2;
        app->sv = sel_end + delta_total;
      }
    }
    else
    {
      u64 removed_first_line = 0;
      for (line = 0; line < line_count; ++line)
      {
        char prefix[2] = {0, 0};
        CopyCtx ctx = {prefix, 0};
        u64 line_off = doc_line_start(&app->doc, start_line + line);
        u64 remove = 0;
        doc_read_range(&app->doc, line_off, min_u64(2, app->doc.len - line_off), copy_span, &ctx);
        if (prefix[0] == '\t') remove = 1;
        else
        {
          if (prefix[0] == ' ') remove++;
          if (prefix[0] == ' ' && prefix[1] == ' ') remove++;
        }
        if (remove > 0)
        {
          app_doc_delete(app, line_off, remove);
          if (line == 0) removed_first_line = remove;
          delta_total += remove;
          changed = true;
        }
      }
      if (changed)
      {
        u64 original_line_start = doc_line_start(&app->doc, start_line);
        u64 start_col = sel_start - original_line_start;
        u64 removed_at_start = min_u64(removed_first_line, start_col);
        app->sa = sel_start - removed_at_start;
        app->sv = sel_end - delta_total;
      }
    }
    if (changed)
    {
      app->co = app->sv;
      sync_caret_from_offsets(app);
      update_title(app);
    }
    app->suppress_tab_char_once = true;
    end_edit_txn(app);
    keep_visible_and_repaint(app);
    return;
  }
  switch (vk)
  {
  case VK_LEFT:
    if (ctrl) move_caret_word_left(app); else move_caret_left(app);
    moved = true;
    break;
  case VK_RIGHT:
    if (ctrl) move_caret_word_right(app); else move_caret_right(app);
    moved = true;
    break;
  case VK_UP:
    if (ctrl)
    {
      u64 old_tl = app->tl;
      app->vsb = false;
      if (app->tl > 0) app->tl--;
      update_scrollbars(app);
      if (app->tl != old_tl)
      {
        preserve_caret_row_for_scroll(app, old_tl);
        moved = true;
      }
    }
    else
    {
      if (app->cl > 0)
        set_caret_line_col(app, app->cl - 1, app->cc);
      else
        set_caret_line_col(app, 0, 0);
      moved = true;
    }
    break;
  case VK_DOWN:
    if (ctrl)
    {
      u64 old_tl = app->tl;
      app->vsb = false;
      app->tl++;
      update_scrollbars(app);
      if (app->tl != old_tl)
      {
        preserve_caret_row_for_scroll(app, old_tl);
        moved = true;
      }
    }
    else
    {
      doc_discover_for_view(&app->doc, app->cl, 2);
      if (app->cl + 1 < app->doc.lines.count)
        set_caret_line_col(app, app->cl + 1, app->cc);
      else
      {
        u64 start = doc_line_start(&app->doc, app->cl);
        u64 end = doc_line_length_clamped(&app->doc, start, UINT32_MAX);
        app->cc = doc_line_visual_width(&app->doc, start, end);
        app->co = end;
      }
      moved = true;
    }
    break;
  case VK_PRIOR:
    set_caret_line_col(app, app->cl > (u64)app->rows ? app->cl - (u64)app->rows : 0, app->cc);
    moved = true;
    break;
  case VK_NEXT:
    set_caret_line_col(app, app->cl + (u64)app->rows, app->cc);
    doc_discover_for_view(&app->doc, app->cl, (u64)app->rows);
    moved = true;
    break;
  case VK_HOME:
    if (ctrl)
    {
      set_caret_line_col(app, 0, 0);
    }
    else
    {
      u64 first_nonblank = doc_line_first_nonblank_col(&app->doc, app->cl);
      u64 target_col = (app->cc == 0) ? first_nonblank : 0;
      set_caret_line_col(app, app->cl, target_col);
    }
    moved = true;
    break;
  case VK_END:
  {
    u64 line = app->cl;
    if (ctrl)
    {
      doc_discover_all_lines(&app->doc);
      line = app->doc.lines.count ? app->doc.lines.count - 1 : 0;
      app->cl = line;
    }
    u64 start = doc_line_start(&app->doc, line);
    u64 end = doc_line_length_clamped(&app->doc, start, UINT32_MAX);
    app->cc = doc_line_visual_width(&app->doc, start, end);
    app->co = end;
    moved = true;
  } break;
  case VK_BACK:
  {
    if (has_box_selection(app))
    {
      apply_basic_edit(app, NULL, 0, true, false);
      break;
    }
    if (has_stream_selection(app))
    {
      begin_edit_txn(app);
      if (delete_selection(app))
      {
        update_title(app);
        break;
      }
    }
    if (ctrl && app->co > 0)
    {
      begin_edit_txn(app);
      u64 old_off = app->co;
      move_caret_word_left(app);
      if (app->co < old_off)
      {
        app_doc_delete(app, app->co, old_off - app->co);
        update_title(app);
      }
      break;
    }
    if (app->co > 0)
    {
      begin_edit_txn(app);
      u64 off = app->co;
      u64 del_start = doc_previous_character(&app->doc, off);
      u64 del = off - del_start;
      if (app->cc == 0 && app->cl > 0)
      {
        u64 prev_start = doc_line_start(&app->doc, app->cl - 1);
        u64 prev_end = doc_line_length_clamped(&app->doc, prev_start, UINT32_MAX);
        del_start = prev_end;
        del = off - prev_end;
        app->cl--;
        app->cc = doc_line_visual_width(&app->doc, prev_start, prev_end);
        app->co = del_start;
      }
      else
      {
        app->co = del_start;
        sync_caret_from_offsets(app);
      }
      app_doc_delete(app, del_start, del);
      update_title(app);
    }
  } break;
  case VK_DELETE:
  {
    if (has_box_selection(app))
    {
      apply_basic_edit(app, NULL, 0, false, true);
      break;
    }
    if (has_stream_selection(app))
    {
      begin_edit_txn(app);
      if (delete_selection(app))
      {
        update_title(app);
        break;
      }
    }
    if (ctrl && app->co < app->doc.len)
    {
      begin_edit_txn(app);
      u64 old_off = app->co;
      move_caret_word_right(app);
      if (app->co > old_off)
      {
        u64 del_len = app->co - old_off;
        app->co = old_off;
        sync_caret_from_offsets(app);
        app_doc_delete(app, old_off, del_len);
        update_title(app);
      }
      break;
    }
    u64 off = app->co;
    if (off < app->doc.len)
    {
      begin_edit_txn(app);
      u64 del = doc_next_character(&app->doc, off) - off;
      char c[2] = {0, 0};
      CopyCtx ctx = { c, 0 };
      doc_read_range(&app->doc, off, min_u64(2, app->doc.len - off), copy_span, &ctx);
      if (c[0] == '\r' && c[1] == '\n') del = 2;
      app_doc_delete(app, off, del);
      update_title(app);
    }
  } break;
  default:
    break;
  }
  if (moved)
  {
    if (has_box_selection(app) && !alt) clear_box_selection(app);
    if (shift)
    {
      if (!has_stream_selection(app)) app->sa = old_off;
      app->sv = app->co;
    }
    else
      clear_stream_selection(app);
  }
  end_edit_txn(app);
  keep_visible_and_repaint(app);
}

static void handle_char(App *app, WPARAM ch)
{
  app->vsb = false;
  if (ch < 32) app->pending_high_surrogate = 0;
  if (ch == L'\t' && app->suppress_tab_char_once)
  {
    app->suppress_tab_char_once = false;
    return;
  }
  if (ch != L'\t') app->suppress_tab_char_once = false;
  char bytes[8];
  int n = 0;
  if (ch == L'\r')
  {
    bytes[0] = '\n';
    n = 1;
  }
  else if (ch == L'\t')
  {
    bytes[0] = ' ';
    bytes[1] = ' ';
    n = 2;
  }
  else if (ch >= 32 && ch != 127)
  {
    WCHAR w[2] = {(WCHAR)ch, 0};
    int count = 1;
    if (ch >= 0xd800 && ch <= 0xdbff)
    {
      app->pending_high_surrogate = (WCHAR)ch;
      return;
    }
    if (ch >= 0xdc00 && ch <= 0xdfff && app->pending_high_surrogate)
    {
      w[0] = app->pending_high_surrogate;
      w[1] = (WCHAR)ch;
      count = 2;
    }
    app->pending_high_surrogate = 0;
    n = WideCharToMultiByte(CP_UTF8, 0, w, count, bytes, (int)sizeof(bytes), NULL, NULL);
  }
  if (n > 0)
  {
    if (has_box_selection(app))
    {
      if (apply_basic_edit(app, bytes, (u64)n, false, false))
        keep_visible_and_repaint(app);
      return;
    }
    begin_edit_txn(app);
    if (has_stream_selection(app)) delete_selection(app);
    u64 off = app->co;
    if (app_doc_insert(app, off, bytes, (u64)n))
    {
      app->co += (u64)n;
      if (bytes[0] == '\n')
      {
        app->cl++;
        app->cc = 0;
      }
      else
        app->cc += ch == L'\t' ? 2 : 1;
      update_title(app);
      clear_stream_selection(app);
      keep_visible_and_repaint(app);
    }
    end_edit_txn(app);
  }
}

static int doc_copy_utf8_z(Document *doc, char **out_utf8)
{
  *out_utf8 = NULL;
  size_t n = (size_t)doc->len;
  char *buf = (char *)malloc(n + 1);
  if (!buf) return 0;
  CopyCtx ctx = { buf, 0 };
  doc_read_range(doc, 0, doc->len, copy_span, &ctx);
  buf[n] = 0;
  *out_utf8 = buf;
  return (int)n;
}

static bool sync_path_utf8_on_change(App *app)
{
  if (!app->doc.dirty) return false;
  if (app->doc.path[0]) return true;
  return app->doc.len != 0;
}

static bool app_load_path(App *app, const WCHAR *path)
{
  if (!doc_load_mapped(&app->doc, path))
  {
    if (app->hwnd)
      MessageBoxW(app->hwnd, L"Could not open file. The current editor contents were kept.",
                  L"text", MB_ICONERROR | MB_OK);
    return false;
  }
  repro_log_loaded_file(path, (unsigned long long)app->doc.len, app->doc.path[0] != 0);
  apply_caret_line_metrics(app);
  clear_history();
  app->tl = app->fc = app->cl = app->cc = app->co = 0;
  clear_stream_selection(app);
  clear_box_selection(app);
  update_title(app);
  request_repaint(app, TRUE);
  return true;
}

static bool save_target_is_document(const Document *doc, const WCHAR *path_key,
                                    const FileProbe *target_probe)
{
  if (!doc->path[0] || !doc->stamp_valid) return false;
  if (path_key && doc->path_key[0] && lstrcmpW(path_key, doc->path_key) == 0) return true;
  return target_probe && target_probe->kind == FILE_PROBE_EXISTS &&
         file_stamp_same_identity(&doc->stamp, &target_probe->stamp);
}

static int prompt_save_conflict(App *app, const FileProbe *probe)
{
  WCHAR message[640];
  if (probe->kind == FILE_PROBE_MISSING)
  {
    wcs_copy_trunc(message, _countof(message),
                   L"This file no longer exists on disk.\n\n"
                   L"Yes: recreate it from the editor copy.\n"
                   L"No: choose a different save path.\n"
                   L"Cancel: keep editing without saving.");
  }
  else if (probe->kind == FILE_PROBE_ERROR)
  {
    wfmt(message, _countof(message),
         L"The current disk version could not be verified (Windows error %lu).\n\n"
         L"Yes: attempt the save anyway.\n"
         L"No: choose a different save path.\n"
         L"Cancel: keep editing without saving.",
         (unsigned long)probe->error);
  }
  else
  {
    wcs_copy_trunc(message, _countof(message),
                   L"This file changed on disk since it was opened or last saved.\n\n"
                   L"Yes: overwrite the disk version.\n"
                   L"No: choose a different save path.\n"
                   L"Cancel: keep editing without saving.");
  }
  return MessageBoxW(app->hwnd, message, L"text",
                     MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON3);
}

static bool app_save_to_path_approved(App *app, const WCHAR *path, bool save_as,
                                      const FileProbe *approved_probe)
{
  WCHAR path_key[MAX_PATH];
  FileProbe initial_probe;
  FileProbe expected_probe;
  FileProbe approved;
  PreparedSave prepared;
  bool has_approval = approved_probe != NULL;
  bool same_document;
  (void)save_as;

  canonicalize_path_key(path, path_key, _countof(path_key));
  initial_probe = file_probe_path(path);
  same_document = save_target_is_document(&app->doc, path_key, &initial_probe);
  if (same_document && app->doc.stamp_valid)
  {
    memset(&expected_probe, 0, sizeof(expected_probe));
    expected_probe.kind = FILE_PROBE_EXISTS;
    expected_probe.stamp = app->doc.stamp;
  }
  else
    expected_probe = initial_probe;
  if (has_approval) approved = *approved_probe;

  if (!doc_prepare_save(&app->doc, path, &prepared))
  {
    MessageBoxW(app->hwnd, L"Could not save file.", L"text", MB_ICONERROR | MB_OK);
    return false;
  }

  for (;;)
  {
    /* This mutex is held only for the final recheck and rename. It coordinates
       text.exe writers without making the document unavailable to readers. */
    HANDLE mutex = acquire_save_mutex(path_key);
    FileProbe current_probe;
    bool expected_matches;
    bool approval_matches;
    int choice;
    if (!mutex)
    {
      doc_discard_prepared_save(&prepared);
      MessageBoxW(app->hwnd, L"Could not coordinate the final save operation.",
                  L"text", MB_ICONERROR | MB_OK);
      return false;
    }
    current_probe = file_probe_path(path);
    expected_matches = current_probe.kind != FILE_PROBE_ERROR &&
                       file_probe_equal(&current_probe, &expected_probe);
    approval_matches = has_approval && file_probe_equal(&current_probe, &approved);
    if (!expected_matches && !approval_matches)
    {
      release_save_mutex(mutex);
      choice = prompt_save_conflict(app, &current_probe);
      if (choice == IDCANCEL)
      {
        doc_discard_prepared_save(&prepared);
        return false;
      }
      if (choice == IDNO)
      {
        doc_discard_prepared_save(&prepared);
        return save_as_dialog(app);
      }
      approved = current_probe;
      has_approval = true;
      continue;
    }
    DWORD cleanup_error = ERROR_SUCCESS;
    if (!doc_commit_prepared_save(&app->doc, path, path_key, &prepared, &cleanup_error))
    {
      DWORD save_error = GetLastError();
      WCHAR message[256];
      release_save_mutex(mutex);
      doc_discard_prepared_save(&prepared);
      wfmt(message, _countof(message),
           L"Could not replace the destination file (Windows error %lu).",
           (unsigned long)save_error);
      MessageBoxW(app->hwnd, message, L"text", MB_ICONERROR | MB_OK);
      return false;
    }
    release_save_mutex(mutex);
    clear_history();
    update_title(app);
    if (cleanup_error != ERROR_SUCCESS)
    {
      WCHAR message[256];
      wfmt(message, _countof(message),
           L"The file was saved, but its temporary backup could not be removed "
           L"(Windows error %lu).", (unsigned long)cleanup_error);
      MessageBoxW(app->hwnd, message, L"text", MB_ICONWARNING | MB_OK);
    }
    return true;
  }
}

static bool app_save_to_path(App *app, const WCHAR *path, bool save_as)
{
  return app_save_to_path_approved(app, path, save_as, NULL);
}

static bool app_check_external_change(App *app, bool interactive)
{
  if (!doc_check_disk_state(&app->doc)) return false;
  if (app->doc.disk_state == DISK_STATE_MISSING ||
      app->doc.disk_state == DISK_STATE_INACCESSIBLE)
    app->doc.dirty = true;
  update_title(app);
  if (!interactive || app->doc.disk_change_prompted) return true;
  app->doc.disk_change_prompted = true;

  if (app->doc.disk_state == DISK_STATE_MISSING)
  {
    FileProbe approved = doc_observed_probe(&app->doc);
    int choice;
    choice = MessageBoxW(app->hwnd,
                         L"This file no longer exists on disk. The editor copy is still in memory.\n\n"
                         L"Yes: recreate the original file.\n"
                         L"No: choose a different save path.\n"
                         L"Cancel: keep editing without saving.",
                         L"text", MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON3);
    if (choice == IDYES)
      app_save_to_path_approved(app, app->doc.path, false, &approved);
    else if (choice == IDNO)
      save_as_dialog(app);
    return true;
  }

  if (app->doc.disk_state == DISK_STATE_INACCESSIBLE)
  {
    WCHAR message[512];
    wfmt(message, _countof(message),
         L"The disk version could not be checked (Windows error %lu).\n\n"
         L"The editor copy is still in memory and has been marked modified. "
         L"Save will require confirmation before replacing the original path.",
         (unsigned long)app->doc.disk_error);
    MessageBoxW(app->hwnd, message, L"text", MB_ICONWARNING | MB_OK);
    return true;
  }

  if (app->doc.dirty)
  {
    MessageBoxW(app->hwnd,
                L"This file changed on disk since it was opened or last saved.\n\n"
                L"Your editor changes are still in memory. Save will ask before overwriting the disk version.",
                L"text", MB_ICONWARNING | MB_OK);
    return true;
  }
  {
    int choice = MessageBoxW(app->hwnd,
                             L"This file changed on disk since it was opened or last saved.\n\n"
                             L"Reload the disk version?",
                             L"text", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON1);
    if (choice == IDYES)
    {
      WCHAR reload_path[MAX_PATH];
      lstrcpynW(reload_path, app->doc.path, MAX_PATH);
      app_load_path(app, reload_path);
    }
    else
    {
      app->doc.dirty = true;
      update_title(app);
    }
  }
  return true;
}

static bool app_confirm_replace_document(App *app)
{
  int choice;
  if (!sync_path_utf8_on_change(app)) return true;
  choice = MessageBoxW(app->hwnd,
                       L"Save changes before opening another file?",
                       L"text", MB_ICONQUESTION | MB_YESNOCANCEL | MB_DEFBUTTON3);
  if (choice == IDCANCEL) return false;
  if (choice == IDYES) return save_dialog(app);
  return true;
}

static void repro_capture_ui_state(const App *app, ReproUiState *state)
{
  if (!app || !state) return;
  state->co = app->co;
  state->sa = app->sa;
  state->sv = app->sv;
  state->tl = app->tl;
  state->fc = app->fc;
  state->doc_len = app->doc.len;
  state->cl = app->cl;
  state->cc = app->cc;
  state->bal = app->bal;
  state->bac = app->bac;
  state->bcl = app->bcl;
  state->bcc = app->bcc;
  state->bdc = app->bdc;
  state->rows = app->rows;
  state->cols = app->cols;
  state->box_sel = has_box_selection(app);
  state->stream_sel = has_stream_selection(app);
}

static void repro_begin_app_event(const App *app, const char *kind, unsigned long a, unsigned long b)
{
  ReproUiState before;
  repro_capture_ui_state(app, &before);
  repro_begin_input_event(kind, a, b, &before);
}

static void repro_end_app_event(const App *app)
{
  ReproUiState after;
  repro_capture_ui_state(app, &after);
  repro_end_input_event(&after);
}

static u64 repro_collect_deleted_preview(const SpanRef *spans, size_t span_count, char *buf, u64 cap)
{
  size_t i;
  u64 total = 0;
  if (!buf || cap == 0) return 0;
  for (i = 0; i < span_count && total < cap; ++i)
  {
    u64 take = min_u64(core_len(&spans[i]), cap - total);
    core_read(&spans[i], 0, take, buf + total);
    total += take;
  }
  return total;
}



/******************************************************************************
 * Commands
 ******************************************************************************/
static bool launch_new_window(App *app)
{
  WCHAR exe[MAX_PATH];
  WCHAR cmdline[MAX_PATH + 4];
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  if (!GetModuleFileNameW(NULL, exe, (DWORD)_countof(exe)))
  {
    MessageBoxW(app->hwnd, L"Could not find current executable.", L"text", MB_ICONERROR | MB_OK);
    return false;
  }
  if (wfmt(cmdline, _countof(cmdline), L"\"%ls\"", exe) < 0)
  {
    MessageBoxW(app->hwnd, L"Could not prepare new window command.", L"text", MB_ICONERROR | MB_OK);
    return false;
  }
  ZeroMemory(&si, sizeof(si));
  ZeroMemory(&pi, sizeof(pi));
  si.cb = sizeof(si);
  if (!CreateProcessW(exe, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
  {
    MessageBoxW(app->hwnd, L"Could not launch new window.", L"text", MB_ICONERROR | MB_OK);
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

static bool run_codex_do_command(App *app)
{
  wchar_t cwd[MAX_PATH];
  wchar_t file_name[MAX_PATH];
  wchar_t drive[_MAX_DRIVE];
  wchar_t dir[_MAX_DIR];
  wchar_t fname[_MAX_FNAME];
  wchar_t ext[_MAX_EXT];
  wchar_t command[1536];
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  if (app->doc.path[0])
  {
    if (app->doc.dirty)
    {
      if (!app_save_to_path(app, app->doc.path, false)) return false;
    }
    if (_wsplitpath_s(app->doc.path, drive, _countof(drive), dir, _countof(dir), fname, _countof(fname), ext, _countof(ext)) != 0)
      return false;
    wfmt(cwd, _countof(cwd), L"%ls%ls", drive, dir);
    wfmt(file_name, _countof(file_name), L"%ls%ls", fname, ext);
  }
  else
  {
    if (!_wgetcwd(cwd, _countof(cwd))) return false;
    wcs_copy_trunc(file_name, _countof(file_name), L"tasks.txt");
  }
  /* Avoid trailing slash before a closing quote in CreateProcess command line.
  A terminal backslash can escape the quote during argv parsing. */
  {
    size_t cwd_len = wcslen(cwd);
    while (cwd_len > 0 && (cwd[cwd_len - 1] == L'\\' || cwd[cwd_len - 1] == L'/'))
    {
      if (cwd_len == 3 && cwd[1] == L':' && (cwd[2] == L'\\' || cwd[2] == L'/')) break;
      cwd[--cwd_len] = 0;
    }
  }
  wfmt(command, _countof(command),
           L"codex --cd \"%ls\" \"Please execute the instructions inside %ls.\"",
           cwd, file_name);
  ZeroMemory(&si, sizeof(si));
  ZeroMemory(&pi, sizeof(pi));
  si.cb = sizeof(si);
  if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, cwd, &si, &pi))
  {
    MessageBoxW(app->hwnd, L"Could not launch Codex command.", L"text", MB_ICONERROR | MB_OK);
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

static void recalc_metrics(App *app, HDC dc)
{
  TEXTMETRICW tm;
  SelectObject(dc, app->font);
  GetTextMetricsW(dc, &tm);
  app->char_w = tm.tmAveCharWidth > 0 ? tm.tmAveCharWidth : 8;
  app->line_h = tm.tmHeight + tm.tmExternalLeading;
  {
    int digits = digit_count_u64(line_count_known(&app->doc));
    if (digits < GUTTER_CHARS) digits = GUTTER_CHARS;
    app->gutter_w = (app->char_w *digits) + EDIT_LINE_NUMBER_LEFT_PADDING + EDIT_LINE_NUMBER_RIGHT_PADDING;
  }
  RECT rc;
  GetClientRect(app->hwnd, &rc);
  app->rows = (rc.bottom - rc.top) / app->line_h;
  if (app->rows < 1) app->rows = 1;
  app->cols = (rc.right - rc.left - app->gutter_w - EDIT_TEXT_LEFT_PADDING) / app->char_w;
  if (app->cols < 1) app->cols = 1;
}

static void apply_font_size(App *app, int new_size)
{
  if (new_size < FONT_SIZE_MIN) new_size = FONT_SIZE_MIN;
  if (new_size > FONT_SIZE_MAX) new_size = FONT_SIZE_MAX;
  if (new_size == app->font_size && app->font) return;
  HFONT new_font = CreateFontW(-new_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               FF_MODERN, L"Consolas");
  if (!new_font) return;
  if (app->font) DeleteObject(app->font);
  app->font = new_font;
  app->font_size = new_size;
  if (app->hwnd && IsWindow(app->hwnd))
  {
    HDC dc = GetDC(app->hwnd);
    recalc_metrics(app, dc);
    ReleaseDC(app->hwnd, dc);
    keep_caret_visible(app);
    update_scrollbars(app);
    request_repaint(app, TRUE);
  }
}



/******************************************************************************
 * File / Find Dialogs
 ******************************************************************************/
static void open_dialog(App *app)
{
  WCHAR path[MAX_PATH] = {0};
  OPENFILENAMEW ofn;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = app->hwnd;
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"Text\0*.txt;*.c;*.cpp;*.h;*.hpp;*.md;*.json;*.log\0All\0*.*\0";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
  if (GetOpenFileNameW(&ofn))
  {
    if (app_confirm_replace_document(app))
      app_load_path(app, path);
  }
}

static bool find_next(App *app, const WCHAR *query)
{
  int i;
  if (!query || !query[0]) return false;
  if (wcscmp(app->fq, query) != 0)
    wcs_copy_n_trunc(app->fq, _countof(app->fq), query, (size_t)-1);
  if (app->find_hwnd && IsWindow(app->find_hwnd))
  {
    find_popup_update_matches(app);
    for (i = 0; i < app->find_result_count; ++i)
    {
      if (app->find_results[i].off >= app->co)
      {
        find_popup_select_result(app, i);
        return true;
      }
    }
    if (app->find_result_count > 0)
    {
      find_popup_select_result(app, 0);
      return true;
    }
    return false;
  }
  return false;
}

static LRESULT CALLBACK find_popup_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  App *app = &g_app;
  switch (msg)
  {
  case WM_ERASEBKGND:
  {
    RECT rc;
    HDC dc = (HDC)wp;
    if (dc && GetClientRect(hwnd, &rc))
    {
      FillRect(dc, &rc, app->cmd_bg_brush ? app->cmd_bg_brush : app->bg_brush);
      return 1;
    }
    break;
  }
  case WM_CREATE:
  {
    app->find_edit = CreateWindowExW(0, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                     8, 8, 344, 28, hwnd, (HMENU)(INT_PTR)IDC_FIND_EDIT, GetModuleHandleW(NULL), NULL);
    app->find_list = CreateWindowExW(0, L"LISTBOX", L"",
                                     WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
                                     8, 42, 344, 124, hwnd, (HMENU)(INT_PTR)IDC_FIND_LIST, GetModuleHandleW(NULL), NULL);
    app->find_count_label = CreateWindowExW(0, L"STATIC", L"0 entities found",
                                            WS_CHILD | WS_VISIBLE,
                                            8, 170, 344, 18, hwnd, (HMENU)(INT_PTR)IDC_FIND_COUNT, GetModuleHandleW(NULL), NULL);
    if (app->font)
    {
      SendMessageW(app->find_edit, WM_SETFONT, (WPARAM)app->font, TRUE);
      SendMessageW(app->find_list, WM_SETFONT, (WPARAM)app->font, TRUE);
      SendMessageW(app->find_count_label, WM_SETFONT, (WPARAM)app->font, TRUE);
    }
    g_find_popup_edit_wndproc = (WNDPROC)SetWindowLongPtrW(app->find_edit, GWLP_WNDPROC, (LONG_PTR)find_popup_edit_wndproc);
    SetWindowTextW(app->find_edit, app->fq);
    SetFocus(app->find_edit);
    find_popup_update_matches(app);
    return 0;
  }
  case WM_SIZE:
  {
    int w = LOWORD(lp);
    int h = HIWORD(lp);
    if (app->find_edit) MoveWindow(app->find_edit, 8, 8, w - 16, 28, TRUE);
    if (app->find_list) MoveWindow(app->find_list, 8, 42, w - 16, h - 72, TRUE);
    if (app->find_count_label) MoveWindow(app->find_count_label, 8, h - 24, w - 16, 18, TRUE);
    return 0;
  }
  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    RECT rc;
    BeginPaint(hwnd, &ps);
    if (GetClientRect(hwnd, &rc))
      FillRect(ps.hdc, &rc, app->cmd_bg_brush ? app->cmd_bg_brush : app->bg_brush);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_ACTIVATE:
    if (LOWORD(wp) == WA_INACTIVE)
    {
      DestroyWindow(hwnd);
      return 0;
    }
    break;
  case WM_COMMAND:
    if (LOWORD(wp) == IDC_FIND_EDIT && HIWORD(wp) == EN_CHANGE)
    {
      GetWindowTextW(app->find_edit, app->fq, (int)_countof(app->fq));
      find_popup_update_matches(app);
      return 0;
    }
    if (LOWORD(wp) == IDC_FIND_LIST && HIWORD(wp) == LBN_DBLCLK)
    {
      find_popup_activate_selected(app);
      SetFocus(app->find_edit);
      return 0;
    }
    break;
  case WM_MEASUREITEM:
  {
    MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lp;
    if (mis && mis->CtlID == IDC_FIND_LIST)
    {
      mis->itemHeight = (UINT)(app->line_h + 10);
      return TRUE;
    }
    break;
  }
  case WM_DRAWITEM:
  {
    DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lp;
    if (dis && dis->CtlID == IDC_FIND_LIST)
    {
      HBRUSH bg_brush;
      COLORREF text_color;
      RECT rc = dis->rcItem;
      WCHAR line_text[32];
      FindResult *result;
      if ((int)dis->itemID < 0 || (int)dis->itemID >= app->find_result_count) return TRUE;
      result = &app->find_results[dis->itemID];
      bg_brush = CreateSolidBrush((dis->itemState & ODS_SELECTED) ? THEME_GUTTER_ACTIVE_LINE_BG : THEME_CMD_POPUP_BG);
      FillRect(dis->hDC, &rc, bg_brush);
      DeleteObject(bg_brush);
      SetBkMode(dis->hDC, TRANSPARENT);
      text_color = (dis->itemState & ODS_SELECTED) ? THEME_SELECTION_TEXT : THEME_TEXT;
      SetTextColor(dis->hDC, text_color);
      rc.left += 8;
      rc.top += 4;
      DrawTextW(dis->hDC, result->preview, -1, &rc, DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
      wfmt(line_text, _countof(line_text), L"(line %llu)", (unsigned long long)(result->line + 1));
      rc = dis->rcItem;
      rc.right -= 8;
      rc.top += 4;
      SetTextColor(dis->hDC, RGB(190, 178, 160));
      DrawTextW(dis->hDC, line_text, -1, &rc, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
      if (dis->itemState & ODS_FOCUS) DrawFocusRect(dis->hDC, &dis->rcItem);
      return TRUE;
    }
    break;
  }
  case WM_CTLCOLORLISTBOX:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORSTATIC:
  {
    HDC dc = (HDC)wp;
    SetTextColor(dc, THEME_TEXT);
    SetBkColor(dc, THEME_CMD_POPUP_BG);
    return (LRESULT)(app->cmd_bg_brush ? app->cmd_bg_brush : app->bg_brush);
  }
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    if (app->find_hwnd == hwnd)
    {
      HWND owner = GetWindow(hwnd, GW_OWNER);
      if (!owner) owner = app->hwnd;
      app->find_hwnd = NULL;
      app->find_edit = NULL;
      app->find_list = NULL;
      app->find_count_label = NULL;
      g_find_popup_edit_wndproc = NULL;
      if (owner && IsWindow(owner))
        PostMessageW(owner, WM_APP_RESTORE_EDITOR_FOCUS, 0, 0);
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static void close_find_popup(App *app)
{
  if (app->find_hwnd && IsWindow(app->find_hwnd))
    DestroyWindow(app->find_hwnd);
}

static void open_find_popup(App *app)
{
  RECT rc = { 0 };
  POINT p = { 0 };
  int x = 40;
  int y = 40;
  int w = 360;
  int h = 220;
  if (app->find_hwnd && IsWindow(app->find_hwnd))
  {
    SetForegroundWindow(app->find_hwnd);
    SetFocus(app->find_edit);
    return;
  }
  if (GetCaretPos(&p))
  {
    ClientToScreen(app->hwnd, &p);
    x = p.x - 9;
    y = p.y - 8;
  }
  else if (GetClientRect(app->hwnd, &rc))
  {
    p.x = app->gutter_w + EDIT_TEXT_LEFT_PADDING + (int)((app->cc - app->fc) * (u64)app->char_w);
    p.y = (int)((app->cl - app->tl) * (u64)app->line_h);
    if (p.x < 0) p.x = 0;
    if (p.y < 0) p.y = 0;
    ClientToScreen(app->hwnd, &p);
    x = p.x;
    y = p.y;
  }
  app->find_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, FIND_POPUP_CLASS_NAME, L"find",
                                   WS_POPUP,
                                   x, y, w, h, app->hwnd, NULL, GetModuleHandleW(NULL), NULL);
  if (!app->find_hwnd) return;
  ShowWindow(app->find_hwnd, SW_SHOW);
  UpdateWindow(app->find_hwnd);
}

static LRESULT CALLBACK goto_popup_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  App *app = &g_app;
  switch (msg)
  {
  case WM_ERASEBKGND:
  {
    RECT rc;
    HDC dc = (HDC)wp;
    if (dc && GetClientRect(hwnd, &rc))
    {
      FillRect(dc, &rc, app->cmd_bg_brush ? app->cmd_bg_brush : app->bg_brush);
      return 1;
    }
    break;
  }
  case WM_CREATE:
  {
    app->goto_edit = CreateWindowExW(0, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
                                     8, 8, 244, 28, hwnd, (HMENU)(INT_PTR)IDC_GOTO_EDIT, GetModuleHandleW(NULL), NULL);
    app->goto_label = CreateWindowExW(0, L"STATIC", L"Line number",
                                      WS_CHILD | WS_VISIBLE,
                                      8, 42, 244, 18, hwnd, (HMENU)(INT_PTR)IDC_GOTO_LABEL, GetModuleHandleW(NULL), NULL);
    if (app->font)
    {
      SendMessageW(app->goto_edit, WM_SETFONT, (WPARAM)app->font, TRUE);
      SendMessageW(app->goto_label, WM_SETFONT, (WPARAM)app->font, TRUE);
    }
    g_goto_popup_edit_wndproc = (WNDPROC)SetWindowLongPtrW(app->goto_edit, GWLP_WNDPROC, (LONG_PTR)goto_popup_edit_wndproc);
    wfmt(app->goto_query, _countof(app->goto_query), L"%llu", (unsigned long long)(app->cl + 1));
    SetWindowTextW(app->goto_edit, app->goto_query);
    SendMessageW(app->goto_edit, EM_SETSEL, 0, -1);
    SetFocus(app->goto_edit);
    return 0;
  }
  case WM_SIZE:
  {
    int w = LOWORD(lp);
    if (app->goto_edit) MoveWindow(app->goto_edit, 8, 8, w - 16, 28, TRUE);
    if (app->goto_label) MoveWindow(app->goto_label, 8, 42, w - 16, 18, TRUE);
    return 0;
  }
  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    RECT rc;
    BeginPaint(hwnd, &ps);
    if (GetClientRect(hwnd, &rc))
      FillRect(ps.hdc, &rc, app->cmd_bg_brush ? app->cmd_bg_brush : app->bg_brush);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_ACTIVATE:
    if (LOWORD(wp) == WA_INACTIVE)
    {
      DestroyWindow(hwnd);
      return 0;
    }
    break;
  case WM_COMMAND:
    if (LOWORD(wp) == IDC_GOTO_EDIT && HIWORD(wp) == EN_CHANGE)
    {
      if (app->goto_label) SetWindowTextW(app->goto_label, L"Line number");
      return 0;
    }
    break;
  case WM_CTLCOLORLISTBOX:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORSTATIC:
  {
    HDC dc = (HDC)wp;
    SetTextColor(dc, THEME_TEXT);
    SetBkColor(dc, THEME_CMD_POPUP_BG);
    return (LRESULT)(app->cmd_bg_brush ? app->cmd_bg_brush : app->bg_brush);
  }
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    if (app->goto_hwnd == hwnd)
    {
      HWND owner = GetWindow(hwnd, GW_OWNER);
      if (!owner) owner = app->hwnd;
      app->goto_hwnd = NULL;
      app->goto_edit = NULL;
      app->goto_label = NULL;
      g_goto_popup_edit_wndproc = NULL;
      if (owner && IsWindow(owner))
        PostMessageW(owner, WM_APP_RESTORE_EDITOR_FOCUS, 0, 0);
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static void close_goto_popup(App *app)
{
  if (app->goto_hwnd && IsWindow(app->goto_hwnd))
    DestroyWindow(app->goto_hwnd);
}

static void open_goto_popup(App *app)
{
  RECT rc = { 0 };
  POINT p = { 0 };
  int x = 40;
  int y = 40;
  int w = 260;
  int h = 72;
  if (app->goto_hwnd && IsWindow(app->goto_hwnd))
  {
    SetForegroundWindow(app->goto_hwnd);
    SetFocus(app->goto_edit);
    SendMessageW(app->goto_edit, EM_SETSEL, 0, -1);
    return;
  }
  if (GetCaretPos(&p))
  {
    ClientToScreen(app->hwnd, &p);
    x = p.x - 9;
    y = p.y - 8;
  }
  else if (GetClientRect(app->hwnd, &rc))
  {
    p.x = app->gutter_w + EDIT_TEXT_LEFT_PADDING + (int)((app->cc - app->fc) * (u64)app->char_w);
    p.y = (int)((app->cl - app->tl) * (u64)app->line_h);
    if (p.x < 0) p.x = 0;
    if (p.y < 0) p.y = 0;
    ClientToScreen(app->hwnd, &p);
    x = p.x;
    y = p.y;
  }
  app->goto_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, GOTO_POPUP_CLASS_NAME, L"go to line",
                                   WS_POPUP,
                                   x, y, w, h, app->hwnd, NULL, GetModuleHandleW(NULL), NULL);
  if (!app->goto_hwnd) return;
  ShowWindow(app->goto_hwnd, SW_SHOW);
  UpdateWindow(app->goto_hwnd);
}

static void copy_selection_or_current_line_to_clipboard(App *app)
{
  if (!has_stream_selection(app)) return;
  u64 start = min_u64(app->sa, app->sv);
  u64 end = max_u64(app->sa, app->sv);
  if (end <= start) return;
  size_t utf8_len = (size_t)(end - start);
  char *utf8 = (char *)malloc(utf8_len + 1);
  if (!utf8) return;
  CopyCtx cctx = { utf8, 0 };
  doc_read_range(&app->doc, start, end - start, copy_span, &cctx);
  utf8[utf8_len] = 0;
  int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)utf8_len, NULL, 0);
  if (wlen <= 0)
  {
    free(utf8);
    return;
  }
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (size_t)(wlen + 1) * sizeof(WCHAR));
  if (!mem)
  {
    free(utf8);
    return;
  }
  WCHAR *wbuf = (WCHAR *)GlobalLock(mem);
  if (!wbuf)
  {
    GlobalFree(mem);
    free(utf8);
    return;
  }
  MultiByteToWideChar(CP_UTF8, 0, utf8, (int)utf8_len, wbuf, wlen);
  wbuf[wlen] = 0;
  GlobalUnlock(mem);
  free(utf8);
  if (OpenClipboard(app->hwnd))
  {
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
  }
  else
    GlobalFree(mem);
}

static bool seed_find_query_from_selection(App *app)
{
  if (!has_stream_selection(app)) return false;
  u64 start = min_u64(app->sa, app->sv);
  u64 end = max_u64(app->sa, app->sv);
  if (end <= start) return false;
  size_t utf8_len = (size_t)(end - start);
  char *utf8 = (char *)malloc(utf8_len + 1);
  if (!utf8) return false;
  CopyCtx cctx = { utf8, 0 };
  doc_read_range(&app->doc, start, end - start, copy_span, &cctx);
  utf8[utf8_len] = 0;
  int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)utf8_len, app->fq, (int)_countof(app->fq) - 1);
  free(utf8);
  if (wide_len <= 0) return false;
  app->fq[wide_len] = 0;
  return true;
}

static u64 doc_newline_len_at(Document *doc, u64 off)
{
  unsigned char c = 0;
  if (off >= doc->len || !doc_get_byte(doc, off, &c)) return 0;
  if (c == '\r')
  {
    unsigned char next = 0;
    if (off + 1 < doc->len && doc_get_byte(doc, off + 1, &next) && next == '\n') return 2;
    return 1;
  }
  if (c == '\n') return 1;
  return 0;
}

static bool current_line_range(App *app, u64 *out_start, u64 *out_end)
{
  u64 start;
  u64 end;
  u64 nl_len;
  if (app->doc.len == 0) return false;
  start = doc_line_start(&app->doc, app->cl);
  if (start > app->doc.len) start = app->doc.len;
  end = doc_line_length_clamped(&app->doc, start, UINT32_MAX);
  nl_len = doc_newline_len_at(&app->doc, end);
  if (nl_len > 0) end += nl_len;
  if (end <= start) return false;
  *out_start = start;
  *out_end = min_u64(end, app->doc.len);
  return *out_end > *out_start;
}

static bool move_caret_line_by_swap(App *app, bool move_down)
{
  u64 line_count;
  u64 move_start_line;
  u64 move_end_line;
  u64 move_start_off;
  u64 move_end_off;
  u64 neighbor_start_off;
  u64 neighbor_end_off;
  u64 region_start_off;
  u64 region_len;
  u64 moved_block_len;
  u64 neighbor_len;
  u64 move_end_line_start;
  u64 move_end_text_end;
  u64 trailing_sep_len;
  u64 neighbor_text_end;
  u64 neighbor_trailing_sep_len;
  int64_t shift;
  u64 old_co;
  u64 old_sa;
  u64 old_sv;
  char *swapped_buf;
  CopyCtx copy_first;
  CopyCtx copy_second;
  bool changed = false;
  u64 sel_lo;
  u64 sel_hi;
  u64 sel_hi_minus_one;
  u64 tmp_line;
  u64 tmp_col;
  doc_discover_all_lines(&app->doc);
  line_count = app->doc.lines.count ? app->doc.lines.count : 1;
  if (line_count <= 1) return false;
  if (has_box_selection(app))
  {
    u64 top, bottom, left, right;
    u64 old_bal, old_bac, old_bcl, old_bcc, old_bdc;
    u64 old_sa, old_sv;
    u64 line;
    u64 max_line = line_count - 1;
    box_selection_bounds(app, &top, &bottom, &left, &right);
    if ((!move_down && top == 0) || (move_down && bottom >= max_line)) return false;
    old_bal = app->bal;
    old_bac = app->bac;
    old_bcl = app->bcl;
    old_bcc = app->bcc;
    old_bdc = app->bdc;
    old_sa = app->sa;
    old_sv = app->sv;
    clear_box_selection(app);
    clear_stream_selection(app);
    if (move_down)
    {
      line = bottom + 1;
      while (line > top)
      {
        line--;
        set_caret_line_col(app, line, old_bdc);
        if (!move_caret_line_by_swap(app, true))
        {
          app->sa = old_sa;
          app->sv = old_sv;
          return false;
        }
      }
      app->bal = min_u64(old_bal + 1, max_line);
      app->bcl = min_u64(old_bcl + 1, max_line);
    }
    else
    {
      for (line = top; line <= bottom; ++line)
      {
        set_caret_line_col(app, line, old_bdc);
        if (!move_caret_line_by_swap(app, false))
        {
          app->sa = old_sa;
          app->sv = old_sv;
          return false;
        }
      }
      app->bal = old_bal > 0 ? old_bal - 1 : 0;
      app->bcl = old_bcl > 0 ? old_bcl - 1 : 0;
    }
    app->bsa = true;
    app->bac = old_bac;
    app->bcc = old_bcc;
    app->bdc = old_bdc;
    app->sa = old_sa;
    app->sv = old_sv;
    set_caret_line_col(app, app->bcl, app->bdc);
    return true;
  }
  if (!has_stream_selection(app))
  {
    u64 src_line = app->cl;
    u64 dst_line;
    u64 upper_line;
    u64 lower_line;
    u64 upper_start;
    u64 upper_text_end;
    u64 lower_start;
    u64 lower_text_end;
    u64 after_lower_start;
    u64 upper_text_len;
    u64 between_sep_len;
    u64 lower_text_len;
    u64 lower_sep_len;
    u64 block_len;
    u64 swapped_len;
    u64 old_col;
    CopyCtx copy_upper_text;
    CopyCtx copy_between_sep;
    CopyCtx copy_lower_text;
    CopyCtx copy_lower_sep;
    if ((!move_down && src_line == 0) || (move_down && src_line + 1 >= line_count)) return false;
    dst_line = move_down ? (src_line + 1) : (src_line - 1);
    upper_line = move_down ? src_line : dst_line;
    lower_line = move_down ? dst_line : src_line;
    upper_start = doc_line_start(&app->doc, upper_line);
    lower_start = doc_line_start(&app->doc, lower_line);
    if (upper_start > app->doc.len || lower_start > app->doc.len || lower_start < upper_start) return false;
    upper_text_end = doc_line_length_clamped(&app->doc, upper_start, UINT32_MAX);
    lower_text_end = doc_line_length_clamped(&app->doc, lower_start, UINT32_MAX);
    if (upper_text_end < upper_start || lower_text_end < lower_start) return false;
    if (lower_line + 1 < line_count)
    {
      after_lower_start = doc_line_start(&app->doc, lower_line + 1);
      if (after_lower_start < lower_start || after_lower_start > app->doc.len) return false;
    }
    else
      after_lower_start = app->doc.len;
    upper_text_len = upper_text_end - upper_start;
    between_sep_len = lower_start - upper_text_end;
    lower_text_len = lower_text_end - lower_start;
    lower_sep_len = after_lower_start - lower_text_end;
    block_len = after_lower_start - upper_start;
    swapped_len = lower_text_len + between_sep_len + upper_text_len + lower_sep_len;
    if (swapped_len != block_len) return false;
    swapped_buf = (char *)calloc((size_t)swapped_len, 1);
    if (!swapped_buf && swapped_len != 0) return false;
    copy_lower_text.dst = swapped_buf;
    copy_lower_text.at = 0;
    if (lower_text_len > 0) doc_read_range(&app->doc, lower_start, lower_text_len, copy_span, &copy_lower_text);
    copy_between_sep.dst = swapped_buf + copy_lower_text.at;
    copy_between_sep.at = 0;
    if (between_sep_len > 0) doc_read_range(&app->doc, upper_text_end, between_sep_len, copy_span, &copy_between_sep);
    copy_upper_text.dst = swapped_buf + copy_lower_text.at + copy_between_sep.at;
    copy_upper_text.at = 0;
    if (upper_text_len > 0) doc_read_range(&app->doc, upper_start, upper_text_len, copy_span, &copy_upper_text);
    copy_lower_sep.dst = swapped_buf + copy_lower_text.at + copy_between_sep.at + copy_upper_text.at;
    copy_lower_sep.at = 0;
    if (lower_sep_len > 0) doc_read_range(&app->doc, lower_text_end, lower_sep_len, copy_span, &copy_lower_sep);
    if (copy_lower_text.at != lower_text_len ||
        copy_between_sep.at != between_sep_len ||
        copy_upper_text.at != upper_text_len ||
        copy_lower_sep.at != lower_sep_len)
    {
      free(swapped_buf);
      return false;
    }
    begin_edit_txn(app);
    if (!app_doc_delete(app, upper_start, block_len) ||
        (swapped_len > 0 && !app_doc_insert(app, upper_start, swapped_buf, swapped_len)))
    {
      end_edit_txn(app);
      free(swapped_buf);
      return false;
    }
    end_edit_txn(app);
    changed = true;
    old_col = app->cc;
    set_caret_line_col(app, dst_line, old_col);
    clear_stream_selection(app);
    clear_box_selection(app);
    free(swapped_buf);
    return changed;
  }
  move_start_line = app->cl;
  move_end_line = app->cl;
  if (has_stream_selection(app))
  {
    sel_lo = min_u64(app->sa, app->sv);
    sel_hi = max_u64(app->sa, app->sv);
    if (sel_hi > sel_lo)
    {
      doc_offset_to_line_col(&app->doc, sel_lo, &move_start_line, &tmp_col);
      sel_hi_minus_one = sel_hi - 1;
      doc_offset_to_line_col(&app->doc, sel_hi_minus_one, &move_end_line, &tmp_col);
      if (move_end_line < move_start_line)
      {
        tmp_line = move_start_line;
        move_start_line = move_end_line;
        move_end_line = tmp_line;
      }
    }
  }
  if ((!move_down && move_start_line == 0) || (move_down && move_end_line + 1 >= line_count)) return false;
  {
    u64 neighbor_line = move_down ? (move_end_line + 1) : (move_start_line - 1);
    u64 neighbor_start = doc_line_start(&app->doc, neighbor_line);
    u64 neighbor_end = (neighbor_line + 1 < line_count) ? doc_line_start(&app->doc, neighbor_line + 1) : app->doc.len;
    int64_t sel_shift = move_down
      ? (int64_t)(neighbor_end - neighbor_start)
      : -((int64_t)(neighbor_end - neighbor_start));
    u64 old_sel_a = app->sa;
    u64 old_sel_v = app->sv;
    u64 old_co_stream = app->co;
    u64 old_col_stream = app->cc;
    u64 line = 0;
    bool ok = true;
    clear_stream_selection(app);
    clear_box_selection(app);
    if (move_down)
    {
      line = move_end_line + 1;
      while (line > move_start_line)
      {
        line--;
        set_caret_line_col(app, line, old_col_stream);
        if (!move_caret_line_by_swap(app, true))
        {
          ok = false;
          break;
        }
      }
    }
    else
    {
      line = move_start_line;
      while (line <= move_end_line)
      {
        set_caret_line_col(app, line, old_col_stream);
        if (!move_caret_line_by_swap(app, false))
        {
          ok = false;
          break;
        }
        line++;
      }
    }
    if (!ok) return false;
    if (sel_shift >= 0)
    {
      u64 d = (u64)sel_shift;
      app->sa = min_u64(old_sel_a + d, app->doc.len);
      app->sv = min_u64(old_sel_v + d, app->doc.len);
      app->co = min_u64(old_co_stream + d, app->doc.len);
    }
    else
    {
      u64 d = (u64)(-sel_shift);
      app->sa = old_sel_a > d ? old_sel_a - d : 0;
      app->sv = old_sel_v > d ? old_sel_v - d : 0;
      app->co = old_co_stream > d ? old_co_stream - d : 0;
    }
    sync_caret_from_offsets(app);
    clear_box_selection(app);
    return true;
  }
  move_start_off = doc_line_start(&app->doc, move_start_line);
  if (move_end_line + 1 < line_count)
    move_end_off = doc_line_start(&app->doc, move_end_line + 1);
  else
    move_end_off = app->doc.len;
  if (move_end_off < move_start_off || move_end_off > app->doc.len) return false;
  moved_block_len = move_end_off - move_start_off;
  if (move_down)
  {
    neighbor_start_off = move_end_off;
    if (move_end_line + 2 < line_count)
      neighbor_end_off = doc_line_start(&app->doc, move_end_line + 2);
    else
      neighbor_end_off = app->doc.len;
    region_start_off = move_start_off;
    shift = (int64_t)(neighbor_end_off - neighbor_start_off);
  }
  else
  {
    neighbor_start_off = doc_line_start(&app->doc, move_start_line - 1);
    neighbor_end_off = move_start_off;
    region_start_off = neighbor_start_off;
    shift = -((int64_t)(neighbor_end_off - neighbor_start_off));
  }
  if (neighbor_end_off < neighbor_start_off || neighbor_end_off > app->doc.len) return false;
  neighbor_len = neighbor_end_off - neighbor_start_off;
  region_len = moved_block_len + neighbor_len;
  if (move_down && neighbor_len == 0 && move_end_line + 1 == line_count - 1 && moved_block_len > 0)
  {
    move_end_line_start = doc_line_start(&app->doc, move_end_line);
    move_end_text_end = doc_line_length_clamped(&app->doc, move_end_line_start, UINT32_MAX);
    trailing_sep_len = move_end_off - move_end_text_end;
    if (trailing_sep_len == 0 || trailing_sep_len > moved_block_len) return false;
    swapped_buf = (char *)calloc((size_t)region_len, 1);
    if (!swapped_buf) return false;
    copy_first.dst = swapped_buf;
    copy_first.at = 0;
    copy_second.dst = swapped_buf + (size_t)trailing_sep_len;
    copy_second.at = 0;
    doc_read_range(&app->doc, move_end_text_end, trailing_sep_len, copy_span, &copy_first);
    doc_read_range(&app->doc, move_start_off, moved_block_len - trailing_sep_len, copy_span, &copy_second);
    if (copy_first.at != trailing_sep_len || copy_second.at != moved_block_len - trailing_sep_len)
    {
      free(swapped_buf);
      return false;
    }
    shift = (int64_t)trailing_sep_len;
    goto perform_swap;
  }
  if (!move_down && moved_block_len == 0 && move_start_line == line_count - 1 && neighbor_len > 0)
  {
    neighbor_text_end = doc_line_length_clamped(&app->doc, neighbor_start_off, UINT32_MAX);
    neighbor_trailing_sep_len = neighbor_end_off - neighbor_text_end;
    if (neighbor_trailing_sep_len == 0 || neighbor_trailing_sep_len > neighbor_len) return false;
    swapped_buf = (char *)calloc((size_t)region_len, 1);
    if (!swapped_buf) return false;
    copy_first.dst = swapped_buf;
    copy_first.at = 0;
    copy_second.dst = swapped_buf + (size_t)neighbor_trailing_sep_len;
    copy_second.at = 0;
    doc_read_range(&app->doc, neighbor_text_end, neighbor_trailing_sep_len, copy_span, &copy_first);
    doc_read_range(&app->doc, neighbor_start_off, neighbor_len - neighbor_trailing_sep_len, copy_span, &copy_second);
    if (copy_first.at != neighbor_trailing_sep_len || copy_second.at != neighbor_len - neighbor_trailing_sep_len)
    {
      free(swapped_buf);
      return false;
    }
    shift = -((int64_t)(neighbor_len - neighbor_trailing_sep_len));
    goto perform_swap;
  }
  swapped_buf = (char *)calloc((size_t)region_len, 1);
  if (!swapped_buf && region_len != 0) return false;
  copy_first.dst = swapped_buf;
  copy_first.at = 0;
  copy_second.dst = swapped_buf + (size_t)(move_down ? neighbor_len : moved_block_len);
  copy_second.at = 0;
  if (move_down)
  {
    if (neighbor_len > 0) doc_read_range(&app->doc, neighbor_start_off, neighbor_len, copy_span, &copy_first);
    if (moved_block_len > 0) doc_read_range(&app->doc, move_start_off, moved_block_len, copy_span, &copy_second);
    if (copy_first.at != neighbor_len || copy_second.at != moved_block_len)
    {
      free(swapped_buf);
      return false;
    }
  }
  else
  {
    if (moved_block_len > 0) doc_read_range(&app->doc, move_start_off, moved_block_len, copy_span, &copy_first);
    if (neighbor_len > 0) doc_read_range(&app->doc, neighbor_start_off, neighbor_len, copy_span, &copy_second);
    if (copy_first.at != moved_block_len || copy_second.at != neighbor_len)
    {
      free(swapped_buf);
      return false;
    }
  }
perform_swap:
  old_co = app->co;
  old_sa = app->sa;
  old_sv = app->sv;
  begin_edit_txn(app);
  if (!app_doc_delete(app, region_start_off, region_len) ||
      (region_len > 0 && !app_doc_insert(app, region_start_off, swapped_buf, region_len)))
  {
    end_edit_txn(app);
    free(swapped_buf);
    return false;
  }
  if (shift >= 0)
  {
    u64 d = (u64)shift;
    app->co = min_u64(old_co + d, app->doc.len);
    app->sa = min_u64(old_sa + d, app->doc.len);
    app->sv = min_u64(old_sv + d, app->doc.len);
  }
  else
  {
    u64 d = (u64)(-shift);
    app->co = old_co > d ? old_co - d : 0;
    app->sa = old_sa > d ? old_sa - d : 0;
    app->sv = old_sv > d ? old_sv - d : 0;
  }
  sync_caret_from_offsets(app);
  end_edit_txn(app);
  changed = true;
  clear_box_selection(app);
  free(swapped_buf);
  return changed;
}

static void copy_current_line_to_clipboard(App *app)
{
  u64 start = 0;
  u64 end = 0;
  size_t line_len;
  size_t clipboard_len;
  unsigned char tail = 0;
  bool has_line_ending;
  char *utf8;
  CopyCtx cctx;
  if (!current_line_range(app, &start, &end)) return;
  line_len = (size_t)(end - start);
  has_line_ending = end > start &&
                    doc_get_byte(&app->doc, end - 1, &tail) &&
                    (tail == '\r' || tail == '\n');
  clipboard_len = line_len + (has_line_ending ? 0 : 2);
  utf8 = (char *)malloc(clipboard_len + 1);
  if (!utf8) return;
  cctx.dst = utf8;
  cctx.at = 0;
  doc_read_range(&app->doc, start, end - start, copy_span, &cctx);
  if (!has_line_ending)
  {
    utf8[line_len] = '\r';
    utf8[line_len + 1] = '\n';
  }
  utf8[clipboard_len] = 0;
  set_clipboard_utf8_text(app->hwnd, utf8, clipboard_len);
  free(utf8);
}

static bool save_dialog(App *app)
{
  if (app->doc.path[0])
  {
    return app_save_to_path(app, app->doc.path, false);
  }
  WCHAR path[MAX_PATH];
  lstrcpynW(path, L"untitled.txt", MAX_PATH);
  OPENFILENAMEW ofn;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = app->hwnd;
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"Text\0*.txt\0All\0*.*\0";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
  if (GetSaveFileNameW(&ofn))
  {
    return app_save_to_path(app, path, true);
  }
  return false;
}

static bool save_as_dialog(App *app)
{
  WCHAR path[MAX_PATH];
  lstrcpynW(path, app->doc.path[0] ? app->doc.path : L"untitled.txt", MAX_PATH);
  OPENFILENAMEW ofn;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = app->hwnd;
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"Text\0*.txt\0All\0*.*\0";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
  if (GetSaveFileNameW(&ofn))
  {
    return app_save_to_path(app, path, true);
  }
  return false;
}

static void paste_clipboard(App *app)
{
  app->vsb = false;
  for (int attempt = 0; !OpenClipboard(app->hwnd); ++attempt)
  {
    if (attempt >= 19) return;
    Sleep(10);
  }
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  if (h)
  {
    const WCHAR *w = (const WCHAR *)GlobalLock(h);
    if (w)
    {
      int bytes = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
      if (bytes > 1)
      {
        char *utf8 = (char *)malloc((size_t)bytes);
        if (utf8)
        {
          WideCharToMultiByte(CP_UTF8, 0, w, -1, utf8, bytes, NULL, NULL);
          begin_edit_txn(app);
          if (has_box_selection(app))
            apply_box_paste_multiline(app, utf8, (u64)bytes - 1);
          else
          {
            bool line_paste = app->clm && !has_stream_selection(app);
            u64 inserted_len = (u64)bytes - 1;
            u64 old_line = app->cl;
            u64 old_col = app->cc;
            u64 pasted_newlines = 0;
            if (has_stream_selection(app)) delete_selection(app);
            {
              u64 off = line_paste ? doc_line_start(&app->doc, app->cl) : app->co;
              app_doc_insert(app, off, utf8, inserted_len);
              app->co = off + inserted_len;
              if (!line_paste)
              {
                sync_caret_from_offsets(app);
              }
              for (int i = 0; line_paste && i < bytes - 1; ++i)
              {
                if (utf8[i] == '\n')
                {
                  pasted_newlines++;
                  app->cl++;
                  app->cc = 0;
                }
                else if (utf8[i] != '\r')
                  app->cc++;
              }
              if (line_paste && inserted_len > 0)
              {
                u64 trim = 0;
                if (inserted_len >= 2 &&
                    utf8[inserted_len - 2] == '\r' &&
                    utf8[inserted_len - 1] == '\n')
                  trim = 2;
                else if (utf8[inserted_len - 1] == '\r' || utf8[inserted_len - 1] == '\n')
                  trim = 1;
                if (trim > 0 && app->co >= trim)
                {
                  app->co -= trim;
                  sync_caret_from_offsets(app);
                }
                set_caret_line_col(app, old_line + pasted_newlines, old_col);
              }
            }
          }
          end_edit_txn(app);
          free(utf8);
        }
      }
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  update_title(app);
  request_repaint(app, FALSE);
}

static void move_caret_left(App *app)
{
  if (app->co > 0)
  {
    app->co = doc_previous_character(&app->doc, app->co);
    sync_caret_from_offsets(app);
  }
  else if (app->cl > 0)
  {
    app->cl--;
    u64 start = doc_line_start(&app->doc, app->cl);
    u64 end = doc_line_length_clamped(&app->doc, start, UINT32_MAX);
    app->cc = doc_line_visual_width(&app->doc, start, end);
    app->co = end;
  }
}

static void move_caret_right(App *app)
{
  u64 start = doc_line_start(&app->doc, app->cl);
  u64 end = doc_line_length_clamped(&app->doc, start, UINT32_MAX);
  if (app->co < end)
  {
    app->co = doc_next_character(&app->doc, app->co);
    sync_caret_from_offsets(app);
  }
  else
  {
    doc_discover_for_view(&app->doc, app->cl, 2);
    if (app->cl + 1 < app->doc.lines.count)
    {
      app->cl++;
      app->cc = 0;
      app->co = doc_line_start(&app->doc, app->cl);
    }
  }
}

static bool doc_get_byte(Document *doc, u64 off, unsigned char *out)
{
  char c = 0;
  CopyCtx ctx = { &c, 0 };
  if (off >= doc->len) return false;
  doc_read_range(doc, off, 1, copy_span, &ctx);
  *out = (unsigned char)c;
  return true;
}

static void select_word_at_caret(App *app)
{
  u64 off = app->co;
  u64 start;
  u64 end;
  unsigned char c = 0;
  if (app->doc.len == 0)
  {
    clear_stream_selection(app);
    return;
  }
  if (off >= app->doc.len) off = app->doc.len - 1;
  if (!doc_get_byte(&app->doc, off, &c))
  {
    clear_stream_selection(app);
    return;
  }
  if (!is_word_byte(c) && off > 0 && doc_get_byte(&app->doc, off - 1, &c) && is_word_byte(c))
    off--;
  else if (!is_word_byte(c))
  {
    clear_stream_selection(app);
    return;
  }
  start = off;
  while (start > 0 && doc_get_byte(&app->doc, start - 1, &c) && is_word_byte(c)) start--;
  end = off;
  while (end < app->doc.len && doc_get_byte(&app->doc, end, &c) && is_word_byte(c)) end++;
  app->sa = start;
  app->sv = end;
  app->co = end;
  sync_caret_from_offsets(app);
}

static bool word_bounds_near_offset(App *app, u64 off, u64 *out_start, u64 *out_end)
{
  unsigned char c = 0;
  u64 start = off;
  u64 end = off;
  if (off >= app->doc.len)
  {
    if (off == 0) return false;
    off--;
  }
  if (!doc_get_byte(&app->doc, off, &c)) return false;
  if (!is_word_byte(c) && off > 0 && doc_get_byte(&app->doc, off - 1, &c) && is_word_byte(c))
    off--;
  else if (!is_word_byte(c))
    return false;
  start = off;
  end = off;
  while (start > 0 && doc_get_byte(&app->doc, start - 1, &c) && is_word_byte(c)) start--;
  while (end < app->doc.len && doc_get_byte(&app->doc, end, &c) && is_word_byte(c)) end++;
  *out_start = start;
  *out_end = end;
  return end > start;
}

static void move_caret_word_right(App *app)
{
  unsigned char c = 0;
  if (app->co >= app->doc.len) return;
  if (!doc_get_byte(&app->doc, app->co, &c)) return;
  if (c == '\r' || c == '\n')
  {
    move_caret_right(app);
    return;
  }
  if (is_word_byte(c))
  {
    while (app->co < app->doc.len &&
           doc_get_byte(&app->doc, app->co, &c) &&
           is_word_byte(c))
      move_caret_right(app);
    while (app->co < app->doc.len &&
           doc_get_byte(&app->doc, app->co, &c) &&
           is_space_byte(c))
      move_caret_right(app);
    return;
  }
  if (is_space_byte(c))
  {
    while (app->co < app->doc.len &&
           doc_get_byte(&app->doc, app->co, &c) &&
           is_space_byte(c))
      move_caret_right(app);
    return;
  }
  move_caret_right(app);
  while (app->co < app->doc.len &&
         doc_get_byte(&app->doc, app->co, &c) &&
         is_space_byte(c))
    move_caret_right(app);
}

static bool set_clipboard_utf8_text(HWND hwnd, const char *utf8, size_t utf8_len)
{
  int wlen;
  HGLOBAL mem;
  WCHAR *wbuf;
  if (!utf8) return false;
  wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)utf8_len, NULL, 0);
  if (wlen <= 0) return false;
  mem = GlobalAlloc(GMEM_MOVEABLE, (size_t)(wlen + 1) * sizeof(WCHAR));
  if (!mem) return false;
  wbuf = (WCHAR *)GlobalLock(mem);
  if (!wbuf)
  {
    GlobalFree(mem);
    return false;
  }
  MultiByteToWideChar(CP_UTF8, 0, utf8, (int)utf8_len, wbuf, wlen);
  wbuf[wlen] = 0;
  GlobalUnlock(mem);
  if (OpenClipboard(hwnd))
  {
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
    return true;
  }
  GlobalFree(mem);
  return false;
}

static char *build_box_selection_utf8(App *app, size_t *out_len)
{
  u64 top, bottom, left, right;
  size_t cap = 0;
  size_t len = 0;
  char *out = NULL;
  if (!has_box_selection(app)) return NULL;
  box_selection_bounds(app, &top, &bottom, &left, &right);
  for (u64 line = top; line <= bottom; ++line)
  {
    u64 ls = doc_line_start(&app->doc, line);
    u64 le = doc_line_length_clamped(&app->doc, ls, UINT32_MAX);
    u64 vl = doc_line_visual_width(&app->doc, ls, le);
    u64 seg_start = doc_line_byte_col_from_visual_col(&app->doc, line, min_u64(left, vl));
    u64 seg_end = doc_line_byte_col_from_visual_col(&app->doc, line, min_u64(right, vl));
    u64 seg_len = seg_end > seg_start ? seg_end - seg_start : 0;
    size_t need = len + (size_t)seg_len + (line < bottom ? 1 : 0) + 1;
    if (need > cap)
    {
      size_t next_cap = cap ? cap * 2 : 64;
      while (next_cap < need) next_cap *= 2;
      char *next = (char *)realloc(out, next_cap);
      if (!next)
      {
        free(out);
        return NULL;
      }
      out = next;
      cap = next_cap;
    }
    if (seg_len > 0)
    {
      CopyCtx ctx = { out + len, 0 };
      doc_read_range(&app->doc, ls + seg_start, seg_len, copy_span, &ctx);
      len += (size_t)seg_len;
    }
    if (line < bottom) out[len++] = '\n';
  }
  if (!out)
  {
    out = (char *)malloc(1);
    if (!out) return NULL;
  }
  out[len] = 0;
  if (out_len) *out_len = len;
  return out;
}

static void copy_box_selection_to_clipboard(App *app)
{
  size_t len = 0;
  char *utf8 = build_box_selection_utf8(app, &len);
  if (!utf8) return;
  set_clipboard_utf8_text(app->hwnd, utf8, len);
  free(utf8);
}

static void move_caret_word_left(App *app)
{
  unsigned char c = 0;
  if (app->co > 0 && doc_get_byte(&app->doc, app->co - 1, &c) &&
      (c == '\r' || c == '\n'))
  {
    /* Match expected editor behavior: first hop to previous line end, then
    another Ctrl+Left moves to the previous word start. */
    move_caret_left(app);
    return;
  }
  if (app->co == 0 || !doc_get_byte(&app->doc, app->co - 1, &c)) return;
  if (is_word_byte(c))
  {
    while (app->co > 0 &&
           doc_get_byte(&app->doc, app->co - 1, &c) &&
           is_word_byte(c))
      move_caret_left(app);
    return;
  }
  if (is_space_byte(c))
  {
    while (app->co > 0 &&
           doc_get_byte(&app->doc, app->co - 1, &c) &&
           is_space_byte(c))
      move_caret_left(app);
    while (app->co > 0 &&
           doc_get_byte(&app->doc, app->co - 1, &c) &&
           is_word_byte(c))
      move_caret_left(app);
    return;
  }
  move_caret_left(app);
  while (app->co > 0 &&
         doc_get_byte(&app->doc, app->co - 1, &c) &&
         is_space_byte(c))
    move_caret_left(app);
}



/******************************************************************************
 * Win32 Theme / Platform
 ******************************************************************************/
static void init_native_dark_mode(void)
{
  HMODULE theme = LoadLibraryW(L"uxtheme.dll");
  if (!theme) return;
  g_set_preferred_app_mode = (SetPreferredAppModeFn)GetProcAddress(theme, MAKEINTRESOURCEA(135));
  g_allow_dark_mode_for_window = (AllowDarkModeForWindowFn)GetProcAddress(theme, MAKEINTRESOURCEA(133));
  g_flush_menu_themes = (FlushMenuThemesFn)GetProcAddress(theme, MAKEINTRESOURCEA(136));
  if (g_set_preferred_app_mode) g_set_preferred_app_mode(APP_MODE_FORCE_DARK);
  if (g_flush_menu_themes) g_flush_menu_themes();
}

static void apply_native_dark_mode(HWND hwnd)
{
  BOOL enabled = TRUE;
  COLORREF bg = THEME_BG;
  COLORREF fg = THEME_FG;
  if (g_allow_dark_mode_for_window) g_allow_dark_mode_for_window(hwnd, TRUE);
  SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled));
  DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &bg, sizeof(bg));
  DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &bg, sizeof(bg));
  DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &fg, sizeof(fg));
}

static int scroll_max_pos(const SCROLLINFO *si)
{
  int page = si->nPage > 0 ? (int)si->nPage : 1;
  int max_pos = si->nMax - page + 1;
  return max_pos > si->nMin ? max_pos : si->nMin;
}

static bool vscroll_can_line_up(const App *app)
{
  SCROLLINFO si;
  int max_pos;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE;
  if (!GetScrollInfo(app->hwnd, SB_VERT, &si)) return false;
  max_pos = scroll_max_pos(&si);
  if (max_pos <= si.nMin) return false;
  return app->tl > (u64)si.nMin;
}

static bool vscroll_can_line_down(App *app)
{
  SCROLLINFO si;
  int max_pos;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE;
  if (!GetScrollInfo(app->hwnd, SB_VERT, &si)) return false;
  max_pos = scroll_max_pos(&si);
  if (max_pos <= si.nMin) return false;
  if (app->tl < (u64)max_pos) return true;
  if (!app->doc.lines.eof)
  {
    u64 budget_rows = (u64)(app->rows > 0 ? app->rows : 1);
    doc_discover_for_view(&app->doc, app->tl + 1, budget_rows);
    update_scrollbars(app);
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE;
    if (!GetScrollInfo(app->hwnd, SB_VERT, &si)) return false;
    max_pos = scroll_max_pos(&si);
  }
  return app->tl < (u64)max_pos;
}

static bool cursor_at_vscroll_bottom(HWND hwnd)
{
  SCROLLBARINFO sbi;
  POINT pt;
  int arrow_h = GetSystemMetrics(SM_CYVSCROLL);
  ZeroMemory(&sbi, sizeof(sbi));
  sbi.cbSize = sizeof(sbi);
  if (!GetCursorPos(&pt)) return false;
  if (!GetScrollBarInfo(hwnd, OBJID_VSCROLL, &sbi)) return false;
  if (pt.x < sbi.rcScrollBar.left || pt.x > sbi.rcScrollBar.right) return false;
  return pt.y >= sbi.rcScrollBar.bottom - arrow_h * 2;
}

static int wheel_scroll_lines_per_notch(void)
{
  UINT lines = 0;
  if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0)) return 3;
  if (lines == WHEEL_PAGESCROLL) return -1;
  if (lines == 0) return 0;
  return (int)lines;
}



/******************************************************************************
 * Window Procedure
 ******************************************************************************/
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  App *app = &g_app;
  switch (msg)
  {
  case WM_APP_RESTORE_EDITOR_FOCUS:
    force_focus_window(hwnd);
    if (app->cmd_restore_pending)
    {
      app->co = min_u64(app->cmd_saved_co, app->doc.len);
      app->sa = min_u64(app->cmd_saved_sa, app->doc.len);
      app->sv = min_u64(app->cmd_saved_sv, app->doc.len);
      sync_caret_from_offsets(app);
      keep_caret_visible(app);
    }
    app->cmd_restore_pending = false;
    request_repaint(app, FALSE);
    return 0;
  case WM_GETMINMAXINFO:
  {
    MINMAXINFO *mmi = (MINMAXINFO *)lp;
    if (mmi)
    {
      mmi->ptMinTrackSize.x = MIN_WINDOW_WIDTH;
      mmi->ptMinTrackSize.y = MIN_WINDOW_HEIGHT;
    }
    return 0;
  }
  case WM_CREATE:
  {
    app->hwnd = hwnd;
    apply_native_dark_mode(hwnd);
    app->bg_brush = CreateSolidBrush(THEME_BG);
    app->cmd_bg_brush = CreateSolidBrush(THEME_CMD_POPUP_BG);
    app->font_size = FONT_SIZE_DEFAULT;
    apply_font_size(app, app->font_size);
    DragAcceptFiles(hwnd, TRUE);
    HDC dc = GetDC(hwnd);
    recalc_metrics(app, dc);
    ReleaseDC(hwnd, dc);
    clear_history();
    clear_stream_selection(app);
    apply_caret_line_metrics(app);
    update_title(app);
    SetTimer(hwnd, FILEWATCH_TIMER_ID, FILEWATCH_TIMER_MS, NULL);
    return 0;
  }
  case WM_SIZE:
  {
    RECT rc;
    int client_w;
    int client_h;
    if (app->handling_main_wm_size)
      return 0;
    app->handling_main_wm_size = true;
    if (!GetClientRect(hwnd, &rc)) SetRect(&rc, 0, 0, 0, 0);
    client_w = rc.right - rc.left;
    client_h = rc.bottom - rc.top;
    if (client_w == app->last_client_w && client_h == app->last_client_h)
    {
      app->handling_main_wm_size = false;
      return 0;
    }
    app->last_client_w = client_w;
    app->last_client_h = client_h;
    HDC dc = GetDC(hwnd);
    recalc_metrics(app, dc);
    ReleaseDC(hwnd, dc);
    update_scrollbars(app);
    repro_log_size((unsigned long)wp, client_w, client_h, app->rows, app->cols,
                   (unsigned long long)app->tl, app->lcp);
    position_caret(app);
    InvalidateRect(hwnd, NULL, TRUE);
    app->handling_main_wm_size = false;
    return 0;
  }
  case WM_SETFOCUS:
    app->ch = 0;
    app_check_external_change(app, true);
    ensure_caret_shape(app);
    position_caret(app);
    return 0;
  case WM_KILLFOCUS:
    app->pending_high_surrogate = 0;
    HideCaret(hwnd);
    DestroyCaret();
    app->ch = 0;
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT:
  {
    bool had_focus = GetFocus() == hwnd;
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    HDC memdc;
    HBITMAP bmp;
    HBITMAP old_bmp;
    int w;
    int h;
    if (had_focus) HideCaret(hwnd);
    GetClientRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    memdc = CreateCompatibleDC(dc);
    bmp = (w > 0 && h > 0) ? CreateCompatibleBitmap(dc, w, h) : NULL;
    if (memdc && bmp)
    {
      old_bmp = (HBITMAP)SelectObject(memdc, bmp);
      paint_editor(app, memdc);
      BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top,
             ps.rcPaint.right - ps.rcPaint.left,
             ps.rcPaint.bottom - ps.rcPaint.top,
             memdc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
      SelectObject(memdc, old_bmp);
      DeleteObject(bmp);
      DeleteDC(memdc);
    }
    else
    {
      if (bmp) DeleteObject(bmp);
      if (memdc) DeleteDC(memdc);
      paint_editor(app, dc);
    }
    EndPaint(hwnd, &ps);
    if (had_focus) ShowCaret(hwnd);
    return 0;
  }
  case WM_KEYDOWN:
    if (wp != VK_PACKET) app->pending_high_surrogate = 0;
    repro_begin_app_event(app, "key", (unsigned long)wp, (unsigned long)lp);
    handle_key(app, wp);
    repro_end_app_event(app);
    return 0;
  case WM_SYSKEYDOWN:
    if (wp == VK_F4 && (GetKeyState(VK_MENU) & 0x8000) != 0)
      return DefWindowProcW(hwnd, msg, wp, lp);
    repro_begin_app_event(app, "syskey", (unsigned long)wp, (unsigned long)lp);
    handle_key(app, wp);
    repro_end_app_event(app);
    return 0;
  case WM_CHAR:
    repro_begin_app_event(app, "char", (unsigned long)wp, (unsigned long)lp);
    handle_char(app, wp);
    repro_end_app_event(app);
    return 0;
  case WM_GETTEXTLENGTH:
  {
    char *utf8 = NULL;
    int utf8_len = doc_copy_utf8_z(&app->doc, &utf8);
    int wide_len = 0;
    if (utf8)
    {
      wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8, utf8_len, NULL, 0);
      free(utf8);
    }
    return wide_len;
  }
  case WM_GETTEXT:
  {
    int cap = (int)wp;
    WCHAR *out = (WCHAR *)lp;
    char *utf8 = NULL;
    int utf8_len = doc_copy_utf8_z(&app->doc, &utf8);
    int written = 0;
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    if (!utf8) return 0;
    written = MultiByteToWideChar(CP_UTF8, 0, utf8, utf8_len, out, cap - 1);
    out[written] = 0;
    free(utf8);
    return written;
  }
  case WM_MOUSEWHEEL:
  {
    u64 tl_before = app->tl;
    int wheel_delta = GET_WHEEL_DELTA_WPARAM(wp);
    int wheel_notches = 0;
    int lines_per_notch = 0;
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
    {
      int z = wheel_delta;
      int steps;
      app->zoom_wheel_delta_remainder += z;
      steps = app->zoom_wheel_delta_remainder / WHEEL_DELTA;
      app->zoom_wheel_delta_remainder -= steps * WHEEL_DELTA;
      if (steps != 0) apply_font_size(app, app->font_size + (steps * 2));
      return 0;
    }
    if (app->lcp) return 0;
    app->vsb = false;
    /* Suppress keep_caret_visible() before any scrollbar/layout work, because
     re-entrant WM_SIZE during ShowScrollBar/SetScrollInfo can otherwise snap
     tl back to the caret line mid-wheel handling. */
    app->skip_keep_caret_visible_once = true;
    {
      int z = wheel_delta;
      int notches;
      lines_per_notch = wheel_scroll_lines_per_notch();
      app->wheel_delta_remainder += z;
      notches = app->wheel_delta_remainder / WHEEL_DELTA;
      app->wheel_delta_remainder -= notches * WHEEL_DELTA;
      wheel_notches = notches;
      if (lines_per_notch != 0 && notches != 0)
      {
        if (lines_per_notch < 0)
          lines_per_notch = app->rows > 0 ? app->rows : 1;
        {
          int steps = (notches > 0 ? notches : -notches) * lines_per_notch;
          if (notches > 0)
          {
            while (steps-- > 0)
            {
              if (app->tl == 0) break;
              app->tl--;
            }
          }
          else
          {
            /* Apply wheel-down delta in one shot, then clamp once.
             Per-step update_scrollbars() can re-enter via scrollbar visibility
             changes and transiently decrease tl, which falsely trips the
             previous early-break logic and makes scrolling appear stuck. */
            u64 delta = (u64)steps;
            if (UINT64_MAX - app->tl < delta)
              app->tl = UINT64_MAX;
            else
              app->tl += delta;
          }
        }
      }
    }
    update_scrollbars(app);
    preserve_caret_row_for_scroll(app, tl_before);
    {
      SCROLLINFO si;
      int max_pos = 0;
      int pos = 0;
      memset(&si, 0, sizeof(si));
      si.cbSize = sizeof(si);
      si.fMask = SIF_ALL;
      if (GetScrollInfo(hwnd, SB_VERT, &si))
      {
        max_pos = scroll_max_pos(&si);
        pos = si.nPos;
      }
      repro_log_wheel(wheel_delta, app->wheel_delta_remainder, wheel_notches, lines_per_notch,
                      (unsigned long long)tl_before, (unsigned long long)app->tl,
                      pos, max_pos, app->lcp, app->rows);
    }
    request_repaint(app, FALSE);
    return 0;
  }
  case WM_CLOSE:
    repro_capture_close_screenshot(hwnd);
    if (sync_path_utf8_on_change(app))
    {
      int choice = MessageBoxW(hwnd, L"save changes?",
                               L"exiting", MB_ICONQUESTION | MB_YESNOCANCEL | MB_DEFBUTTON1);
      if (choice == IDYES)
      {
        if (app->doc.path[0])
        {
          if (!app_save_to_path(app, app->doc.path, false)) return 0;
        }
        else
        {
          if (!save_dialog(app) && sync_path_utf8_on_change(app)) return 0;
        }
      }
      else if (choice != IDNO)
        return 0;
    }
    DestroyWindow(hwnd);
    return 0;
  case WM_VSCROLL:
  {
    if (app->lcp) return 0;
    u64 tl_before = app->tl;
    int code = LOWORD(wp);
    SCROLLINFO si;
    int max_pos;
    if (code == SB_THUMBTRACK || code == SB_THUMBPOSITION)
    {
      // Thumb dragging should always map against the real line count, not estimates.
      ensure_honest_vertical_scroll_range(app);
    }
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(hwnd, SB_VERT, &si);
    max_pos = scroll_max_pos(&si);
    switch (code)
    {
    case SB_LINEUP: app->vsb = false; if (app->tl) app->tl--; break;
    case SB_LINEDOWN: app->vsb = false; app->tl++; break;
    case SB_PAGEUP: app->vsb = false; app->tl = app->tl > (u64)app->rows ? app->tl - (u64)app->rows : 0; break;
    case SB_PAGEDOWN: app->vsb = false; app->tl += (u64)app->rows; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
    {
      int thumb_pos = (code == SB_THUMBTRACK) ? si.nTrackPos : si.nPos;
      if (thumb_pos < 0) thumb_pos = 0;
      app->tl = (u64)min_u64((u64)max_pos, (u64)thumb_pos);
      app->vsb = ((int)app->tl >= max_pos);
      break;
    }
    case SB_ENDSCROLL:
      if ((int)app->tl >= max_pos || cursor_at_vscroll_bottom(hwnd))
      {
        app->vsb = true;
        app->tl = (u64)max_pos;
      }
      else
        app->vsb = false;
      break;
    default:
      app->vsb = false;
      break;
    }
    /* Same re-entrancy guard as wheel: avoid caret-driven tl correction while
     the scroll action is still resolving nested size/scrollbar messages. */
    app->skip_keep_caret_visible_once = true;
    update_scrollbars(app);
    repro_log_vscroll(code, si.nPos, si.nTrackPos, max_pos,
                      (unsigned long long)tl_before, (unsigned long long)app->tl, app->vsb);
    preserve_caret_row_for_scroll(app, tl_before);
    request_repaint(app, FALSE);
    return 0;
  }
  case WM_HSCROLL:
  {
    int code = LOWORD(wp);
    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(hwnd, SB_HORZ, &si);
    switch (code)
    {
    case SB_LINELEFT: if (app->fc) app->fc--; break;
    case SB_LINERIGHT: app->fc++; break;
    case SB_PAGELEFT: app->fc = app->fc > (u64)app->cols ? app->fc - (u64)app->cols : 0; break;
    case SB_PAGERIGHT: app->fc += (u64)app->cols; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
      app->fc = (u64)si.nTrackPos;
      break;
    default: break;
    }
    /* Prevent immediate caret-visibility correction from snapping fc back to
     caret column while horizontal scrollbar interaction is in-flight. */
    app->skip_keep_caret_visible_once = true;
    update_scrollbars(app);
    clamp_caret_to_horizontal_view(app);
    /* update_scrollbars() can re-enter through WM_SIZE when scrollbar
     visibility changes. Re-arm the guard so the final caret positioning for
     this repaint still respects the scrollbar-driven horizontal offset. */
    app->skip_keep_caret_visible_once = true;
    request_repaint(app, FALSE);
    return 0;
  }
  case WM_LBUTTONDOWN:
    app->pending_high_surrogate = 0;
  {
    repro_begin_app_event(app, "ldown", (unsigned long)GET_X_LPARAM(lp), (unsigned long)GET_Y_LPARAM(lp));
    app->vsb = false;
    SetFocus(hwnd);
    int x = GET_X_LPARAM(lp);
    int y = GET_Y_LPARAM(lp);
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    u64 old = app->co;
    set_caret_from_xy(app, x, y);
    if (alt)
    {
      app->bsa = true;
      app->bal = app->cl;
      app->bac = app->cc;
      app->bcl = app->cl;
      app->bcc = app->cc;
      app->bdc = app->cc;
      collapse_stream_selection_to_caret(app);
      app->sbxm = true;
    }
    else if (shift)
    {
      if (has_box_selection(app)) clear_box_selection(app);
      if (!has_stream_selection(app)) app->sa = old;
      app->sv = app->co;
      app->sbxm = false;
    }
    else
    {
      clear_stream_selection(app);
      app->sbxm = false;
    }
    app->swdm = false;
    app->swm = true;
    SetCapture(hwnd);
    request_repaint(app, FALSE);
    repro_end_app_event(app);
    return 0;
  }
  case WM_LBUTTONDBLCLK:
  {
    repro_begin_app_event(app, "ldbl", (unsigned long)GET_X_LPARAM(lp), (unsigned long)GET_Y_LPARAM(lp));
    int x = GET_X_LPARAM(lp);
    int y = GET_Y_LPARAM(lp);
    app->vsb = false;
    SetFocus(hwnd);
    set_caret_from_xy(app, x, y);
    if (x < app->gutter_w + EDIT_TEXT_LEFT_PADDING)
    {
      clear_stream_selection(app);
      app->swdm = false;
      app->swm = false;
      request_repaint(app, FALSE);
      repro_end_app_event(app);
      return 0;
    }
    select_word_at_caret(app);
    app->swdm = true;
    app->wdas = min_u64(app->sa, app->sv);
    app->wdae = max_u64(app->sa, app->sv);
    app->swm = true;
    SetCapture(hwnd);
    keep_caret_visible(app);
    request_repaint(app, FALSE);
    repro_end_app_event(app);
    return 0;
  }
  case WM_MOUSEMOVE:
    if (app->swm && (wp & MK_LBUTTON))
    {
      repro_begin_app_event(app, "move", (unsigned long)GET_X_LPARAM(lp), (unsigned long)GET_Y_LPARAM(lp));
      int x = GET_X_LPARAM(lp);
      int y = GET_Y_LPARAM(lp);
      set_caret_from_drag_xy(app, x, y);
      if (app->sbxm)
      {
        app->bcl = app->cl;
        app->bcc = app->cc;
        app->bdc = app->cc;
        if (app->bal == app->bcl &&
            app->bac == app->bcc)
          clear_box_selection(app);
        else
          app->bsa = true;
        collapse_stream_selection_to_caret(app);
      }
      else if (app->swdm)
      {
        u64 word_start = app->co;
        u64 word_end = app->co;
        if (word_bounds_near_offset(app, app->co, &word_start, &word_end))
        {
          if (word_start >= app->wdas)
          {
            app->sa = app->wdas;
            app->sv = word_end;
            app->co = word_end;
          }
          else
          {
            app->sa = app->wdae;
            app->sv = word_start;
            app->co = word_start;
          }
          sync_caret_from_offsets(app);
        }
      }
      else
        app->sv = app->co;
      keep_caret_visible(app);
      request_repaint(app, FALSE);
      repro_end_app_event(app);
      return 0;
    }
    break;
  case WM_LBUTTONUP:
    if (app->swm)
    {
      ReproUiState selection_state;
      repro_begin_app_event(app, "lup", (unsigned long)GET_X_LPARAM(lp), (unsigned long)GET_Y_LPARAM(lp));
      app->swm = false;
      app->sbxm = false;
      app->swdm = false;
      ReleaseCapture();
      repro_end_app_event(app);
      repro_capture_ui_state(app, &selection_state);
      if (selection_state.box_sel)
        repro_log_drag_selection("box", &selection_state);
      else if (selection_state.stream_sel)
        repro_log_drag_selection("stream", &selection_state);
      return 0;
    }
    break;
  case WM_DROPFILES:
  {
    HDROP drop = (HDROP)wp;
    WCHAR path[MAX_PATH];
    if (DragQueryFileW(drop, 0, path, MAX_PATH))
    {
      if (app_confirm_replace_document(app))
        app_load_path(app, path);
    }
    DragFinish(drop);
    return 0;
  }
  case WM_TIMER:
    if (wp == FILEWATCH_TIMER_ID)
    {
      app_check_external_change(app, false);
      return 0;
    }
    if (wp == LINECOUNT_TIMER_ID && app->lcp)
    {
      ULONGLONG start = monotonic_tick_ms();
      while (!app->doc.lines.eof)
      {
        doc_discover_step(&app->doc, LINE_DISCOVERY_ASYNC_CHUNK);
        if (monotonic_tick_ms() - start >= LINE_DISCOVERY_ASYNC_SLICE_MS) break;
      }
      if (app->doc.lines.eof)
      {
        app->lcp = false;
        apply_scroll_limits_and_position(app);
      }
      update_scrollbars(app);
      request_repaint(app, FALSE);
      return 0;
    }
    break;
  case WM_DESTROY:
    repro_shutdown();
    apply_scroll_limits_and_position(app);
    KillTimer(hwnd, FILEWATCH_TIMER_ID);
    if (app->swm) ReleaseCapture();
    clear_history();
    if (app->cmd_bg_brush) DeleteObject(app->cmd_bg_brush);
    app->cmd_bg_brush = NULL;
    if (app->bg_brush) DeleteObject(app->bg_brush);
    doc_clear(&app->doc);
    if (app->font) DeleteObject(app->font);
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
  (void)prev;
  (void)cmd;
  memset(&g_app, 0, sizeof(g_app));
  repro_init();
  init_native_dark_mode();
  doc_set_empty(&g_app.doc);
  int argc = 0;
  WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv && argc > 1)
  {
    doc_load_mapped(&g_app.doc, argv[1]);
    repro_log_loaded_file(argv[1], (unsigned long long)g_app.doc.len, g_app.doc.path[0] != 0);
    g_app.lcp = should_defer_line_count(&g_app.doc);
    if (!g_app.lcp) doc_discover_all_lines(&g_app.doc);
  }
  if (argv) LocalFree(argv);
  WNDCLASSW wc;
  WNDCLASSW cmd_wc;
  WNDCLASSW find_wc;
  WNDCLASSW goto_wc;
  memset(&wc, 0, sizeof(wc));
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = inst;
  wc.lpszClassName = APP_CLASS_NAME;
  wc.hCursor = LoadCursorW(NULL, IDC_IBEAM);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  RegisterClassW(&wc);
  memset(&cmd_wc, 0, sizeof(cmd_wc));
  cmd_wc.lpfnWndProc = command_popup_wndproc;
  cmd_wc.hInstance = inst;
  cmd_wc.lpszClassName = CMD_POPUP_CLASS_NAME;
  cmd_wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  cmd_wc.hbrBackground = NULL;
  RegisterClassW(&cmd_wc);
  memset(&find_wc, 0, sizeof(find_wc));
  find_wc.lpfnWndProc = find_popup_wndproc;
  find_wc.hInstance = inst;
  find_wc.lpszClassName = FIND_POPUP_CLASS_NAME;
  find_wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  find_wc.hbrBackground = NULL;
  RegisterClassW(&find_wc);
  memset(&goto_wc, 0, sizeof(goto_wc));
  goto_wc.lpfnWndProc = goto_popup_wndproc;
  goto_wc.hInstance = inst;
  goto_wc.lpszClassName = GOTO_POPUP_CLASS_NAME;
  goto_wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  goto_wc.hbrBackground = NULL;
  RegisterClassW(&goto_wc);
  HWND hwnd = CreateWindowExW(0, APP_CLASS_NAME, L"text", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
                              CW_USEDEFAULT, CW_USEDEFAULT, 480, 520, NULL, NULL, inst, NULL);
  if (!hwnd) return 1;
  ShowWindow(hwnd, show);
  UpdateWindow(hwnd);
  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0)
  {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return (int)msg.wParam;
}

