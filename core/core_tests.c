#include "core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
typedef struct Memory { size_t calls, fail, live, bytes, peak; } Memory;
typedef union Allocation { struct { size_t size; } info; long double align; void *ptr; } Allocation;
static void *test_alloc(void *user, size_t size)
{
    Memory *m = (Memory *)user;
    Allocation *p;
    ++m->calls;
    if (m->fail && m->calls == m->fail) return NULL;
    p = (Allocation *)malloc(sizeof(*p) + size);
    if (!p) return NULL;
    p->info.size = size; ++m->live; m->bytes += size;
    if (m->bytes > m->peak) m->peak = m->bytes;
    return p + 1;
}
static void test_free(void *user, void *ptr)
{
    Memory *m = (Memory *)user;
    Allocation *p = (Allocation *)ptr - 1;
    CHECK(m->live && m->bytes >= p->info.size);
    --m->live; m->bytes -= p->info.size; free(p);
}
static Core make_core(Memory *m)
{
    Core r;
    CoreAllocator a = {test_alloc, test_free, m};
    CHECK(core_init(&r, &a)); return r;
}
static double now_ms(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter); QueryPerformanceFrequency(&frequency);
    return 1000.0 * (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    return 1000.0 * (double)clock() / CLOCKS_PER_SEC;
#endif
}
static void equal(const Core *r, const char *s, size_t len)
{
    char bytes[8192]; CoreRun run;
    CHECK(len <= sizeof(bytes)); CHECK(core_len(r) == len);
    CHECK(core_read(r, 0, len, bytes)); CHECK(!memcmp(bytes, s, len));
    CHECK(core_run(r, len, &run) && !run.len);
}
static unsigned rng = 0x12345678;
static unsigned random_u32(void)
{ rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int cancel_run(void *user, const char *data, uint64_t len)
{ (void)data; (void)len; ++*(int *)user; return 0; }
static void release_count(void *user, const void *data, uint64_t len)
{ (void)data; (void)len; ++*(int *)user; }

static void boundaries(void)
{
    Memory m = {0}; Core r = make_core(&m), part = make_core(&m), snap = make_core(&m);
    const char bytes[] = {'a', 0, '\r', '\n', (char)0xff, 'z'};
    int releases = 0, called = 0;
    CHECK(core_from_memory(&r, bytes, sizeof(bytes), release_count, &releases));
    for (size_t off = 0; off <= sizeof(bytes); ++off) {
        for (size_t len = 0; len <= sizeof(bytes) - off; ++len) {
            CHECK(core_slice(&part, &r, off, len)); equal(&part, bytes + off, len);
            core_clone(&snap, &r);
            CHECK(core_cut(&snap, off, len, &part));
            CHECK(core_replace(&snap, off, 0, &part)); equal(&snap, bytes, sizeof(bytes));
        }
    }
    CHECK(!core_slice(&part, &r, UINT64_MAX, 1));
    CHECK(!core_replace(&r, 1, UINT64_MAX, NULL));
    CHECK(!core_insert(&r, 0, NULL, 1));
    CHECK(!core_cut(&r, 0, 1, &r));
    CHECK(!core_runs(&r, 0, sizeof(bytes), cancel_run, &called) && called == 1);
    CHECK(!core_read(&r, 0, sizeof(bytes) + 1, (void *)bytes));
    core_clone(&snap, &r); core_clone(&snap, &snap);
    core_dispose(&r); CHECK(releases == 0); equal(&snap, bytes, sizeof(bytes));
    core_dispose(&part); core_dispose(&snap); CHECK(releases == 1);
    CHECK(core_from_memory(&r, NULL, 0, release_count, &releases)); CHECK(releases == 2);
    core_dispose(&r); core_dispose(&r); CHECK(!m.live);
}

static void randomized(void)
{
    Memory m = {0}; Core r = make_core(&m), part = make_core(&m), snapshots[8];
    char model[8192], saved[8][8192]; size_t len = 0, saved_len[8] = {0};
    for (int i = 0; i < 8; ++i) snapshots[i] = make_core(&m);
    for (unsigned step = 0; step < 20000; ++step) {
        size_t off = random_u32() % (len + 1), n = random_u32() % 24;
        unsigned op = random_u32() % 6;
        if (len > 4000) op = 1;
        if (op == 0) {
            char input[24];
            for (size_t i = 0; i < n; ++i) input[i] = (char)random_u32();
            CHECK(core_insert(&r, off, input, n));
            memmove(model + off + n, model + off, len - off);
            memcpy(model + off, input, n); len += n;
        } else if (op == 1) {
            if (n > len - off) n = len - off;
            CHECK(core_cut(&r, off, n, &part)); equal(&part, model + off, n);
            memmove(model + off, model + off + n, len - off - n); len -= n;
        } else if (op == 2) {
            unsigned slot = random_u32() % 8;
            core_clone(&snapshots[slot], &r);
            memcpy(saved[slot], model, len); saved_len[slot] = len;
        } else if (op == 3 && len < 2000) {
            CHECK(core_replace(&r, off, 0, &r));
            char old[8192]; memcpy(old, model, len);
            memmove(model + off + len, model + off, len - off);
            memcpy(model + off, old, len); len *= 2;
        } else if (op == 4) {
            size_t source = random_u32() % (len + 1);
            if (n > len - source) n = len - source;
            CHECK(core_slice(&part, &r, source, n));
            char old[24]; memcpy(old, model + source, n);
            CHECK(core_replace(&r, off, 0, &part));
            memmove(model + off + n, model + off, len - off);
            memcpy(model + off, old, n); len += n;
        } else {
            if (n > len - off) n = len - off;
            CHECK(core_slice(&r, &r, off, n));
            memmove(model, model + off, n); len = n;
        }
        equal(&r, model, len);
        for (int i = 0; i < 8; ++i) equal(&snapshots[i], saved[i], saved_len[i]);
    }
    core_dispose(&r); core_dispose(&part);
    for (int i = 0; i < 8; ++i) core_dispose(&snapshots[i]);
    CHECK(!m.live);
}

static void allocation_failures(void)
{
    unsigned failed = 0;
    for (unsigned op = 0; op < 6; ++op) {
        int completed = 0;
        for (size_t nth = 1; nth < 256 && !completed; ++nth) {
            Memory m = {0}; Core r = make_core(&m), out = make_core(&m), before = make_core(&m);
            char model[64]; int released = 0, ok;
            for (int i = 0; i < 64; ++i) {
                model[i] = (char)('a' + i % 26);
                Core one = make_core(&m);
                CHECK(core_from_memory(&one, model + i, 1, NULL, NULL));
                CHECK(core_replace(&r, core_len(&r), 0, &one)); core_dispose(&one);
            }
            CHECK(core_insert(&out, 0, "old", 3)); core_clone(&before, &r);
            m.fail = m.calls + nth;
            if (op == 0) ok = core_insert(&r, 17, "new", 3);
            else if (op == 1) ok = core_replace(&r, 17, 29, &before);
            else if (op == 2) ok = core_slice(&out, &r, 17, 29);
            else if (op == 3) ok = core_cut(&r, 17, 29, &out);
            else if (op == 4) ok = core_from_memory(&out, model, 64, release_count, &released);
            else ok = core_replace(&r, 17, 29, NULL);
            if (!ok) {
                ++failed; equal(&r, model, 64); equal(&out, "old", 3); CHECK(!released);
            } else completed = 1;
            equal(&before, model, 64);
            m.fail = 0; core_dispose(&r); core_dispose(&before); core_dispose(&out);
            CHECK(!m.live && !m.bytes);
            CHECK(released == (op == 4 && ok ? 1 : 0));
        }
        CHECK(completed);
    }
    printf("Allocation failures: %u failure points, unchanged outputs, no leaks\n", failed);
}

static void structure_and_overflow(void)
{
    Memory m = {0}; Core r = make_core(&m), snap = make_core(&m), cut = make_core(&m);
    Core one = make_core(&m);
    double start = now_ms();
    CHECK(core_from_memory(&one, "x", 1, NULL, NULL));
    for (unsigned i = 0; i < 20000; ++i)
        CHECK(core_replace(&r, random_u32() % (i + 1), 0, &one));
    CHECK(core_len(&r) == 20000 && core_pieces(&r) == 20000);
    CHECK(core_height(&r) < 24);
    core_clone(&snap, &r);
    size_t before = m.calls;
    CHECK(core_cut(&r, 1000, 18000, &cut));
    CHECK(core_replace(&r, 1000, 0, &cut));
    CHECK(m.calls - before < 256);
    char bytes[20000]; CHECK(core_read(&r, 0, sizeof(bytes), bytes));
    for (size_t i = 0; i < sizeof(bytes); ++i) CHECK(bytes[i] == 'x');
    printf("20,000 fragmented inserts + shared cut/reinsert: %.3f ms, height %u\n",
           now_ms() - start, core_height(&r));
    core_dispose(&r); core_dispose(&snap); core_dispose(&cut);
    for (int i = 0; i < 10000; ++i) CHECK(core_insert(&r, i, "x", 1));
    CHECK(core_pieces(&r) == 1); core_dispose(&r);
    core_clone(&r, &one);
    for (unsigned i = 0; i < 63; ++i) CHECK(core_replace(&r, core_len(&r), 0, &r));
    CHECK(core_len(&r) == (UINT64_C(1) << 63));
    CHECK(!core_replace(&r, core_len(&r), 0, &r));
    CHECK(core_insert(&r, 0, "x", 1));
    core_dispose(&r); core_dispose(&one); CHECK(!m.live);
}

#ifdef _WIN32
typedef struct Mapping { HANDLE file, map; const char *data; int released; } Mapping;
static void unmap_fixture(void *user, const void *data, uint64_t len)
{
    Mapping *m = (Mapping *)user; (void)len;
    UnmapViewOfFile(data); CloseHandle(m->map); CloseHandle(m->file); ++m->released;
}
typedef struct Verify { const char *expected; uint64_t offset; } Verify;
static int verify_run(void *user, const char *data, uint64_t len)
{
    Verify *v = (Verify *)user;
    /* Pointer identity proves zero-copy range serialization, without paging
       all 453 MB into RAM merely to assert the same bytes equal themselves. */
    CHECK(data == v->expected + v->offset);
    v->offset += len; return 1;
}
static void large_fixture(const char *path)
{
    Memory m = {0}; Mapping mapping = {0}; LARGE_INTEGER size;
    Core source = make_core(&m), r = make_core(&m), part = make_core(&m);
    Core copy = make_core(&m); CoreRun run;
    double start = now_ms();
    mapping.file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(mapping.file != INVALID_HANDLE_VALUE);
    CHECK(GetFileSizeEx(mapping.file, &size) && size.QuadPart > 1024);
    mapping.map = CreateFileMappingW(mapping.file, NULL, PAGE_READONLY, 0, 0, NULL);
    CHECK(mapping.map != NULL);
    mapping.data = (const char *)MapViewOfFile(mapping.map, FILE_MAP_READ, 0, 0, 0);
    CHECK(mapping.data != NULL);
    printf("Map %s (%llu bytes): %.3f ms\n", path, (unsigned long long)size.QuadPart, now_ms() - start);
    start = now_ms();
    CHECK(core_from_memory(&source, mapping.data, (uint64_t)size.QuadPart, unmap_fixture, &mapping));
    CHECK(core_replace(&r, 0, 0, &source));
    printf("Adopt + insert entire mapped file: %.3f ms\n", now_ms() - start);
    uint64_t length = core_len(&r), off = length / 3, len = length / 2;
    start = now_ms();
    for (int i = 0; i < 1000; ++i) {
        CHECK(core_cut(&r, off, len, &part));
        CHECK(core_replace(&r, off, 0, &part));
        CHECK(core_len(&r) == length);
    }
    printf("1,000 large cut/reinsert pairs: %.3f ms; peak core allocation %zu bytes\n",
           now_ms() - start, m.peak);
    CHECK(m.peak < 32768);
    Verify verify = {mapping.data, 0};
    CHECK(core_runs(&r, 0, length, verify_run, &verify) && verify.offset == length);
    CHECK(core_slice(&copy, &r, off, len));
    CHECK(core_replace(&r, 0, 0, &copy));
    CHECK(core_run(&r, 0, &run) && run.data == mapping.data + off && run.len == len);
    char expected[32], actual[32]; memcpy(expected, mapping.data + off, sizeof(expected));
    core_dispose(&source); core_dispose(&part); core_dispose(&copy);
    CHECK(!mapping.released);
    CHECK(core_read(&r, 0, sizeof(actual), actual)); CHECK(!memcmp(actual, expected, sizeof(actual)));
    core_dispose(&r); CHECK(mapping.released == 1 && !m.live);
}
#endif

static const char *active_test;
static double test_started;
static void failed_timing(void) {
    if (active_test) printf("FAIL %s (%.3f ms)\n", active_test, now_ms() - test_started);
}
#define RUN(name, expression) do { \
    active_test = name; test_started = now_ms(); \
    expression; \
    printf("PASS %s (%.3f ms)\n", name, now_ms() - test_started); \
    active_test = NULL; \
} while (0)
int main(int argc, char **argv) {
    double start = now_ms();
    atexit(failed_timing);
    RUN("boundaries_and_ownership", boundaries());
    RUN("randomized_edits_and_snapshots", randomized());
    RUN("allocation_failures", allocation_failures());
    RUN("structure_and_overflow", structure_and_overflow());
#ifdef _WIN32
    if (argc > 1) RUN("large_mapped_fixture", large_fixture(argv[1]));
#else
    (void)argc; (void)argv;
#endif
    printf("PASS: standalone core tests, %.3f ms total\n", now_ms() - start);
    return 0;
}
