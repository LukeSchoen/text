#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* The bundled compiler's Windows headers omit winioctl.h. */
#ifndef FSCTL_SET_SPARSE
#define FSCTL_SET_SPARSE 0x000900c4
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define wWinMain text_wWinMain
#include "main.c"
#undef wWinMain

typedef struct ReadCtx {
    char *dst;
    u64 cap;
    u64 used;
} ReadCtx;

typedef struct TimedRun {
    const char *name;
    bool passed;
    double elapsed_ms;
} TimedRun;

static int g_failures = 0;

static bool create_large_fixture(wchar_t *path, size_t path_count) {
    wchar_t temp_dir[MAX_PATH];
    HANDLE file = INVALID_HANDLE_VALUE;
    LARGE_INTEGER size;

    if (!path || path_count < MAX_PATH) return false;
    path[0] = 0;
    if (!GetTempPathW((DWORD)MAX_PATH, temp_dir)) return false;
    if (!GetTempFileNameW(temp_dir, L"txt", 0, path)) return false;

    file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE) goto fail;
    /* Avoid allocating/zeroing 300 MiB when writing the boundary sentinels.
       Filesystems without sparse support still exercise the same assertions. */
    DWORD returned;
    DeviceIoControl(file, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &returned, NULL);
    size.QuadPart = 300LL * 1024LL * 1024LL;
    if (!SetFilePointerEx(file, size, NULL, FILE_BEGIN) || !SetEndOfFile(file)) goto fail;
    const u64 offsets[] = {10000, 150ULL * 1024 * 1024};
    const char *marks[] = {"LDR", "LR"};
    for (unsigned i = 0; i < 2; ++i) {
        DWORD written;
        size.QuadPart = offsets[i];
        DWORD count = (DWORD)strlen(marks[i]);
        if (!SetFilePointerEx(file, size, NULL, FILE_BEGIN) ||
            !WriteFile(file, marks[i], count, &written, NULL) || written != count) goto fail;
    }
    CloseHandle(file);
    return true;

fail:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    DeleteFileW(path);
    path[0] = 0;
    return false;
}

static void on_span(const char *data, u64 n, void *user) {
    ReadCtx *ctx = (ReadCtx *)user;
    u64 remaining = (ctx->used < ctx->cap) ? (ctx->cap - ctx->used) : 0;
    u64 to_copy = n < remaining ? n : remaining;
    if (to_copy > 0) memcpy(ctx->dst + ctx->used, data, (size_t)to_copy);
    ctx->used += to_copy;
}

static bool read_exact(Document *doc, u64 off, char *dst, u64 len) {
    ReadCtx ctx;
    if (!doc || !dst) return false;
    if (off + len > doc->len) return false;
    ctx.dst = dst;
    ctx.cap = len;
    ctx.used = 0;
    doc_read_range(doc, off, len, on_span, &ctx);
    return ctx.used == len;
}

static void fail(const char *name, const char *msg) {
    fprintf(stderr, "FAIL %s: %s\n", name, msg);
    g_failures++;
}

static bool load_fixture(Document *doc, const wchar_t *path) {
    if (!doc || !path) return false;
    memset(doc, 0, sizeof(*doc));
    if (!doc_set_empty(doc)) return false;
    if (!doc_load_mapped(doc, path)) {
        doc_clear(doc);
        return false;
    }
    return true;
}

static double now_ms(void);

static bool test_large_mapped_edits(const wchar_t *path) {
    const char *name = "large_mapped_edits";
    Document doc;
    Core saved = {0};
    char inserted[10000], actual[10002];
    bool ok = false;
    if (!load_fixture(&doc, path)) { fail(name, "load failed"); return false; }
    u64 original_len = doc.len, off = original_len / 2 - 1;
    /* Sentinels are in ORIGINAL backing: exercise mapped deletion and insertion. */
    core_clone(&saved, &doc.rope);
    if (!doc_delete_range(&doc, 10001, 1) ||
        !read_exact(&doc, 10000, actual, 2) || memcmp(actual, "LR", 2)) goto done;
    for (size_t i = 0; i < sizeof(inserted); ++i) inserted[i] = 'A' + i % 26;
    if (!doc_insert_bytes(&doc, off + 1, inserted, sizeof(inserted), NULL) ||
        doc.len != original_len - 1 + sizeof(inserted) ||
        !read_exact(&doc, off, actual, sizeof(actual)) ||
        actual[0] != 'L' || actual[10001] != 'R' ||
        memcmp(actual + 1, inserted, sizeof(inserted))) goto done;
    if (!core_read(&saved, 10000, 3, actual) || memcmp(actual, "LDR", 3)) goto done;
    /* Delete half a single mapped buffer: survivors must still point directly
       into that buffer. Resolving the deletion caret must not read the tail. */
    core_clone(&doc.rope, &saved);
    doc.len = original_len;
    doc_reset_lines(&doc);
    CoreRun original, left, right;
    u64 line, col, removed = original_len / 2;
    core_run(&saved, 0, &original);
    double started = now_ms();
    if (!doc_delete_range(&doc, 10001, removed)) goto done;
    double delete_ms = now_ms() - started;
    if (core_pieces(&doc.rope) != 2 ||
        !core_run(&doc.rope, 0, &left) || left.data != original.data || left.len != 10001 ||
        !core_run(&doc.rope, 10001, &right) || right.data != original.data + 10001 + removed ||
        right.len != original_len - removed - 10001) goto done;
    started = now_ms();
    doc_offset_to_line_col(&doc, 10001, &line, &col);
    printf("DELETE 150 MiB: %.6f ms; resolve caret: %.6f ms; indexed %llu bytes\n",
           delete_ms, now_ms() - started, (unsigned long long)doc.lines.scanned_to);
    if (line != 0 || col != 10001 || doc.lines.scanned_to > 10001) goto done;
    /* Shift cached blocks, discard them with a newline delete, then reuse them.
       Stale lazy offsets must never leak into the rebuilt line index. */
    doc_clear(&doc);
    memset(inserted, '\n', 4098);
    if (!doc_insert_bytes(&doc, 0, inserted, 4098, NULL)) goto done;
    doc_discover_all_lines(&doc);
    if (!doc_insert_bytes(&doc, 1, "X", 1, NULL) ||
        !doc_delete_range(&doc, 3, 2)) goto done;
    doc_discover_all_lines(&doc);
    if (!doc.lines.eof || doc.lines.count != 4097) goto done;
    for (u64 i = 0; i < doc.lines.count; ++i)
        if (doc_line_start(&doc, i) != (i < 2 ? i : i + 1)) goto done;
    ok = true;
done:
    core_dispose(&saved);
    doc_clear(&doc);
    if (!ok) fail(name, "edit boundaries, shared snapshot or length mismatch");
    return ok;
}

static double now_ms(void) {
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static TimedRun run_timed(const char *name, bool (*fn)(const wchar_t *), const wchar_t *fixture) {
    TimedRun r;
    double start = now_ms();
    r.name = name;
    r.passed = fn(fixture);
    r.elapsed_ms = now_ms() - start;
    return r;
}

static bool test_utf8(const wchar_t *unused)
{
  (void)unused;
  const char input[] = "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\tZ\n";
  const uint32_t expected[] = {'A', 0xe9, 0x20ac, 0x1f600, ' ', ' ', 'Z'};
  const u64 boundaries[] = {0, 1, 3, 6, 10, 11, 12, 13};
  const unsigned char invalid[][4] = {
    {0xc0, 0xaf}, {0xe0, 0x80, 0x80}, {0xed, 0xa0, 0x80},
    {0xf4, 0x90, 0x80, 0x80}, {0xf5, 0x80, 0x80, 0x80}, {0x80}, {0xe2, 'X'}
  };
  Document doc;
  uint32_t cells[16], cp;
  int failed = 0;
  memset(&doc, 0, sizeof(doc));
  if (!doc_set_empty(&doc)) return false;
  /* Leave physical gaps in ADD backing so every UTF-8 byte crosses a piece. */
  for (size_t i = 0; i < sizeof(input) - 1; ++i)
  {
    char pair[2] = {'!', input[i]};
    u64 off = doc.len;
    if (!doc_insert_bytes(&doc, off, pair, 2, NULL) || !doc_delete_range(&doc, off, 1)) { doc_clear(&doc); return false; }
  }
  if (core_pieces(&doc.rope) != sizeof(input) - 1) failed++;
  for (unsigned i = 0; i + 1 < _countof(boundaries); ++i)
  {
    if (doc_next_character(&doc, boundaries[i]) != boundaries[i + 1]) failed++;
    if (doc_previous_character(&doc, boundaries[i + 1]) != boundaries[i]) failed++;
  }
  if (doc_line_visual_width(&doc, 0, doc.len) != _countof(expected)) failed++;
  if (line_to_wide_visible(&doc, 0, doc.len, 0, 16, cells) != _countof(expected) ||
      memcmp(cells, expected, sizeof(expected))) failed++;
  for (unsigned col = 0; col < _countof(expected); ++col)
  {
    if (line_to_wide_visible(&doc, 0, doc.len, col, 1, cells) != 1 || cells[0] != expected[col]) failed++;
  }
  for (unsigned col = 0; col <= 4; ++col)
    if (doc_line_byte_col_from_visual_col(&doc, 0, col) != boundaries[col]) failed++;
  for (unsigned i = 0; i < _countof(boundaries); ++i)
  {
    u64 line, col;
    const u64 columns[] = {0, 1, 2, 3, 4, 6, 7, 0};
    doc_offset_to_line_col(&doc, boundaries[i], &line, &col);
    if (line != (i == 7 ? 1 : 0) || col != columns[i] ||
        doc.lines.scanned_to > boundaries[i]) failed++;
  }
  for (unsigned i = 0; i < _countof(invalid); ++i)
    if (utf8_decode(invalid[i], 4, &cp) != 1 || cp != 0xfffd) failed++;
  if (utf8_decode((const unsigned char *)"\xf0\x9f\x98", 3, &cp) != 1 || cp != 0xfffd) failed++;
  doc_clear(&doc);
  doc_discover_all_lines(&doc);
  if (!doc.lines.eof || doc.lines.count != 1) failed++;
  doc_dispose_contents(&doc);
  if (failed) fail("utf8", "decoding or viewport assertion failed");
  return failed == 0;
}

int wmain(int argc, wchar_t **argv) {
    wchar_t fixture[MAX_PATH];
    bool pedantic = argc == 2 && wcscmp(argv[1], L"--pedantic") == 0;
    if (argc > 1 && !pedantic) {
        fprintf(stderr, "usage: text_tests.exe [--pedantic]\n");
        return 2;
    }
    if (!pedantic) {
        TimedRun r = run_timed("utf8_piece_boundaries_and_viewport", test_utf8, NULL);
        printf("%s %s (%.3f ms)\n", r.passed ? "PASS" : "FAIL", r.name, r.elapsed_ms);
        return r.passed ? 0 : 1;
    }
    double started = now_ms();
    if (!create_large_fixture(fixture, _countof(fixture))) {
        printf("FAIL large_mapped_edits: fixture setup (%.3f ms)\n", now_ms() - started);
        return 1;
    }
    printf("SETUP large_fixture (%.3f ms)\n", now_ms() - started);
    TimedRun r = run_timed("large_mapped_edits", test_large_mapped_edits, fixture);
    printf("%s %s (%.3f ms)\n", r.passed ? "PASS" : "FAIL", r.name, r.elapsed_ms);
    if (!DeleteFileW(fixture)) return 1;
    return r.passed ? 0 : 1;
}
