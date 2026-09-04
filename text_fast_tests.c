#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
    size.QuadPart = 300LL * 1024LL * 1024LL;
    if (!SetFilePointerEx(file, size, NULL, FILE_BEGIN) || !SetEndOfFile(file)) goto fail;
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

static bool read_byte(Document *doc, u64 off, unsigned char *out) {
    char c = 0;
    if (!out) return false;
    if (!read_exact(doc, off, &c, 1)) return false;
    *out = (unsigned char)c;
    return true;
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

static bool test_delete_one_char_at_pos_10000(const wchar_t *path) {
    const char *name = "delete_one_char_at_pos_10000";
    Document doc;
    u64 pos = 10000;
    u64 before_len;
    unsigned char before = 0;
    unsigned char after_expected = 0;
    unsigned char after_actual = 0;

    if (!load_fixture(&doc, path)) {
        fail(name, "could not load test fixture");
        return false;
    }

    if (doc.len <= pos + 1) {
        fail(name, "fixture too small for delete test");
        doc_clear(&doc);
        return false;
    }

    before_len = doc.len;
    if (!read_byte(&doc, pos, &before) || !read_byte(&doc, pos + 1, &after_expected)) {
        fail(name, "failed to read bytes around delete position");
        doc_clear(&doc);
        return false;
    }

    if (!doc_delete_range(&doc, pos, 1)) {
        fail(name, "doc_delete_range failed");
        doc_clear(&doc);
        return false;
    }

    if (doc.len != before_len - 1) {
        fail(name, "length did not shrink by one");
        doc_clear(&doc);
        return false;
    }

    if (!read_byte(&doc, pos, &after_actual)) {
        fail(name, "failed to read byte after delete");
        doc_clear(&doc);
        return false;
    }

    if (after_actual != after_expected) {
        char msg[160];
        snprintf(msg, sizeof(msg), "wrong byte after delete (deleted=%u expected-next=%u got=%u)",
                 (unsigned)before, (unsigned)after_expected, (unsigned)after_actual);
        fail(name, msg);
        doc_clear(&doc);
        return false;
    }

    doc_clear(&doc);
    printf("PASS %s\n", name);
    return true;
}

static bool build_insert_block(char **out_buf, u64 *out_len) {
    u64 len = 10000;
    char *buf = (char *)malloc((size_t)len);
    if (!buf) return false;
    for (u64 i = 0; i < len; ++i) {
        buf[i] = (char)('A' + (char)(i % 26));
    }
    *out_buf = buf;
    *out_len = len;
    return true;
}

static bool test_insert_10000_chars_halfway(const wchar_t *path) {
    const char *name = "insert_10000_chars_halfway";
    Document doc;
    u64 before_len;
    u64 insert_off;
    char *ins = NULL;
    u64 ins_len = 0;
    unsigned char left_before = 0;
    unsigned char right_before = 0;
    unsigned char left_after = 0;
    unsigned char right_after = 0;
    char sample[64];

    if (!load_fixture(&doc, path)) {
        fail(name, "could not load test fixture");
        return false;
    }

    if (doc.len < 2) {
        fail(name, "fixture too small for insert test");
        doc_clear(&doc);
        return false;
    }

    before_len = doc.len;
    insert_off = before_len / 2;

    if (insert_off > 0 && !read_byte(&doc, insert_off - 1, &left_before)) {
        fail(name, "failed to read left neighbor before insert");
        doc_clear(&doc);
        return false;
    }
    if (insert_off < before_len && !read_byte(&doc, insert_off, &right_before)) {
        fail(name, "failed to read right neighbor before insert");
        doc_clear(&doc);
        return false;
    }

    if (!build_insert_block(&ins, &ins_len)) {
        fail(name, "failed to allocate insert block");
        doc_clear(&doc);
        return false;
    }

    if (!doc_insert_bytes(&doc, insert_off, ins, ins_len, NULL)) {
        free(ins);
        fail(name, "doc_insert_bytes failed");
        doc_clear(&doc);
        return false;
    }

    if (doc.len != before_len + ins_len) {
        free(ins);
        fail(name, "length did not grow by 10000");
        doc_clear(&doc);
        return false;
    }

    if (!read_exact(&doc, insert_off, sample, (u64)sizeof(sample))) {
        free(ins);
        fail(name, "failed to read inserted sample block");
        doc_clear(&doc);
        return false;
    }
    if (memcmp(sample, ins, sizeof(sample)) != 0) {
        free(ins);
        fail(name, "inserted bytes mismatch in sample window");
        doc_clear(&doc);
        return false;
    }

    if (insert_off > 0 && !read_byte(&doc, insert_off - 1, &left_after)) {
        free(ins);
        fail(name, "failed to read left neighbor after insert");
        doc_clear(&doc);
        return false;
    }
    if (!read_byte(&doc, insert_off + ins_len, &right_after)) {
        free(ins);
        fail(name, "failed to read right neighbor after insert");
        doc_clear(&doc);
        return false;
    }

    if (insert_off > 0 && left_after != left_before) {
        free(ins);
        fail(name, "left neighbor changed unexpectedly");
        doc_clear(&doc);
        return false;
    }
    if (right_after != right_before) {
        free(ins);
        fail(name, "right neighbor mismatch after insert");
        doc_clear(&doc);
        return false;
    }

    free(ins);
    doc_clear(&doc);
    printf("PASS %s\n", name);
    return true;
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

int wmain(int argc, wchar_t **argv) {
    wchar_t fixture[MAX_PATH];
    TimedRun runs[2];
    int exit_code = 0;

    if (argc != 1) {
        fprintf(stderr, "usage: text_fast_tests.exe\n");
        return 2;
    }
    (void)argv;

    if (!create_large_fixture(fixture, sizeof(fixture) / sizeof(fixture[0]))) {
        fprintf(stderr, "failed to create generated 300 MiB fixture\n");
        return 1;
    }
    runs[0] = run_timed("delete_one_char_at_pos_10000", test_delete_one_char_at_pos_10000, fixture);
    runs[1] = run_timed("insert_10000_chars_halfway", test_insert_10000_chars_halfway, fixture);
    for (size_t i = 0; i < sizeof(runs) / sizeof(runs[0]); ++i) {
        printf("TIMING %s %.3f ms (%s)\n",
               runs[i].name, runs[i].elapsed_ms, runs[i].passed ? "pass" : "fail");
    }

    if (g_failures) {
        printf("%d fast backend test(s) failed\n", g_failures);
        exit_code = 1;
    } else {
        printf("all fast backend tests passed\n");
    }

    if (!DeleteFileW(fixture)) {
        fprintf(stderr, "failed to remove generated fixture\n");
        exit_code = 1;
    }
    return exit_code;
}
