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
#include <direct.h>

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
#define MIN_WINDOW_HEIGHT 240
#define TAB_WIDTH 4
#define MAX_PAINT_COLS 8192
#define LINE_DISCOVERY_BUDGET (256u * 1024u)
#define LINE_DISCOVERY_ASYNC_CHUNK (64u * 1024u)
#define LINE_DISCOVERY_ASYNC_SLICE_MS 10
#define LINECOUNT_TIMER_ID 1
#define LINECOUNT_ASYNC_FILE_BYTES (8u * 1024u * 1024u)
#define ADD_BLOCK_DEFAULT (64u * 1024u)
#define THEME_BG RGB(35, 31, 24)
#define THEME_FG RGB(155, 155, 155)
#define THEME_GUTTER_BG THEME_BG
#define THEME_GUTTER_FG RGB(122, 104, 88)
#define THEME_TEXT RGB(220, 220, 220)
#define THEME_GUTTER_ACTIVE_LINE_BG RGB(68, 58, 48)
#define THEME_SELECTION_BG RGB(0, 120, 215)
#define THEME_SELECTION_TEXT RGB(255, 255, 255)
#define FONT_SIZE_MIN 10
#define FONT_SIZE_MAX 48
#define FONT_SIZE_DEFAULT 20
#define IDC_FIND_COUNT 5001
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

typedef enum BackingKind
{
  BACKING_ORIGINAL,
  BACKING_ADD
} BackingKind;

typedef struct Piece
{
  struct Piece *prev;
  struct Piece *next;
  const char *data;
  u64 len;
  BackingKind kind;
} Piece;

typedef struct AddBlock
{
  struct AddBlock *next;
  char *data;
  u64 len, cap;
} AddBlock;

typedef struct LineIndex
{
  u64 *starts;
  u64 count, cap, scanned_to;
  bool eof;
} LineIndex;

typedef struct MappedFile
{
  HANDLE file;
  HANDLE map;
  const char *data;
  u64 len;
} MappedFile;

typedef struct Document
{
  Piece *head;
  Piece *tail;
  u64 len, piece_count;
  bool dirty;
  WCHAR path[MAX_PATH];
  MappedFile mf;
  AddBlock *add_head;
  AddBlock *add_tail;
  LineIndex lines;
} Document;

typedef struct App
{
  HWND hwnd;
  Document doc;
  HFONT font;
  int font_size;
  HBRUSH bg_brush;
  int char_w;
  int line_h;
  int gutter_w;
  int rows;
  int cols;
  u64 tl, fc, cl, cc, co, sa, sv;
  bool bsa;
  u64 bal, bac, bcl, bcc, bdc;
  bool vsb;
  UINT find_msg;
  HWND find_hwnd;
  WCHAR fq[256];
  FINDREPLACEW fs;
  bool swm;
  bool sbxm;
  bool swdm;
  u64 wdas, wdae;
  int ch;
  bool clm;
  bool lcp;
} App;

typedef struct SpanRef
{
  const char *data;
  u64 len;
  BackingKind kind;
} SpanRef;

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

typedef struct Resolve
{
  Piece *piece;
  u64 local, base;
} Resolve;

/******************************************************************************
 * Global State
 ******************************************************************************/

static App g_app;

static HBRUSH g_find_bg_brush;

static WNDPROC g_find_edit_wndproc;

static WNDPROC g_find_button_wndproc;

static UndoStack g_undo_stack;

static UndoStack g_redo_stack;

static UndoTxn g_txn;

static int g_txn_depth;

static bool g_history_replaying;

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

/* Document primitives. */
static int doc_copy_utf8_z(Document *doc, char **out_utf8);

static bool doc_set_empty(Document *doc);

static bool doc_insert_bytes(Document *doc, u64 off, const char *text, u64 n, const char **out_add_ptr);

static bool doc_insert_span(Document *doc, u64 off, const char *data, u64 n, BackingKind kind);

static bool doc_delete_range(Document *doc, u64 off, u64 n);

static Resolve doc_resolve_offset(Document *doc, u64 off);

/* App-facing edit operations. */
static bool app_doc_insert(App *app, u64 off, const char *text, u64 n);

static bool app_doc_delete(App *app, u64 off, u64 n);

/* Caret, selection, and repaint. */
static void sync_caret_from_offsets(App *app);

static void keep_caret_visible(App *app);

static void update_title(App *app);

static void position_caret(App *app);

static void request_repaint(App *app, BOOL erase);

static void keep_visible_and_repaint(App *app);

static void apply_caret_line_metrics(App *app);

static void apply_scroll_limits_and_count(App *app);

static void apply_scroll_limits_and_position(App *app);

static void apply_font_size(App *app, int new_size);

static bool sync_path_utf8_on_change(App *app);

static void update_find_count_label(HWND hwnd);

static void box_selection_bounds(const App *app, u64 *top, u64 *bottom, u64 *left, u64 *right);

static bool doc_get_byte(Document *doc, u64 off, unsigned char *out);

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

/******************************************************************************
 * Undo / Redo
 ******************************************************************************/
static void undo_op_free(UndoOp *op)
{
  if (!op) return;
  free(op->spans);
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
  SpanRef *spans = NULL;
  size_t count = 0;
  size_t cap = 0;
  if (off >= doc->len || len == 0)
  {
    *out_spans = NULL;
    *out_count = 0;
    return true;
  }
  if (off + len > doc->len) len = doc->len - off;
  Resolve r = doc_resolve_offset(doc, off);
  Piece *p = r.piece;
  u64 local = r.local;
  while (p && len)
  {
    u64 take = min_u64(p->len - local, len);
    if (take)
    {
      if (count == cap)
      {
        size_t new_cap = cap ? cap * 2 : 8;
        SpanRef *new_spans = (SpanRef *)realloc(spans, new_cap *sizeof(SpanRef));
        if (!new_spans)
        {
          free(spans);
          return false;
        }
        spans = new_spans;
        cap = new_cap;
      }
      spans[count].data = p->data + local;
      spans[count].len = take;
      spans[count].kind = p->kind;
      count++;
    }
    len -= take;
    p = p->next;
    local = 0;
  }
  *out_spans = spans;
  *out_count = count;
  return true;
}

static bool txn_record_insert(u64 off, const char *data, u64 len)
{
  UndoOp op;
  SpanRef *spans;
  if (g_history_replaying || g_txn_depth <= 0 || len == 0) return true;
  spans = (SpanRef *)malloc(sizeof(SpanRef));
  if (!spans) return false;
  spans[0].data = data;
  spans[0].len = len;
  spans[0].kind = BACKING_ADD;
  memset(&op, 0, sizeof(op));
  op.kind = UNDO_OP_INSERT;
  op.off = off;
  op.len = len;
  op.spans = spans;
  op.span_count = 1;
  if (!undo_txn_push_op(&g_txn, &op))
  {
    free(spans);
    return false;
  }
  return true;
}

static bool txn_record_delete(u64 off, u64 len, SpanRef *spans, size_t span_count)
{
  UndoOp op;
  if (g_history_replaying || g_txn_depth <= 0 || len == 0)
  {
    free(spans);
    return true;
  }
  memset(&op, 0, sizeof(op));
  op.kind = UNDO_OP_DELETE;
  op.off = off;
  op.len = len;
  op.spans = spans;
  op.span_count = span_count;
  if (!undo_txn_push_op(&g_txn, &op))
  {
    free(spans);
    return false;
  }
  return true;
}

static bool doc_reinsert_spans(Document *doc, u64 off, const SpanRef *spans, size_t span_count)
{
  u64 at = off;
  for (size_t i = 0; i < span_count; ++i)
  {
    if (!doc_insert_span(doc, at, spans[i].data, spans[i].len, spans[i].kind)) return false;
    at += spans[i].len;
  }
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

static bool apply_txn_forward(App *app, const UndoTxn *txn)
{
  for (size_t i = 0; i < txn->count; ++i)
  {
    const UndoOp *op = &txn->ops[i];
    if (op->kind == UNDO_OP_INSERT)
    {
      if (!doc_reinsert_spans(&app->doc, op->off, op->spans, op->span_count)) return false;
    }
    else
    {
      if (!doc_delete_range(&app->doc, op->off, op->len)) return false;
    }
  }
  return true;
}

static bool apply_txn_reverse(App *app, const UndoTxn *txn)
{
  for (size_t i = txn->count; i-- > 0;)
  {
    const UndoOp *op = &txn->ops[i];
    if (op->kind == UNDO_OP_INSERT)
    {
      if (!doc_delete_range(&app->doc, op->off, op->len)) return false;
    }
    else
    {
      if (!doc_reinsert_spans(&app->doc, op->off, op->spans, op->span_count)) return false;
    }
  }
  return true;
}

static void perform_undo(App *app)
{
  if (g_txn_depth > 0) end_edit_txn(app);
  if (g_undo_stack.count == 0) return;
  UndoTxn txn = g_undo_stack.items[g_undo_stack.count - 1];
  g_undo_stack.count--;
  g_history_replaying = true;
  if (apply_txn_reverse(app, &txn))
  {
    apply_after_state(app, &txn, false);
    if (!undo_stack_push(&g_redo_stack, &txn)) undo_txn_free(&txn);
  }
  else
    undo_txn_free(&txn);
  g_history_replaying = false;
}

static void perform_redo(App *app)
{
  if (g_txn_depth > 0) end_edit_txn(app);
  if (g_redo_stack.count == 0) return;
  UndoTxn txn = g_redo_stack.items[g_redo_stack.count - 1];
  g_redo_stack.count--;
  g_history_replaying = true;
  if (apply_txn_forward(app, &txn))
  {
    apply_after_state(app, &txn, true);
    if (!undo_stack_push(&g_undo_stack, &txn)) undo_txn_free(&txn);
  }
  else
    undo_txn_free(&txn);
  g_history_replaying = false;
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

/******************************************************************************
 * Document Storage And Piece List
 ******************************************************************************/
static void mapped_close(MappedFile *mf)
{
  if (mf->data) UnmapViewOfFile(mf->data);
  if (mf->map) CloseHandle(mf->map);
  if (mf->file && mf->file != INVALID_HANDLE_VALUE) CloseHandle(mf->file);
  memset(mf, 0, sizeof(*mf));
}

static void line_index_free(LineIndex *li)
{
  free(li->starts);
  memset(li, 0, sizeof(*li));
}

static bool line_index_push_start(LineIndex *li, u64 off)
{
  if (li->count == li->cap)
  {
    u64 new_cap = li->cap ? li->cap * 2 : 4096;
    u64 *new_starts = (u64 *)realloc(li->starts, (size_t)(new_cap *sizeof(u64)));
    if (!new_starts) return false;
    li->starts = new_starts;
    li->cap = new_cap;
  }
  li->starts[li->count++] = off;
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
  while (keep < li->count && li->starts[keep] <= off) ++keep;
  li->count = keep;
  if (li->scanned_to >= off) li->scanned_to = off;
  li->eof = false;
}

static void line_index_adjust_insert(LineIndex *li, u64 off, u64 n)
{
  for (u64 i = 1; i < li->count; ++i)
  {
    if (li->starts[i] > off) li->starts[i] += n;
  }
  if (li->scanned_to > off) li->scanned_to += n;
}

static void line_index_adjust_delete(LineIndex *li, u64 off, u64 n)
{
  u64 end = off + n;
  for (u64 i = 1; i < li->count; ++i)
  {
    if (li->starts[i] > end)
      li->starts[i] -= n;
    else if (li->starts[i] > off)
      li->starts[i] = off;
  }
  if (li->scanned_to > end)
    li->scanned_to -= n;
  else if (li->scanned_to > off)
  {
    li->scanned_to = off;
    li->eof = false;
  }
}

static void piece_free_all(Document *doc)
{
  Piece *p = doc->head;
  while (p)
  {
    Piece *next = p->next;
    free(p);
    p = next;
  }
  doc->head = NULL;
  doc->tail = NULL;
  doc->piece_count = 0;
}

static void add_blocks_free(AddBlock *b)
{
  while (b)
  {
    AddBlock *next = b->next;
    free(b->data);
    free(b);
    b = next;
  }
}

static Piece *piece_new(const char *data, u64 len, BackingKind kind)
{
  Piece *p = (Piece *)calloc(1, sizeof(Piece));
  if (!p) return NULL;
  p->data = data;
  p->len = len;
  p->kind = kind;
  return p;
}

static void piece_insert_before(Document *doc, Piece *before, Piece *p)
{
  if (!before)
  {
    p->prev = doc->tail;
    p->next = NULL;
    if (doc->tail) doc->tail->next = p; else doc->head = p;
    doc->tail = p;
  }
  else
  {
    p->prev = before->prev;
    p->next = before;
    if (before->prev) before->prev->next = p; else doc->head = p;
    before->prev = p;
  }
  doc->piece_count++;
}

static void piece_unlink_free(Document *doc, Piece *p)
{
  if (p->prev) p->prev->next = p->next; else doc->head = p->next;
  if (p->next) p->next->prev = p->prev; else doc->tail = p->prev;
  doc->piece_count--;
  free(p);
}

static bool pieces_adjacent(Piece *a, Piece *b)
{
  return a && b && a->kind == b->kind && a->data + a->len == b->data;
}

static Piece *piece_coalesce_around(Document *doc, Piece *p)
{
  if (!p) return NULL;
  if (pieces_adjacent(p->prev, p))
  {
    Piece *left = p->prev;
    left->len += p->len;
    piece_unlink_free(doc, p);
    p = left;
  }
  if (pieces_adjacent(p, p->next))
  {
    Piece *right = p->next;
    p->len += right->len;
    piece_unlink_free(doc, right);
  }
  return p;
}

static void doc_clear(Document *doc)
{
  piece_free_all(doc);
  add_blocks_free(doc->add_head);
  mapped_close(&doc->mf);
  line_index_free(&doc->lines);
  memset(doc, 0, sizeof(*doc));
  doc_reset_lines(doc);
}

static AddBlock *add_block_new(u64 cap)
{
  AddBlock *b = (AddBlock *)calloc(1, sizeof(AddBlock));
  if (!b) return NULL;
  b->data = (char *)malloc((size_t)cap);
  if (!b->data)
  {
    free(b);
    return NULL;
  }
  b->cap = cap;
  return b;
}

static bool doc_append_add_bytes(Document *doc, const char *src, u64 n, const char **out)
{
  if (n == 0)
  {
    *out = "";
    return true;
  }
  AddBlock *b = doc->add_tail;
  if (!b || b->cap - b->len < n)
  {
    u64 cap = max_u64(ADD_BLOCK_DEFAULT, n);
    AddBlock *nb = add_block_new(cap);
    if (!nb) return false;
    if (doc->add_tail) doc->add_tail->next = nb; else doc->add_head = nb;
    doc->add_tail = nb;
    b = nb;
  }
  char *ptr = b->data + b->len;
  memcpy(ptr, src, (size_t)n);
  b->len += n;
  *out = ptr;
  return true;
}

static bool doc_set_empty(Document *doc)
{
  doc_clear(doc);
  return true;
}

static bool doc_load_mapped(Document *doc, const WCHAR *path)
{
  doc_clear(doc);
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
  if (f == INVALID_HANDLE_VALUE) return doc_set_empty(doc);
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(f, &sz) || sz.QuadPart < 0)
  {
    CloseHandle(f);
    return doc_set_empty(doc);
  }
  doc->mf.file = f;
  doc->mf.len = (u64)sz.QuadPart;
  doc->len = doc->mf.len;
  lstrcpynW(doc->path, path, MAX_PATH);
  if (doc->len == 0) return true;
  doc->mf.map = CreateFileMappingW(f, NULL, PAGE_READONLY, 0, 0, NULL);
  if (!doc->mf.map)
  {
    doc_clear(doc);
    return doc_set_empty(doc);
  }
  doc->mf.data = (const char *)MapViewOfFile(doc->mf.map, FILE_MAP_READ, 0, 0, 0);
  if (!doc->mf.data)
  {
    doc_clear(doc);
    return doc_set_empty(doc);
  }
  Piece *p = piece_new(doc->mf.data, doc->len, BACKING_ORIGINAL);
  if (!p)
  {
    doc_clear(doc);
    return doc_set_empty(doc);
  }
  piece_insert_before(doc, NULL, p);
  return true;
}

static Resolve doc_resolve_offset(Document *doc, u64 off)
{
  Resolve r;
  memset(&r, 0, sizeof(r));
  if (off > doc->len) off = doc->len;
  u64 base = 0;
  for (Piece *p = doc->head; p; p = p->next)
  {
    u64 end = base + p->len;
    if (off < end)
    {
      r.piece = p;
      r.local = off - base;
      r.base = base;
      return r;
    }
    if (off == end && !p->next)
    {
      r.piece = p;
      r.local = p->len;
      r.base = base;
      return r;
    }
    base = end;
  }
  r.base = doc->len;
  return r;
}

static Piece *piece_split(Document *doc, Piece *p, u64 local)
{
  if (!p) return NULL;
  if (local == 0) return p;
  if (local >= p->len) return p->next;
  Piece *right = piece_new(p->data + local, p->len - local, p->kind);
  if (!right) return NULL;
  p->len = local;
  piece_insert_before(doc, p->next, right);
  return right;
}

typedef void (*SpanFn)(const char *data, u64 len, void *user);

static void doc_read_range(Document *doc, u64 off, u64 len, SpanFn fn, void *user)
{
  if (off >= doc->len || len == 0) return;
  if (off + len > doc->len) len = doc->len - off;
  Resolve r = doc_resolve_offset(doc, off);
  Piece *p = r.piece;
  u64 local = r.local;
  while (p && len)
  {
    u64 take = min_u64(p->len - local, len);
    if (take) fn(p->data + local, take, user);
    len -= take;
    p = p->next;
    local = 0;
  }
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

static bool doc_range_has_newline(Document *doc, u64 off, u64 len)
{
  if (off >= doc->len || len == 0) return false;
  if (off + len > doc->len) len = doc->len - off;
  Resolve r = doc_resolve_offset(doc, off);
  Piece *p = r.piece;
  u64 local = r.local;
  while (p && len)
  {
    u64 take = min_u64(p->len - local, len);
    const char *s = p->data + local;
    for (u64 i = 0; i < take; ++i)
    {
      if (s[i] == '\n' || s[i] == '\r') return true;
    }
    len -= take;
    p = p->next;
    local = 0;
  }
  return false;
}

static bool bytes_have_newline(const char *s, u64 n)
{
  for (u64 i = 0; i < n; ++i)
  {
    if (s[i] == '\n' || s[i] == '\r') return true;
  }
  return false;
}

/* Unsafe rope primitive: prefer app_doc_insert/app_doc_delete in editor flows. */
static bool doc_insert_span(Document *doc, u64 off, const char *data, u64 n, BackingKind kind)
{
  Resolve r;
  Piece *right;
  Piece *np;
  if (n == 0) return true;
  if (off > doc->len) off = doc->len;
  r = doc_resolve_offset(doc, off);
  right = r.piece ? piece_split(doc, r.piece, r.local) : NULL;
  np = piece_new(data, n, kind);
  if (!np) return false;
  piece_insert_before(doc, right, np);
  piece_coalesce_around(doc, np);
  doc->len += n;
  doc->dirty = true;
  if (bytes_have_newline(data, n))
  {
    LineIndex *li = &doc->lines;
    if (li->eof && off >= li->scanned_to)
    {
      for (u64 i = 0; i < n; ++i)
      {
        if (data[i] == '\n') line_index_push_start(li, off + i + 1);
      }
      li->scanned_to = doc->len;
      li->eof = true;
    }
    else
      line_index_invalidate_from(li, off);
  }
  else
    line_index_adjust_insert(&doc->lines, off, n);
  return true;
}

static bool doc_insert_bytes(Document *doc, u64 off, const char *text, u64 n, const char **out_add_ptr)
{
  if (n == 0) return true;
  if (off > doc->len) off = doc->len;
  const char *add_ptr = NULL;
  if (!doc_append_add_bytes(doc, text, n, &add_ptr)) return false;
  if (!doc_insert_span(doc, off, add_ptr, n, BACKING_ADD)) return false;
  if (out_add_ptr) *out_add_ptr = add_ptr;
  return true;
}

/* Unsafe rope primitive: prefer app_doc_insert/app_doc_delete in editor flows. */
static bool doc_delete_range(Document *doc, u64 off, u64 n)
{
  if (off >= doc->len || n == 0) return true;
  n = min_u64(n, doc->len - off);
  bool touches_newline = doc_range_has_newline(doc, off, n);
  Resolve start = doc_resolve_offset(doc, off);
  Piece *first = piece_split(doc, start.piece, start.local);
  Resolve end = doc_resolve_offset(doc, off + n);
  Piece *after = piece_split(doc, end.piece, end.local);
  Piece *p = first;
  while (p && p != after)
  {
    Piece *next = p->next;
    piece_unlink_free(doc, p);
    p = next;
  }
  doc->len -= n;
  doc->dirty = true;
  if (after) piece_coalesce_around(doc, after);
  else if (doc->tail) piece_coalesce_around(doc, doc->tail);
  if (touches_newline) line_index_invalidate_from(&doc->lines, off);
  else line_index_adjust_delete(&doc->lines, off, n);
  return true;
}

static bool app_doc_insert(App *app, u64 off, const char *text, u64 n)
{
  const char *add_ptr = NULL;
  if (!doc_insert_bytes(&app->doc, off, text, n, &add_ptr)) return false;
  if (!txn_record_insert(off, add_ptr, n)) return false;
  return true;
}

static bool app_doc_delete(App *app, u64 off, u64 n)
{
  SpanRef *spans = NULL;
  size_t span_count = 0;
  u64 del_len = 0;
  if (off >= app->doc.len || n == 0) return true;
  del_len = min_u64(n, app->doc.len - off);
  if (g_txn_depth > 0 && !g_history_replaying)
  {
    if (!doc_capture_spans(&app->doc, off, del_len, &spans, &span_count)) return false;
  }
  if (!doc_delete_range(&app->doc, off, del_len))
  {
    free(spans);
    return false;
  }
  if (!txn_record_delete(off, del_len, spans, span_count)) return false;
  return true;
}

static bool doc_flatten_to_one_add_piece(Document *doc)
{
  AddBlock *flat = add_block_new(doc->len ? doc->len : 1);
  if (!flat) return false;
  if (doc->len)
  {
    CopyCtx ctx = { flat->data, 0 };
    doc_read_range(doc, 0, doc->len, copy_span, &ctx);
    flat->len = doc->len;
  }
  Piece *new_piece = NULL;
  if (doc->len)
  {
    new_piece = piece_new(flat->data, doc->len, BACKING_ADD);
    if (!new_piece)
    {
      free(flat->data);
      free(flat);
      return false;
    }
  }
  Piece *old_pieces = doc->head;
  AddBlock *old_add = doc->add_head;
  doc->head = doc->tail = NULL;
  doc->piece_count = 0;
  doc->add_head = doc->add_tail = flat;
  if (new_piece) piece_insert_before(doc, NULL, new_piece);
  while (old_pieces)
  {
    Piece *next = old_pieces->next;
    free(old_pieces);
    old_pieces = next;
  }
  add_blocks_free(old_add);
  mapped_close(&doc->mf);
  doc_reset_lines(doc);
  return true;
}

static bool doc_save(Document *doc, const WCHAR *path)
{
  if (!doc_flatten_to_one_add_piece(doc)) return false;
  HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return false;
  u64 written_total = 0;
  const char *data = doc->head ? doc->head->data : "";
  while (written_total < doc->len)
  {
    DWORD chunk = (DWORD)min_u64(doc->len - written_total, 1u << 24);
    DWORD wrote = 0;
    if (!WriteFile(f, data + written_total, chunk, &wrote, NULL))
    {
      CloseHandle(f);
      return false;
    }
    written_total += wrote;
    if (wrote == 0) break;
  }
  CloseHandle(f);
  if (written_total != doc->len) return false;
  doc->dirty = false;
  lstrcpynW(doc->path, path, MAX_PATH);
  return true;
}

/******************************************************************************
 * Line Index
 ******************************************************************************/
static void scan_for_lines(Document *doc, u64 target)
{
  LineIndex *li = &doc->lines;
  u64 budget = LINE_DISCOVERY_BUDGET;
  u64 off = li->scanned_to;
  Resolve r = doc_resolve_offset(doc, off);
  Piece *p = r.piece;
  u64 local = r.local;
  while (!li->eof && li->count <= target && budget > 0)
  {
    if (!p)
    {
      li->scanned_to = doc->len;
      li->eof = true;
      break;
    }
    const char *s = p->data + local;
    u64 avail = p->len - local;
    while (avail && budget > 0)
    {
      --budget;
      ++off;
      if (*s++ == '\n')
      {
        line_index_push_start(li, off);
        if (li->count > target) break;
      }
      --avail;
    }
    if (avail == 0)
    {
      p = p->next;
      local = 0;
    }
    else
      local = p->len - avail;
    li->scanned_to = off;
    if (off >= doc->len)
    {
      li->eof = true;
      break;
    }
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
  return doc->lines.starts[line];
}

static u64 doc_line_length_clamped(Document *doc, u64 start, u64 limit)
{
  if (start >= doc->len) return doc->len;
  u64 end = min_u64(doc->len, start + limit);
  Resolve r = doc_resolve_offset(doc, start);
  Piece *p = r.piece;
  u64 local = r.local;
  u64 off = start;
  while (p && off < end)
  {
    u64 take = min_u64(p->len - local, end - off);
    const char *s = p->data + local;
    for (u64 i = 0; i < take; ++i)
    {
      if (s[i] == '\r' || s[i] == '\n') return off + i;
    }
    off += take;
    p = p->next;
    local = 0;
  }
  return end;
}

static u64 doc_line_col_clamped(Document *doc, u64 line, u64 col)
{
  u64 start = doc_line_start(doc, line);
  u64 end = doc_line_length_clamped(doc, start, col + 1);
  return min_u64(col, end - start);
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
    if (doc->lines.starts[mid] <= off)
    {
      line = mid;
      lo = mid + 1;
    }
    else
      hi = mid;
  }
  *out_line = line;
  *out_col = off - doc->lines.starts[line];
}

/******************************************************************************
 * View / Caret / Scroll
 ******************************************************************************/
static void update_title(App *app)
{
  WCHAR title[MAX_PATH + 64];
  const WCHAR *name = app->doc.path[0] ? app->doc.path : L"Untitled";
  wsprintfW(title, L"%s%s - text", sync_path_utf8_on_change(app) ? L"* " : L"", name);
  SetWindowTextW(app->hwnd, title);
}

static void update_scrollbars(App *app)
{
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
  if (app->lcp)
    ShowScrollBar(app->hwnd, SB_VERT, FALSE);
  else
  {
    bool need_vscroll = known_lines > page;
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
    u64 max_line_cols = 1;
    u64 first_visible_line = app->tl;
    u64 last_visible_line = app->tl + (u64)(app->rows > 0 ? app->rows : 1);
    for (u64 line = first_visible_line; line < last_visible_line && line < app->doc.lines.count; ++line)
    {
      u64 start = app->doc.lines.starts[line];
      u64 end = (line + 1 < app->doc.lines.count) ? app->doc.lines.starts[line + 1] : app->doc.len;
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
        u64 width = end - start;
        if (width > max_line_cols) max_line_cols = width;
      }
    }
    {
      bool need_hscroll = max_line_cols > (u64)page_cols;
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
}

static bool doc_discover_step(Document *doc, u64 budget)
{
  LineIndex *li = &doc->lines;
  u64 off;
  Resolve r;
  Piece *p;
  u64 local;
  if (li->eof || budget == 0) return li->eof;
  off = li->scanned_to;
  r = doc_resolve_offset(doc, off);
  p = r.piece;
  local = r.local;
  while (budget > 0)
  {
    if (!p)
    {
      li->scanned_to = doc->len;
      li->eof = true;
      break;
    }
    {
      const char *s = p->data + local;
      u64 avail = p->len - local;
      while (avail && budget > 0)
      {
        --budget;
        ++off;
        if (*s++ == '\n') line_index_push_start(li, off);
        --avail;
      }
      if (avail == 0)
      {
        p = p->next;
        local = 0;
      }
      else
        local = p->len - avail;
      li->scanned_to = off;
      if (off >= doc->len)
      {
        li->eof = true;
        break;
      }
    }
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

static void keep_caret_visible(App *app)
{
  if (app->cl < app->tl) app->tl = app->cl;
  if (app->cl >= app->tl + (u64)app->rows)
    app->tl = app->cl - (u64)app->rows + 1;
  if (app->cc < app->fc) app->fc = app->cc;
  if (app->cc >= app->fc + (u64)app->cols)
    app->fc = app->cc - (u64)app->cols + 1;
}

static void position_caret(App *app)
{
  if (GetFocus() != app->hwnd) return;
  ensure_caret_shape(app);
  update_scrollbars(app);
  keep_caret_visible(app);
  u64 visual_line = app->cl;
  if (app->bsa)
    visual_line = app->bal < app->bcl ? app->bal : app->bcl;
  int row = (int)(visual_line - app->tl);
  int col = (int)(app->cc - app->fc);
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
  app->co = doc_line_start(&app->doc, app->cl) + app->cc;
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
    if (del_col_start < ll)
    {
      u64 del_start = ls + del_col_start;
      u64 del_to = min_u64(del_col_end, ll);
      if (del_to > del_col_start)
      {
        app_doc_delete(app, del_start, del_to - del_col_start);
        ll -= (del_to - del_col_start);
        changed = true;
      }
    }
    if (insert_len > 0)
    {
      u64 ins_col = left;
      if (ll < ins_col)
      {
        if (!app_insert_spaces(app, ls + ll, ins_col - ll))
          continue;
        changed = true;
      }
      if (app_doc_insert(app, ls + ins_col, insert_text, insert_len)) changed = true;
    }
  }
  if (changed)
  {
    update_title(app);
    doc_ensure_line(&app->doc, bottom);
    app->bac = left + insert_len;
    app->bcc = left + insert_len;
    app->bdc = left + insert_len;
    set_caret_line_col(app, app->bcl, app->bdc);
  }
  end_edit_txn(app);
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
  if (app->bal == app->bcl &&
      app->bac == app->bcc)
    clear_box_selection(app);
  return true;
}

/******************************************************************************
 * Commands
 ******************************************************************************/
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
      if (!doc_save(&app->doc, app->doc.path))
      {
        MessageBoxW(app->hwnd, L"Could not save current file before DO command.", L"text", MB_ICONERROR | MB_OK);
        return false;
      }
      update_title(app);
    }
    if (_wsplitpath_s(app->doc.path, drive, _countof(drive), dir, _countof(dir), fname, _countof(fname), ext, _countof(ext)) != 0)
      return false;
    swprintf(cwd, _countof(cwd), L"%ls%ls", drive, dir);
    swprintf(file_name, _countof(file_name), L"%ls%ls", fname, ext);
  }
  else
  {
    if (!_wgetcwd(cwd, _countof(cwd))) return false;
    wcscpy_s(file_name, _countof(file_name), L"tasks.txt");
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
  swprintf(command, _countof(command),
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
 * Rendering
 ******************************************************************************/
typedef struct VisibleCtx
{
  u64 skip;
  WCHAR *out;
  int n;
  int max;
} VisibleCtx;

static void visible_span(const char *data, u64 len, void *user)
{
  VisibleCtx *ctx = (VisibleCtx *)user;
  if (ctx->skip >= len)
  {
    ctx->skip -= len;
    return;
  }
  data += ctx->skip;
  len -= ctx->skip;
  ctx->skip = 0;
  for (u64 i = 0; i < len && ctx->n < ctx->max; ++i)
  {
    unsigned char c = (unsigned char)data[i];
    if (c == '\r' || c == '\n') break;
    if (c == '\t')
    {
      int spaces = TAB_WIDTH - (ctx->n % TAB_WIDTH);
      while (spaces-- > 0 && ctx->n < ctx->max) ctx->out[ctx->n++] = L' ';
    }
    else if (c < 32)
      ctx->out[ctx->n++] = L' ';
    else
      ctx->out[ctx->n++] = (WCHAR)c;
  }
}

static int line_to_wide_visible(Document *doc, u64 start, u64 end, u64 fc, int max_cols, WCHAR *out)
{
  if (start >= end) return 0;
  VisibleCtx ctx = { fc, out, 0, max_cols };
  doc_read_range(doc, start, end - start, visible_span, &ctx);
  return ctx.n;
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
  WCHAR text_buf[MAX_PAINT_COLS];
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
      swprintf(num_buf, _countof(num_buf), L"%*llu", gutter_chars, (unsigned long long)(line + 1));
      TextOutW(dc, EDIT_LINE_NUMBER_LEFT_PADDING, y, num_buf, lstrlenW(num_buf));
      u64 start = app->doc.lines.starts[line];
      u64 end = app->doc.len;
      if (line + 1 < app->doc.lines.count)
      {
        end = app->doc.lines.starts[line + 1];
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
      if (n > 0) TextOutW(dc, text_left, y, text_buf, n);
      if (has_stream_selection(app))
      {
        u64 sel_start = min_u64(app->sa, app->sv);
        u64 sel_end = max_u64(app->sa, app->sv);
        u64 draw_start = max_u64(sel_start, start);
        u64 draw_end = min_u64(sel_end, end);
        if (draw_end > draw_start)
        {
          u64 col_start = draw_start - start;
          u64 col_end = draw_end - start;
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
              TextOutW(dc, text_left + vis_start *app->char_w, y, text_buf + vis_start, vis_end - vis_start);
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
              TextOutW(dc, text_left + vis_start *app->char_w, y, text_buf + vis_start, vis_end - vis_start);
              SetTextColor(dc, THEME_TEXT);
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
    doc_load_mapped(&app->doc, path);
    apply_caret_line_metrics(app);
    clear_history();
    app->tl = app->fc = app->cl = app->cc = app->co = 0;
    update_title(app);
    request_repaint(app, TRUE);
  }
}

static bool find_next(App *app, const WCHAR *query)
{
  int qbytes = WideCharToMultiByte(CP_UTF8, 0, query, -1, NULL, 0, NULL, NULL);
  if (qbytes <= 1 || app->doc.len == 0) return false;
  char *q = (char *)malloc((size_t)qbytes);
  char *hay = (char *)malloc((size_t)app->doc.len);
  if (!q || !hay)
  {
    free(q);
    free(hay);
    return false;
  }
  WideCharToMultiByte(CP_UTF8, 0, query, -1, q, qbytes, NULL, NULL);
  SearchCtx sctx = { hay, 0 };
  doc_read_range(&app->doc, 0, app->doc.len, append_span, &sctx);
  u64 qlen = (u64)qbytes - 1;
  u64 start = app->co < app->doc.len ? app->co : 0;
  const char *match = NULL;
  if (start < app->doc.len)
  {
    for (u64 i = start; i + qlen <= app->doc.len; ++i)
    {
      if (memcmp(hay + i, q, (size_t)qlen) == 0)
      {
        match = hay + i;
        break;
      }
    }
  }
  if (!match && start > 0)
  {
    for (u64 i = 0; i < start && i + qlen <= app->doc.len; ++i)
    {
      if (memcmp(hay + i, q, (size_t)qlen) == 0)
      {
        match = hay + i;
        break;
      }
    }
  }
  if (match)
  {
    u64 off = (u64)(match - hay);
    u64 end_off = min_u64(off + qlen, app->doc.len);
    app->sa = off;
    app->sv = end_off;
    app->co = end_off;
    doc_offset_to_line_col(&app->doc, app->co, &app->cl, &app->cc);
    keep_visible_and_repaint(app);
  }
  free(q);
  free(hay);
  return match != NULL;
}

static u64 count_query_matches(App *app, const WCHAR *query)
{
  int qbytes = WideCharToMultiByte(CP_UTF8, 0, query, -1, NULL, 0, NULL, NULL);
  if (qbytes <= 1 || app->doc.len == 0) return 0;
  char *q = (char *)malloc((size_t)qbytes);
  char *hay = (char *)malloc((size_t)app->doc.len);
  u64 count = 0;
  if (!q || !hay)
  {
    free(q);
    free(hay);
    return 0;
  }
  WideCharToMultiByte(CP_UTF8, 0, query, -1, q, qbytes, NULL, NULL);
  SearchCtx sctx = { hay, 0 };
  doc_read_range(&app->doc, 0, app->doc.len, append_span, &sctx);
  {
    u64 qlen = (u64)qbytes - 1;
    u64 i = 0;
    while (i + qlen <= app->doc.len)
    {
      if (memcmp(hay + i, q, (size_t)qlen) == 0)
      {
        count++;
        i += qlen;
      }
      else
        i++;
    }
  }
  free(q);
  free(hay);
  return count;
}

static void trigger_find_from_dialog(HWND dlg)
{
  App *app = &g_app;
  HWND edit = GetDlgItem(dlg, edt1);
  GetDlgItemTextW(dlg, edt1, app->fq, (int)_countof(app->fq));
  update_find_count_label(dlg);
  find_next(app, app->fq);
  if (edit) SetFocus(edit);
}

static LRESULT CALLBACK find_edit_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  HWND dlg = GetParent(hwnd);
  if (msg == WM_KEYDOWN)
  {
    if (wp == VK_RETURN)
    {
      trigger_find_from_dialog(dlg);
      return 0;
    }
    if (wp == VK_ESCAPE)
    {
      DestroyWindow(dlg);
      return 0;
    }
  }
  if (msg == WM_CHAR)
  {
    if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
  }
  if (g_find_edit_wndproc) return CallWindowProcW(g_find_edit_wndproc, hwnd, msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK find_button_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  HWND dlg = GetParent(hwnd);
  if (msg == WM_KEYDOWN && wp == VK_RETURN)
  {
    trigger_find_from_dialog(dlg);
    return 0;
  }
  if (msg == WM_CHAR && wp == VK_RETURN) return 0;
  if (g_find_button_wndproc) return CallWindowProcW(g_find_button_wndproc, hwnd, msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static void update_find_count_label(HWND hwnd)
{
  App *app = &g_app;
  HWND label = GetDlgItem(hwnd, IDC_FIND_COUNT);
  WCHAR query[256];
  WCHAR text[96];
  u64 count;
  if (!label) return;
  GetDlgItemTextW(hwnd, edt1, query, (int)_countof(query));
  count = count_query_matches(app, query);
  swprintf(text, _countof(text), L"%llu entities found", (unsigned long long)count);
  SetWindowTextW(label, text);
}

static UINT_PTR CALLBACK find_dialog_hook(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  (void)lp;
  if (msg == WM_INITDIALOG)
  {
    HWND edit;
    HWND button;
    HWND label;
    HFONT font;
    RECT rc_edit;
    RECT rc_button;
    POINT p;
    SetWindowTextW(hwnd, L"find");
    SetDlgItemTextW(hwnd, IDOK, L"Find");
    ShowWindow(GetDlgItem(hwnd, IDCANCEL), SW_HIDE);
    ShowWindow(GetDlgItem(hwnd, stc2), SW_HIDE);
    edit = GetDlgItem(hwnd, edt1);
    button = GetDlgItem(hwnd, IDOK);
    if (edit && button && GetWindowRect(edit, &rc_edit) && GetWindowRect(button, &rc_button))
    {
      p.x = rc_edit.left;
      p.y = rc_edit.bottom + 8;
      ScreenToClient(hwnd, &p);
      label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                              p.x, p.y, rc_button.right - rc_edit.left, 18,
                              hwnd, (HMENU)(INT_PTR)IDC_FIND_COUNT, GetModuleHandleW(NULL), NULL);
      if (label)
      {
        font = (HFONT)SendMessageW(edit, WM_GETFONT, 0, 0);
        if (font) SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
      }
      g_find_edit_wndproc = (WNDPROC)SetWindowLongPtrW(edit, GWLP_WNDPROC, (LONG_PTR)find_edit_wndproc);
      g_find_button_wndproc = (WNDPROC)SetWindowLongPtrW(button, GWLP_WNDPROC, (LONG_PTR)find_button_wndproc);
    }
    SetWindowTextW(GetDlgItem(hwnd, IDC_FIND_COUNT), L"0 entities found");
    return 1;
  }
  if (msg == WM_COMMAND)
  {
    WORD id = LOWORD(wp);
    if (id == IDCANCEL)
    {
      DestroyWindow(hwnd);
      return 1;
    }
  }
  if (msg == WM_KEYDOWN)
  {
    if (wp == VK_RETURN)
    {
      HWND find_button = GetDlgItem(hwnd, IDOK);
      if (find_button) SendMessageW(find_button, BM_CLICK, 0, 0);
      return 1;
    }
    if (wp == VK_ESCAPE)
    {
      DestroyWindow(hwnd);
      return 1;
    }
  }
  if (msg == WM_CHAR)
  {
    if (wp == VK_RETURN || wp == VK_ESCAPE) return 1;
  }
  if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORSTATIC)
  {
    HDC dc = (HDC)wp;
    SetTextColor(dc, THEME_TEXT);
    SetBkColor(dc, THEME_BG);
    return (UINT_PTR)g_find_bg_brush;
  }
  return 0;
}

static void open_find_dialog(App *app)
{
  if (app->find_hwnd && IsWindow(app->find_hwnd))
  {
    SetForegroundWindow(app->find_hwnd);
    return;
  }
  memset(&app->fs, 0, sizeof(app->fs));
  app->fs.lStructSize = sizeof(app->fs);
  app->fs.hwndOwner = app->hwnd;
  app->fs.lpstrFindWhat = app->fq;
  app->fs.wFindWhatLen = (WORD)_countof(app->fq);
  app->fs.Flags = FR_DOWN | FR_HIDEUPDOWN | FR_HIDEWHOLEWORD | FR_HIDEMATCHCASE | FR_ENABLEHOOK;
  app->fs.lpfnHook = find_dialog_hook;
  app->find_hwnd = FindTextW(&app->fs);
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
  u64 src_line;
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
  char *swapped_buf;
  CopyCtx copy_upper_text;
  CopyCtx copy_between_sep;
  CopyCtx copy_lower_text;
  CopyCtx copy_lower_sep;
  bool changed = false;
  u64 old_col;
  doc_discover_all_lines(&app->doc);
  line_count = app->doc.lines.count ? app->doc.lines.count : 1;
  if (line_count <= 1) return false;
  src_line = app->cl;
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

static void copy_current_line_to_clipboard(App *app)
{
  u64 start = 0;
  u64 end = 0;
  if (!current_line_range(app, &start, &end)) return;
  app->sa = start;
  app->sv = end;
  copy_selection_or_current_line_to_clipboard(app);
  app->sa = app->co;
  app->sv = app->co;
}

static void save_dialog(App *app)
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
  if (app->doc.path[0] || GetSaveFileNameW(&ofn))
  {
    doc_save(&app->doc, path);
    update_title(app);
  }
}

static void paste_clipboard(App *app)
{
  app->vsb = false;
  if (!OpenClipboard(app->hwnd)) return;
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
            apply_basic_edit(app, utf8, (u64)bytes - 1, false, false);
          else
          {
            bool line_paste = app->clm && !has_stream_selection(app);
            u64 inserted_len = (u64)bytes - 1;
            u64 old_line = app->cl;
            u64 old_col = app->cc;
            u64 pasted_newlines = 0;
            if (has_stream_selection(app)) delete_selection(app);
            u64 off = line_paste ? doc_line_start(&app->doc, app->cl) : app->co;
            app_doc_insert(app, off, utf8, inserted_len);
            app->co = off + inserted_len;
            for (int i = 0; i < bytes - 1; ++i)
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
  if (app->cc > 0)
  {
    app->cc--;
    if (app->co) app->co--;
  }
  else if (app->cl > 0)
  {
    app->cl--;
    u64 start = doc_line_start(&app->doc, app->cl);
    u64 end = doc_line_length_clamped(&app->doc, start, UINT32_MAX);
    app->cc = end - start;
    app->co = end;
  }
}

static void move_caret_right(App *app)
{
  u64 col = doc_line_col_clamped(&app->doc, app->cl, app->cc + 1);
  if (col > app->cc)
  {
    app->cc = col;
    if (app->co < app->doc.len) app->co++;
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
    u64 ll = le - ls;
    u64 seg_start = min_u64(left, ll);
    u64 seg_end = min_u64(right, ll);
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
    else
      PostMessageW(app->hwnd, WM_CLOSE, 0, 0);
    return;
  }
  if (ctrl && vk == 'O') { open_dialog(app); return; }
  if (ctrl && vk == 'S') { save_dialog(app); return; }
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
  if (ctrl && shift && vk == 'D') { run_codex_do_command(app); return; }
  if (ctrl && (vk == VK_OEM_MINUS || vk == VK_SUBTRACT)) { apply_font_size(app, app->font_size - 2); return; }
  if (ctrl && (vk == VK_OEM_PLUS || vk == VK_ADD)) { apply_font_size(app, app->font_size + 2); return; }
  if (ctrl && (vk == '0' || vk == VK_NUMPAD0)) { apply_font_size(app, FONT_SIZE_DEFAULT); return; }
  if (ctrl && vk == 'V') { paste_clipboard(app); return; }
  if (ctrl && vk == 'F')
  {
    if (seed_find_query_from_selection(app))
      find_next(app, app->fq);
    else
      open_find_dialog(app);
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
    if (app->cl > 0)
      set_caret_line_col(app, app->cl - 1, app->cc);
    else
      set_caret_line_col(app, 0, 0);
    moved = true;
    break;
  case VK_DOWN:
    doc_discover_for_view(&app->doc, app->cl, 2);
    if (app->cl + 1 < app->doc.lines.count)
      set_caret_line_col(app, app->cl + 1, app->cc);
    else
    {
      u64 start = doc_line_start(&app->doc, app->cl);
      u64 end = doc_line_length_clamped(&app->doc, start, UINT32_MAX);
      app->cc = end - start;
      app->co = end;
    }
    moved = true;
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
    set_caret_line_col(app, ctrl ? 0 : app->cl, 0);
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
    app->cc = end - start;
    app->co = end;
    moved = true;
  } break;
  case VK_BACK:
  {
    if (has_box_selection(app))
    {
      if (apply_basic_edit(app, NULL, 0, true, false)) break;
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
      u64 del_start = off - 1;
      u64 del = 1;
      if (app->cc == 0 && app->cl > 0)
      {
        u64 prev_start = doc_line_start(&app->doc, app->cl - 1);
        u64 prev_end = doc_line_length_clamped(&app->doc, prev_start, UINT32_MAX);
        del_start = prev_end;
        del = off - prev_end;
        app->cl--;
        app->cc = prev_end - prev_start;
        app->co = del_start;
      }
      else
      {
        app->cc--;
        app->co--;
      }
      app_doc_delete(app, del_start, del);
      update_title(app);
    }
  } break;
  case VK_DELETE:
  {
    if (has_box_selection(app))
    {
      if (apply_basic_edit(app, NULL, 0, false, true)) break;
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
      u64 del = 1;
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
  char bytes[8];
  int n = 0;
  if (ch == L'\r')
  {
    bytes[0] = '\n';
    n = 1;
  }
  else if (ch >= 32 && ch != 127)
  {
    WCHAR w[2] = {(WCHAR)ch, 0};
    n = WideCharToMultiByte(CP_UTF8, 0, w, 1, bytes, (int)sizeof(bytes), NULL, NULL);
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
        app->cc += (u64)n;
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

/******************************************************************************
 * Window Procedure
 ******************************************************************************/
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  App *app = &g_app;
  if (app->find_msg && msg == app->find_msg)
  {
    if (!app->find_hwnd || !IsWindow(app->find_hwnd)) return 0;
    FINDREPLACEW *fr = (FINDREPLACEW *)lp;
    if (!fr) return 0;
    if (fr->Flags & FR_DIALOGTERM)
    {
      app->find_hwnd = NULL;
      g_find_edit_wndproc = NULL;
      g_find_button_wndproc = NULL;
    }
    else if (fr->Flags & FR_FINDNEXT)
    {
      update_find_count_label(app->find_hwnd);
      find_next(app, app->fq);
      {
        HWND edit = GetDlgItem(app->find_hwnd, edt1);
        if (edit) SetFocus(edit);
      }
    }
    return 0;
  }
  switch (msg)
  {
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
    g_find_bg_brush = CreateSolidBrush(THEME_BG);
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
    return 0;
  }
  case WM_SIZE:
  {
    HDC dc = GetDC(hwnd);
    recalc_metrics(app, dc);
    ReleaseDC(hwnd, dc);
    update_scrollbars(app);
    position_caret(app);
    InvalidateRect(hwnd, NULL, TRUE);
    return 0;
  }
  case WM_SETFOCUS:
    app->ch = 0;
    ensure_caret_shape(app);
    position_caret(app);
    return 0;
  case WM_KILLFOCUS:
    HideCaret(hwnd);
    DestroyCaret();
    app->ch = 0;
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    HDC memdc;
    HBITMAP bmp;
    HBITMAP old_bmp;
    int w;
    int h;
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
    return 0;
  }
  case WM_KEYDOWN:
    handle_key(app, wp);
    return 0;
  case WM_SYSKEYDOWN:
    if (wp == VK_F4 && (GetKeyState(VK_MENU) & 0x8000) != 0)
      return DefWindowProcW(hwnd, msg, wp, lp);
    handle_key(app, wp);
    return 0;
  case WM_CHAR:
    handle_char(app, wp);
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
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
    {
      int z = GET_WHEEL_DELTA_WPARAM(wp);
      int steps = z / WHEEL_DELTA;
      if (steps != 0) apply_font_size(app, app->font_size + (steps * 2));
      return 0;
    }
    if (app->lcp) return 0;
    app->vsb = false;
    int z = GET_WHEEL_DELTA_WPARAM(wp);
    int lines = z / WHEEL_DELTA;
    if (lines > 0)
      app->tl = app->tl > (u64)(lines * 3) ? app->tl - (u64)(lines * 3) : 0;
    else if (lines < 0)
      app->tl += (u64)((-lines) * 3);
    update_scrollbars(app);
    app->cl = app->tl;
    set_caret_line_col(app, app->cl, app->cc);
    request_repaint(app, FALSE);
    return 0;
  }
  case WM_CLOSE:
    if (sync_path_utf8_on_change(app))
    {
      int choice = MessageBoxW(hwnd, L"save changes?",
                               L"exiting", MB_ICONQUESTION | MB_YESNOCANCEL | MB_DEFBUTTON1);
      if (choice == IDYES)
      {
        if (app->doc.path[0])
        {
          if (!doc_save(&app->doc, app->doc.path)) return 0;
          update_title(app);
        }
        else
        {
          save_dialog(app);
          if (sync_path_utf8_on_change(app)) return 0;
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
    update_scrollbars(app);
    set_caret_line_col(app, app->tl, app->cc);
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
    update_scrollbars(app);
    request_repaint(app, FALSE);
    return 0;
  }
  case WM_LBUTTONDOWN:
  {
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
      app->sa = app->co;
      app->sv = app->co;
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
    return 0;
  }
  case WM_LBUTTONDBLCLK:
  {
    int x = GET_X_LPARAM(lp);
    int y = GET_Y_LPARAM(lp);
    app->vsb = false;
    SetFocus(hwnd);
    set_caret_from_xy(app, x, y);
    select_word_at_caret(app);
    app->swdm = true;
    app->wdas = min_u64(app->sa, app->sv);
    app->wdae = max_u64(app->sa, app->sv);
    app->swm = true;
    SetCapture(hwnd);
    keep_caret_visible(app);
    request_repaint(app, FALSE);
    return 0;
  }
  case WM_MOUSEMOVE:
    if (app->swm && (wp & MK_LBUTTON))
    {
      int x = GET_X_LPARAM(lp);
      int y = GET_Y_LPARAM(lp);
      set_caret_from_xy(app, x, y);
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
        app->sa = app->co;
        app->sv = app->co;
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
      return 0;
    }
    break;
  case WM_LBUTTONUP:
    if (app->swm)
    {
      app->swm = false;
      app->sbxm = false;
      app->swdm = false;
      ReleaseCapture();
      return 0;
    }
    break;
  case WM_DROPFILES:
  {
    HDROP drop = (HDROP)wp;
    WCHAR path[MAX_PATH];
    if (DragQueryFileW(drop, 0, path, MAX_PATH))
    {
      doc_load_mapped(&app->doc, path);
      apply_caret_line_metrics(app);
      clear_history();
      app->tl = app->fc = app->cl = app->cc = app->co = 0;
      clear_stream_selection(app);
      update_title(app);
      request_repaint(app, TRUE);
    }
    DragFinish(drop);
    return 0;
  }
  case WM_TIMER:
    if (wp == LINECOUNT_TIMER_ID && app->lcp)
    {
      ULONGLONG start = GetTickCount64();
      while (!app->doc.lines.eof)
      {
        doc_discover_step(&app->doc, LINE_DISCOVERY_ASYNC_CHUNK);
        if (GetTickCount64() - start >= LINE_DISCOVERY_ASYNC_SLICE_MS) break;
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
    apply_scroll_limits_and_position(app);
    if (app->swm) ReleaseCapture();
    clear_history();
    if (g_find_bg_brush) DeleteObject(g_find_bg_brush);
    g_find_bg_brush = NULL;
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
  init_native_dark_mode();
  g_app.find_msg = RegisterWindowMessageW(FINDMSGSTRING);
  doc_set_empty(&g_app.doc);
  int argc = 0;
  WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv && argc > 1)
  {
    doc_load_mapped(&g_app.doc, argv[1]);
    g_app.lcp = should_defer_line_count(&g_app.doc);
    if (!g_app.lcp) doc_discover_all_lines(&g_app.doc);
  }
  if (argv) LocalFree(argv);
  WNDCLASSW wc;
  memset(&wc, 0, sizeof(wc));
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = inst;
  wc.lpszClassName = APP_CLASS_NAME;
  wc.hCursor = LoadCursorW(NULL, IDC_IBEAM);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  RegisterClassW(&wc);
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
