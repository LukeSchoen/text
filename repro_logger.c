#define WIN32_LEAN_AND_MEAN
#include "repro_logger.h"

#ifdef REPRO_BUILD
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

enum {
  REPRO_FLAG_WHEEL = 1 << 0,
  REPRO_FLAG_VSCROLL = 1 << 1,
  REPRO_FLAG_SIZE = 1 << 2,
  REPRO_FLAG_SCROLLBAR = 1 << 3,
  REPRO_FLAG_INPUT = 1 << 4,
  REPRO_FLAG_EDIT = 1 << 5,
  REPRO_FLAG_SEMANTIC = 1 << 6,
};

static FILE *g_repro_file;
static unsigned g_repro_flags;
static unsigned long long g_repro_seq;
static char g_repro_dir[MAX_PATH];
static unsigned g_wheel_stall_streak;
static unsigned g_wheel_stall_total;
static unsigned g_wheel_stall_max_streak;
static bool g_input_active;
static bool g_input_has_edits;
static bool g_input_overflow;
static char g_input_kind[24];
static unsigned long g_input_a;
static unsigned long g_input_b;
static ReproUiState g_input_before;
static char g_input_edits[512];
static size_t g_input_edits_len;

static void repro_write_line(const char *fmt, ...)
{
  va_list args;
  if (!g_repro_file) return;
  fprintf(g_repro_file, "%06llu ", g_repro_seq++);
  va_start(args, fmt);
  vfprintf(g_repro_file, fmt, args);
  va_end(args);
  fputc('\n', g_repro_file);
  fflush(g_repro_file);
}

static void trim_token(char *s)
{
  char *start = s;
  char *end;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') ++start;
  if (start != s) memmove(s, start, strlen(start) + 1);
  end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) --end;
  *end = 0;
}

static bool states_equal(const ReproUiState *a, const ReproUiState *b)
{
  return a->co == b->co &&
         a->sa == b->sa &&
         a->sv == b->sv &&
         a->tl == b->tl &&
         a->fc == b->fc &&
         a->doc_len == b->doc_len &&
         a->cl == b->cl &&
         a->cc == b->cc &&
         a->bal == b->bal &&
         a->bac == b->bac &&
         a->bcl == b->bcl &&
         a->bcc == b->bcc &&
         a->bdc == b->bdc &&
         a->rows == b->rows &&
         a->cols == b->cols &&
         a->box_sel == b->box_sel &&
         a->stream_sel == b->stream_sel;
}

static void append_text(char *dst, size_t dst_cap, size_t *io_len, const char *src)
{
  size_t avail;
  size_t need;
  if (!dst || !io_len || !src || dst_cap == 0) return;
  if (*io_len >= dst_cap - 1) return;
  avail = dst_cap - 1 - *io_len;
  need = strlen(src);
  if (need > avail) need = avail;
  memcpy(dst + *io_len, src, need);
  *io_len += need;
  dst[*io_len] = 0;
}

static void append_preview(char *dst, size_t dst_cap, size_t *io_len, const char *src, unsigned long long len)
{
  unsigned long long i;
  char buf[8];
  if (!dst || !io_len || !src || dst_cap == 0) return;
  append_text(dst, dst_cap, io_len, "\"");
  for (i = 0; i < len && i < 24; ++i)
  {
    unsigned char c = (unsigned char)src[i];
    if (*io_len >= dst_cap - 1) break;
    if (c == '\\') append_text(dst, dst_cap, io_len, "\\\\");
    else if (c == '\"') append_text(dst, dst_cap, io_len, "\\\"");
    else if (c == '\n') append_text(dst, dst_cap, io_len, "\\n");
    else if (c == '\r') append_text(dst, dst_cap, io_len, "\\r");
    else if (c == '\t') append_text(dst, dst_cap, io_len, "\\t");
    else if (c >= 32 && c < 127)
    {
      buf[0] = (char)c;
      buf[1] = 0;
      append_text(dst, dst_cap, io_len, buf);
    }
    else
    {
      snprintf(buf, sizeof(buf), "\\x%02X", (unsigned)c);
      append_text(dst, dst_cap, io_len, buf);
    }
  }
  if (len > 24) append_text(dst, dst_cap, io_len, "...");
  append_text(dst, dst_cap, io_len, "\"");
}

static void append_wide_preview(char *dst, size_t dst_cap, size_t *io_len, const WCHAR *src)
{
  char utf8[MAX_PATH * 3];
  int n;
  if (!src) src = L"";
  n = WideCharToMultiByte(CP_UTF8, 0, src, -1, utf8, (int)sizeof(utf8), NULL, NULL);
  if (n <= 0)
  {
    append_preview(dst, dst_cap, io_len, "<wide-conversion-failed>", 24);
    return;
  }
  append_preview(dst, dst_cap, io_len, utf8, (unsigned long long)(n - 1));
}

static void append_edit(const char *op, unsigned long long off, unsigned long long len, const char *text, unsigned long long text_len)
{
  char part[160];
  size_t part_len = 0;
  if (!g_input_active || (g_repro_flags & REPRO_FLAG_EDIT) == 0) return;
  snprintf(part, sizeof(part), "%s%s@%llu+%llu=", g_input_has_edits ? ";" : "", op, off, len);
  part_len = strlen(part);
  append_preview(part, sizeof(part), &part_len, text, text_len);
  if (g_input_edits_len + part_len >= sizeof(g_input_edits) - 1)
  {
    g_input_overflow = true;
    return;
  }
  append_text(g_input_edits, sizeof(g_input_edits), &g_input_edits_len, part);
  g_input_has_edits = true;
}

static void parse_flags_file(const char *flags_path)
{
  FILE *f;
  char line[128];
  g_repro_flags = 0;
  f = fopen(flags_path, "rb");
  if (!f)
  {
    g_repro_flags = REPRO_FLAG_WHEEL | REPRO_FLAG_VSCROLL | REPRO_FLAG_SIZE | REPRO_FLAG_INPUT | REPRO_FLAG_EDIT | REPRO_FLAG_SEMANTIC;
    return;
  }
  while (fgets(line, (int)sizeof(line), f))
  {
    trim_token(line);
    if (line[0] == 0 || line[0] == '#') continue;
    if (strcmp(line, "all") == 0) g_repro_flags = REPRO_FLAG_WHEEL | REPRO_FLAG_VSCROLL | REPRO_FLAG_SIZE | REPRO_FLAG_SCROLLBAR | REPRO_FLAG_INPUT | REPRO_FLAG_EDIT | REPRO_FLAG_SEMANTIC;
    else if (strcmp(line, "wheel") == 0) g_repro_flags |= REPRO_FLAG_WHEEL;
    else if (strcmp(line, "vscroll") == 0) g_repro_flags |= REPRO_FLAG_VSCROLL;
    else if (strcmp(line, "size") == 0) g_repro_flags |= REPRO_FLAG_SIZE;
    else if (strcmp(line, "scrollbar") == 0) g_repro_flags |= REPRO_FLAG_SCROLLBAR;
    else if (strcmp(line, "input") == 0) g_repro_flags |= REPRO_FLAG_INPUT;
    else if (strcmp(line, "edit") == 0) g_repro_flags |= REPRO_FLAG_EDIT;
    else if (strcmp(line, "semantic") == 0) g_repro_flags |= REPRO_FLAG_SEMANTIC;
  }
  fclose(f);
  if (g_repro_flags == 0) g_repro_flags = REPRO_FLAG_WHEEL;
}

static bool save_window_bmp(HWND hwnd, const char *path)
{
  RECT rc;
  int w, h;
  HDC screen_dc = NULL;
  HDC win_dc = NULL;
  HDC mem_dc = NULL;
  HBITMAP bmp = NULL;
  HBITMAP old_bmp = NULL;
  BITMAPINFO bi;
  BITMAPFILEHEADER bfh;
  FILE *f = NULL;
  void *pixels = NULL;
  DWORD stride;
  DWORD image_size;
  BOOL captured = FALSE;
  HMODULE user32 = NULL;
  typedef BOOL (WINAPI *PrintWindowFn)(HWND, HDC, UINT);
  PrintWindowFn print_window = NULL;
  bool ok = false;
  if (!hwnd || !path || !GetWindowRect(hwnd, &rc)) return false;
  w = rc.right - rc.left;
  h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0) return false;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  stride = (DWORD)w * 4;
  image_size = stride * (DWORD)h;
  RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
  GdiFlush();
  Sleep(50);
  screen_dc = GetDC(NULL);
  if (!screen_dc) goto done;
  mem_dc = CreateCompatibleDC(screen_dc);
  if (!mem_dc) goto done;
  bmp = CreateDIBSection(screen_dc, &bi, DIB_RGB_COLORS, &pixels, NULL, 0);
  if (!bmp || !pixels) goto done;
  old_bmp = (HBITMAP)SelectObject(mem_dc, bmp);
  user32 = GetModuleHandleW(L"user32.dll");
  if (user32)
    print_window = (PrintWindowFn)GetProcAddress(user32, "PrintWindow");
  if (print_window)
    captured = print_window(hwnd, mem_dc, 0x00000002);
  if (captured)
  {
    DWORD *px = (DWORD *)pixels;
    DWORD sample_count = (DWORD)w * (DWORD)h;
    DWORD non_black = 0;
    DWORD i;
    for (i = 0; i < sample_count; ++i)
    {
      if ((px[i] & 0x00FFFFFFu) != 0)
      {
        ++non_black;
        if (non_black > 32) break;
      }
    }
    if (non_black <= 32) captured = FALSE;
  }
  if (!captured)
  {
    win_dc = GetWindowDC(hwnd);
    if (!win_dc) goto done;
    captured = BitBlt(mem_dc, 0, 0, w, h, win_dc, 0, 0, SRCCOPY);
  }
  if (!captured) goto done;
  f = fopen(path, "wb");
  if (!f) goto done;
  memset(&bfh, 0, sizeof(bfh));
  bfh.bfType = 0x4D42;
  bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  bfh.bfSize = bfh.bfOffBits + image_size;
  if (fwrite(&bfh, sizeof(bfh), 1, f) != 1) goto done;
  if (fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f) != 1) goto done;
  if (fwrite(pixels, image_size, 1, f) != 1) goto done;
  ok = true;
done:
  if (f) fclose(f);
  if (old_bmp && mem_dc) SelectObject(mem_dc, old_bmp);
  if (bmp) DeleteObject(bmp);
  if (mem_dc) DeleteDC(mem_dc);
  if (win_dc) ReleaseDC(hwnd, win_dc);
  if (screen_dc) ReleaseDC(NULL, screen_dc);
  return ok;
}

void repro_init(void)
{
  char exe_path[MAX_PATH];
  char *slash;
  char log_path[MAX_PATH];
  char flags_path[MAX_PATH];
  DWORD len = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return;
  slash = strrchr(exe_path, '\\');
  if (!slash) return;
  *slash = 0;
  snprintf(g_repro_dir, sizeof(g_repro_dir), "%s", exe_path);
  snprintf(log_path, sizeof(log_path), "%s\\log.txt", exe_path);
  snprintf(flags_path, sizeof(flags_path), "%s\\repro_flags.txt", exe_path);
  parse_flags_file(flags_path);
  g_repro_file = fopen(log_path, "wb");
  if (!g_repro_file) return;
  g_repro_seq = 0;
  g_wheel_stall_streak = 0;
  g_wheel_stall_total = 0;
  g_wheel_stall_max_streak = 0;
  g_input_active = false;
  repro_write_line("repro start flags=0x%X", g_repro_flags);
  if ((g_repro_flags & REPRO_FLAG_WHEEL) != 0)
    repro_write_line("fields: seq event ... ; wheel has stuck=<0|1> streak=<n> totalStuck=<n>");
}

void repro_shutdown(void)
{
  if (!g_repro_file) return;
  if ((g_repro_flags & REPRO_FLAG_WHEEL) != 0)
    repro_write_line("summary wheelStuckTotal=%u wheelStuckMaxStreak=%u", g_wheel_stall_total, g_wheel_stall_max_streak);
  repro_write_line("repro shutdown");
  fclose(g_repro_file);
  g_repro_file = NULL;
}

void repro_log_size(unsigned long size_type, int client_w, int client_h, int rows, int cols, unsigned long long tl, bool lcp)
{
  if ((g_repro_flags & REPRO_FLAG_SIZE) == 0) return;
  repro_write_line("size type=%lu w=%d h=%d rows=%d cols=%d tl=%llu lcp=%d", size_type, client_w, client_h, rows, cols, tl, lcp ? 1 : 0);
}

void repro_log_wheel(int delta, int remainder, int notches, int lines_per_notch,
                     unsigned long long tl_before, unsigned long long tl_after,
                     int sb_pos, int sb_max, bool lcp, int rows)
{
  bool stuck;
  unsigned stuck_streak;
  unsigned stuck_total;
  if ((g_repro_flags & REPRO_FLAG_WHEEL) == 0) return;
  stuck = (delta != 0 && notches != 0 && tl_before == tl_after);
  if (stuck)
  {
    ++g_wheel_stall_streak;
    ++g_wheel_stall_total;
    if (g_wheel_stall_streak > g_wheel_stall_max_streak) g_wheel_stall_max_streak = g_wheel_stall_streak;
  }
  else
  {
    g_wheel_stall_streak = 0;
  }
  stuck_streak = g_wheel_stall_streak;
  stuck_total = g_wheel_stall_total;
  repro_write_line("wheel d=%d rem=%d notches=%d lpn=%d tl=%llu->%llu sb=%d/%d lcp=%d rows=%d stuck=%d streak=%u totalStuck=%u",
                   delta, remainder, notches, lines_per_notch, tl_before, tl_after, sb_pos, sb_max, lcp ? 1 : 0, rows,
                   stuck ? 1 : 0, stuck_streak, stuck_total);
}

void repro_log_vscroll(int code, int npos, int ntrack, int sb_max,
                       unsigned long long tl_before, unsigned long long tl_after, bool vsb)
{
  if ((g_repro_flags & REPRO_FLAG_VSCROLL) == 0) return;
  repro_write_line("vscroll code=%d nPos=%d nTrack=%d sbMax=%d tl=%llu->%llu vsb=%d",
                   code, npos, ntrack, sb_max, tl_before, tl_after, vsb ? 1 : 0);
}

void repro_log_scrollbar(unsigned long long tl_before, unsigned long long tl_after,
                         unsigned long long known_lines, unsigned long long page,
                         unsigned long long max_top, bool eof, bool vsb)
{
  if ((g_repro_flags & REPRO_FLAG_SCROLLBAR) == 0) return;
  repro_write_line("scrollbar tl=%llu->%llu known=%llu page=%llu maxTop=%llu eof=%d vsb=%d",
                   tl_before, tl_after, known_lines, page, max_top, eof ? 1 : 0, vsb ? 1 : 0);
}

void repro_begin_input_event(const char *kind, unsigned long a, unsigned long b, const ReproUiState *before)
{
  if ((g_repro_flags & REPRO_FLAG_INPUT) == 0 || !before) return;
  g_input_active = true;
  g_input_has_edits = false;
  g_input_overflow = false;
  g_input_a = a;
  g_input_b = b;
  g_input_before = *before;
  g_input_edits_len = 0;
  g_input_edits[0] = 0;
  snprintf(g_input_kind, sizeof(g_input_kind), "%s", kind ? kind : "input");
}

void repro_note_insert(unsigned long long off, unsigned long long len, const char *text, unsigned long long text_len)
{
  append_edit("ins", off, len, text, text_len < len ? text_len : len);
}

void repro_note_delete(unsigned long long off, unsigned long long len, const char *text, unsigned long long text_len)
{
  append_edit("del", off, len, text, text_len < len ? text_len : len);
}

void repro_end_input_event(const ReproUiState *after)
{
  if (!g_input_active || !after) return;
  if ((g_repro_flags & REPRO_FLAG_INPUT) != 0 &&
      (!states_equal(&g_input_before, after) || g_input_has_edits))
  {
    repro_write_line(
      "%s a=%lu b=%lu co=%llu/%llu sa=%llu/%llu sv=%llu/%llu cl=%llu/%llu cc=%llu/%llu tl=%llu/%llu fc=%llu/%llu len=%llu/%llu stream=%d/%d box=%d/%d boxrc=%llu,%llu->%llu,%llu/%llu,%llu->%llu,%llu rows=%d cols=%d%s%s%s",
      g_input_kind,
      g_input_a, g_input_b,
      g_input_before.co, after->co,
      g_input_before.sa, after->sa,
      g_input_before.sv, after->sv,
      g_input_before.cl, after->cl,
      g_input_before.cc, after->cc,
      g_input_before.tl, after->tl,
      g_input_before.fc, after->fc,
      g_input_before.doc_len, after->doc_len,
      g_input_before.stream_sel ? 1 : 0, after->stream_sel ? 1 : 0,
      g_input_before.box_sel ? 1 : 0, after->box_sel ? 1 : 0,
      g_input_before.bal, g_input_before.bac, g_input_before.bcl, g_input_before.bcc,
      after->bal, after->bac, after->bcl, after->bcc,
      after->rows, after->cols,
      g_input_has_edits ? " edits=" : "",
      g_input_has_edits ? g_input_edits : "",
      g_input_overflow ? ";..." : "");
  }
  g_input_active = false;
}

void repro_log_loaded_file(const WCHAR *path, unsigned long long len, bool ok)
{
  char line[MAX_PATH * 3 + 96];
  size_t line_len = 0;
  if ((g_repro_flags & REPRO_FLAG_SEMANTIC) == 0) return;
  snprintf(line, sizeof(line), "file_load ok=%d len=%llu path=", ok ? 1 : 0, len);
  line_len = strlen(line);
  append_wide_preview(line, sizeof(line), &line_len, path);
  repro_write_line("%s", line);
}

void repro_log_drag_selection(const char *mode, const ReproUiState *state)
{
  if ((g_repro_flags & REPRO_FLAG_SEMANTIC) == 0 || !state) return;
  if (mode && strcmp(mode, "box") == 0)
  {
    repro_write_line("drag_select mode=box co=%llu line=%llu col=%llu boxrc=%llu,%llu->%llu,%llu rows=%d cols=%d",
                     state->co, state->cl, state->cc, state->bal, state->bac, state->bcl, state->bcc,
                     state->rows, state->cols);
  }
  else
  {
    unsigned long long start = state->sa < state->sv ? state->sa : state->sv;
    unsigned long long end = state->sa < state->sv ? state->sv : state->sa;
    repro_write_line("drag_select mode=stream range=%llu..%llu len=%llu co=%llu line=%llu col=%llu rows=%d cols=%d",
                     start, end, end - start, state->co, state->cl, state->cc, state->rows, state->cols);
  }
}

void repro_capture_close_screenshot(HWND hwnd)
{
  char path[MAX_PATH];
  bool ok;
  if ((g_repro_flags & REPRO_FLAG_SEMANTIC) == 0) return;
  snprintf(path, sizeof(path), "%s\\close_screenshot.bmp", g_repro_dir[0] ? g_repro_dir : ".");
  ok = save_window_bmp(hwnd, path);
  repro_write_line("close_screenshot ok=%d path=\"close_screenshot.bmp\"", ok ? 1 : 0);
}

#endif
