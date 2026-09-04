#ifndef REPRO_LOGGER_H
#define REPRO_LOGGER_H

#include <stdbool.h>
#include <windows.h>

#ifdef REPRO_BUILD
typedef struct ReproUiState
{
  unsigned long long co, sa, sv, tl, fc, doc_len;
  unsigned long long cl, cc;
  unsigned long long bal, bac, bcl, bcc, bdc;
  int rows, cols;
  bool box_sel;
  bool stream_sel;
} ReproUiState;

void repro_init(void);
void repro_shutdown(void);
void repro_log_size(unsigned long size_type, int client_w, int client_h, int rows, int cols, unsigned long long tl, bool lcp);
void repro_log_wheel(int delta, int remainder, int notches, int lines_per_notch,
                     unsigned long long tl_before, unsigned long long tl_after,
                     int sb_pos, int sb_max, bool lcp, int rows);
void repro_log_vscroll(int code, int npos, int ntrack, int sb_max,
                       unsigned long long tl_before, unsigned long long tl_after, bool vsb);
void repro_log_scrollbar(unsigned long long tl_before, unsigned long long tl_after,
                         unsigned long long known_lines, unsigned long long page,
                         unsigned long long max_top, bool eof, bool vsb);
void repro_begin_input_event(const char *kind, unsigned long a, unsigned long b, const ReproUiState *before);
void repro_note_insert(unsigned long long off, unsigned long long len, const char *text, unsigned long long text_len);
void repro_note_delete(unsigned long long off, unsigned long long len, const char *text, unsigned long long text_len);
void repro_end_input_event(const ReproUiState *after);
void repro_log_loaded_file(const WCHAR *path, unsigned long long len, bool ok);
void repro_log_drag_selection(const char *mode, const ReproUiState *state);
void repro_capture_close_screenshot(HWND hwnd);
#else
typedef struct ReproUiState
{
  unsigned long long co, sa, sv, tl, fc, doc_len;
  unsigned long long cl, cc;
  unsigned long long bal, bac, bcl, bcc, bdc;
  int rows, cols;
  bool box_sel;
  bool stream_sel;
} ReproUiState;

static inline void repro_init(void) { }
static inline void repro_shutdown(void) { }
static inline void repro_log_size(unsigned long size_type, int client_w, int client_h, int rows, int cols, unsigned long long tl, bool lcp)
{ (void)size_type; (void)client_w; (void)client_h; (void)rows; (void)cols; (void)tl; (void)lcp; }
static inline void repro_log_wheel(int delta, int remainder, int notches, int lines_per_notch,
                                   unsigned long long tl_before, unsigned long long tl_after,
                                   int sb_pos, int sb_max, bool lcp, int rows)
{ (void)delta; (void)remainder; (void)notches; (void)lines_per_notch; (void)tl_before; (void)tl_after; (void)sb_pos; (void)sb_max; (void)lcp; (void)rows; }
static inline void repro_log_vscroll(int code, int npos, int ntrack, int sb_max,
                                     unsigned long long tl_before, unsigned long long tl_after, bool vsb)
{ (void)code; (void)npos; (void)ntrack; (void)sb_max; (void)tl_before; (void)tl_after; (void)vsb; }
static inline void repro_log_scrollbar(unsigned long long tl_before, unsigned long long tl_after,
                                       unsigned long long known_lines, unsigned long long page,
                                       unsigned long long max_top, bool eof, bool vsb)
{ (void)tl_before; (void)tl_after; (void)known_lines; (void)page; (void)max_top; (void)eof; (void)vsb; }
static inline void repro_begin_input_event(const char *kind, unsigned long a, unsigned long b, const ReproUiState *before)
{ (void)kind; (void)a; (void)b; (void)before; }
static inline void repro_note_insert(unsigned long long off, unsigned long long len, const char *text, unsigned long long text_len)
{ (void)off; (void)len; (void)text; (void)text_len; }
static inline void repro_note_delete(unsigned long long off, unsigned long long len, const char *text, unsigned long long text_len)
{ (void)off; (void)len; (void)text; (void)text_len; }
static inline void repro_end_input_event(const ReproUiState *after)
{ (void)after; }
static inline void repro_log_loaded_file(const WCHAR *path, unsigned long long len, bool ok)
{ (void)path; (void)len; (void)ok; }
static inline void repro_log_drag_selection(const char *mode, const ReproUiState *state)
{ (void)mode; (void)state; }
static inline void repro_capture_close_screenshot(HWND hwnd)
{ (void)hwnd; }
#endif

#endif
