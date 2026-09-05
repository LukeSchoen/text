#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))

#define APP_WINDOW_CLASS L"TextSuiteFastText"
#define APP_COMMAND_POPUP_CLASS L"TextSuiteCommandPopup"
#define TEST_STARTUP_TIMEOUT_MS 2500
#define TEST_PROCESS_EXIT_TIMEOUT_MS 1000
#define TEST_UI_TIMEOUT_MS 1500
#define TEST_POLL_SLEEP_MS 5

typedef struct TestApp {
    PROCESS_INFORMATION pi;
    HWND window;
} TestApp;

typedef BOOL (*TestFn)(void);

typedef struct TestCase {
    const char *name;
    TestFn fn;
} TestCase;

typedef BOOL (*WaitPredicateFn)(void *ctx);

typedef struct FindWindowState {
    DWORD pid;
    HWND found;
} FindWindowState;

typedef struct FindDialogState {
    DWORD pid;
    HWND found;
} FindDialogState;

typedef struct FindClassWindowState {
    DWORD pid;
    const wchar_t *class_name;
    HWND found;
} FindClassWindowState;

typedef struct AppWindowSnapshot {
    HWND windows[128];
    int count;
} AppWindowSnapshot;

typedef struct FindNewAppWindowState {
    const AppWindowSnapshot *before;
    HWND found;
} FindNewAppWindowState;

typedef struct WindowGoneWait {
    HWND hwnd;
} WindowGoneWait;

typedef struct ScrollWait {
    HWND hwnd;
    SCROLLINFO si;
    int baseline;
} ScrollWait;

typedef struct DragDebug {
    SCROLLBARINFO sbi;
    POINT start;
    POINT end;
} DragDebug;

static wchar_t g_exe_path[MAX_PATH];
static wchar_t g_look_dir[MAX_PATH];
static int g_failures;
static DragDebug g_last_drag;

static ULONGLONG monotonic_tick_ms(void) {
    static ULONGLONG (WINAPI *get_tick_count64)(void);
    static int initialized;
    if (!initialized) {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        if (k32) get_tick_count64 = (ULONGLONG (WINAPI *)(void))GetProcAddress(k32, "GetTickCount64");
        initialized = 1;
    }
    if (get_tick_count64) return get_tick_count64();
    return (ULONGLONG)GetTickCount();
}

static int swprintf_trunc(wchar_t *dst, size_t dst_count, const wchar_t *fmt, ...) {
    int rc;
    va_list args;
    if (!dst || dst_count == 0 || !fmt) return -1;
    va_start(args, fmt);
    rc = _vsnwprintf(dst, dst_count, fmt, args);
    va_end(args);
    dst[dst_count - 1] = 0;
    return rc;
}

static void pump_messages_briefly(void) {
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Sleep(TEST_POLL_SLEEP_MS);
}

static BOOL wait_until_true(DWORD timeout_ms, WaitPredicateFn predicate, void *ctx) {
    ULONGLONG deadline = monotonic_tick_ms() + timeout_ms;

    while (monotonic_tick_ms() <= deadline) {
        if (predicate(ctx)) return TRUE;
        pump_messages_briefly();
    }
    return predicate(ctx);
}

static void fail_message(const char *test_name, const char *message) {
    fprintf(stderr, "FAIL %s: %s\n", test_name, message);
    ++g_failures;
}

static BOOL write_bmp_file(const wchar_t *path, HBITMAP bitmap, int width, int height) {
    BITMAPINFO bi;
    BITMAPFILEHEADER bfh;
    HDC dc;
    uint8_t *pixels = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD wrote;
    BOOL ok = FALSE;
    size_t image_size;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    image_size = (size_t)width * (size_t)height * 4u;
    pixels = (uint8_t *)malloc(image_size);
    if (!pixels) return FALSE;

    dc = CreateCompatibleDC(NULL);
    if (!dc) goto cleanup;
    if (!GetDIBits(dc, bitmap, 0, (UINT)height, pixels, &bi, DIB_RGB_COLORS)) {
        DeleteDC(dc);
        goto cleanup;
    }
    DeleteDC(dc);

    ZeroMemory(&bfh, sizeof(bfh));
    bfh.bfType = 0x4D42;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = (DWORD)(bfh.bfOffBits + image_size);

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto cleanup;

    if (!WriteFile(file, &bfh, sizeof(bfh), &wrote, NULL) || wrote != sizeof(bfh)) goto cleanup;
    if (!WriteFile(file, &bi.bmiHeader, sizeof(bi.bmiHeader), &wrote, NULL) || wrote != sizeof(bi.bmiHeader)) goto cleanup;
    if (!WriteFile(file, pixels, (DWORD)image_size, &wrote, NULL) || wrote != (DWORD)image_size) goto cleanup;

    ok = TRUE;

cleanup:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    free(pixels);
    return ok;
}

static BOOL capture_window_screenshot(HWND hwnd, const wchar_t *path) {
    RECT rc;
    HDC src = NULL;
    HDC mem = NULL;
    HBITMAP bmp = NULL;
    HGDIOBJ old = NULL;
    int width;
    int height;
    BOOL ok = FALSE;

    if (!GetWindowRect(hwnd, &rc)) return FALSE;
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return FALSE;

    src = GetWindowDC(hwnd);
    if (!src) return FALSE;
    mem = CreateCompatibleDC(src);
    if (!mem) goto cleanup;
    bmp = CreateCompatibleBitmap(src, width, height);
    if (!bmp) goto cleanup;
    old = SelectObject(mem, bmp);

    if (!BitBlt(mem, 0, 0, width, height, src, 0, 0, SRCCOPY | CAPTUREBLT)) goto cleanup;
    ok = write_bmp_file(path, bmp, width, height);

cleanup:
    if (old) SelectObject(mem, old);
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    if (src) ReleaseDC(hwnd, src);
    return ok;
}

static BOOL maybe_capture_look(HWND hwnd, const char *test_name, const char *stage_name) {
    wchar_t path[MAX_PATH];
    wchar_t wtest[96];
    wchar_t wstage[64];
    size_t i;

    if (!g_look_dir[0] || !hwnd || !test_name || !stage_name) return TRUE;

    for (i = 0; i + 1 < COUNT_OF(wtest) && test_name[i]; ++i) wtest[i] = (wchar_t)(unsigned char)test_name[i];
    wtest[i] = 0;
    for (i = 0; i + 1 < COUNT_OF(wstage) && stage_name[i]; ++i) wstage[i] = (wchar_t)(unsigned char)stage_name[i];
    wstage[i] = 0;

    swprintf_trunc(path, COUNT_OF(path), L"%ls\\%ls_%ls.bmp", g_look_dir, wtest, wstage);
    return capture_window_screenshot(hwnd, path);
}

static BOOL window_has_color(HWND hwnd, COLORREF target, int tolerance) {
    RECT rc;
    HDC src = NULL;
    HDC mem = NULL;
    HBITMAP bmp = NULL;
    HGDIOBJ old = NULL;
    BITMAPINFO bi;
    uint8_t *pixels = NULL;
    int width;
    int height;
    BOOL found = FALSE;

    if (!GetWindowRect(hwnd, &rc)) return FALSE;
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return FALSE;

    src = GetWindowDC(hwnd);
    if (!src) return FALSE;
    mem = CreateCompatibleDC(src);
    if (!mem) goto cleanup;
    bmp = CreateCompatibleBitmap(src, width, height);
    if (!bmp) goto cleanup;
    old = SelectObject(mem, bmp);
    if (!BitBlt(mem, 0, 0, width, height, src, 0, 0, SRCCOPY | CAPTUREBLT)) goto cleanup;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    pixels = (uint8_t *)malloc((size_t)width * (size_t)height * 4u);
    if (!pixels) goto cleanup;
    if (!GetDIBits(mem, bmp, 0, (UINT)height, pixels, &bi, DIB_RGB_COLORS)) goto cleanup;

    {
        int tr = GetRValue(target);
        int tg = GetGValue(target);
        int tb = GetBValue(target);
        for (int i = 0; i < width * height; ++i) {
            int b = pixels[(size_t)i * 4 + 0];
            int g = pixels[(size_t)i * 4 + 1];
            int r = pixels[(size_t)i * 4 + 2];
            if (abs(r - tr) <= tolerance && abs(g - tg) <= tolerance && abs(b - tb) <= tolerance) {
                found = TRUE;
                break;
            }
        }
    }

cleanup:
    free(pixels);
    if (old) SelectObject(mem, old);
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    if (src) ReleaseDC(hwnd, src);
    return found;
}

static BOOL find_color_scanline(HWND hwnd, COLORREF target, int tolerance, int *out_y) {
    RECT rc;
    HDC src = NULL;
    HDC mem = NULL;
    HBITMAP bmp = NULL;
    HGDIOBJ old = NULL;
    BITMAPINFO bi;
    uint8_t *pixels = NULL;
    int width;
    int height;
    BOOL found = FALSE;
    int tr = (int)GetRValue(target);
    int tg = (int)GetGValue(target);
    int tb = (int)GetBValue(target);

    if (!GetWindowRect(hwnd, &rc)) return FALSE;
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return FALSE;

    src = GetWindowDC(hwnd);
    if (!src) return FALSE;
    mem = CreateCompatibleDC(src);
    if (!mem) goto cleanup;
    bmp = CreateCompatibleBitmap(src, width, height);
    if (!bmp) goto cleanup;
    old = SelectObject(mem, bmp);
    if (!BitBlt(mem, 0, 0, width, height, src, 0, 0, SRCCOPY | CAPTUREBLT)) goto cleanup;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    pixels = (uint8_t *)malloc((size_t)width * (size_t)height * 4u);
    if (!pixels) goto cleanup;
    if (!GetDIBits(mem, bmp, 0, (UINT)height, pixels, &bi, DIB_RGB_COLORS)) goto cleanup;

    for (int y = 0; y < height && !found; ++y) {
        int hits = 0;
        for (int x = 0; x < width; ++x) {
            size_t idx = ((size_t)y * (size_t)width + (size_t)x) * 4u;
            int b = pixels[idx + 0];
            int g = pixels[idx + 1];
            int r = pixels[idx + 2];
            if (abs(r - tr) <= tolerance && abs(g - tg) <= tolerance && abs(b - tb) <= tolerance) {
                hits++;
                if (hits >= 8) {
                    *out_y = y;
                    found = TRUE;
                    break;
                }
            }
        }
    }

cleanup:
    free(pixels);
    if (old) SelectObject(mem, old);
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    if (src) ReleaseDC(hwnd, src);
    return found;
}

static BOOL CALLBACK enum_top_windows(HWND hwnd, LPARAM lParam) {
    FindWindowState *state = (FindWindowState *)lParam;
    DWORD pid = 0;
    wchar_t class_name[128];

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != state->pid || !IsWindowVisible(hwnd)) return TRUE;
    if (!GetClassNameW(hwnd, class_name, (int)COUNT_OF(class_name))) return TRUE;
    if (wcscmp(class_name, APP_WINDOW_CLASS) == 0) {
        state->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND find_main_window(DWORD pid) {
    FindWindowState state;

    ZeroMemory(&state, sizeof(state));
    state.pid = pid;
    EnumWindows(enum_top_windows, (LPARAM)&state);
    return state.found;
}

static BOOL CALLBACK enum_pid_dialogs(HWND hwnd, LPARAM lParam) {
    FindDialogState *state = (FindDialogState *)lParam;
    DWORD pid = 0;
    wchar_t class_name[64];
    wchar_t title[64];

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != state->pid || !IsWindowVisible(hwnd)) return TRUE;
    if (!GetClassNameW(hwnd, class_name, (int)COUNT_OF(class_name))) return TRUE;
    GetWindowTextW(hwnd, title, (int)COUNT_OF(title));
    if (wcscmp(class_name, L"#32770") == 0) {
        if (wcsstr(title, L"find") || wcsstr(title, L"Find")) {
            state->found = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static HWND find_process_dialog(DWORD pid) {
    FindDialogState state;
    ZeroMemory(&state, sizeof(state));
    state.pid = pid;
    EnumWindows(enum_pid_dialogs, (LPARAM)&state);
    return state.found;
}

static BOOL CALLBACK enum_pid_any_dialogs(HWND hwnd, LPARAM lParam) {
    FindDialogState *state = (FindDialogState *)lParam;
    DWORD pid = 0;
    wchar_t class_name[64];

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != state->pid || !IsWindowVisible(hwnd)) return TRUE;
    if (!GetClassNameW(hwnd, class_name, (int)COUNT_OF(class_name))) return TRUE;
    if (wcscmp(class_name, L"#32770") == 0) {
        state->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

static void send_key_press(WORD vk);

static HWND find_any_process_dialog(DWORD pid) {
    FindDialogState state;
    ZeroMemory(&state, sizeof(state));
    state.pid = pid;
    EnumWindows(enum_pid_any_dialogs, (LPARAM)&state);
    return state.found;
}

static BOOL any_process_dialog_exists(void *ctx) {
    FindDialogState *state = (FindDialogState *)ctx;
    state->found = find_any_process_dialog(state->pid);
    return state->found != NULL;
}

static HWND wait_for_any_process_dialog(DWORD pid) {
    FindDialogState state;
    ZeroMemory(&state, sizeof(state));
    state.pid = pid;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS * 2, any_process_dialog_exists, &state)) return NULL;
    return state.found;
}

static void send_dialog_key(HWND dialog, WORD vk) {
    if (!dialog) return;
    SetForegroundWindow(dialog);
    BringWindowToTop(dialog);
    pump_messages_briefly();
    send_key_press(vk);
}

static BOOL CALLBACK debug_print_dialog_child(HWND hwnd, LPARAM unused) {
    wchar_t class_name[64];
    wchar_t text[512];
    (void)unused;
    class_name[0] = 0;
    text[0] = 0;
    GetClassNameW(hwnd, class_name, (int)COUNT_OF(class_name));
    GetWindowTextW(hwnd, text, (int)COUNT_OF(text));
    if (text[0])
        fwprintf(stderr, L"  debug: dialog child class=[%ls] id=%d text=[%ls]\n",
                 class_name, GetDlgCtrlID(hwnd), text);
    return TRUE;
}

static void debug_print_process_dialog(DWORD pid) {
    HWND dialog = find_any_process_dialog(pid);
    wchar_t text[512];
    if (!dialog) return;
    text[0] = 0;
    GetWindowTextW(dialog, text, (int)COUNT_OF(text));
    fwprintf(stderr, L"  debug: active dialog title=[%ls]\n", text);
    EnumChildWindows(dialog, debug_print_dialog_child, 0);
}

static BOOL CALLBACK enum_pid_class_windows(HWND hwnd, LPARAM lParam) {
    FindClassWindowState *state = (FindClassWindowState *)lParam;
    DWORD pid = 0;
    wchar_t class_name[128];

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != state->pid || !IsWindowVisible(hwnd)) return TRUE;
    if (!GetClassNameW(hwnd, class_name, (int)COUNT_OF(class_name))) return TRUE;
    if (wcscmp(class_name, state->class_name) == 0) {
        state->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND find_process_window_by_class(DWORD pid, const wchar_t *class_name) {
    FindClassWindowState state;
    ZeroMemory(&state, sizeof(state));
    state.pid = pid;
    state.class_name = class_name;
    EnumWindows(enum_pid_class_windows, (LPARAM)&state);
    return state.found;
}

static BOOL is_visible_app_window(HWND hwnd) {
    wchar_t class_name[128];
    if (!IsWindowVisible(hwnd)) return FALSE;
    if (!GetClassNameW(hwnd, class_name, (int)COUNT_OF(class_name))) return FALSE;
    return wcscmp(class_name, APP_WINDOW_CLASS) == 0;
}

static BOOL CALLBACK enum_app_windows_snapshot(HWND hwnd, LPARAM lParam) {
    AppWindowSnapshot *snapshot = (AppWindowSnapshot *)lParam;
    if (!is_visible_app_window(hwnd)) return TRUE;
    if (snapshot->count < (int)COUNT_OF(snapshot->windows)) {
        snapshot->windows[snapshot->count++] = hwnd;
    }
    return TRUE;
}

static void collect_app_windows(AppWindowSnapshot *snapshot) {
    ZeroMemory(snapshot, sizeof(*snapshot));
    EnumWindows(enum_app_windows_snapshot, (LPARAM)snapshot);
}

static BOOL snapshot_contains_window(const AppWindowSnapshot *snapshot, HWND hwnd) {
    int i;
    for (i = 0; i < snapshot->count; ++i) {
        if (snapshot->windows[i] == hwnd) return TRUE;
    }
    return FALSE;
}

static BOOL CALLBACK enum_new_app_window(HWND hwnd, LPARAM lParam) {
    FindNewAppWindowState *state = (FindNewAppWindowState *)lParam;
    if (!is_visible_app_window(hwnd)) return TRUE;
    if (snapshot_contains_window(state->before, hwnd)) return TRUE;
    state->found = hwnd;
    return FALSE;
}

static BOOL new_app_window_exists(void *ctx) {
    FindNewAppWindowState *state = (FindNewAppWindowState *)ctx;
    state->found = NULL;
    EnumWindows(enum_new_app_window, (LPARAM)state);
    return state->found != NULL;
}

static BOOL window_is_gone(void *ctx) {
    WindowGoneWait *wait = (WindowGoneWait *)ctx;
    return !IsWindow(wait->hwnd);
}

static void close_window_by_hwnd(HWND hwnd) {
    DWORD pid = 0;
    HANDLE process = NULL;
    WindowGoneWait wait;
    if (!hwnd) return;
    GetWindowThreadProcessId(hwnd, &pid);
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    wait.hwnd = hwnd;
    if (wait_until_true(TEST_PROCESS_EXIT_TIMEOUT_MS, window_is_gone, &wait)) return;
    if (pid) {
        process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (process) {
            TerminateProcess(process, 1);
            WaitForSingleObject(process, TEST_PROCESS_EXIT_TIMEOUT_MS);
            CloseHandle(process);
        }
    }
}

typedef struct FindChildClassState {
    const wchar_t *class_name;
    HWND found;
} FindChildClassState;

static BOOL CALLBACK enum_child_class_windows(HWND hwnd, LPARAM lParam) {
    FindChildClassState *state = (FindChildClassState *)lParam;
    wchar_t class_name[128];

    if (!GetClassNameW(hwnd, class_name, (int)COUNT_OF(class_name))) return TRUE;
    if (wcscmp(class_name, state->class_name) == 0) {
        state->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND find_child_window_by_class(HWND parent, const wchar_t *class_name) {
    FindChildClassState state;
    ZeroMemory(&state, sizeof(state));
    state.class_name = class_name;
    EnumChildWindows(parent, enum_child_class_windows, (LPARAM)&state);
    return state.found;
}


static BOOL main_window_exists(void *ctx) {
    TestApp *app = (TestApp *)ctx;
    app->window = find_main_window(app->pi.dwProcessId);
    return app->window != NULL;
}

static BOOL launch_app_with_argument(TestApp *app, const wchar_t *argument) {
    STARTUPINFOW si;
    wchar_t cmdline[MAX_PATH * 4];

    ZeroMemory(app, sizeof(*app));
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (argument && argument[0]) swprintf_trunc(cmdline, COUNT_OF(cmdline), L"\"%ls\" \"%ls\"", g_exe_path, argument);
    else swprintf_trunc(cmdline, COUNT_OF(cmdline), L"\"%ls\"", g_exe_path);

    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &app->pi)) return FALSE;
    WaitForInputIdle(app->pi.hProcess, TEST_STARTUP_TIMEOUT_MS);
    if (wait_until_true(TEST_STARTUP_TIMEOUT_MS, main_window_exists, app)) return TRUE;

    if (app->pi.hProcess) {
        TerminateProcess(app->pi.hProcess, 1);
        WaitForSingleObject(app->pi.hProcess, TEST_PROCESS_EXIT_TIMEOUT_MS);
    }
    if (app->pi.hThread) CloseHandle(app->pi.hThread);
    if (app->pi.hProcess) CloseHandle(app->pi.hProcess);
    ZeroMemory(app, sizeof(*app));
    return FALSE;
}

static BOOL launch_app(TestApp *app) {
    return launch_app_with_argument(app, NULL);
}

static BOOL resolve_repo_test_file(wchar_t *path, size_t path_count, const wchar_t *file_name) {
    wchar_t tmp[MAX_PATH];
    wchar_t *slash;
    wchar_t *slash2;

    if (!path || path_count == 0 || !file_name || !file_name[0]) return FALSE;
    if (!GetFullPathNameW(g_exe_path, (DWORD)COUNT_OF(tmp), tmp, NULL)) return FALSE;
    slash = wcsrchr(tmp, L'\\');
    if (!slash) return FALSE;
    *slash = 0; /* ...\.build */
    slash2 = wcsrchr(tmp, L'\\');
    if (!slash2) return FALSE;
    *slash2 = 0; /* repo root */
    if (swprintf_trunc(path, path_count, L"%ls\\%ls", tmp, file_name) < 0) return FALSE;
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static BOOL focus_app_window(HWND hwnd);
static void send_modified_press(WORD mod1, WORD mod2, WORD vk);

static void close_app(TestApp *app) {
    if (app->window && focus_app_window(app->window)) {
        send_modified_press(VK_SHIFT, 0, VK_ESCAPE);
    } else if (app->window) {
        PostMessageW(app->window, WM_CLOSE, 0, 0);
    }
    if (app->pi.hProcess) {
        DWORD wait = WaitForSingleObject(app->pi.hProcess, TEST_PROCESS_EXIT_TIMEOUT_MS);
        if (wait == WAIT_TIMEOUT) {
            HWND dlg = find_process_dialog(app->pi.dwProcessId);
            if (dlg) {
                SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDYES, BN_CLICKED), 0);
                wait = WaitForSingleObject(app->pi.hProcess, TEST_PROCESS_EXIT_TIMEOUT_MS);
            }
        }
        if (wait == WAIT_TIMEOUT) {
            TerminateProcess(app->pi.hProcess, 1);
            WaitForSingleObject(app->pi.hProcess, TEST_PROCESS_EXIT_TIMEOUT_MS);
        }
    }
    if (app->pi.hThread) CloseHandle(app->pi.hThread);
    if (app->pi.hProcess) CloseHandle(app->pi.hProcess);
    ZeroMemory(app, sizeof(*app));
}

static BOOL get_vscroll(HWND hwnd, SCROLLINFO *si) {
    ZeroMemory(si, sizeof(*si));
    si->cbSize = sizeof(*si);
    si->fMask = SIF_ALL;
    return GetScrollInfo(hwnd, SB_VERT, si);
}

static BOOL get_hscroll(HWND hwnd, SCROLLINFO *si) {
    ZeroMemory(si, sizeof(*si));
    si->cbSize = sizeof(*si);
    si->fMask = SIF_ALL;
    return GetScrollInfo(hwnd, SB_HORZ, si);
}

static int scroll_max_pos(const SCROLLINFO *si) {
    int page = si->nPage > 0 ? (int)si->nPage : 1;
    int max_pos = si->nMax - page + 1;
    return max_pos > si->nMin ? max_pos : si->nMin;
}

static BOOL vscroll_is_at_end(void *ctx) {
    ScrollWait *wait = (ScrollWait *)ctx;
    if (!get_vscroll(wait->hwnd, &wait->si)) return FALSE;
    return wait->si.nPos >= scroll_max_pos(&wait->si);
}

static BOOL vscroll_moved_down(void *ctx) {
    ScrollWait *wait = (ScrollWait *)ctx;
    if (!get_vscroll(wait->hwnd, &wait->si)) return FALSE;
    return wait->si.nPos > wait->baseline;
}

static BOOL vscroll_moved_up_or_equal(void *ctx) {
    ScrollWait *wait = (ScrollWait *)ctx;
    if (!get_vscroll(wait->hwnd, &wait->si)) return FALSE;
    return wait->si.nPos <= wait->baseline;
}

static BOOL vscroll_not_at_end(void *ctx) {
    ScrollWait *wait = (ScrollWait *)ctx;
    if (!get_vscroll(wait->hwnd, &wait->si)) return FALSE;
    return wait->si.nPos < scroll_max_pos(&wait->si);
}

static BOOL hscroll_moved_right(void *ctx) {
    ScrollWait *wait = (ScrollWait *)ctx;
    if (!get_hscroll(wait->hwnd, &wait->si)) return FALSE;
    return wait->si.nPos > wait->baseline;
}

static BOOL hscroll_moved_left(void *ctx) {
    ScrollWait *wait = (ScrollWait *)ctx;
    if (!get_hscroll(wait->hwnd, &wait->si)) return FALSE;
    return wait->si.nPos < wait->baseline;
}

static BOOL vscroll_visible(void *ctx) {
    HWND hwnd = (HWND)ctx;
    SCROLLBARINFO sbi;
    ZeroMemory(&sbi, sizeof(sbi));
    sbi.cbSize = sizeof(sbi);
    if (!GetScrollBarInfo(hwnd, OBJID_VSCROLL, &sbi)) return FALSE;
    return (sbi.rgstate[0] & STATE_SYSTEM_INVISIBLE) == 0;
}

static BOOL window_is_maximized(void *ctx) {
    HWND hwnd = (HWND)ctx;
    return IsZoomed(hwnd);
}

static BOOL vscroll_scrollable(void *ctx) {
    HWND hwnd = (HWND)ctx;
    SCROLLINFO si;
    if (!get_vscroll(hwnd, &si)) return FALSE;
    return scroll_max_pos(&si) > si.nMin;
}

static INPUT make_mouse_button(DWORD flags) {
    INPUT input;
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    return input;
}

static INPUT make_key(WORD vk, DWORD flags) {
    INPUT input;
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    switch (vk) {
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_INSERT:
    case VK_DELETE:
    case VK_RCONTROL:
    case VK_RMENU:
        flags |= KEYEVENTF_EXTENDEDKEY;
        break;
    default:
        break;
    }
    input.ki.dwFlags = flags;
    return input;
}

static INPUT make_mouse_wheel(LONG amount) {
    INPUT input;
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = amount;
    return input;
}

static void send_inputs(INPUT *inputs, UINT count) {
    SendInput(count, inputs, sizeof(INPUT));
    pump_messages_briefly();
}

static void send_key_press(WORD vk) {
    INPUT keys[2];
    keys[0] = make_key(vk, 0);
    keys[1] = make_key(vk, KEYEVENTF_KEYUP);
    send_inputs(keys, 2);
}

static void send_unicode_char(wchar_t ch) {
    INPUT keys[2];
    ZeroMemory(keys, sizeof(keys));
    keys[0].type = INPUT_KEYBOARD;
    keys[0].ki.wScan = ch;
    keys[0].ki.dwFlags = KEYEVENTF_UNICODE;
    keys[1] = keys[0];
    keys[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    send_inputs(keys, 2);
}

static void send_unicode_string(const wchar_t *text) {
    if (!text) return;
    for (; *text; ++text) send_unicode_char(*text);
}

static void send_modified_press(WORD mod1, WORD mod2, WORD vk) {
    INPUT keys[2];
    if (mod1) {
        keys[0] = make_key(mod1, 0);
        send_inputs(keys, 1);
    }
    if (mod2) {
        keys[0] = make_key(mod2, 0);
        send_inputs(keys, 1);
    }
    keys[0] = make_key(vk, 0);
    keys[1] = make_key(vk, KEYEVENTF_KEYUP);
    send_inputs(keys, 2);
    if (mod2) {
        keys[0] = make_key(mod2, KEYEVENTF_KEYUP);
        send_inputs(keys, 1);
    }
    if (mod1) {
        keys[0] = make_key(mod1, KEYEVENTF_KEYUP);
        send_inputs(keys, 1);
    }
}

static BOOL get_clipboard_text_dup(wchar_t **out_text) {
    HANDLE h;
    const wchar_t *src;
    size_t len;
    wchar_t *dup;

    *out_text = NULL;
    if (!OpenClipboard(NULL)) return FALSE;
    h = GetClipboardData(CF_UNICODETEXT);
    if (!h) {
        CloseClipboard();
        return FALSE;
    }
    src = (const wchar_t *)GlobalLock(h);
    if (!src) {
        CloseClipboard();
        return FALSE;
    }
    len = wcslen(src);
    dup = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!dup) {
        GlobalUnlock(h);
        CloseClipboard();
        return FALSE;
    }
    memcpy(dup, src, (len + 1) * sizeof(wchar_t));
    GlobalUnlock(h);
    CloseClipboard();
    *out_text = dup;
    return TRUE;
}

static BOOL set_clipboard_text(const wchar_t *text) {
    size_t chars;
    size_t bytes;
    HGLOBAL mem;
    wchar_t *dst;

    if (!text) return FALSE;
    chars = wcslen(text) + 1;
    bytes = chars * sizeof(wchar_t);
    mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return FALSE;
    dst = (wchar_t *)GlobalLock(mem);
    if (!dst) {
        GlobalFree(mem);
        return FALSE;
    }
    memcpy(dst, text, bytes);
    GlobalUnlock(mem);

    if (!OpenClipboard(NULL)) {
        GlobalFree(mem);
        return FALSE;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        CloseClipboard();
        GlobalFree(mem);
        return FALSE;
    }
    CloseClipboard();
    return TRUE;
}

static BOOL clipboard_equals(const wchar_t *expected) {
    wchar_t *actual = NULL;
    BOOL ok = FALSE;
    if (!get_clipboard_text_dup(&actual)) return FALSE;
    ok = wcscmp(actual, expected) == 0;
    free(actual);
    return ok;
}

typedef struct ClipboardWait {
    const wchar_t *expected;
} ClipboardWait;

static BOOL clipboard_matches(void *ctx) {
    ClipboardWait *wait = (ClipboardWait *)ctx;
    return clipboard_equals(wait->expected);
}

static BOOL wait_for_clipboard_text(const wchar_t *expected) {
    ClipboardWait wait;
    wait.expected = expected;
    return wait_until_true(TEST_UI_TIMEOUT_MS, clipboard_matches, &wait);
}

static void fail_clipboard_mismatch(const char *test_name, const wchar_t *expected) {
    wchar_t *actual = NULL;
    if (!get_clipboard_text_dup(&actual)) {
        fail_message(test_name, "clipboard read failed");
        return;
    }
    fwprintf(stderr, L"FAIL %hs: clipboard mismatch\n  expected: [%ls]\n  actual:   [%ls]\n", test_name, expected, actual);
    ++g_failures;
    free(actual);
}

static void clear_clipboard(void) {
    if (!OpenClipboard(NULL)) return;
    EmptyClipboard();
    CloseClipboard();
}

static BOOL get_text_from_hwnd(HWND hwnd, wchar_t **out_text) {
    ULONGLONG deadline;
    LRESULT len;
    wchar_t *text;
    LRESULT copied;
    *out_text = NULL;
    deadline = monotonic_tick_ms() + TEST_UI_TIMEOUT_MS;

    for (;;) {
        len = SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0);
        if (len < 0) return FALSE;

        text = (wchar_t *)calloc((size_t)len + 1, sizeof(wchar_t));
        if (!text) return FALSE;

        copied = SendMessageW(hwnd, WM_GETTEXT, (WPARAM)(len + 1), (LPARAM)text);
        if (copied < 0) {
            free(text);
            return FALSE;
        }
        text[(size_t)len] = 0;

        if (len > 0 || copied > 0 || monotonic_tick_ms() > deadline) {
            *out_text = text;
            return TRUE;
        }

        free(text);
        pump_messages_briefly();
    }
}

static BOOL get_window_text_dup(HWND hwnd, wchar_t **out_text) {
    GUITHREADINFO info;
    DWORD tid;
    wchar_t *text = NULL;
    *out_text = NULL;

    if (!get_text_from_hwnd(hwnd, &text)) return FALSE;
    if (text && text[0] != 0) {
        *out_text = text;
        return TRUE;
    }

    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    tid = GetWindowThreadProcessId(hwnd, NULL);
    if (tid && GetGUIThreadInfo(tid, &info) && info.hwndFocus && info.hwndFocus != hwnd) {
        wchar_t *focus_text = NULL;
        if (!get_text_from_hwnd(info.hwndFocus, &focus_text)) {
            free(text);
            return FALSE;
        }
        if (focus_text && focus_text[0] != 0) {
            free(text);
            *out_text = focus_text;
            return TRUE;
        }
        free(focus_text);
    }

    *out_text = text;
    return TRUE;
}

static BOOL window_text_equals(HWND hwnd, const wchar_t *expected) {
    wchar_t *actual = NULL;
    BOOL ok = FALSE;
    if (!get_window_text_dup(hwnd, &actual)) return FALSE;
    ok = wcscmp(actual, expected) == 0;
    free(actual);
    return ok;
}

typedef struct WindowTextWait {
    HWND hwnd;
    const wchar_t *expected;
} WindowTextWait;

static BOOL window_text_matches(void *ctx) {
    WindowTextWait *wait = (WindowTextWait *)ctx;
    return window_text_equals(wait->hwnd, wait->expected);
}

static BOOL wait_for_window_text(HWND hwnd, const wchar_t *expected) {
    WindowTextWait wait;
    wait.hwnd = hwnd;
    wait.expected = expected;
    return wait_until_true(TEST_UI_TIMEOUT_MS, window_text_matches, &wait);
}

static void fail_text_mismatch(const char *test_name, HWND hwnd, const wchar_t *expected) {
    wchar_t *actual = NULL;
    if (!get_window_text_dup(hwnd, &actual)) {
        fail_message(test_name, "window text read failed");
        return;
    }
    if (!actual || actual[0] == 0) {
        wchar_t cls[128];
        GUITHREADINFO info;
        DWORD tid = GetWindowThreadProcessId(hwnd, NULL);
        cls[0] = 0;
        GetClassNameW(hwnd, cls, (int)COUNT_OF(cls));
        ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        if (tid && GetGUIThreadInfo(tid, &info)) {
            wchar_t fcls[128];
            fcls[0] = 0;
            if (info.hwndFocus) GetClassNameW(info.hwndFocus, fcls, (int)COUNT_OF(fcls));
            fwprintf(stderr, L"  debug: hwnd=%p class=%ls focus=%p focus_class=%ls\n",
                     hwnd, cls, info.hwndFocus, fcls);
        } else {
            fwprintf(stderr, L"  debug: hwnd=%p class=%ls focus=<unavailable>\n", hwnd, cls);
        }
    }
    fwprintf(stderr, L"FAIL %hs: text mismatch\n  expected: [%ls]\n  actual:   [%ls]\n", test_name, expected, actual);
    ++g_failures;
    free(actual);
}

static BOOL focus_app_window(HWND hwnd) {
    POINT pt;
    INPUT click[2];

    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    pt.x = 12;
    pt.y = 12;
    if (!ClientToScreen(hwnd, &pt)) return FALSE;
    SetCursorPos(pt.x, pt.y);
    click[0] = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
    click[1] = make_mouse_button(MOUSEEVENTF_LEFTUP);
    send_inputs(click, 2);
    return TRUE;
}

static BOOL client_to_screen_point(HWND hwnd, int x, int y, POINT *out) {
    POINT pt;
    pt.x = x;
    pt.y = y;
    if (!ClientToScreen(hwnd, &pt)) return FALSE;
    *out = pt;
    return TRUE;
}

static BOOL click_client(HWND hwnd, int x, int y, BOOL with_shift) {
    POINT pt;
    INPUT click[4];
    int n = 0;
    if (!client_to_screen_point(hwnd, x, y, &pt)) return FALSE;
    SetCursorPos(pt.x, pt.y);
    pump_messages_briefly();
    if (with_shift) click[n++] = make_key(VK_SHIFT, 0);
    click[n++] = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
    click[n++] = make_mouse_button(MOUSEEVENTF_LEFTUP);
    if (with_shift) click[n++] = make_key(VK_SHIFT, KEYEVENTF_KEYUP);
    send_inputs(click, (UINT)n);
    return TRUE;
}

static BOOL get_caret_screen_pos(HWND hwnd, POINT *out) {
    GUITHREADINFO info;
    DWORD thread_id;
    POINT pt;

    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    thread_id = GetWindowThreadProcessId(hwnd, NULL);
    if (!thread_id) return FALSE;
    if (!GetGUIThreadInfo(thread_id, &info)) return FALSE;
    if (info.hwndCaret != hwnd) return FALSE;
    pt.x = info.rcCaret.left;
    pt.y = info.rcCaret.top;
    if (!ClientToScreen(hwnd, &pt)) return FALSE;
    *out = pt;
    return TRUE;
}

static BOOL double_click_client(HWND hwnd, int x, int y) {
    POINT pt;
    INPUT click[4];
    if (!client_to_screen_point(hwnd, x, y, &pt)) return FALSE;
    SetCursorPos(pt.x, pt.y);
    pump_messages_briefly();
    click[0] = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
    click[1] = make_mouse_button(MOUSEEVENTF_LEFTUP);
    click[2] = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
    click[3] = make_mouse_button(MOUSEEVENTF_LEFTUP);
    send_inputs(click, 4);
    return TRUE;
}

static BOOL double_click_drag_client(HWND hwnd, int x1, int y1, int x2, int y2) {
    POINT start;
    POINT end;
    INPUT input;
    if (!client_to_screen_point(hwnd, x1, y1, &start)) return FALSE;
    if (!client_to_screen_point(hwnd, x2, y2, &end)) return FALSE;

    SetCursorPos(start.x, start.y);
    pump_messages_briefly();
    input = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
    send_inputs(&input, 1);
    input = make_mouse_button(MOUSEEVENTF_LEFTUP);
    send_inputs(&input, 1);

    input = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
    send_inputs(&input, 1);
    for (int i = 1; i <= 10; ++i) {
        POINT step = start;
        step.x = start.x + ((end.x - start.x) * i) / 10;
        step.y = start.y + ((end.y - start.y) * i) / 10;
        SetCursorPos(step.x, step.y);
        pump_messages_briefly();
    }
    input = make_mouse_button(MOUSEEVENTF_LEFTUP);
    send_inputs(&input, 1);
    return TRUE;
}

static BOOL drag_select_client(HWND hwnd, int x1, int y1, int x2, int y2) {
    POINT start;
    POINT end;
    INPUT input;
    if (!client_to_screen_point(hwnd, x1, y1, &start)) return FALSE;
    if (!client_to_screen_point(hwnd, x2, y2, &end)) return FALSE;

    SetCursorPos(start.x, start.y);
    pump_messages_briefly();
    input = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
    send_inputs(&input, 1);

    for (int i = 1; i <= 10; ++i) {
        POINT step = start;
        step.x = start.x + ((end.x - start.x) * i) / 10;
        step.y = start.y + ((end.y - start.y) * i) / 10;
        SetCursorPos(step.x, step.y);
        pump_messages_briefly();
    }

    input = make_mouse_button(MOUSEEVENTF_LEFTUP);
    send_inputs(&input, 1);
    return TRUE;
}

static BOOL drag_select_client_alt(HWND hwnd, int x1, int y1, int x2, int y2) {
    INPUT key;
    BOOL ok;

    key = make_key(VK_MENU, 0);
    send_inputs(&key, 1);
    ok = drag_select_client(hwnd, x1, y1, x2, y2);
    key = make_key(VK_MENU, KEYEVENTF_KEYUP);
    send_inputs(&key, 1);
    return ok;
}

static BOOL drag_vertical_scrollbar_to_bottom(HWND hwnd) {
    SCROLLBARINFO sbi;
    POINT start;
    POINT end;
    INPUT input;

    if (!focus_app_window(hwnd)) return FALSE;

    ZeroMemory(&sbi, sizeof(sbi));
    sbi.cbSize = sizeof(sbi);
    if (!GetScrollBarInfo(hwnd, OBJID_VSCROLL, &sbi)) return FALSE;

    start.x = (sbi.rcScrollBar.left + sbi.rcScrollBar.right) / 2;
    start.y = sbi.rcScrollBar.top + (sbi.xyThumbTop + sbi.xyThumbBottom) / 2;
    end.x = start.x;
    end.y = sbi.rcScrollBar.bottom - 2;

    g_last_drag.sbi = sbi;
    g_last_drag.start = start;
    g_last_drag.end = end;

    SetCursorPos(start.x, start.y);
    pump_messages_briefly();
    input = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
    send_inputs(&input, 1);

    for (int i = 1; i <= 14; ++i) {
        POINT step = start;
        step.y = start.y + ((end.y - start.y) * i) / 14;
        SetCursorPos(step.x, step.y);
        pump_messages_briefly();
    }

    input = make_mouse_button(MOUSEEVENTF_LEFTUP);
    send_inputs(&input, 1);
    return TRUE;
}

static BOOL drag_vertical_scrollbar_fraction(HWND hwnd, double fraction) {
    SCROLLBARINFO sbi;
    POINT start;
    POINT end;
    INPUT input;

    if (!focus_app_window(hwnd)) return FALSE;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;

    ZeroMemory(&sbi, sizeof(sbi));
    sbi.cbSize = sizeof(sbi);
    if (!GetScrollBarInfo(hwnd, OBJID_VSCROLL, &sbi)) return FALSE;

    start.x = (sbi.rcScrollBar.left + sbi.rcScrollBar.right) / 2;
    start.y = sbi.rcScrollBar.top + (sbi.xyThumbTop + sbi.xyThumbBottom) / 2;
    end.x = start.x;
    end.y = sbi.rcScrollBar.top + 2 + (int)((double)(sbi.rcScrollBar.bottom - sbi.rcScrollBar.top - 4) * fraction);

    SetCursorPos(start.x, start.y);
    pump_messages_briefly();
    input = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
    send_inputs(&input, 1);
    for (int i = 1; i <= 14; ++i) {
        POINT step = start;
        step.y = start.y + ((end.y - start.y) * i) / 14;
        SetCursorPos(step.x, step.y);
        pump_messages_briefly();
    }
    input = make_mouse_button(MOUSEEVENTF_LEFTUP);
    send_inputs(&input, 1);
    return TRUE;
}

static BOOL click_horizontal_scrollbar_track_right(HWND hwnd) {
    SCROLLBARINFO sbi;
    POINT click;
    INPUT input[2];

    if (!focus_app_window(hwnd)) return FALSE;
    ZeroMemory(&sbi, sizeof(sbi));
    sbi.cbSize = sizeof(sbi);
    if (!GetScrollBarInfo(hwnd, OBJID_HSCROLL, &sbi)) return FALSE;
    click.x = sbi.rcScrollBar.left + sbi.xyThumbBottom + 8;
    if (click.x > sbi.rcScrollBar.right - 2) click.x = sbi.rcScrollBar.right - 2;
    click.y = (sbi.rcScrollBar.top + sbi.rcScrollBar.bottom) / 2;
    SetCursorPos(click.x, click.y);
    pump_messages_briefly();
    input[0] = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
    input[1] = make_mouse_button(MOUSEEVENTF_LEFTUP);
    send_inputs(input, 2);
    return TRUE;
}

static BOOL drag_horizontal_scrollbar_to_right(HWND hwnd) {
    SCROLLBARINFO sbi;
    POINT start;
    POINT end;
    INPUT input;
    int arrow_w = GetSystemMetrics(SM_CXHSCROLL);

    if (!focus_app_window(hwnd)) return FALSE;
    ZeroMemory(&sbi, sizeof(sbi));
    sbi.cbSize = sizeof(sbi);
    if (!GetScrollBarInfo(hwnd, OBJID_HSCROLL, &sbi)) return FALSE;

    start.x = sbi.rcScrollBar.left + (sbi.xyThumbTop + sbi.xyThumbBottom) / 2;
    start.y = (sbi.rcScrollBar.top + sbi.rcScrollBar.bottom) / 2;
    end.x = sbi.rcScrollBar.right - arrow_w - 2;
    if (end.x < sbi.rcScrollBar.left + arrow_w + 2) end.x = sbi.rcScrollBar.right - 2;
    end.y = start.y;

    SetCursorPos(start.x, start.y);
    pump_messages_briefly();
    input = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
    send_inputs(&input, 1);
    for (int i = 1; i <= 14; ++i) {
        POINT step = start;
        step.x = start.x + ((end.x - start.x) * i) / 14;
        SetCursorPos(step.x, step.y);
        pump_messages_briefly();
    }
    input = make_mouse_button(MOUSEEVENTF_LEFTUP);
    send_inputs(&input, 1);
    return TRUE;
}

static BOOL create_tall_fixture(wchar_t *path, size_t path_count) {
    wchar_t temp_dir[MAX_PATH];
    HANDLE file;
    char line[64];
    DWORD written;

    if (!GetTempPathW((DWORD)COUNT_OF(temp_dir), temp_dir)) return FALSE;
    if (!GetTempFileNameW(temp_dir, L"txt", 0, path)) return FALSE;

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;

    for (int i = 0; i < 25000; ++i) {
        int len = snprintf(line, sizeof(line), "line %05d abcdefghijklmnopqrstuvwxyz\r\n", i);
        if (!WriteFile(file, line, (DWORD)len, &written, NULL) || written != (DWORD)len) {
            CloseHandle(file);
            return FALSE;
        }
    }

    CloseHandle(file);
    (void)path_count;
    return TRUE;
}

static BOOL create_text_fixture(wchar_t *path, size_t path_count, const char *utf8) {
    wchar_t temp_dir[MAX_PATH];
    HANDLE file;
    DWORD written;
    size_t len = strlen(utf8);

    if (!GetTempPathW((DWORD)COUNT_OF(temp_dir), temp_dir)) return FALSE;
    if (!GetTempFileNameW(temp_dir, L"txt", 0, path)) return FALSE;

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    if (!WriteFile(file, utf8, (DWORD)len, &written, NULL) || written != (DWORD)len) {
        CloseHandle(file);
        return FALSE;
    }
    CloseHandle(file);
    (void)path_count;
    return TRUE;
}

static BOOL launch_with_text_fixture(TestApp *app, wchar_t *fixture, size_t fixture_count, const char *test_name, const char *utf8) {
    ZeroMemory(fixture, fixture_count * sizeof(wchar_t));
    if (!create_text_fixture(fixture, fixture_count, utf8)) {
        fail_message(test_name, "could not create fixture file");
        return FALSE;
    }
    if (!launch_app_with_argument(app, fixture)) {
        DeleteFileW(fixture);
        fail_message(test_name, "could not launch app");
        return FALSE;
    }
    return TRUE;
}

static BOOL launch_with_tall_fixture(TestApp *app, wchar_t *fixture, size_t fixture_count, const char *test_name) {
    ZeroMemory(fixture, fixture_count * sizeof(wchar_t));
    if (!create_tall_fixture(fixture, fixture_count)) {
        fail_message(test_name, "could not create tall fixture file");
        return FALSE;
    }
    if (!launch_app_with_argument(app, fixture)) {
        DeleteFileW(fixture);
        fail_message(test_name, "could not launch app");
        return FALSE;
    }
    return TRUE;
}

typedef struct FileTextWait {
    const wchar_t *path;
    const char *expected;
} FileTextWait;

static BOOL file_text_matches(void *ctx) {
    FileTextWait *wait = (FileTextWait *)ctx;
    HANDLE file;
    LARGE_INTEGER size;
    DWORD read = 0;
    size_t expected_len = strlen(wait->expected);
    char *bytes;
    BOOL matches = FALSE;

    file = CreateFileW(wait->path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (uint64_t)size.QuadPart != (uint64_t)expected_len) {
        CloseHandle(file);
        return FALSE;
    }
    bytes = (char *)malloc(expected_len + 1);
    if (!bytes) {
        CloseHandle(file);
        return FALSE;
    }
    if (ReadFile(file, bytes, (DWORD)expected_len, &read, NULL) &&
        read == (DWORD)expected_len) {
        bytes[expected_len] = 0;
        matches = memcmp(bytes, wait->expected, expected_len) == 0;
    }
    free(bytes);
    CloseHandle(file);
    return matches;
}

static BOOL wait_for_file_text(const wchar_t *path, const char *expected) {
    FileTextWait wait;
    wait.path = path;
    wait.expected = expected;
    return wait_until_true(TEST_UI_TIMEOUT_MS * 2, file_text_matches, &wait);
}

static BOOL find_save_artifact(const wchar_t *path, wchar_t *found, size_t found_count) {
    WIN32_FIND_DATAW data;
    HANDLE search;
    wchar_t pattern[MAX_PATH + 96];
    const wchar_t *base;
    size_t base_len;

    if (found && found_count) found[0] = 0;
    if (swprintf_trunc(pattern, COUNT_OF(pattern), L"%ls*", path) < 0) return FALSE;
    base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    base_len = wcslen(base);
    search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) return FALSE;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            wcsncmp(data.cFileName, base, base_len) == 0 &&
            data.cFileName[base_len] != 0) {
            if (found && found_count)
                swprintf_trunc(found, found_count, L"%.*ls%ls", (int)(base - path), path,
                               data.cFileName);
            FindClose(search);
            return TRUE;
        }
    } while (FindNextFileW(search, &data));
    FindClose(search);
    return FALSE;
}

static void delete_save_artifacts(const wchar_t *path) {
    wchar_t artifact[MAX_PATH + 96];
    while (find_save_artifact(path, artifact, COUNT_OF(artifact))) {
        if (!DeleteFileW(artifact)) break;
    }
}

static const char g_selectissue_code_text[] =
    "reproduce this issue with a test case\r\n"
    "then fix and test till its all green\r\n\r\n"
    "typedef unsigned int Uint32;\r\n"
    "typedef int Sint32;\r\n\r\n"
    "struct Foo\r\n"
    "{\r\n"
    "\tUint32 first;\r\n"
    "\tSint32 second;\r\n"
    "};\r\n\r\n"
    "int main(void)\r\n"
    "{\r\n"
    "\tstruct Foo f;\r\n"
    "\tf.first = 1;\r\n"
    "\tf.second = -1;\r\n"
    "\treturn 0;\r\n"
    "}\r\n";

static BOOL load_focus_selectissue(TestApp *app, wchar_t *fixture, size_t fixture_count, const char *test_name) {
    if (!launch_with_text_fixture(app, fixture, fixture_count, test_name,
        "alpha beta gamma delta\r\n"
        "short\r\n"
        "    indented line with several words and punctuation, to stress line selection.\r\n"
        "tiny\r\n"
        "this is a much longer line than the others and it should stay selectable as a whole line.\r\n"
        "mid\r\n"
        "last line with spaces at the end    \r\n")) return FALSE;
    if (!focus_app_window(app->window)) {
        close_app(app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!wait_for_window_text(app->window,
        L"alpha beta gamma delta\r\nshort\r\n    indented line with several words and punctuation, to stress line selection.\r\ntiny\r\nthis is a much longer line than the others and it should stay selectable as a whole line.\r\nmid\r\nlast line with spaces at the end    \r\n")) {
        close_app(app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app->window,
            L"alpha beta gamma delta\r\nshort\r\n    indented line with several words and punctuation, to stress line selection.\r\ntiny\r\nthis is a much longer line than the others and it should stay selectable as a whole line.\r\nmid\r\nlast line with spaces at the end    \r\n");
        return FALSE;
    }
    return TRUE;
}

static BOOL test_selectissue_md_loads(void) {
    const char *test_name = "selectissue_md_loads";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!load_focus_selectissue(&app, fixture, COUNT_OF(fixture), test_name)) return FALSE;

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_loaded_tab_mouse_backspace_deletes_only_indent(void) {
    const char *test_name = "loaded_tab_mouse_backspace_deletes_only_indent";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    POINT line_start;
    POINT one_col;
    POINT tab_line_start;
    POINT click_pt;
    int char_w;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name,
        "ab\r\n"
        "\tUint32 first;\r\n"
        "Sint32 second;\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!wait_for_window_text(app.window, L"ab\r\n\tUint32 first;\r\nSint32 second;\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"ab\r\n\tUint32 first;\r\nSint32 second;\r\n");
        return FALSE;
    }

    send_key_press(VK_HOME);
    if (!get_caret_screen_pos(app.window, &line_start)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read initial caret position");
        return FALSE;
    }
    send_key_press(VK_RIGHT);
    if (!get_caret_screen_pos(app.window, &one_col)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read one-column caret position");
        return FALSE;
    }
    char_w = one_col.x - line_start.x;
    if (char_w <= 0) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "invalid measured character width");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    if (!get_caret_screen_pos(app.window, &tab_line_start)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read tab-line caret position");
        return FALSE;
    }

    click_pt.x = tab_line_start.x + (2 * char_w);
    click_pt.y = tab_line_start.y + 4;
    if (!ScreenToClient(app.window, &click_pt) ||
        !click_client(app.window, click_pt.x, click_pt.y, FALSE)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not click visual tab boundary");
        return FALSE;
    }
    send_key_press(VK_BACK);

    if (!wait_for_window_text(app.window, L"ab\r\nUint32 first;\r\nSint32 second;\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"ab\r\nUint32 first;\r\nSint32 second;\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_loaded_tab_mouse_drag_selects_rendered_text(void) {
    const char *test_name = "loaded_tab_mouse_drag_selects_rendered_text";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    POINT line_start;
    POINT one_col;
    POINT tab_line_start;
    POINT start_pt;
    POINT end_pt;
    int char_w;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name,
        "ab\r\n"
        "\tUint32 first;\r\n"
        "Sint32 second;\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!wait_for_window_text(app.window, L"ab\r\n\tUint32 first;\r\nSint32 second;\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"ab\r\n\tUint32 first;\r\nSint32 second;\r\n");
        return FALSE;
    }

    send_key_press(VK_HOME);
    if (!get_caret_screen_pos(app.window, &line_start)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read initial caret position");
        return FALSE;
    }
    send_key_press(VK_RIGHT);
    if (!get_caret_screen_pos(app.window, &one_col)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read one-column caret position");
        return FALSE;
    }
    char_w = one_col.x - line_start.x;
    if (char_w <= 0) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "invalid measured character width");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    if (!get_caret_screen_pos(app.window, &tab_line_start)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read tab-line caret position");
        return FALSE;
    }

    start_pt.x = tab_line_start.x + (2 * char_w);
    start_pt.y = tab_line_start.y + 4;
    end_pt.x = tab_line_start.x + (8 * char_w);
    end_pt.y = tab_line_start.y + 4;
    if (!ScreenToClient(app.window, &start_pt) ||
        !ScreenToClient(app.window, &end_pt) ||
        !drag_select_client(app.window, start_pt.x, start_pt.y, end_pt.x, end_pt.y)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not drag-select rendered tabbed text");
        return FALSE;
    }

    clear_clipboard();
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!wait_for_clipboard_text(L"Uint32")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"Uint32");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_launches_new_window_class(void) {
    const char *test_name = "launches_new_window_class";
    TestApp app;

    if (!launch_app(&app)) {
        fail_message(test_name, "could not launch app");
        return FALSE;
    }

    close_app(&app);
    return TRUE;
}

static BOOL test_ctrl_n_opens_fresh_new_window(void) {
    const char *test_name = "ctrl_n_opens_fresh_new_window";
    TestApp app;
    AppWindowSnapshot before;
    FindNewAppWindowState wait;

    if (!launch_app(&app)) {
        fail_message(test_name, "could not launch app");
        return FALSE;
    }
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    collect_app_windows(&before);
    ZeroMemory(&wait, sizeof(wait));
    wait.before = &before;
    send_modified_press(VK_CONTROL, 0, 'N');
    if (!wait_until_true(TEST_STARTUP_TIMEOUT_MS, new_app_window_exists, &wait)) {
        close_app(&app);
        fail_message(test_name, "Ctrl+N did not open a new app window");
        return FALSE;
    }

    if (!window_text_equals(wait.found, L"")) {
        fail_text_mismatch(test_name, wait.found, L"");
        close_window_by_hwnd(wait.found);
        close_app(&app);
        return FALSE;
    }

    close_window_by_hwnd(wait.found);
    close_app(&app);
    return TRUE;
}

static BOOL test_vertical_scrollbar_scrollable_for_tall_file(void) {
    const char *test_name = "vertical_scrollbar_scrollable_for_tall_file";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    SCROLLINFO si;

    if (!launch_with_tall_fixture(&app, fixture, COUNT_OF(fixture), test_name)) return FALSE;

    if (!get_vscroll(app.window, &si) || scroll_max_pos(&si) <= si.nMin) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "vertical scrollbar was not scrollable");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_vertical_scrollbar_release_at_end_is_100_percent(void) {
    const char *test_name = "vertical_scrollbar_release_at_end_is_100_percent";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    ScrollWait wait;
    SCROLLINFO before;

    if (!launch_with_tall_fixture(&app, fixture, COUNT_OF(fixture), test_name)) return FALSE;

    if (!get_vscroll(app.window, &before) || scroll_max_pos(&before) <= before.nMin) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "vertical scrollbar was not scrollable");
        return FALSE;
    }

    if (!drag_vertical_scrollbar_to_bottom(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not drag vertical scrollbar");
        return FALSE;
    }

    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, vscroll_is_at_end, &wait)) {
        char message[256];
        int max_pos = scroll_max_pos(&wait.si);
        snprintf(message, sizeof(message),
            "expected vertical scrollbar at 100%%, got pos=%d max=%d nMin=%d nMax=%d nPage=%u drag=%ld,%ld->%ld,%ld",
            wait.si.nPos, max_pos, wait.si.nMin, wait.si.nMax, wait.si.nPage,
            (long)g_last_drag.start.x, (long)g_last_drag.start.y,
            (long)g_last_drag.end.x, (long)g_last_drag.end.y);
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, message);
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_pagedown_moves_vertical_scroll(void) {
    const char *test_name = "pagedown_moves_vertical_scroll";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    ScrollWait wait;
    SCROLLINFO before;
    INPUT keys[2];

    if (!launch_with_tall_fixture(&app, fixture, COUNT_OF(fixture), test_name)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    if (!get_vscroll(app.window, &before)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read initial vertical scrollbar state");
        return FALSE;
    }

    keys[0] = make_key(VK_NEXT, 0);
    keys[1] = make_key(VK_NEXT, KEYEVENTF_KEYUP);
    send_inputs(keys, 2);

    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    wait.baseline = before.nPos;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, vscroll_moved_down, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "PageDown did not increase vertical scroll position");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_mousewheel_down_then_up_restores_scroll(void) {
    const char *test_name = "mousewheel_down_then_up_restores_scroll";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    ScrollWait wait;
    SCROLLINFO start;
    SCROLLINFO after_down;
    INPUT wheel;

    if (!launch_with_tall_fixture(&app, fixture, COUNT_OF(fixture), test_name)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    if (!get_vscroll(app.window, &start)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read initial vertical scrollbar state");
        return FALSE;
    }

    wheel = make_mouse_wheel(-WHEEL_DELTA * 4);
    send_inputs(&wheel, 1);

    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    wait.baseline = start.nPos;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, vscroll_moved_down, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "mouse wheel down did not increase vertical scroll position");
        return FALSE;
    }
    after_down = wait.si;

    wheel = make_mouse_wheel(WHEEL_DELTA * 4);
    send_inputs(&wheel, 1);

    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    wait.baseline = after_down.nPos;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, vscroll_moved_up_or_equal, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "mouse wheel up did not decrease vertical scroll position");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_mousewheel_scroll_keeps_caret_document_location(void) {
    const char *test_name = "mousewheel_scroll_keeps_caret_document_location";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    ScrollWait wait;
    SCROLLINFO before_scroll;
    POINT caret_before;
    POINT caret_after;
    wchar_t *line_before = NULL;
    wchar_t *line_after = NULL;
    INPUT wheel;

    if (!launch_with_tall_fixture(&app, fixture, COUNT_OF(fixture), test_name)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!get_vscroll(app.window, &before_scroll) || scroll_max_pos(&before_scroll) <= before_scroll.nMin) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "vertical scrollbar was not scrollable");
        return FALSE;
    }

    send_key_press(VK_HOME);
    for (int i = 0; i < 24; ++i) send_key_press(VK_DOWN);
    for (int i = 0; i < 10; ++i) send_key_press(VK_RIGHT);

    clear_clipboard();
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!get_clipboard_text_dup(&line_before) || !line_before || line_before[0] == 0) {
        close_app(&app);
        DeleteFileW(fixture);
        free(line_before);
        fail_message(test_name, "could not capture caret line before wheel scroll");
        return FALSE;
    }

    if (!get_caret_screen_pos(app.window, &caret_before)) {
        close_app(&app);
        DeleteFileW(fixture);
        free(line_before);
        fail_message(test_name, "could not read caret position before wheel scroll");
        return FALSE;
    }

    wheel = make_mouse_wheel(-WHEEL_DELTA);
    send_inputs(&wheel, 1);

    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    wait.baseline = before_scroll.nPos;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, vscroll_moved_down, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        free(line_before);
        fail_message(test_name, "mouse wheel down did not increase vertical scroll position");
        return FALSE;
    }

    clear_clipboard();
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!get_clipboard_text_dup(&line_after) || !line_after || line_after[0] == 0) {
        close_app(&app);
        DeleteFileW(fixture);
        free(line_before);
        free(line_after);
        fail_message(test_name, "could not capture caret line after wheel scroll");
        return FALSE;
    }
    if (wcscmp(line_before, line_after) != 0) {
        close_app(&app);
        DeleteFileW(fixture);
        free(line_before);
        free(line_after);
        fail_message(test_name, "caret moved to a different document line during wheel scroll");
        return FALSE;
    }

    if (!get_caret_screen_pos(app.window, &caret_after)) {
        close_app(&app);
        DeleteFileW(fixture);
        free(line_before);
        free(line_after);
        fail_message(test_name, "could not read caret position after wheel scroll");
        return FALSE;
    }
    if (caret_after.y >= caret_before.y) {
        close_app(&app);
        DeleteFileW(fixture);
        free(line_before);
        free(line_after);
        fail_message(test_name, "caret did not move upward on screen while wheel scrolled down");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    free(line_before);
    free(line_after);
    return TRUE;
}

static BOOL test_maximized_mousewheel_reaches_bottom_without_sticking(void) {
    const char *test_name = "maximized_mousewheel_reaches_bottom_without_sticking";
    wchar_t input_path[MAX_PATH];
    TestApp app;
    RECT rc;
    SCROLLINFO before;
    SCROLLINFO after;
    INPUT wheel;
    INPUT click[2];
    int advances = 0;
    int stalls = 0;
    int max_stall_run = 0;
    int stall_run = 0;
    BOOL reached_bottom = FALSE;

    if (!resolve_repo_test_file(input_path, COUNT_OF(input_path), L"paradym.md")) {
        fail_message(test_name, "could not resolve repo paradym.md path");
        return FALSE;
    }
    if (!launch_app_with_argument(&app, input_path)) {
        fail_message(test_name, "could not launch app");
        return FALSE;
    }
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    ShowWindow(app.window, SW_MAXIMIZE);
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, window_is_maximized, app.window)) {
        close_app(&app);
        fail_message(test_name, "window did not enter maximized state");
        return FALSE;
    }
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, vscroll_visible, app.window)) {
        close_app(&app);
        fail_message(test_name, "vertical scrollbar did not become visible");
        return FALSE;
    }
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, vscroll_scrollable, app.window)) {
        close_app(&app);
        fail_message(test_name, "vertical scrollbar was not scrollable");
        return FALSE;
    }
    if (!GetClientRect(app.window, &rc)) {
        close_app(&app);
        fail_message(test_name, "could not read maximized client rectangle");
        return FALSE;
    }

    {
        POINT center;
        center.x = (rc.left + rc.right) / 2;
        center.y = (rc.top + rc.bottom) / 2;
        if (!ClientToScreen(app.window, &center)) {
            close_app(&app);
            fail_message(test_name, "could not convert client center to screen");
            return FALSE;
        }
        SetCursorPos(center.x, center.y);
        click[0] = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
        click[1] = make_mouse_button(MOUSEEVENTF_LEFTUP);
        send_inputs(click, 2);
    }
    if (!get_vscroll(app.window, &before)) {
        close_app(&app);
        fail_message(test_name, "could not read initial vertical scrollbar state");
        return FALSE;
    }

    wheel = make_mouse_wheel(-WHEEL_DELTA * 4);
    for (int i = 0; i < 1200; ++i) {
        send_inputs(&wheel, 1);
        if (!get_vscroll(app.window, &after)) {
            close_app(&app);
            fail_message(test_name, "could not read vertical scrollbar after mouse wheel input");
            return FALSE;
        }

        if (after.nPos > before.nPos) {
            ++advances;
            stall_run = 0;
        } else if (before.nPos < scroll_max_pos(&before)) {
            ++stalls;
            ++stall_run;
            if (stall_run > max_stall_run) max_stall_run = stall_run;
        } else {
            reached_bottom = TRUE;
            break;
        }

        before = after;
        if (after.nPos >= scroll_max_pos(&after)) {
            reached_bottom = TRUE;
            break;
        }
    }

    if (!reached_bottom) {
        char message[256];
        int max_pos = scroll_max_pos(&after);
        snprintf(message, sizeof(message),
            "mouse wheel in maximized window did not reach bottom (pos=%d max=%d advances=%d stalls=%d max_stall_run=%d)",
            after.nPos, max_pos, advances, stalls, max_stall_run);
        close_app(&app);
        fail_message(test_name, message);
        return FALSE;
    }
    if (advances == 0) {
        close_app(&app);
        fail_message(test_name, "mouse wheel never advanced vertical scroll");
        return FALSE;
    }
    if (max_stall_run >= 80) {
        char message[256];
        snprintf(message, sizeof(message),
            "scroll stutter too long before bottom (advances=%d stalls=%d max_stall_run=%d)",
            advances, stalls, max_stall_run);
        close_app(&app);
        fail_message(test_name, message);
        return FALSE;
    }

    close_app(&app);
    return TRUE;
}

static BOOL test_ctrl_word_navigation_selection_copy(void) {
    const char *test_name = "ctrl_word_navigation_insert_points";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha beta\r\ngamma delta\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"alpha beta\r\ngamma delta\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"alpha beta\r\ngamma delta\r\n");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_key_press(VK_BACK);
    send_modified_press(VK_CONTROL, 0, VK_LEFT);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"lphabeta\r\ngamma delta\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"lphabeta\r\ngamma delta\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_shift_word_selection_copy(void) {
    const char *test_name = "ctrl_shift_word_selection_copy";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha beta gamma\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"alpha beta gamma\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"alpha beta gamma\r\n");
        return FALSE;
    }

    send_key_press(VK_END);
    send_modified_press(VK_CONTROL, VK_SHIFT, VK_LEFT);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"alpha beta \r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"alpha beta \r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_word_navigation_advances_horizontal_scroll(void) {
    const char *test_name = "ctrl_word_navigation_advances_horizontal_scroll";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    SCROLLINFO before;
    SCROLLINFO after;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name,
        "w001 w002 w003 w004 w005 w006 w007 w008 w009 w010 w011 w012 w013 w014 w015 w016 w017 w018 w019 w020 w021 w022 w023 w024 w025 w026 w027 w028 w029 w030 w031 w032 w033 w034 w035 w036 w037 w038 w039 w040 w041 w042 w043 w044 w045 w046 w047 w048 w049 w050 w051 w052 w053 w054 w055 w056 w057 w058 w059 w060 w061 w062 w063 w064 w065 w066 w067 w068 w069 w070 w071 w072 w073 w074 w075 w076 w077 w078 w079 w080 w081 w082 w083 w084 w085 w086 w087 w088 w089 w090 w091 w092 w093 w094 w095 w096 w097 w098 w099 w100 w101 w102 w103 w104 w105 w106 w107 w108 w109 w110 w111 w112 w113 w114 w115 w116 w117 w118 w119 w120\r\n")) {
        return FALSE;
    }
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!get_hscroll(app.window, &before)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read initial horizontal scrollbar state");
        return FALSE;
    }

    if (!drag_horizontal_scrollbar_to_right(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not drag horizontal scrollbar thumb");
        return FALSE;
    }

    if (!get_hscroll(app.window, &after)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read final horizontal scrollbar state");
        return FALSE;
    }
    if (after.nPos <= before.nPos) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "Ctrl+Right did not advance horizontal scroll");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_down_scroll_long_file_does_not_stick(void) {
    const char *test_name = "ctrl_down_scroll_long_file_does_not_stick";
    wchar_t input_path[MAX_PATH];
    TestApp app;
    SCROLLINFO before;
    SCROLLINFO step_before;
    SCROLLINFO step_after;
    POINT caret_before;
    POINT caret_after;
    int advances = 0;

    if (!resolve_repo_test_file(input_path, COUNT_OF(input_path), L"main.c")) {
        fail_message(test_name, "could not resolve repo main.c path");
        return FALSE;
    }
    if (!launch_app_with_argument(&app, input_path)) {
        fail_message(test_name, "could not launch app");
        return FALSE;
    }
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!wait_until_true(180000, vscroll_visible, app.window)) {
        close_app(&app);
        fail_message(test_name, "vertical scrollbar did not become visible");
        return FALSE;
    }
    if (!get_vscroll(app.window, &before) || scroll_max_pos(&before) <= before.nMin) {
        close_app(&app);
        fail_message(test_name, "vertical scrollbar was not scrollable");
        return FALSE;
    }

    /* Put caret away from bottom-row edge; Ctrl+Down should still scroll the viewport. */
    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_DOWN);
    send_key_press(VK_DOWN);
    send_key_press(VK_DOWN);
    if (!get_caret_screen_pos(app.window, &caret_before)) {
        close_app(&app);
        fail_message(test_name, "could not read caret position before Ctrl+Down");
        return FALSE;
    }

    for (int i = 0; i < 40; ++i) {
        if (!get_vscroll(app.window, &step_before)) {
            close_app(&app);
            fail_message(test_name, "could not read vertical scroll before Ctrl+Down");
            return FALSE;
        }
        send_modified_press(VK_CONTROL, 0, VK_DOWN);
        if (!get_vscroll(app.window, &step_after)) {
            close_app(&app);
            fail_message(test_name, "could not read vertical scroll after Ctrl+Down");
            return FALSE;
        }
        if (step_after.nPos > step_before.nPos) {
            ++advances;
        } else if (step_before.nPos < scroll_max_pos(&step_before)) {
            char message[256];
            snprintf(message, sizeof(message),
                "Ctrl+Down scroll got stuck before end at iter=%d pos=%d max=%d",
                i, step_before.nPos, scroll_max_pos(&step_before));
            close_app(&app);
            fail_message(test_name, message);
            return FALSE;
        }
        if (step_after.nPos >= scroll_max_pos(&step_after)) break;
    }

    if (advances == 0) {
        close_app(&app);
        fail_message(test_name, "Ctrl+Down never advanced vertical scroll position");
        return FALSE;
    }

    if (!get_caret_screen_pos(app.window, &caret_after)) {
        close_app(&app);
        fail_message(test_name, "could not read caret position after Ctrl+Down");
        return FALSE;
    }
    if (caret_after.y >= caret_before.y) {
        close_app(&app);
        fail_message(test_name, "caret did not get dragged toward viewport top while Ctrl+Down scrolling");
        return FALSE;
    }

    close_app(&app);
    return TRUE;
}

static BOOL test_ctrl_down_scroll_with_caret_at_top_does_not_stick(void) {
    const char *test_name = "ctrl_down_scroll_with_caret_at_top_does_not_stick";
    wchar_t input_path[MAX_PATH];
    TestApp app;
    SCROLLINFO step_before;
    SCROLLINFO step_after;
    int advances = 0;

    if (!resolve_repo_test_file(input_path, COUNT_OF(input_path), L"main.c")) {
        fail_message(test_name, "could not resolve repo main.c path");
        return FALSE;
    }
    if (!launch_app_with_argument(&app, input_path)) {
        fail_message(test_name, "could not launch app");
        return FALSE;
    }
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!wait_until_true(180000, vscroll_visible, app.window)) {
        close_app(&app);
        fail_message(test_name, "vertical scrollbar did not become visible");
        return FALSE;
    }
    ShowWindow(app.window, SW_MAXIMIZE);
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, window_is_maximized, app.window)) {
        close_app(&app);
        fail_message(test_name, "window did not enter maximized state");
        return FALSE;
    }

    /* Keep caret at the first visible row and verify scrolling still progresses. */
    send_key_press(VK_HOME);

    for (int i = 0; i < 80; ++i) {
        if (!get_vscroll(app.window, &step_before)) {
            close_app(&app);
            fail_message(test_name, "could not read vertical scroll before Ctrl+Down");
            return FALSE;
        }
        send_modified_press(VK_CONTROL, 0, VK_DOWN);
        if (!get_vscroll(app.window, &step_after)) {
            close_app(&app);
            fail_message(test_name, "could not read vertical scroll after Ctrl+Down");
            return FALSE;
        }
        if (step_after.nPos > step_before.nPos) {
            ++advances;
        } else if (step_before.nPos < scroll_max_pos(&step_before)) {
            char message[256];
            snprintf(message, sizeof(message),
                "Ctrl+Down stalled with caret at top (iter=%d pos=%d max=%d)",
                i, step_before.nPos, scroll_max_pos(&step_before));
            close_app(&app);
            fail_message(test_name, message);
            return FALSE;
        }
        if (step_after.nPos >= scroll_max_pos(&step_after)) break;
    }

    if (advances == 0) {
        close_app(&app);
        fail_message(test_name, "Ctrl+Down never advanced vertical scroll position");
        return FALSE;
    }

    close_app(&app);
    return TRUE;
}

static BOOL test_maximized_mousewheel_with_caret_at_top_does_not_stick(void) {
    const char *test_name = "maximized_mousewheel_with_caret_at_top_does_not_stick";
    wchar_t input_path[MAX_PATH];
    TestApp app;
    SCROLLINFO before;
    SCROLLINFO after;
    INPUT wheel;
    int advances = 0;
    int stall_run = 0;
    int max_stall_run = 0;
    BOOL reached_bottom = FALSE;

    if (!resolve_repo_test_file(input_path, COUNT_OF(input_path), L"main.c")) {
        fail_message(test_name, "could not resolve repo main.c path");
        return FALSE;
    }
    if (!launch_app_with_argument(&app, input_path)) {
        fail_message(test_name, "could not launch app");
        return FALSE;
    }
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    ShowWindow(app.window, SW_MAXIMIZE);
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, window_is_maximized, app.window)) {
        close_app(&app);
        fail_message(test_name, "window did not enter maximized state");
        return FALSE;
    }
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, vscroll_scrollable, app.window)) {
        close_app(&app);
        fail_message(test_name, "vertical scrollbar was not scrollable");
        return FALSE;
    }

    send_key_press(VK_HOME);

    if (!get_vscroll(app.window, &before)) {
        close_app(&app);
        fail_message(test_name, "could not read initial vertical scrollbar state");
        return FALSE;
    }

    wheel = make_mouse_wheel(-WHEEL_DELTA * 4);
    for (int i = 0; i < 800; ++i) {
        send_inputs(&wheel, 1);
        if (!get_vscroll(app.window, &after)) {
            close_app(&app);
            fail_message(test_name, "could not read vertical scrollbar after wheel input");
            return FALSE;
        }

        if (after.nPos > before.nPos) {
            ++advances;
            stall_run = 0;
        } else if (before.nPos < scroll_max_pos(&before)) {
            ++stall_run;
            if (stall_run > max_stall_run) max_stall_run = stall_run;
        } else {
            reached_bottom = TRUE;
            break;
        }

        before = after;
        if (after.nPos >= scroll_max_pos(&after)) {
            reached_bottom = TRUE;
            break;
        }
    }

    if (!reached_bottom) {
        char message[256];
        snprintf(message, sizeof(message),
                 "did not reach bottom while wheel scrolling from top-caret (adv=%d max_stall_run=%d pos=%d max=%d)",
                 advances, max_stall_run, before.nPos, scroll_max_pos(&before));
        close_app(&app);
        fail_message(test_name, message);
        return FALSE;
    }
    if (advances == 0) {
        close_app(&app);
        fail_message(test_name, "mouse wheel never advanced vertical scroll");
        return FALSE;
    }
    if (max_stall_run > 20) {
        char message[256];
        snprintf(message, sizeof(message),
                 "scroll stalled too long before bottom with caret at top (max_stall_run=%d)", max_stall_run);
        close_app(&app);
        fail_message(test_name, message);
        return FALSE;
    }

    close_app(&app);
    return TRUE;
}

static BOOL test_maximized_generated_large_file_top_wheel_moves_immediately(void) {
    const char *test_name = "maximized_generated_large_file_top_wheel_moves_immediately";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    SCROLLINFO before;
    SCROLLINFO after;
    INPUT wheel;
    RECT rc;
    INPUT click[2];

    if (!launch_with_tall_fixture(&app, fixture, COUNT_OF(fixture), test_name)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    ShowWindow(app.window, SW_MAXIMIZE);
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, window_is_maximized, app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "window did not enter maximized state");
        return FALSE;
    }
    if (!GetClientRect(app.window, &rc)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read client rectangle");
        return FALSE;
    }
    {
        POINT center;
        center.x = (rc.left + rc.right) / 2;
        center.y = (rc.top + rc.bottom) / 2;
        if (!ClientToScreen(app.window, &center)) {
            close_app(&app);
            DeleteFileW(fixture);
            fail_message(test_name, "could not convert center point");
            return FALSE;
        }
        SetCursorPos(center.x, center.y);
        click[0] = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
        click[1] = make_mouse_button(MOUSEEVENTF_LEFTUP);
        send_inputs(click, 2);
    }

    send_key_press(VK_HOME);
    if (!get_vscroll(app.window, &before)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read initial vertical scrollbar state");
        return FALSE;
    }

    wheel = make_mouse_wheel(-WHEEL_DELTA);
    send_inputs(&wheel, 1);
    Sleep(80);

    if (!get_vscroll(app.window, &after)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read vertical scrollbar after wheel");
        return FALSE;
    }
    if (scroll_max_pos(&after) > after.nMin && after.nPos <= before.nPos) {
        char message[256];
        snprintf(message, sizeof(message),
                 "wheel did not move from top on generated large file (before pos=%d max=%d, after pos=%d max=%d)",
                 before.nPos, scroll_max_pos(&before), after.nPos, scroll_max_pos(&after));
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, message);
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_maximized_paradym_top_wheel_moves_immediately(void) {
    const char *test_name = "maximized_paradym_top_wheel_moves_immediately";
    wchar_t input_path[MAX_PATH];
    TestApp app;
    SCROLLINFO before;
    SCROLLINFO after;
    INPUT wheel;
    RECT rc;
    INPUT click[2];

    if (!resolve_repo_test_file(input_path, COUNT_OF(input_path), L"paradym.md")) {
        fail_message(test_name, "could not resolve repo paradym.md path");
        return FALSE;
    }
    if (!launch_app_with_argument(&app, input_path)) {
        fail_message(test_name, "could not launch app");
        return FALSE;
    }
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    ShowWindow(app.window, SW_MAXIMIZE);
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, window_is_maximized, app.window)) {
        close_app(&app);
        fail_message(test_name, "window did not enter maximized state");
        return FALSE;
    }
    if (!GetClientRect(app.window, &rc)) {
        close_app(&app);
        fail_message(test_name, "could not read client rectangle");
        return FALSE;
    }
    {
        POINT center;
        center.x = (rc.left + rc.right) / 2;
        center.y = (rc.top + rc.bottom) / 2;
        if (!ClientToScreen(app.window, &center)) {
            close_app(&app);
            fail_message(test_name, "could not convert center point");
            return FALSE;
        }
        SetCursorPos(center.x, center.y);
        click[0] = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
        click[1] = make_mouse_button(MOUSEEVENTF_LEFTUP);
        send_inputs(click, 2);
    }

    send_key_press(VK_HOME);
    if (!get_vscroll(app.window, &before)) {
        close_app(&app);
        fail_message(test_name, "could not read initial vertical scrollbar state");
        return FALSE;
    }

    wheel = make_mouse_wheel(-WHEEL_DELTA);
    send_inputs(&wheel, 1);
    Sleep(80);

    if (!get_vscroll(app.window, &after)) {
        close_app(&app);
        fail_message(test_name, "could not read vertical scrollbar after wheel");
        return FALSE;
    }
    if (scroll_max_pos(&after) > after.nMin && after.nPos <= before.nPos) {
        char message[256];
        snprintf(message, sizeof(message),
                 "wheel did not move from top on paradym (before pos=%d max=%d, after pos=%d max=%d)",
                 before.nPos, scroll_max_pos(&before), after.nPos, scroll_max_pos(&after));
        close_app(&app);
        fail_message(test_name, message);
        return FALSE;
    }

    close_app(&app);
    return TRUE;
}

static BOOL test_vertical_scrollbar_drag_up_from_bottom_releases_stickiness(void) {
    const char *test_name = "vertical_scrollbar_drag_up_from_bottom_releases_stickiness";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    ScrollWait wait;

    if (!launch_with_tall_fixture(&app, fixture, COUNT_OF(fixture), test_name)) return FALSE;
    if (!drag_vertical_scrollbar_to_bottom(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not drag vertical scrollbar to bottom");
        return FALSE;
    }

    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, vscroll_is_at_end, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not reach end before drag-up check");
        return FALSE;
    }

    if (!drag_vertical_scrollbar_fraction(app.window, 0.60)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not drag vertical scrollbar up from bottom");
        return FALSE;
    }

    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, vscroll_not_at_end, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "dragging up from bottom left scrollbar pinned at end");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_horizontal_scrollbar_track_click_moves_right(void) {
    const char *test_name = "horizontal_scrollbar_track_click_moves_right";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    ScrollWait wait;
    SCROLLINFO before;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!get_hscroll(app.window, &before) || scroll_max_pos(&before) <= before.nMin) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "horizontal scrollbar was not scrollable");
        return FALSE;
    }
    if (!click_horizontal_scrollbar_track_right(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not click horizontal scrollbar track");
        return FALSE;
    }
    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    wait.baseline = before.nPos;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, hscroll_moved_right, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "track click did not move horizontal scrollbar right");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_horizontal_scrollbar_thumb_drag_moves_right(void) {
    const char *test_name = "horizontal_scrollbar_thumb_drag_moves_right";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    ScrollWait wait;
    SCROLLINFO before;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!get_hscroll(app.window, &before) || scroll_max_pos(&before) <= before.nMin) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "horizontal scrollbar was not scrollable");
        return FALSE;
    }
    if (!drag_horizontal_scrollbar_to_right(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not drag horizontal scrollbar thumb");
        return FALSE;
    }
    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    wait.baseline = before.nPos;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, hscroll_moved_right, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "thumb drag did not move horizontal scrollbar right");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_drag_selection_scrolls_horizontally_left(void) {
    const char *test_name = "drag_selection_scrolls_horizontally_left";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    SCROLLINFO before;
    SCROLLINFO right;
    ScrollWait wait;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name,
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!get_hscroll(app.window, &before) || scroll_max_pos(&before) <= before.nMin) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "horizontal scrollbar was not scrollable");
        return FALSE;
    }
    if (!drag_horizontal_scrollbar_to_right(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not move horizontal scrollbar right");
        return FALSE;
    }
    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    wait.baseline = before.nPos;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, hscroll_moved_right, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "horizontal scrollbar did not move right before drag-selection repro");
        return FALSE;
    }
    right = wait.si;

    if (!drag_select_client(app.window, 180, 8, -160, 8)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not drag select left");
        return FALSE;
    }
    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    wait.baseline = right.nPos;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, hscroll_moved_left, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "drag-selecting left did not move horizontal scrollbar left");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_horizontal_scrollbar_uses_widest_visible_line_not_caret_line(void) {
    const char *test_name = "horizontal_scrollbar_uses_widest_visible_line_not_caret_line";
    const char *fixture_text =
        "short\r\n"
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\r\n"
        "tiny\r\n";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    SCROLLINFO before;
    ScrollWait wait;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, fixture_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    /* Keep caret on a short line. Scrollability must still come from widest visible line. */
    send_key_press(VK_HOME);

    if (!get_hscroll(app.window, &before) || scroll_max_pos(&before) <= before.nMin) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "horizontal scrollbar did not account for widest visible line");
        return FALSE;
    }

    if (!click_horizontal_scrollbar_track_right(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not click horizontal scrollbar track");
        return FALSE;
    }
    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    wait.baseline = before.nPos;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, hscroll_moved_right, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "horizontal scrollbar did not move right");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_horizontal_scrollbar_with_caret_at_origin_moves_right(void) {
    const char *test_name = "horizontal_scrollbar_with_caret_at_origin_moves_right";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    ScrollWait wait;
    SCROLLINFO before;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name,
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    send_key_press(VK_HOME);
    if (!get_hscroll(app.window, &before) || scroll_max_pos(&before) <= before.nMin) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "horizontal scrollbar was not scrollable for generated long line");
        return FALSE;
    }
    if (!click_horizontal_scrollbar_track_right(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not click horizontal scrollbar track");
        return FALSE;
    }
    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    wait.baseline = before.nPos;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, hscroll_moved_right, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "horizontal scrollbar did not move right from origin caret state");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_right_symbols_are_separate_jumps(void) {
    const char *test_name = "ctrl_right_symbols_are_separate_jumps";
    const char *fixture_text = "test, test? test. test- test+ test* test/\r\ntail\r\n";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, fixture_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    send_key_press(VK_HOME);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"test test? test. test- test+ test* test/\r\ntail\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"test test? test. test- test+ test* test/\r\ntail\r\n");
        return FALSE;
    }
    close_app(&app);
    DeleteFileW(fixture);

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, fixture_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window (step 2)");
        return FALSE;
    }
    send_key_press(VK_HOME);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"test, est? test. test- test+ test* test/\r\ntail\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"test, est? test. test- test+ test* test/\r\ntail\r\n");
        return FALSE;
    }
    close_app(&app);
    DeleteFileW(fixture);

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, fixture_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window (step 3)");
        return FALSE;
    }
    send_key_press(VK_HOME);
    for (int i = 0; i < 14; ++i) {
        send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    }
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"test, test? test. test- test+ test* test/tail\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"test, test? test. test- test+ test* test/tail\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_basic_text_regressions(void) {
    const char *test_name = "basic_text_regressions";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one two\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"one two\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one two\r\n");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"one wo\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one wo\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_tab_inserts_two_spaces(void) {
    const char *test_name = "tab_inserts_two_spaces";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcd\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_unicode_char(L'a');
    send_key_press(VK_TAB);
    send_unicode_char(L'b');
    if (!window_text_equals(app.window, L"a  b")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"a  b");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_tab_mid_word_inserts_two_spaces_at_caret(void) {
    const char *test_name = "tab_mid_word_inserts_two_spaces_at_caret";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    POINT before = {0};
    POINT after = {0};

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcd\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    if (!wait_for_window_text(app.window,
        L"alpha beta gamma delta\r\nshort\r\n    indented line with several words and punctuation, to stress line selection.\r\ntiny\r\nthis is a much longer line than the others and it should stay selectable as a whole line.\r\nmid\r\nlast line with spaces at the end    \r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window,
            L"alpha beta gamma delta\r\nshort\r\n    indented line with several words and punctuation, to stress line selection.\r\ntiny\r\nthis is a much longer line than the others and it should stay selectable as a whole line.\r\nmid\r\nlast line with spaces at the end    \r\n");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    if (!get_caret_screen_pos(app.window, &before)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret position before tab");
        return FALSE;
    }
    send_key_press(VK_TAB);
    if (!window_text_equals(app.window, L"ab  cd\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"ab  cd\r\n");
        return FALSE;
    }
    if (!get_caret_screen_pos(app.window, &after)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret position after tab");
        return FALSE;
    }
    if (after.y != before.y) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "caret changed rows after mid-word tab");
        return FALSE;
    }
    if (after.x <= before.x) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "caret did not advance after mid-word tab");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_tab_with_single_line_selection_indents_selection_start(void) {
    const char *test_name = "tab_with_single_line_selection_indents_selection_start";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_SHIFT, 0, VK_RIGHT);
    send_modified_press(VK_SHIFT, 0, VK_RIGHT);
    send_key_press(VK_TAB);
    send_key_press(VK_RIGHT);
    send_key_press(VK_LEFT);
    if (!window_text_equals(app.window, L"  abcd\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"  abcd\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_tab_with_multiline_selection_indents_each_selected_line(void) {
    const char *test_name = "tab_with_multiline_selection_indents_each_selected_line";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo\r\nthree\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_key_press(VK_TAB);
    if (!window_text_equals(app.window, L"  one\r\n  two\r\nthree\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"  one\r\n  two\r\nthree\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_shift_tab_unindents_selected_lines_by_up_to_two_spaces(void) {
    const char *test_name = "shift_tab_unindents_selected_lines_by_up_to_two_spaces";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "  one\r\n two\r\nthree\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_TAB);
    if (!window_text_equals(app.window, L"one\r\ntwo\r\nthree\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one\r\ntwo\r\nthree\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_shift_tab_noop_when_selected_lines_have_no_leading_spaces(void) {
    const char *test_name = "shift_tab_noop_when_selected_lines_have_no_leading_spaces";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo\r\nthree\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_TAB);
    if (!window_text_equals(app.window, L"one\r\ntwo\r\nthree\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one\r\ntwo\r\nthree\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_shift_tab_after_tab_reverts_caret_only_indent(void) {
    const char *test_name = "shift_tab_after_tab_reverts_caret_only_indent";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcd\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    if (!wait_for_window_text(app.window, L"abcd\r\n")) {
        fail_text_mismatch(test_name, app.window, L"abcd\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }
    send_key_press(VK_HOME);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_key_press(VK_TAB);
    if (!wait_for_window_text(app.window, L"ab  cd\r\n")) {
        fail_text_mismatch(test_name, app.window, L"ab  cd\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }

    send_modified_press(VK_SHIFT, 0, VK_TAB);
    if (!wait_for_window_text(app.window, L"abcd\r\n")) {
        fail_text_mismatch(test_name, app.window, L"abcd\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_shift_tab_caret_line_unindents_by_two_spaces_per_press(void) {
    const char *test_name = "shift_tab_caret_line_unindents_by_two_spaces_per_press";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "    return x;\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    if (!wait_for_window_text(app.window, L"    return x;\r\n")) {
        fail_text_mismatch(test_name, app.window, L"    return x;\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }
    send_key_press(VK_END);
    send_modified_press(VK_CONTROL, 0, VK_LEFT);
    send_modified_press(VK_CONTROL, 0, VK_LEFT);
    send_modified_press(VK_CONTROL, 0, VK_LEFT);
    send_modified_press(VK_SHIFT, 0, VK_TAB);
    if (!wait_for_window_text(app.window, L"  return x;\r\n")) {
        fail_text_mismatch(test_name, app.window, L"  return x;\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }

    send_modified_press(VK_SHIFT, 0, VK_TAB);
    if (!wait_for_window_text(app.window, L"return x;\r\n")) {
        fail_text_mismatch(test_name, app.window, L"return x;\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_tab_multiline_selection_excludes_line_when_ending_at_its_start(void) {
    const char *test_name = "tab_multiline_selection_excludes_line_when_ending_at_its_start";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo\r\nthree\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_key_press(VK_TAB);
    send_key_press(VK_RIGHT);
    send_key_press(VK_LEFT);
    if (!window_text_equals(app.window, L"  one\r\n  two\r\n  three\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"  one\r\n  two\r\n  three\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_backspace_deletes_previous_word(void) {
    const char *test_name = "ctrl_backspace_deletes_previous_word";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one two three\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"one two three\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one two three\r\n");
        return FALSE;
    }

    send_key_press(VK_END);
    send_modified_press(VK_CONTROL, 0, VK_BACK);
    if (!window_text_equals(app.window, L"one two \r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one two \r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_delete_deletes_next_word(void) {
    const char *test_name = "ctrl_delete_deletes_next_word";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one two three\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"one two three\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one two three\r\n");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_CONTROL, 0, VK_DELETE);
    if (!window_text_equals(app.window, L"two three\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"two three\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_undo_redo_basic_edit_sequence(void) {
    const char *test_name = "undo_redo_basic_edit_sequence";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one two\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_END);
    send_key_press(VK_BACK);
    if (!window_text_equals(app.window, L"one tw\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one tw\r\n");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'Z');
    if (!window_text_equals(app.window, L"one two\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one two\r\n");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'Y');
    if (!window_text_equals(app.window, L"one tw\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one tw\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_undo_redo_cut_paste_sequence(void) {
    const char *test_name = "undo_redo_cut_paste_sequence";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one two\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_key_press(VK_HOME);
    send_modified_press(VK_CONTROL, VK_SHIFT, VK_RIGHT);
    send_modified_press(VK_CONTROL, 0, 'X');
    if (!window_text_equals(app.window, L"two\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"two\r\n");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'Z');
    if (!window_text_equals(app.window, L"one two\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one two\r\n");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'Y');
    if (!window_text_equals(app.window, L"two\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"two\r\n");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_CONTROL, 0, 'V');
    if (!window_text_equals(app.window, L"one two\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one two\r\n");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'Z');
    if (!window_text_equals(app.window, L"two\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"two\r\n");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'Y');
    if (!window_text_equals(app.window, L"one two\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one two\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_shift_multiline_select_copy(void) {
    const char *test_name = "shift_multiline_select_delete";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo\r\nthree\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"one\r\ntwo\r\nthree\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one\r\ntwo\r\nthree\r\n");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"three\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"three\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_shift_multiline_replace_typing(void) {
    const char *test_name = "shift_multiline_replace_typing";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo\r\nthree\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"one\r\ntwo\r\nthree\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one\r\ntwo\r\nthree\r\n");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_key_press(VK_BACK);
    if (!window_text_equals(app.window, L"three\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"three\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_shift_up_on_first_row_selects_to_start(void) {
    const char *test_name = "shift_up_on_first_row_selects_to_start";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha\r\nbeta")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_key_press(VK_HOME);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_SHIFT, 0, VK_UP);
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!wait_for_clipboard_text(L"alp")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"alp");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_shift_down_on_final_row_selects_to_end(void) {
    const char *test_name = "shift_down_on_final_row_selects_to_end";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha\r\nbeta")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!wait_for_clipboard_text(L"eta")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"eta");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_shift_down_insert_column(void) {
    const char *test_name = "alt_shift_down_insert_column";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcd\r\nefgh\r\nijkl\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_key_press('1');
    if (!window_text_equals(app.window, L"ab1cd\r\nef1gh\r\nijkl\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"ab1cd\r\nef1gh\r\nijkl\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_down_swaps_line_with_trailing_empty_final_line(void) {
    const char *test_name = "alt_down_swaps_line_with_trailing_empty_final_line";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "line 1\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    send_key_press('1');
    if (!window_text_equals(app.window, L"\r\nli1ne 1")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"\r\nli1ne 1");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_up_from_empty_last_line_swaps_with_previous_line(void) {
    const char *test_name = "alt_up_from_empty_last_line_swaps_with_previous_line";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "line 1\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, VK_END);
    send_modified_press(VK_MENU, 0, VK_UP);
    send_key_press('1');
    if (!window_text_equals(app.window, L"1\r\nline 1")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"1\r\nline 1");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_up_twice_from_last_text_line_keeps_lines_separate(void) {
    const char *test_name = "alt_up_twice_from_last_text_line_keeps_lines_separate";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo\r\nthree")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, VK_END);
    send_modified_press(VK_MENU, 0, VK_UP);
    send_modified_press(VK_MENU, 0, VK_UP);
    if (!window_text_equals(app.window, L"three\r\none\r\ntwo")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"three\r\none\r\ntwo");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_down_on_last_line_without_trailing_newline_is_noop(void) {
    const char *test_name = "alt_down_on_last_line_without_trailing_newline_is_noop";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, VK_END);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    send_key_press('1');
    if (!window_text_equals(app.window, L"one\r\ntwo1")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one\r\ntwo1");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_up_on_first_line_is_noop(void) {
    const char *test_name = "alt_up_on_first_line_is_noop";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_MENU, 0, VK_UP);
    send_key_press('1');
    if (!window_text_equals(app.window, L"1one\r\ntwo\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"1one\r\ntwo\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_down_moves_all_selected_lines_together(void) {
    const char *test_name = "alt_down_moves_all_selected_lines_together";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo\r\nthree\r\nfour")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    if (!window_text_equals(app.window, L"one\r\nfour\r\ntwo\r\nthree\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one\r\nfour\r\ntwo\r\nthree\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_down_multiline_selection_at_bottom_is_noop(void) {
    const char *test_name = "alt_down_multiline_selection_at_bottom_is_noop";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo\r\nthree\r\nfour\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    if (!window_text_equals(app.window, L"one\r\ntwo\r\nthree\r\nfour")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one\r\ntwo\r\nthree\r\nfour");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_type_number_lines_keeps_caret_logical_and_visual_sync(void) {
    const char *test_name = "type_number_lines_keeps_caret_logical_and_visual_sync";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    POINT caret = {0};
    POINT prev_caret = {0};
    int x_line8 = -1;
    int x_line9 = -1;
    int x_line10 = -1;
    int x_line11 = -1;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    for (int i = 1; i <= 12; ++i) {
        char digits[16];
        wchar_t wide_digits[16];
        wchar_t last_digit[2];
        int n = snprintf(digits, sizeof(digits), "%d", i);

        if (n <= 0 || n >= (int)sizeof(digits)) {
            close_app(&app);
            DeleteFileW(fixture);
            fail_message(test_name, "number formatting failed");
            return FALSE;
        }

        for (int j = 0; j < n; ++j) wide_digits[j] = (wchar_t)digits[j];
        wide_digits[n] = 0;
        if (!set_clipboard_text(wide_digits)) {
            close_app(&app);
            DeleteFileW(fixture);
            fail_message(test_name, "could not seed clipboard with typed number");
            return FALSE;
        }
        send_modified_press(VK_CONTROL, 0, 'V');

        clear_clipboard();
        send_modified_press(VK_SHIFT, 0, VK_LEFT);
        send_modified_press(VK_CONTROL, 0, 'C');
        last_digit[0] = (wchar_t)digits[n - 1];
        last_digit[1] = 0;
        if (!wait_for_clipboard_text(last_digit)) {
            close_app(&app);
            DeleteFileW(fixture);
            fail_clipboard_mismatch(test_name, last_digit);
            return FALSE;
        }
        send_key_press(VK_RIGHT);
        send_key_press(VK_RETURN);

        if (!get_caret_screen_pos(app.window, &caret)) {
            close_app(&app);
            DeleteFileW(fixture);
            fail_message(test_name, "could not read caret screen position");
            return FALSE;
        }
        if (i == 1) {
            prev_caret = caret;
        } else {
            if (caret.y <= prev_caret.y) {
                close_app(&app);
                DeleteFileW(fixture);
                fail_message(test_name, "caret did not move downward after Enter");
                return FALSE;
            }
            prev_caret = caret;
        }
        if (i == 8) x_line8 = caret.x;
        if (i == 9) x_line9 = caret.x;
        if (i == 10) x_line10 = caret.x;
        if (i == 11) x_line11 = caret.x;
    }

    if (x_line8 < 0 || x_line9 < 0 || x_line10 < 0 || x_line11 < 0) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "did not capture all caret x checkpoints");
        return FALSE;
    }
    if (!(x_line8 < x_line9 || x_line9 < x_line10 || x_line10 < x_line11)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "caret x never shifted right across the 9 to 10 transition window");
        return FALSE;
    }
    if (x_line11 < x_line10 || x_line10 < x_line9 || x_line9 < x_line8) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "caret x moved left during gutter growth window");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_enter_on_line9_does_not_place_caret_in_old_gutter(void) {
    const char *test_name = "enter_on_line9_does_not_place_caret_in_old_gutter";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    POINT after_enter = {0};
    POINT after_home = {0};

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name,
        "1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    for (int i = 0; i < 8; ++i) send_key_press(VK_DOWN);
    send_key_press(VK_END);
    send_key_press(VK_RETURN);

    if (!get_caret_screen_pos(app.window, &after_enter)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret position after Enter");
        return FALSE;
    }

    send_key_press(VK_HOME);
    if (!get_caret_screen_pos(app.window, &after_home)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret position after Home");
        return FALSE;
    }

    if (after_enter.x != after_home.x) {
        char message[160];
        snprintf(message, sizeof(message),
            "caret x mismatch after line9 Enter: enter=%ld home=%ld",
            (long)after_enter.x, (long)after_home.x);
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, message);
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_home_toggles_between_bol_and_indent(void) {
    const char *test_name = "home_toggles_between_bol_and_indent";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    POINT after_bol = {0};
    POINT after_indent = {0};

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "  test()")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_END);
    send_key_press(VK_HOME);
    if (!get_caret_screen_pos(app.window, &after_bol)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret position after first Home");
        return FALSE;
    }

    send_key_press(VK_HOME);
    if (!get_caret_screen_pos(app.window, &after_indent)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret position after second Home");
        return FALSE;
    }

    if (after_indent.x <= after_bol.x) {
        char message[160];
        snprintf(message, sizeof(message),
            "second Home did not move caret into indentation: bol=%ld indent=%ld",
            (long)after_bol.x, (long)after_indent.x);
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, message);
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_look_tall_caret_three_lines(void) {
    const char *test_name = "look_tall_caret_three_lines";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcd\r\nefgh\r\nijkl\r\nmnop\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    Sleep(120);
    if (!maybe_capture_look(app.window, test_name, "active")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not write screenshot");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL popup_window_exists(void *ctx) {
    TestApp *app = (TestApp *)ctx;
    return find_process_window_by_class(app->pi.dwProcessId, APP_COMMAND_POPUP_CLASS) != NULL;
}

static BOOL popup_window_closed(void *ctx) {
    TestApp *app = (TestApp *)ctx;
    return find_process_window_by_class(app->pi.dwProcessId, APP_COMMAND_POPUP_CLASS) == NULL;
}

static BOOL find_popup_window_exists(void *ctx) {
    TestApp *app = (TestApp *)ctx;
    return find_process_window_by_class(app->pi.dwProcessId, L"TextSuiteFindPopup") != NULL;
}

static BOOL app_window_is_foreground(void *ctx) {
    TestApp *app = (TestApp *)ctx;
    return GetForegroundWindow() == app->window;
}

static BOOL test_look_ctrl_d_command_popup(void) {
    const char *test_name = "look_ctrl_d_command_popup";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    HWND popup = NULL;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha\r\nbeta\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'D');
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, popup_window_exists, &app)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "Ctrl+D popup did not appear");
        return FALSE;
    }
    popup = find_process_window_by_class(app.pi.dwProcessId, APP_COMMAND_POPUP_CLASS);
    if (!popup) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not locate Ctrl+D popup window");
        return FALSE;
    }
    Sleep(120);
    if (!maybe_capture_look(popup, test_name, "active")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not write screenshot");
        return FALSE;
    }

    send_key_press(VK_ESCAPE);
    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_d_escape_restores_editor_focus(void) {
    const char *test_name = "ctrl_d_escape_restores_editor_focus";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    POINT before = {0};
    POINT after = {0};

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abc\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!get_caret_screen_pos(app.window, &before)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret before Ctrl+D");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'D');
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, popup_window_exists, &app)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "Ctrl+D popup did not appear");
        return FALSE;
    }
    send_key_press(VK_ESCAPE);
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, popup_window_closed, &app)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "Ctrl+D popup did not close on Escape");
        return FALSE;
    }
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, app_window_is_foreground, &app)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "main app window did not regain foreground focus");
        return FALSE;
    }

    send_key_press(VK_RIGHT);
    if (!get_caret_screen_pos(app.window, &after)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "caret unavailable after Escape from Ctrl+D");
        return FALSE;
    }
    if (after.x <= before.x) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "right key did not move caret after Escape from Ctrl+D");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_d_popup_ctrl_backspace_deletes_word_without_inserting_glyph(void) {
    const char *test_name = "ctrl_d_popup_ctrl_backspace_deletes_word_without_inserting_glyph";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    HWND popup = NULL;
    HWND edit = NULL;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'D');
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, popup_window_exists, &app)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "Ctrl+D popup did not appear");
        return FALSE;
    }
    popup = find_process_window_by_class(app.pi.dwProcessId, APP_COMMAND_POPUP_CLASS);
    edit = popup ? find_child_window_by_class(popup, L"Edit") : NULL;
    if (!edit) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not locate Ctrl+D popup edit");
        return FALSE;
    }

    send_key_press('A');
    send_key_press('L');
    send_key_press('P');
    send_key_press('H');
    send_key_press('A');
    send_key_press(VK_SPACE);
    send_key_press('B');
    send_key_press('E');
    send_key_press('T');
    send_key_press('A');
    send_modified_press(VK_CONTROL, 0, VK_BACK);

    if (!window_text_equals(edit, L"alpha ")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, edit, L"alpha ");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_d_popup_backspace_deletes_previous_character(void) {
    const char *test_name = "ctrl_d_popup_backspace_deletes_previous_character";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    HWND popup = NULL;
    HWND edit = NULL;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha beta\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'D');
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, popup_window_exists, &app)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "Ctrl+D popup did not appear");
        return FALSE;
    }
    popup = find_process_window_by_class(app.pi.dwProcessId, APP_COMMAND_POPUP_CLASS);
    edit = popup ? find_child_window_by_class(popup, L"Edit") : NULL;
    if (!edit) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not locate Ctrl+D popup edit");
        return FALSE;
    }

    send_key_press('A');
    send_key_press(VK_BACK);

    if (!window_text_equals(edit, L"")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, edit, L"");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_f_popup_ctrl_backspace_deletes_word_without_inserting_glyph(void) {
    const char *test_name = "ctrl_f_popup_ctrl_backspace_deletes_word_without_inserting_glyph";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    HWND popup = NULL;
    HWND edit = NULL;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha beta\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'F');
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, find_popup_window_exists, &app)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "Ctrl+F popup did not appear");
        return FALSE;
    }
    popup = find_process_window_by_class(app.pi.dwProcessId, L"TextSuiteFindPopup");
    edit = popup ? find_child_window_by_class(popup, L"Edit") : NULL;
    if (!edit) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not locate Ctrl+F popup edit");
        return FALSE;
    }

    send_unicode_char(L'a');
    send_unicode_char(L'l');
    send_unicode_char(L'p');
    send_unicode_char(L'h');
    send_unicode_char(L'a');
    send_unicode_char(L' ');
    send_unicode_char(L'b');
    send_unicode_char(L'e');
    send_unicode_char(L't');
    send_unicode_char(L'a');
    send_modified_press(VK_CONTROL, 0, VK_BACK);

    if (!window_text_equals(edit, L"alpha ")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, edit, L"alpha ");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_f_popup_backspace_deletes_previous_character(void) {
    const char *test_name = "ctrl_f_popup_backspace_deletes_previous_character";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    HWND popup = NULL;
    HWND edit = NULL;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha beta\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'F');
    if (!wait_until_true(TEST_UI_TIMEOUT_MS, find_popup_window_exists, &app)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "Ctrl+F popup did not appear");
        return FALSE;
    }
    popup = find_process_window_by_class(app.pi.dwProcessId, L"TextSuiteFindPopup");
    edit = popup ? find_child_window_by_class(popup, L"Edit") : NULL;
    if (!edit) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not locate Ctrl+F popup edit");
        return FALSE;
    }

    send_unicode_char(L'a');
    send_unicode_char(L'l');
    send_unicode_char(L'p');
    send_unicode_char(L'h');
    send_unicode_char(L'a');
    send_key_press(VK_BACK);

    if (!window_text_equals(edit, L"alph")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, edit, L"alph");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_shift_up_insert_column_pads_short_line(void) {
    const char *test_name = "alt_shift_up_insert_column_pads_short_line";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "ab\r\ncdef\r\nghij\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_UP);
    send_key_press('1');
    if (!window_text_equals(app.window, L"ab  1\r\ncdef1\r\nghij\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"ab  1\r\ncdef1\r\nghij\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_shift_up_delete_column(void) {
    const char *test_name = "alt_shift_up_delete_column";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcd\r\nefgh\r\nijkl\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_UP);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"abd\r\nefh\r\nijkl\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"abd\r\nefh\r\nijkl\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_shift_up_clear_keeps_caret_column(void) {
    const char *test_name = "alt_shift_up_clear_keeps_caret_column";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcd\r\nefgh\r\nijkl\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_UP);
    send_key_press(VK_RIGHT);
    send_key_press('1');
    if (!window_text_equals(app.window, L"abc1d\r\nefgh\r\nijkl\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"abc1d\r\nefgh\r\nijkl\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_shift_up_then_shift_right_delete_uses_current_caret_anchor(void) {
    const char *test_name = "alt_shift_up_then_shift_right_delete_uses_current_caret_anchor";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abc\r\ndef\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_UP);
    send_modified_press(VK_SHIFT, 0, VK_RIGHT);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"ac\r\ndef\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"ac\r\ndef\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_tab_mid_word_after_clearing_box_selection_inserts_at_caret(void) {
    const char *test_name = "tab_mid_word_after_clearing_box_selection_inserts_at_caret";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "asd\r\nasd\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_UP);
    send_key_press(VK_RIGHT);
    send_key_press(VK_TAB);
    if (!window_text_equals(app.window, L"as  d\r\nasd\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"as  d\r\nasd\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_shift_tab_after_clearing_box_selection_reverts_mid_word_tab(void) {
    const char *test_name = "shift_tab_after_clearing_box_selection_reverts_mid_word_tab";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "asd\r\nasd\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_UP);
    send_key_press(VK_RIGHT);
    send_key_press(VK_TAB);
    if (!wait_for_window_text(app.window, L"as  d\r\nasd\r\n")) {
        fail_text_mismatch(test_name, app.window, L"as  d\r\nasd\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }

    send_modified_press(VK_SHIFT, 0, VK_TAB);
    if (!wait_for_window_text(app.window, L"asd\r\nasd\r\n")) {
        fail_text_mismatch(test_name, app.window, L"asd\r\nasd\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_shift_tab_after_clearing_multiline_box_selection_reverts_mid_word_tab(void) {
    const char *test_name = "shift_tab_after_clearing_multiline_box_selection_reverts_mid_word_tab";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcd\r\nabcd\r\nabcd\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_UP);
    send_modified_press(VK_MENU, VK_SHIFT, VK_UP);
    send_key_press(VK_RIGHT);
    send_key_press(VK_TAB);
    if (!wait_for_window_text(app.window, L"ab  cd\r\nabcd\r\nabcd\r\n")) {
        fail_text_mismatch(test_name, app.window, L"ab  cd\r\nabcd\r\nabcd\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }

    send_modified_press(VK_SHIFT, 0, VK_TAB);
    if (!wait_for_window_text(app.window, L"abcd\r\nabcd\r\nabcd\r\n")) {
        fail_text_mismatch(test_name, app.window, L"abcd\r\nabcd\r\nabcd\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_tab_with_mid_word_box_selection_inserts_at_box_column(void) {
    const char *test_name = "tab_with_mid_word_box_selection_inserts_at_box_column";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "asd\r\nasd\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_UP);
    send_key_press(VK_TAB);
    if (!window_text_equals(app.window, L"a  sd\r\na  sd\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"a  sd\r\na  sd\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_shift_tab_with_mid_word_multiline_box_selection_removes_inserted_spaces(void) {
    const char *test_name = "shift_tab_with_mid_word_multiline_box_selection_removes_inserted_spaces";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcd\r\nabcd\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_UP);
    send_key_press(VK_TAB);
    if (!wait_for_window_text(app.window, L"a  bcd\r\na  bcd\r\n")) {
        fail_text_mismatch(test_name, app.window, L"a  bcd\r\na  bcd\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }

    send_modified_press(VK_SHIFT, 0, VK_TAB);
    if (!wait_for_window_text(app.window, L"abcd\r\nabcd\r\n")) {
        fail_text_mismatch(test_name, app.window, L"abcd\r\nabcd\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_shift_repeated_backspace_keeps_multiline_column_effect(void) {
    const char *test_name = "alt_shift_repeated_backspace_keeps_multiline_column_effect";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcd\r\nefgh\r\nijkl\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_UP);
    send_key_press(VK_BACK);
    send_key_press(VK_BACK);
    send_key_press('1');
    if (!window_text_equals(app.window, L"ab 1\r\nef 1\r\nijkl\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"ab 1\r\nef 1\r\nijkl\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_shift_repeated_backspace_at_column_zero_is_noop(void) {
    const char *test_name = "alt_shift_repeated_backspace_at_column_zero_is_noop";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abc\r\ndef\r\nghi\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_key_press(VK_BACK);
    send_key_press(VK_BACK);
    send_key_press(VK_BACK);
    send_key_press('1');
    if (!window_text_equals(app.window, L"1abc\r\n1def\r\n1ghi\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"1abc\r\n1def\r\n1ghi\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_shift_selection_alt_down_moves_selected_lines(void) {
    const char *test_name = "alt_shift_selection_alt_down_moves_selected_lines";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo\r\nthree\r\nfour")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    send_key_press('1');
    if (!window_text_equals(app.window, L"one\r\nfour\r\nt1wo\r\nt1hree")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one\r\nfour\r\nt1wo\r\nt1hree");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_shift_selection_repeated_alt_down_at_bottom_is_noop(void) {
    const char *test_name = "alt_shift_selection_repeated_alt_down_at_bottom_is_noop";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo\r\nthree\r\nfour\r\nfive")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    send_modified_press(VK_MENU, 0, VK_DOWN);
    send_key_press('1');
    if (!window_text_equals(app.window, L"one\r\nfour\r\nfive\r\nt1wo\r\nt1hree")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"one\r\nfour\r\nfive\r\nt1wo\r\nt1hree");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_large_file_scroll_end_insert_lines_updates_scrollbar(void) {
    const char *test_name = "large_file_scroll_end_insert_lines_updates_scrollbar";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    SCROLLINFO before;
    SCROLLINFO after;
    ScrollWait wait;
    POINT caret_before = {0};
    POINT caret_after = {0};

    if (!launch_with_tall_fixture(&app, fixture, COUNT_OF(fixture), test_name)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!wait_until_true(180000, vscroll_visible, app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "large-file line discovery did not finish in time");
        return FALSE;
    }
    if (!drag_vertical_scrollbar_to_bottom(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not drag vertical scrollbar to bottom");
        return FALSE;
    }
    ZeroMemory(&wait, sizeof(wait));
    wait.hwnd = app.window;
    if (!wait_until_true(TEST_UI_TIMEOUT_MS * 3, vscroll_is_at_end, &wait)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not reach end of large file");
        return FALSE;
    }
    if (!get_vscroll(app.window, &before)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read scrollbar state before typing");
        return FALSE;
    }
    if (!get_caret_screen_pos(app.window, &caret_before)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret position before typing");
        return FALSE;
    }

    send_key_press(VK_END);
    send_key_press(VK_RETURN);
    send_key_press(VK_RETURN);
    send_key_press(VK_RETURN);
    send_key_press('9');
    send_key_press('8');
    send_key_press('7');
    send_key_press('6');
    send_key_press('5');
    send_key_press('4');
    send_key_press('3');
    send_key_press('2');
    send_key_press('1');

    if (!get_vscroll(app.window, &after)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read scrollbar state after typing");
        return FALSE;
    }
    if (scroll_max_pos(&after) <= scroll_max_pos(&before)) {
        char message[256];
        snprintf(message, sizeof(message),
            "vertical scrollbar max did not grow after appending lines (before max=%d pos=%d nMax=%d nPage=%u, after max=%d pos=%d nMax=%d nPage=%u)",
            scroll_max_pos(&before), before.nPos, before.nMax, before.nPage,
            scroll_max_pos(&after), after.nPos, after.nMax, after.nPage);
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, message);
        return FALSE;
    }

    if (!get_caret_screen_pos(app.window, &caret_after)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret position after typing");
        return FALSE;
    }
    if (caret_after.y < caret_before.y) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "caret moved upward unexpectedly after appending lines");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, VK_SHIFT, VK_LEFT);
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!wait_for_clipboard_text(L"987654321")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"987654321");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_shift_multi_down_then_tab_indents_each_selected_line(void) {
    const char *test_name = "alt_shift_multi_down_then_tab_indents_each_selected_line";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "one\r\ntwo\r\nthree\r\nfour\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_key_press(VK_TAB);
    if (!window_text_equals(app.window, L"  one\r\n  two\r\n  three\r\n  four\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"  one\r\n  two\r\n  three\r\n  four\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_shift_multi_down_then_shift_tab_unindents_each_selected_line(void) {
    const char *test_name = "alt_shift_multi_down_then_shift_tab_unindents_each_selected_line";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "  one\r\n  two\r\n  three\r\n  four\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_TAB);
    if (!window_text_equals(app.window, L"one\r\ntwo\r\nthree\r\nfour\r\n")) {
        fail_text_mismatch(test_name, app.window, L"one\r\ntwo\r\nthree\r\nfour\r\n");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_box_cut_undo_redo_multi_line(void) {
    const char *test_name = "box_cut_undo_redo_multi_line";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcde\r\nfghij\r\nklmno\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_key_press(VK_HOME);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, VK_SHIFT, VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_RIGHT);
    send_modified_press(VK_CONTROL, 0, 'X');
    if (!wait_for_clipboard_text(L"bc\ngh\nlm")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"bc\ngh\nlm");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"ade\r\nfij\r\nkno\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"ade\r\nfij\r\nkno\r\n");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'Z');
    if (!window_text_equals(app.window, L"abcde\r\nfghij\r\nklmno\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"abcde\r\nfghij\r\nklmno\r\n");
        return FALSE;
    }
    send_modified_press(VK_CONTROL, 0, 'Y');
    if (!window_text_equals(app.window, L"ade\r\nfij\r\nkno\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"ade\r\nfij\r\nkno\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_box_cut_paste_preserves_multiline_shape(void) {
    const char *test_name = "box_cut_paste_preserves_multiline_shape";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcde\r\nfghij\r\nklmno\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_key_press(VK_HOME);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, VK_SHIFT, VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_RIGHT);
    send_modified_press(VK_CONTROL, 0, 'X');
    if (!wait_for_clipboard_text(L"bc\ngh\nlm")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"bc\ngh\nlm");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"ade\r\nfij\r\nkno\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"ade\r\nfij\r\nkno\r\n");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'V');
    if (!window_text_equals(app.window, L"abcde\r\nfghij\r\nklmno\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"abcde\r\nfghij\r\nklmno\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_c_ctrl_x_selection_clipboard(void) {
    const char *test_name = "ctrl_c_ctrl_x_selection_clipboard";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha beta\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_key_press(VK_HOME);
    send_modified_press(VK_CONTROL, VK_SHIFT, VK_RIGHT);
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!wait_for_clipboard_text(L"alpha ")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"alpha ");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'X');
    if (!wait_for_clipboard_text(L"alpha ")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"alpha ");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"beta\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"beta\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_mouse_drag_selection_copy(void) {
    const char *test_name = "mouse_drag_selection_copy";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcdefghij\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    if (!drag_select_client(app.window, 82, 8, 130, 8)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not drag-select text");
        return FALSE;
    }
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!wait_for_clipboard_text(L"abcde")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"abcde");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_shift_click_selection_cut(void) {
    const char *test_name = "shift_click_selection_cut";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcdefghij\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    if (!click_client(app.window, 82, 8, FALSE)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not click start point");
        return FALSE;
    }
    if (!click_client(app.window, 106, 8, TRUE)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not shift-click end point");
        return FALSE;
    }
    send_modified_press(VK_CONTROL, 0, 'X');
    if (!window_text_equals(app.window, L"cdefghij\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"cdefghij\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_double_click_word_selection_copy(void) {
    const char *test_name = "double_click_word_selection_copy";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha beta\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    if (!double_click_client(app.window, 28, 8)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not double-click target word");
        return FALSE;
    }
    send_key_press(VK_BACK);
    if (!window_text_equals(app.window, L" beta\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L" beta\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_c_no_selection_line_copy_paste(void) {
    const char *test_name = "ctrl_c_no_selection_line_copy_paste";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha\r\nbeta\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!wait_for_clipboard_text(L"beta\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"beta\r\n");
        return FALSE;
    }

    send_key_press(VK_END);
    send_modified_press(VK_CONTROL, 0, 'V');
    if (!window_text_equals(app.window, L"alpha\r\nbeta\r\nbeta\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"alpha\r\nbeta\r\nbeta\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_c_no_selection_single_line_copy_paste(void) {
    const char *test_name = "ctrl_c_no_selection_single_line_copy_paste";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!wait_for_clipboard_text(L"alpha\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"alpha\r\n");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'V');
    if (!window_text_equals(app.window, L"alpha\r\nalpha")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"alpha\r\nalpha");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_x_no_selection_line_cut_paste(void) {
    const char *test_name = "ctrl_x_no_selection_line_cut_paste";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha\r\nbeta\r\ngamma\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_modified_press(VK_CONTROL, 0, 'X');
    if (!wait_for_clipboard_text(L"beta\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"beta\r\n");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"alpha\r\ngamma\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"alpha\r\ngamma\r\n");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'V');
    if (!window_text_equals(app.window, L"alpha\r\nbeta\r\ngamma\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"alpha\r\nbeta\r\ngamma\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_selectissue_md_line10_copy(void) {
    const char *test_name = "selectissue_md_line10_copy";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, g_selectissue_code_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_key_press(VK_HOME);
    for (int i = 0; i < 9; ++i) send_key_press(VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_END);
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!wait_for_clipboard_text(L"\tSint32 second;")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"\tSint32 second;");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_selectissue_md_line10_shift_down_copy(void) {
    const char *test_name = "selectissue_md_line10_shift_down_copy";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, g_selectissue_code_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_key_press(VK_HOME);
    for (int i = 0; i < 9; ++i) send_key_press(VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!wait_for_clipboard_text(L"\tSint32 second;\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"\tSint32 second;\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_selectissue_md_select_lines_backspace(void) {
    const char *test_name = "selectissue_md_select_lines_backspace";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!load_focus_selectissue(&app, fixture, COUNT_OF(fixture), test_name)) return FALSE;
    send_key_press(VK_HOME);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_key_press(VK_BACK);
    if (!window_text_equals(app.window,
        L"short\r\n    indented line with several words and punctuation, to stress line selection.\r\ntiny\r\nthis is a much longer line than the others and it should stay selectable as a whole line.\r\nmid\r\nlast line with spaces at the end    \r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window,
            L"short\r\n    indented line with several words and punctuation, to stress line selection.\r\ntiny\r\nthis is a much longer line than the others and it should stay selectable as a whole line.\r\nmid\r\nlast line with spaces at the end    \r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_paste_selectissue_text_then_backspace_lines(void) {
    const char *test_name = "paste_selectissue_text_then_backspace_lines";
    TestApp app;

    if (!launch_app(&app)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    if (!set_clipboard_text(
        L"reproduce this issue with a test case\r\n"
        L"then fix and test till its all green\r\n\r\n"
        L"typedef unsigned int Uint32;\r\n"
        L"typedef int Sint32;\r\n\r\n"
        L"struct Foo\r\n"
        L"{\r\n"
        L"\tUint32 first;\r\n"
        L"\tSint32 second;\r\n"
        L"};\r\n\r\n"
        L"int main(void)\r\n"
        L"{\r\n"
        L"\tstruct Foo f;\r\n"
        L"\tf.first = 1;\r\n"
        L"\tf.second = -1;\r\n"
        L"\treturn 0;\r\n"
        L"}\r\n")) {
        close_app(&app);
        fail_message(test_name, "could not seed clipboard");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'V');
    if (!wait_for_window_text(app.window,
        L"reproduce this issue with a test case\r\nthen fix and test till its all green\r\n\r\ntypedef unsigned int Uint32;\r\ntypedef int Sint32;\r\n\r\nstruct Foo\r\n{\r\n\tUint32 first;\r\n\tSint32 second;\r\n};\r\n\r\nint main(void)\r\n{\r\n\tstruct Foo f;\r\n\tf.first = 1;\r\n\tf.second = -1;\r\n\treturn 0;\r\n}\r\n")) {
        close_app(&app);
        fail_text_mismatch(test_name, app.window,
            L"reproduce this issue with a test case\r\nthen fix and test till its all green\r\n\r\ntypedef unsigned int Uint32;\r\ntypedef int Sint32;\r\n\r\nstruct Foo\r\n{\r\n\tUint32 first;\r\n\tSint32 second;\r\n};\r\n\r\nint main(void)\r\n{\r\n\tstruct Foo f;\r\n\tf.first = 1;\r\n\tf.second = -1;\r\n\treturn 0;\r\n}\r\n");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_key_press(VK_BACK);
    if (!window_text_equals(app.window,
        L"then fix and test till its all green\r\n\r\ntypedef unsigned int Uint32;\r\ntypedef int Sint32;\r\n\r\nstruct Foo\r\n{\r\n\tUint32 first;\r\n\tSint32 second;\r\n};\r\n\r\nint main(void)\r\n{\r\n\tstruct Foo f;\r\n\tf.first = 1;\r\n\tf.second = -1;\r\n\treturn 0;\r\n}\r\n")) {
        close_app(&app);
        fail_text_mismatch(test_name, app.window,
            L"then fix and test till its all green\r\n\r\ntypedef unsigned int Uint32;\r\ntypedef int Sint32;\r\n\r\nstruct Foo\r\n{\r\n\tUint32 first;\r\n\tSint32 second;\r\n};\r\n\r\nint main(void)\r\n{\r\n\tstruct Foo f;\r\n\tf.first = 1;\r\n\tf.second = -1;\r\n\treturn 0;\r\n}\r\n");
        return FALSE;
    }

    close_app(&app);
    return TRUE;
}

static BOOL test_type_selectissue_text_then_backspace_lines(void) {
    const char *test_name = "type_selectissue_text_then_backspace_lines";
    TestApp app;

    if (!launch_app(&app)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_unicode_string(L"reproduce this issue with a test case\r\n"
                        L"then fix and test till its all green\r\n\r\n"
                        L"typedef unsigned int Uint32;\r\n"
                        L"typedef int Sint32;\r\n\r\n"
                        L"struct Foo\r\n"
                        L"{\r\n"
                        L"\tUint32 first;\r\n"
                        L"\tSint32 second;\r\n"
                        L"};\r\n\r\n"
                        L"int main(void)\r\n"
                        L"{\r\n"
                        L"\tstruct Foo f;\r\n"
                        L"\tf.first = 1;\r\n"
                        L"\tf.second = -1;\r\n"
                        L"\treturn 0;\r\n"
                        L"}\r\n");
    if (!wait_for_window_text(app.window,
        L"reproduce this issue with a test case\r\nthen fix and test till its all green\r\n\r\ntypedef unsigned int Uint32;\r\ntypedef int Sint32;\r\n\r\nstruct Foo\r\n{\r\n\tUint32 first;\r\n\tSint32 second;\r\n};\r\n\r\nint main(void)\r\n{\r\n\tstruct Foo f;\r\n\tf.first = 1;\r\n\tf.second = -1;\r\n\treturn 0;\r\n}\r\n")) {
        close_app(&app);
        fail_text_mismatch(test_name, app.window,
            L"reproduce this issue with a test case\r\nthen fix and test till its all green\r\n\r\ntypedef unsigned int Uint32;\r\ntypedef int Sint32;\r\n\r\nstruct Foo\r\n{\r\n\tUint32 first;\r\n\tSint32 second;\r\n};\r\n\r\nint main(void)\r\n{\r\n\tstruct Foo f;\r\n\tf.first = 1;\r\n\tf.second = -1;\r\n\treturn 0;\r\n}\r\n");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_key_press(VK_BACK);
    if (!window_text_equals(app.window,
        L"then fix and test till its all green\r\n\r\ntypedef unsigned int Uint32;\r\ntypedef int Sint32;\r\n\r\nstruct Foo\r\n{\r\n\tUint32 first;\r\n\tSint32 second;\r\n};\r\n\r\nint main(void)\r\n{\r\n\tstruct Foo f;\r\n\tf.first = 1;\r\n\tf.second = -1;\r\n\treturn 0;\r\n}\r\n")) {
        close_app(&app);
        fail_text_mismatch(test_name, app.window,
            L"then fix and test till its all green\r\n\r\ntypedef unsigned int Uint32;\r\ntypedef int Sint32;\r\n\r\nstruct Foo\r\n{\r\n\tUint32 first;\r\n\tSint32 second;\r\n};\r\n\r\nint main(void)\r\n{\r\n\tstruct Foo f;\r\n\tf.first = 1;\r\n\tf.second = -1;\r\n\treturn 0;\r\n}\r\n");
        return FALSE;
    }

    close_app(&app);
    return TRUE;
}

static BOOL test_selectissue_md_select_three_lines_backspace(void) {
    const char *test_name = "selectissue_md_select_three_lines_backspace";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!load_focus_selectissue(&app, fixture, COUNT_OF(fixture), test_name)) return FALSE;

    send_key_press(VK_HOME);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_modified_press(VK_SHIFT, 0, VK_DOWN);
    send_key_press(VK_BACK);
    if (!window_text_equals(app.window,
        L"tiny\r\nthis is a much longer line than the others and it should stay selectable as a whole line.\r\nmid\r\nlast line with spaces at the end    \r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window,
            L"tiny\r\nthis is a much longer line than the others and it should stay selectable as a whole line.\r\nmid\r\nlast line with spaces at the end    \r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_alt_mouse_drag_box_selection_cut(void) {
    const char *test_name = "alt_mouse_drag_box_selection_cut";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "abcde\r\nfghij\r\nklmno\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    if (!drag_select_client_alt(app.window, 82, 8, 106, 26)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not alt-drag select text");
        return FALSE;
    }
    send_modified_press(VK_CONTROL, 0, 'X');
    if (!wait_for_clipboard_text(L"de\nij")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"de\nij");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"abc\r\nfgh\r\nklmno\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"abc\r\nfgh\r\nklmno\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_right_line_end_before_next_line(void) {
    const char *test_name = "ctrl_right_line_end_before_next_line";
    const char *fixture_text = "cat mat sat\r\nbat hat rat\r\n";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, fixture_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"cat at sat\r\nbat hat rat\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"cat at sat\r\nbat hat rat\r\n");
        return FALSE;
    }
    close_app(&app);
    DeleteFileW(fixture);

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, fixture_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window (step 2)");
        return FALSE;
    }
    send_key_press(VK_HOME);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"cat mat at\r\nbat hat rat\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"cat mat at\r\nbat hat rat\r\n");
        return FALSE;
    }
    close_app(&app);
    DeleteFileW(fixture);

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, fixture_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window (step 3)");
        return FALSE;
    }
    send_key_press(VK_HOME);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"cat mat satbat hat rat\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"cat mat satbat hat rat\r\n");
        return FALSE;
    }
    close_app(&app);
    DeleteFileW(fixture);

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, fixture_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window (step 4)");
        return FALSE;
    }
    send_key_press(VK_HOME);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_modified_press(VK_CONTROL, 0, VK_RIGHT);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"cat mat sat\r\nat hat rat\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"cat mat sat\r\nat hat rat\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_left_line_end_before_previous_word(void) {
    const char *test_name = "ctrl_left_line_end_before_previous_word";
    const char *fixture_text = "cat mat sat\r\nbat hat rat\r\n";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, fixture_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_END);
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* bat hat |rat */
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* bat |hat rat */
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"cat mat sat\r\nbat at rat\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"cat mat sat\r\nbat at rat\r\n");
        return FALSE;
    }
    close_app(&app);
    DeleteFileW(fixture);

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, fixture_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window (step 2)");
        return FALSE;
    }
    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_END);
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* bat hat |rat */
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* bat |hat rat */
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* |bat hat rat */
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"cat mat sat\r\nat hat rat\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"cat mat sat\r\nat hat rat\r\n");
        return FALSE;
    }
    close_app(&app);
    DeleteFileW(fixture);

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, fixture_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window (step 3)");
        return FALSE;
    }
    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_END);
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* bat hat |rat */
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* bat |hat rat */
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* |bat hat rat */
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* cat mat sat| */
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"cat mat satbat hat rat\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"cat mat satbat hat rat\r\n");
        return FALSE;
    }
    close_app(&app);
    DeleteFileW(fixture);

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, fixture_text)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window (step 4)");
        return FALSE;
    }
    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_END);
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* bat hat |rat */
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* bat |hat rat */
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* |bat hat rat */
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* cat mat sat| */
    send_modified_press(VK_CONTROL, 0, VK_LEFT); /* cat mat |sat */
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"cat mat at\r\nbat hat rat\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"cat mat at\r\nbat hat rat\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_left_symbols_are_separate_jumps(void) {
    const char *test_name = "ctrl_left_symbols_are_separate_jumps";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "test, test?\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    send_key_press(VK_END);
    send_modified_press(VK_CONTROL, 0, VK_LEFT);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"test, test\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"test, test\r\n");
        return FALSE;
    }
    close_app(&app);
    DeleteFileW(fixture);

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "test, test?\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window (step 2)");
        return FALSE;
    }
    send_key_press(VK_END);
    send_modified_press(VK_CONTROL, 0, VK_LEFT);
    send_modified_press(VK_CONTROL, 0, VK_LEFT);
    send_key_press(VK_DELETE);
    if (!window_text_equals(app.window, L"test, est?\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"test, est?\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_find_search_selection_is_visibly_blue(void) {
    const char *test_name = "find_search_selection_is_visibly_blue";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "findme one findme\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    for (int i = 0; i < 6; ++i) send_modified_press(VK_SHIFT, 0, VK_RIGHT);
    send_modified_press(VK_CONTROL, 0, 'F');
    send_key_press(VK_F3);
    Sleep(120);
    if (!window_has_color(app.window, RGB(0, 120, 215), 8)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "selection highlight blue not visible after find");
        return FALSE;
    }
    if (!maybe_capture_look(app.window, test_name, "selected")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not write screenshot");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_ctrl_x_line_cut_keeps_caret_horizontal_position(void) {
    const char *test_name = "ctrl_x_line_cut_keeps_caret_horizontal_position";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    POINT before_cut_pt = {0};
    POINT after_cut_pt = {0};

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha\r\nbeta\r\ngamma\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    if (!get_caret_screen_pos(app.window, &before_cut_pt)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret position before cut");
        return FALSE;
    }
    send_modified_press(VK_CONTROL, 0, 'X');
    if (!get_caret_screen_pos(app.window, &after_cut_pt)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret position after cut");
        return FALSE;
    }
    if (after_cut_pt.x != before_cut_pt.x) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "caret x changed after line cut");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"alpha\r\ngamma\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"alpha\r\ngamma\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_select_all_cut_paste_keeps_caret_at_end_of_pasted_text(void) {
    const char *test_name = "select_all_cut_paste_keeps_caret_at_end_of_pasted_text";
    TestApp app;
    POINT expected = {0};
    POINT after = {0};

    if (!launch_app(&app)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_key_press('A');
    send_key_press('L');
    send_key_press('P');
    send_key_press('H');
    send_key_press('A');
    send_key_press(VK_RETURN);
    send_key_press('B');
    send_key_press('E');
    send_key_press('T');
    send_key_press('A');
    if (!get_caret_screen_pos(app.window, &expected)) {
        close_app(&app);
        fail_message(test_name, "could not read caret position after typing");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'A');
    send_modified_press(VK_CONTROL, 0, 'X');
    if (!set_clipboard_text(L"alpha\r\nbeta")) {
        close_app(&app);
        fail_message(test_name, "could not seed clipboard before paste");
        return FALSE;
    }
    send_modified_press(VK_CONTROL, 0, 'V');
    if (!get_caret_screen_pos(app.window, &after)) {
        close_app(&app);
        fail_message(test_name, "could not read caret position after cut/paste");
        return FALSE;
    }
    if (after.x != expected.x || after.y != expected.y) {
        close_app(&app);
        fail_message(test_name, "caret did not return to end of pasted two-line text");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"alpha\r\nbeta")) {
        close_app(&app);
        fail_text_mismatch(test_name, app.window, L"alpha\r\nbeta");
        return FALSE;
    }

    close_app(&app);
    return TRUE;
}

static BOOL test_ctrl_zoom_shortcuts_minus_plus_zero(void) {
    const char *test_name = "ctrl_zoom_shortcuts_minus_plus_zero";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    SCROLLINFO before;
    SCROLLINFO after_minus;
    SCROLLINFO after_plus;
    SCROLLINFO after_zero;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name,
        "1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9\r\n10\r\n11\r\n12\r\n13\r\n14\r\n15\r\n16\r\n17\r\n18\r\n19\r\n20\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }
    if (!get_vscroll(app.window, &before)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read initial vertical scroll info");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, VK_SUBTRACT);
    if (!get_vscroll(app.window, &after_minus)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read vertical scroll after ctrl-minus");
        return FALSE;
    }
    if ((int)after_minus.nPage <= (int)before.nPage) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "ctrl-minus did not increase visible rows");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, VK_ADD);
    if (!get_vscroll(app.window, &after_plus)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read vertical scroll after ctrl-plus");
        return FALSE;
    }
    if ((int)after_plus.nPage >= (int)after_minus.nPage) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "ctrl-plus did not decrease visible rows");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, '0');
    if (!get_vscroll(app.window, &after_zero)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read vertical scroll after ctrl-zero");
        return FALSE;
    }
    if ((int)after_zero.nPage != (int)before.nPage) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "ctrl-zero did not restore default zoom");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_double_click_drag_selects_whole_words(void) {
    const char *test_name = "double_click_drag_selects_whole_words";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha bravo charlie delta\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    if (!double_click_drag_client(app.window, 114, 8, 206, 8)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not double-click-drag selection");
        return FALSE;
    }
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!wait_for_clipboard_text(L"bravo charlie")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"bravo charlie");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_find_dialog_typing_does_not_crash(void) {
    const char *test_name = "find_dialog_typing_does_not_crash";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha beta\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'F');
    Sleep(80);
    send_key_press('A');
    send_key_press('B');
    send_key_press('C');
    Sleep(120);
    if (WaitForSingleObject(app.pi.hProcess, 0) != WAIT_TIMEOUT) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "app exited while typing in find dialog");
        return FALSE;
    }
    if (!find_main_window(app.pi.dwProcessId)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "main window missing after find typing");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_find_dialog_hover_and_findnext_work(void) {
    const char *test_name = "find_dialog_hover_and_findnext_work";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    HWND dlg = NULL;
    RECT rc;
    INPUT wheel;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha beta alpha\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'F');
    Sleep(120);
    dlg = find_process_dialog(app.pi.dwProcessId);
    if (!dlg) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not find find dialog");
        return FALSE;
    }
    if (!GetWindowRect(dlg, &rc)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not get find dialog rect");
        return FALSE;
    }
    SetCursorPos((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
    pump_messages_briefly();
    wheel = make_mouse_wheel(WHEEL_DELTA);
    send_inputs(&wheel, 1);
    send_key_press('A');
    send_key_press('L');
    send_key_press('P');
    send_key_press('H');
    send_key_press('A');
    send_key_press(VK_RETURN);
    if (WaitForSingleObject(app.pi.hProcess, 0) != WAIT_TIMEOUT) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "app exited during find hover/type/findnext");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_find_dialog_enter_triggers_findnext(void) {
    const char *test_name = "find_dialog_enter_triggers_findnext";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha beta alpha\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_modified_press(VK_CONTROL, 0, 'F');
    Sleep(100);
    send_key_press('A');
    send_key_press('L');
    send_key_press('P');
    send_key_press('H');
    send_key_press('A');
    send_key_press(VK_RETURN);
    Sleep(100);

    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not re-focus app window after find enter");
        return FALSE;
    }
    send_modified_press(VK_CONTROL, 0, 'C');
    if (!wait_for_clipboard_text(L"alpha")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_clipboard_mismatch(test_name, L"alpha");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_find_dialog_enter_twice_advances_to_next_match(void) {
    const char *test_name = "find_dialog_enter_twice_advances_to_next_match";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    HWND dlg = NULL;
    HWND find_button = NULL;
    RECT rc_button;
    RECT rc_dlg;
    INPUT click[2];
    int y_before = -1;
    int y_after = -1;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name,
                                  "first alpha line\r\nsecond alpha line\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    clear_clipboard();
    send_modified_press(VK_CONTROL, 0, 'F');
    Sleep(100);
    send_key_press('A');
    send_key_press('L');
    send_key_press('P');
    send_key_press('H');
    send_key_press('A');
    Sleep(120);
    dlg = find_process_dialog(app.pi.dwProcessId);
    if (!dlg) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not find find dialog");
        return FALSE;
    }
    find_button = GetDlgItem(dlg, IDOK);
    if (!find_button || !GetWindowRect(find_button, &rc_button)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not locate find button");
        return FALSE;
    }
    SetCursorPos((rc_button.left + rc_button.right) / 2, (rc_button.top + rc_button.bottom) / 2);
    click[0] = make_mouse_button(MOUSEEVENTF_LEFTDOWN);
    click[1] = make_mouse_button(MOUSEEVENTF_LEFTUP);
    send_inputs(click, 2);
    Sleep(120);
    if (!find_color_scanline(app.window, RGB(0, 120, 215), 12, &y_before)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not detect selection highlight after first find");
        return FALSE;
    }

    if (!GetWindowRect(dlg, &rc_dlg)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not locate find dialog bounds");
        return FALSE;
    }
    SetCursorPos((rc_dlg.left + rc_dlg.right) / 2, (rc_dlg.top + rc_dlg.bottom) / 2);
    send_inputs(click, 2);
    send_key_press(VK_RETURN);
    Sleep(120);
    if (!find_color_scanline(app.window, RGB(0, 120, 215), 12, &y_after)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not detect selection highlight after second find");
        return FALSE;
    }
    if (y_before == y_after) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "second Enter did not move selection to a different match");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_find_dialog_escape_closes_window(void) {
    const char *test_name = "find_dialog_escape_closes_window";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    HWND dlg;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha beta\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'F');
    Sleep(120);
    dlg = find_process_dialog(app.pi.dwProcessId);
    if (!dlg) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not find find dialog");
        return FALSE;
    }
    if (!focus_app_window(dlg)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus find dialog");
        return FALSE;
    }

    send_key_press(VK_ESCAPE);
    Sleep(150);
    if (find_process_dialog(app.pi.dwProcessId)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "find dialog still open after escape");
        return FALSE;
    }
    if (WaitForSingleObject(app.pi.hProcess, 0) != WAIT_TIMEOUT) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "app exited after escape on find dialog");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_save_prompt_close_button_cancels_close(void) {
    const char *test_name = "save_prompt_close_button_cancels_close";
    TestApp app;
    HWND prompt;

    if (!launch_app(&app)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press('A');
    PostMessageW(app.window, WM_CLOSE, 0, 0);
    Sleep(150);
    prompt = find_any_process_dialog(app.pi.dwProcessId);
    if (!prompt) {
        close_app(&app);
        fail_message(test_name, "save prompt not shown");
        return FALSE;
    }

    SendMessageW(prompt, WM_CLOSE, 0, 0);
    Sleep(150);
    if (find_any_process_dialog(app.pi.dwProcessId)) {
        close_app(&app);
        fail_message(test_name, "save prompt still visible after close button");
        return FALSE;
    }
    if (WaitForSingleObject(app.pi.hProcess, 0) != WAIT_TIMEOUT) {
        close_app(&app);
        fail_message(test_name, "app closed after save prompt close button");
        return FALSE;
    }

    PostMessageW(app.window, WM_CLOSE, 0, 0);
    Sleep(120);
    prompt = find_any_process_dialog(app.pi.dwProcessId);
    if (prompt) SendMessageW(prompt, WM_COMMAND, MAKEWPARAM(IDNO, BN_CLICKED), 0);
    if (app.pi.hProcess) WaitForSingleObject(app.pi.hProcess, TEST_PROCESS_EXIT_TIMEOUT_MS);
    if (app.pi.hThread) CloseHandle(app.pi.hThread);
    if (app.pi.hProcess) CloseHandle(app.pi.hProcess);
    ZeroMemory(&app, sizeof(app));
    return TRUE;
}

static BOOL test_alt_f4_closes_clean_window(void) {
    const char *test_name = "alt_f4_closes_clean_window";
    TestApp app;

    if (!launch_app(&app)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_MENU, 0, VK_F4);
    Sleep(200);
    if (WaitForSingleObject(app.pi.hProcess, 0) == WAIT_TIMEOUT) {
        close_app(&app);
        fail_message(test_name, "app did not close on alt+f4");
        return FALSE;
    }

    if (app.pi.hThread) CloseHandle(app.pi.hThread);
    if (app.pi.hProcess) CloseHandle(app.pi.hProcess);
    ZeroMemory(&app, sizeof(app));
    return TRUE;
}

static BOOL test_escape_closes_clean_window(void) {
    const char *test_name = "escape_closes_clean_window";
    TestApp app;

    if (!launch_app(&app)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_ESCAPE);
    Sleep(200);
    if (WaitForSingleObject(app.pi.hProcess, 0) == WAIT_TIMEOUT) {
        close_app(&app);
        fail_message(test_name, "app did not close on escape");
        return FALSE;
    }

    if (app.pi.hThread) CloseHandle(app.pi.hThread);
    if (app.pi.hProcess) CloseHandle(app.pi.hProcess);
    ZeroMemory(&app, sizeof(app));
    return TRUE;
}

static BOOL test_close_untitled_empty_dirty_skips_save_prompt(void) {
    const char *test_name = "close_untitled_empty_dirty_skips_save_prompt";
    TestApp app;

    if (!launch_app(&app)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press('A');
    send_key_press(VK_BACK);
    PostMessageW(app.window, WM_CLOSE, 0, 0);
    Sleep(150);

    if (WaitForSingleObject(app.pi.hProcess, 0) == WAIT_TIMEOUT) {
        HWND dialog = find_any_process_dialog(app.pi.dwProcessId);
        if (dialog) {
            close_app(&app);
            fail_message(test_name, "save prompt shown for empty untitled document");
            return FALSE;
        }
        close_app(&app);
        fail_message(test_name, "app did not close for empty untitled document");
        return FALSE;
    }

    if (app.pi.hThread) CloseHandle(app.pi.hThread);
    if (app.pi.hProcess) CloseHandle(app.pi.hProcess);
    ZeroMemory(&app, sizeof(app));
    return TRUE;
}

static BOOL test_untitled_empty_dirty_hides_star_title(void) {
    const char *test_name = "untitled_empty_dirty_hides_star_title";
    TestApp app;
    wchar_t *title = NULL;

    if (!launch_app(&app)) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press('A');
    send_key_press(VK_BACK);
    if (!get_window_text_dup(app.window, &title)) {
        close_app(&app);
        fail_message(test_name, "could not read window title");
        return FALSE;
    }
    if (wcsncmp(title, L"* ", 2) == 0) {
        free(title);
        close_app(&app);
        fail_message(test_name, "title still shows dirty star for empty untitled document");
        return FALSE;
    }
    free(title);
    close_app(&app);
    return TRUE;
}

static BOOL test_ctrl_end_jumps_to_last_char_on_last_line(void) {
    const char *test_name = "ctrl_end_jumps_to_last_char_on_last_line";
    wchar_t fixture[MAX_PATH];
    TestApp app;

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha\r\nbeta\r\ngamma")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, VK_HOME);
    send_modified_press(VK_CONTROL, 0, VK_END);
    send_key_press(VK_BACK);
    if (!window_text_equals(app.window, L"alpha\r\nbeta\r\ngamm")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"alpha\r\nbeta\r\ngamm");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_line_paste_keeps_caret_horizontal_position(void) {
    const char *test_name = "line_paste_keeps_caret_horizontal_position";
    wchar_t fixture[MAX_PATH];
    TestApp app;
    POINT before = {0};
    POINT after = {0};

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "alpha\r\nbeta\r\n")) return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_key_press(VK_HOME);
    send_key_press(VK_DOWN);
    send_key_press(VK_RIGHT);
    send_key_press(VK_RIGHT);
    if (!get_caret_screen_pos(app.window, &before)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret position before paste");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, 'C');
    send_modified_press(VK_CONTROL, 0, 'V');
    if (!get_caret_screen_pos(app.window, &after)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not read caret position after paste");
        return FALSE;
    }
    if (before.x != after.x) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "caret x changed after line paste");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"alpha\r\nbeta\r\nbeta\r\n")) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, app.window, L"alpha\r\nbeta\r\nbeta\r\n");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_duplicate_open_clean_peer_reloads_after_save(void) {
    const char *test_name = "duplicate_open_clean_peer_reloads_after_save";
    TestApp first;
    TestApp second;
    wchar_t fixture[MAX_PATH];
    HWND prompt;

    ZeroMemory(&first, sizeof(first));
    ZeroMemory(&second, sizeof(second));
    if (!create_text_fixture(fixture, COUNT_OF(fixture), "base")) {
        fail_message(test_name, "could not create fixture");
        return FALSE;
    }
    if (!launch_app_with_argument(&first, fixture) ||
        !launch_app_with_argument(&second, fixture)) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "could not open the same file in two processes");
        return FALSE;
    }
    if (!window_text_equals(first.window, L"base") ||
        !window_text_equals(second.window, L"base")) {
        debug_print_process_dialog(first.pi.dwProcessId);
        debug_print_process_dialog(second.pi.dwProcessId);
        fail_message(test_name, "one duplicate editor did not load the initial revision");
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        return FALSE;
    }

    if (!focus_app_window(first.window)) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus first editor");
        return FALSE;
    }
    send_modified_press(VK_CONTROL, 0, VK_END);
    send_key_press('A');
    if (!wait_for_window_text(first.window, L"basea")) {
        debug_print_process_dialog(first.pi.dwProcessId);
        fail_text_mismatch(test_name, first.window, L"basea");
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        return FALSE;
    }
    send_modified_press(VK_CONTROL, 0, 'S');
    if (!wait_for_file_text(fixture, "basea")) {
        debug_print_process_dialog(first.pi.dwProcessId);
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "first editor did not atomically save its revision");
        return FALSE;
    }

    focus_app_window(second.window);
    prompt = wait_for_any_process_dialog(second.pi.dwProcessId);
    if (!prompt) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "clean peer did not offer to reload the new revision");
        return FALSE;
    }
    send_dialog_key(prompt, VK_RETURN);
    if (!wait_for_window_text(second.window, L"basea")) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, second.window, L"basea");
        return FALSE;
    }

    close_app(&first);
    close_app(&second);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_duplicate_open_dirty_peer_requires_explicit_overwrite(void) {
    const char *test_name = "duplicate_open_dirty_peer_requires_explicit_overwrite";
    TestApp first;
    TestApp second;
    wchar_t fixture[MAX_PATH];
    HWND prompt;

    ZeroMemory(&first, sizeof(first));
    ZeroMemory(&second, sizeof(second));
    if (!create_text_fixture(fixture, COUNT_OF(fixture), "base")) {
        fail_message(test_name, "could not create fixture");
        return FALSE;
    }
    if (!launch_app_with_argument(&first, fixture) ||
        !launch_app_with_argument(&second, fixture)) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "could not open the same file in two processes");
        return FALSE;
    }
    if (!window_text_equals(first.window, L"base") ||
        !window_text_equals(second.window, L"base")) {
        debug_print_process_dialog(first.pi.dwProcessId);
        debug_print_process_dialog(second.pi.dwProcessId);
        fail_message(test_name, "one duplicate editor did not load the initial revision");
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        return FALSE;
    }

    focus_app_window(second.window);
    send_modified_press(VK_CONTROL, 0, VK_END);
    send_key_press('B');
    focus_app_window(first.window);
    send_modified_press(VK_CONTROL, 0, VK_END);
    send_key_press('A');
    if (!wait_for_window_text(first.window, L"basea")) {
        debug_print_process_dialog(first.pi.dwProcessId);
        fail_text_mismatch(test_name, first.window, L"basea");
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        return FALSE;
    }
    send_modified_press(VK_CONTROL, 0, 'S');
    if (!wait_for_file_text(fixture, "basea")) {
        debug_print_process_dialog(first.pi.dwProcessId);
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "first editor save was not visible on disk");
        return FALSE;
    }

    focus_app_window(second.window);
    prompt = wait_for_any_process_dialog(second.pi.dwProcessId);
    if (!prompt) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "dirty peer did not warn about the new disk revision");
        return FALSE;
    }
    send_dialog_key(prompt, VK_RETURN);
    if (!wait_for_window_text(second.window, L"baseb")) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, second.window, L"baseb");
        return FALSE;
    }

    focus_app_window(first.window);
    send_modified_press(VK_CONTROL, 0, VK_END);
    send_key_press('C');
    send_modified_press(VK_CONTROL, 0, 'S');
    if (!wait_for_file_text(fixture, "baseac")) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "second external revision was not saved");
        return FALSE;
    }
    focus_app_window(second.window);
    prompt = wait_for_any_process_dialog(second.pi.dwProcessId);
    if (!prompt) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "a later disk revision did not re-arm the dirty warning");
        return FALSE;
    }
    send_dialog_key(prompt, VK_RETURN);
    if (!wait_for_window_text(second.window, L"baseb")) {
        fail_text_mismatch(test_name, second.window, L"baseb");
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        return FALSE;
    }

    focus_app_window(second.window);
    send_modified_press(VK_CONTROL, 0, 'S');
    prompt = wait_for_any_process_dialog(second.pi.dwProcessId);
    if (!prompt) {
        debug_print_process_dialog(second.pi.dwProcessId);
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "save did not require a conflict decision");
        return FALSE;
    }
    send_dialog_key(prompt, VK_RETURN); /* Cancel is deliberately the default. */
    if (!wait_for_file_text(fixture, "baseac")) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "default cancel overwrote the newer disk revision");
        return FALSE;
    }

    focus_app_window(second.window);
    send_modified_press(VK_CONTROL, 0, 'S');
    prompt = wait_for_any_process_dialog(second.pi.dwProcessId);
    if (!prompt) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "second save did not repeat the conflict decision");
        return FALSE;
    }
    send_dialog_key(prompt, VK_LEFT);
    send_dialog_key(prompt, VK_LEFT);
    send_dialog_key(prompt, VK_RETURN);
    if (!wait_for_file_text(fixture, "baseb")) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "explicit overwrite did not commit the editor revision");
        return FALSE;
    }

    focus_app_window(first.window);
    prompt = wait_for_any_process_dialog(first.pi.dwProcessId);
    if (!prompt) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_message(test_name, "first editor did not observe the peer overwrite");
        return FALSE;
    }
    send_dialog_key(prompt, VK_RETURN);
    if (!wait_for_window_text(first.window, L"baseb")) {
        close_app(&first);
        close_app(&second);
        DeleteFileW(fixture);
        fail_text_mismatch(test_name, first.window, L"baseb");
        return FALSE;
    }

    close_app(&first);
    close_app(&second);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_deleted_file_keeps_editor_copy_until_explicit_recreate(void) {
    const char *test_name = "deleted_file_keeps_editor_copy_until_explicit_recreate";
    TestApp app;
    TestApp focus_peer;
    wchar_t fixture[MAX_PATH];
    HWND prompt;

    ZeroMemory(&app, sizeof(app));
    ZeroMemory(&focus_peer, sizeof(focus_peer));
    if (!create_text_fixture(fixture, COUNT_OF(fixture), "kept") ||
        !launch_app_with_argument(&app, fixture) ||
        !launch_app_with_argument(&focus_peer, fixture)) {
        close_app(&app);
        close_app(&focus_peer);
        DeleteFileW(fixture);
        fail_message(test_name, "could not arrange deleted-file scenario");
        return FALSE;
    }
    focus_app_window(focus_peer.window);
    send_modified_press(VK_CONTROL, 0, VK_END);
    send_key_press('X');
    send_modified_press(VK_CONTROL, 0, 'S');
    if (!wait_for_file_text(fixture, "keptx")) {
        debug_print_process_dialog(focus_peer.pi.dwProcessId);
        close_app(&app);
        close_app(&focus_peer);
        DeleteFileW(fixture);
        fail_message(test_name, "could not replace the originally mapped file before deletion");
        return FALSE;
    }
    if (!DeleteFileW(fixture)) {
        close_app(&app);
        close_app(&focus_peer);
        DeleteFileW(fixture);
        fail_message(test_name, "replacement revision could not be deleted");
        return FALSE;
    }

    focus_app_window(app.window);
    prompt = wait_for_any_process_dialog(app.pi.dwProcessId);
    if (!prompt) {
        close_app(&app);
        close_app(&focus_peer);
        fail_message(test_name, "missing-file decision was not shown");
        return FALSE;
    }
    if (!window_text_equals(app.window, L"kept")) {
        fail_text_mismatch(test_name, app.window, L"kept");
        close_app(&app);
        close_app(&focus_peer);
        return FALSE;
    }
    send_dialog_key(prompt, VK_RETURN); /* Keep editing is the default. */
    if (GetFileAttributesW(fixture) != INVALID_FILE_ATTRIBUTES ||
        !wait_for_window_text(app.window, L"kept")) {
        close_app(&app);
        close_app(&focus_peer);
        DeleteFileW(fixture);
        fail_message(test_name, "default missing-file action did not preserve the editor-only copy");
        return FALSE;
    }

    focus_app_window(app.window);
    send_modified_press(VK_CONTROL, 0, 'S');
    prompt = wait_for_any_process_dialog(app.pi.dwProcessId);
    if (!prompt) {
        close_app(&app);
        close_app(&focus_peer);
        fail_message(test_name, "save did not require an explicit recreate decision");
        return FALSE;
    }
    send_dialog_key(prompt, VK_RETURN);
    if (GetFileAttributesW(fixture) != INVALID_FILE_ATTRIBUTES) {
        close_app(&app);
        close_app(&focus_peer);
        DeleteFileW(fixture);
        fail_message(test_name, "default save cancellation recreated the file");
        return FALSE;
    }

    focus_app_window(app.window);
    send_modified_press(VK_CONTROL, 0, 'S');
    prompt = wait_for_any_process_dialog(app.pi.dwProcessId);
    if (!prompt) {
        close_app(&app);
        close_app(&focus_peer);
        fail_message(test_name, "recreate decision was not repeated after cancellation");
        return FALSE;
    }
    send_dialog_key(prompt, VK_LEFT);
    send_dialog_key(prompt, VK_LEFT);
    send_dialog_key(prompt, VK_RETURN);
    if (!wait_for_file_text(fixture, "kept")) {
        close_app(&app);
        close_app(&focus_peer);
        DeleteFileW(fixture);
        fail_message(test_name, "explicit recreate did not restore the editor copy");
        return FALSE;
    }

    close_app(&app);
    close_app(&focus_peer);
    DeleteFileW(fixture);
    return TRUE;
}

static BOOL test_save_mapped_file_leaves_no_replacement_temp(void) {
    const char *test_name = "save_mapped_file_leaves_no_replacement_temp";
    TestApp app;
    wchar_t fixture[MAX_PATH];
    wchar_t artifact[MAX_PATH + 96];

    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), test_name, "base"))
        return FALSE;
    if (!focus_app_window(app.window)) {
        close_app(&app);
        DeleteFileW(fixture);
        fail_message(test_name, "could not focus app window");
        return FALSE;
    }

    send_modified_press(VK_CONTROL, 0, VK_END);
    send_key_press('X');
    if (!wait_for_window_text(app.window, L"basex")) {
        fail_text_mismatch(test_name, app.window, L"basex");
        close_app(&app);
        DeleteFileW(fixture);
        return FALSE;
    }
    send_modified_press(VK_CONTROL, 0, 'S');
    if (!wait_for_file_text(fixture, "basex")) {
        debug_print_process_dialog(app.pi.dwProcessId);
        close_app(&app);
        DeleteFileW(fixture);
        delete_save_artifacts(fixture);
        fail_message(test_name, "save was not committed");
        return FALSE;
    }
    if (find_save_artifact(fixture, artifact, COUNT_OF(artifact))) {
        fwprintf(stderr, L"  debug: leftover save artifact=[%ls]\n", artifact);
        close_app(&app);
        DeleteFileW(fixture);
        delete_save_artifacts(fixture);
        fail_message(test_name, "successful save left a sibling temporary file");
        return FALSE;
    }

    close_app(&app);
    DeleteFileW(fixture);
    return TRUE;
}

static TestCase g_tests[] = {
    {"launches_new_window_class", test_launches_new_window_class},
    {"ctrl_n_opens_fresh_new_window", test_ctrl_n_opens_fresh_new_window},
    {"vertical_scrollbar_scrollable_for_tall_file", test_vertical_scrollbar_scrollable_for_tall_file},
    {"vertical_scrollbar_release_at_end_is_100_percent", test_vertical_scrollbar_release_at_end_is_100_percent},
    {"vertical_scrollbar_drag_up_from_bottom_releases_stickiness", test_vertical_scrollbar_drag_up_from_bottom_releases_stickiness},
    {"pagedown_moves_vertical_scroll", test_pagedown_moves_vertical_scroll},
    {"mousewheel_down_then_up_restores_scroll", test_mousewheel_down_then_up_restores_scroll},
    {"mousewheel_scroll_keeps_caret_document_location", test_mousewheel_scroll_keeps_caret_document_location},
    {"maximized_mousewheel_reaches_bottom_without_sticking", test_maximized_mousewheel_reaches_bottom_without_sticking},
    {"ctrl_down_scroll_long_file_does_not_stick", test_ctrl_down_scroll_long_file_does_not_stick},
    {"ctrl_down_scroll_with_caret_at_top_does_not_stick", test_ctrl_down_scroll_with_caret_at_top_does_not_stick},
    {"maximized_mousewheel_with_caret_at_top_does_not_stick", test_maximized_mousewheel_with_caret_at_top_does_not_stick},
    {"maximized_generated_large_file_top_wheel_moves_immediately", test_maximized_generated_large_file_top_wheel_moves_immediately},
    {"maximized_paradym_top_wheel_moves_immediately", test_maximized_paradym_top_wheel_moves_immediately},
    {"ctrl_word_navigation_insert_points", test_ctrl_word_navigation_selection_copy},
    {"ctrl_shift_word_selection_copy", test_ctrl_shift_word_selection_copy},
    {"ctrl_word_navigation_advances_horizontal_scroll", test_ctrl_word_navigation_advances_horizontal_scroll},
    {"horizontal_scrollbar_track_click_moves_right", test_horizontal_scrollbar_track_click_moves_right},
    {"horizontal_scrollbar_thumb_drag_moves_right", test_horizontal_scrollbar_thumb_drag_moves_right},
    {"drag_selection_scrolls_horizontally_left", test_drag_selection_scrolls_horizontally_left},
    {"horizontal_scrollbar_uses_widest_visible_line_not_caret_line", test_horizontal_scrollbar_uses_widest_visible_line_not_caret_line},
    {"horizontal_scrollbar_with_caret_at_origin_moves_right", test_horizontal_scrollbar_with_caret_at_origin_moves_right},
    {"ctrl_right_symbols_are_separate_jumps", test_ctrl_right_symbols_are_separate_jumps},
    {"basic_text_regressions", test_basic_text_regressions},
    {"tab_inserts_two_spaces", test_tab_inserts_two_spaces},
    {"tab_mid_word_inserts_two_spaces_at_caret", test_tab_mid_word_inserts_two_spaces_at_caret},
    {"tab_with_single_line_selection_indents_selection_start", test_tab_with_single_line_selection_indents_selection_start},
    {"tab_with_multiline_selection_indents_each_selected_line", test_tab_with_multiline_selection_indents_each_selected_line},
    {"shift_tab_unindents_selected_lines_by_up_to_two_spaces", test_shift_tab_unindents_selected_lines_by_up_to_two_spaces},
    {"shift_tab_noop_when_selected_lines_have_no_leading_spaces", test_shift_tab_noop_when_selected_lines_have_no_leading_spaces},
    {"shift_tab_after_tab_reverts_caret_only_indent", test_shift_tab_after_tab_reverts_caret_only_indent},
    {"shift_tab_caret_line_unindents_by_two_spaces_per_press", test_shift_tab_caret_line_unindents_by_two_spaces_per_press},
    {"tab_multiline_selection_excludes_line_when_ending_at_its_start", test_tab_multiline_selection_excludes_line_when_ending_at_its_start},
    {"ctrl_backspace_deletes_previous_word", test_ctrl_backspace_deletes_previous_word},
    {"ctrl_delete_deletes_next_word", test_ctrl_delete_deletes_next_word},
    {"undo_redo_basic_edit_sequence", test_undo_redo_basic_edit_sequence},
    {"undo_redo_cut_paste_sequence", test_undo_redo_cut_paste_sequence},
    {"shift_multiline_select_delete", test_shift_multiline_select_copy},
    {"shift_multiline_replace_typing", test_shift_multiline_replace_typing},
    {"shift_up_on_first_row_selects_to_start", test_shift_up_on_first_row_selects_to_start},
    {"shift_down_on_final_row_selects_to_end", test_shift_down_on_final_row_selects_to_end},
    {"alt_shift_down_insert_column", test_alt_shift_down_insert_column},
    {"alt_down_swaps_line_with_trailing_empty_final_line", test_alt_down_swaps_line_with_trailing_empty_final_line},
    {"alt_up_from_empty_last_line_swaps_with_previous_line", test_alt_up_from_empty_last_line_swaps_with_previous_line},
    {"alt_up_twice_from_last_text_line_keeps_lines_separate", test_alt_up_twice_from_last_text_line_keeps_lines_separate},
    {"alt_down_on_last_line_without_trailing_newline_is_noop", test_alt_down_on_last_line_without_trailing_newline_is_noop},
    {"alt_up_on_first_line_is_noop", test_alt_up_on_first_line_is_noop},
    {"alt_down_moves_all_selected_lines_together", test_alt_down_moves_all_selected_lines_together},
    {"alt_down_multiline_selection_at_bottom_is_noop", test_alt_down_multiline_selection_at_bottom_is_noop},
    {"type_number_lines_keeps_caret_logical_and_visual_sync", test_type_number_lines_keeps_caret_logical_and_visual_sync},
    {"enter_on_line9_does_not_place_caret_in_old_gutter", test_enter_on_line9_does_not_place_caret_in_old_gutter},
    {"home_toggles_between_bol_and_indent", test_home_toggles_between_bol_and_indent},
    {"ctrl_d_escape_restores_editor_focus", test_ctrl_d_escape_restores_editor_focus},
    {"ctrl_d_popup_ctrl_backspace_deletes_word_without_inserting_glyph", test_ctrl_d_popup_ctrl_backspace_deletes_word_without_inserting_glyph},
    {"ctrl_d_popup_backspace_deletes_previous_character", test_ctrl_d_popup_backspace_deletes_previous_character},
    {"look_tall_caret_three_lines", test_look_tall_caret_three_lines},
    {"look_ctrl_d_command_popup", test_look_ctrl_d_command_popup},
    {"large_file_scroll_end_insert_lines_updates_scrollbar", test_large_file_scroll_end_insert_lines_updates_scrollbar},
    {"alt_shift_up_insert_column_pads_short_line", test_alt_shift_up_insert_column_pads_short_line},
    {"alt_shift_up_delete_column", test_alt_shift_up_delete_column},
    {"alt_shift_up_clear_keeps_caret_column", test_alt_shift_up_clear_keeps_caret_column},
    {"alt_shift_up_then_shift_right_delete_uses_current_caret_anchor", test_alt_shift_up_then_shift_right_delete_uses_current_caret_anchor},
    {"tab_mid_word_after_clearing_box_selection_inserts_at_caret", test_tab_mid_word_after_clearing_box_selection_inserts_at_caret},
    {"shift_tab_after_clearing_box_selection_reverts_mid_word_tab", test_shift_tab_after_clearing_box_selection_reverts_mid_word_tab},
    {"shift_tab_after_clearing_multiline_box_selection_reverts_mid_word_tab", test_shift_tab_after_clearing_multiline_box_selection_reverts_mid_word_tab},
    {"tab_with_mid_word_box_selection_inserts_at_box_column", test_tab_with_mid_word_box_selection_inserts_at_box_column},
    {"shift_tab_with_mid_word_multiline_box_selection_removes_inserted_spaces", test_shift_tab_with_mid_word_multiline_box_selection_removes_inserted_spaces},
    {"alt_shift_repeated_backspace_keeps_multiline_column_effect", test_alt_shift_repeated_backspace_keeps_multiline_column_effect},
    {"alt_shift_repeated_backspace_at_column_zero_is_noop", test_alt_shift_repeated_backspace_at_column_zero_is_noop},
    {"alt_shift_selection_alt_down_moves_selected_lines", test_alt_shift_selection_alt_down_moves_selected_lines},
    {"alt_shift_selection_repeated_alt_down_at_bottom_is_noop", test_alt_shift_selection_repeated_alt_down_at_bottom_is_noop},
    {"alt_shift_multi_down_then_tab_indents_each_selected_line", test_alt_shift_multi_down_then_tab_indents_each_selected_line},
    {"alt_shift_multi_down_then_shift_tab_unindents_each_selected_line", test_alt_shift_multi_down_then_shift_tab_unindents_each_selected_line},
    {"box_cut_undo_redo_multi_line", test_box_cut_undo_redo_multi_line},
    {"box_cut_paste_preserves_multiline_shape", test_box_cut_paste_preserves_multiline_shape},
    {"ctrl_c_ctrl_x_selection_clipboard", test_ctrl_c_ctrl_x_selection_clipboard},
    {"ctrl_c_no_selection_line_copy_paste", test_ctrl_c_no_selection_line_copy_paste},
    {"ctrl_c_no_selection_single_line_copy_paste", test_ctrl_c_no_selection_single_line_copy_paste},
    {"ctrl_x_no_selection_line_cut_paste", test_ctrl_x_no_selection_line_cut_paste},
    {"selectissue_md_loads", test_selectissue_md_loads},
    {"loaded_tab_mouse_backspace_deletes_only_indent", test_loaded_tab_mouse_backspace_deletes_only_indent},
    {"loaded_tab_mouse_drag_selects_rendered_text", test_loaded_tab_mouse_drag_selects_rendered_text},
    {"selectissue_md_line10_copy", test_selectissue_md_line10_copy},
    {"selectissue_md_line10_shift_down_copy", test_selectissue_md_line10_shift_down_copy},
    {"selectissue_md_select_lines_backspace", test_selectissue_md_select_lines_backspace},
    {"paste_selectissue_text_then_backspace_lines", test_paste_selectissue_text_then_backspace_lines},
    {"type_selectissue_text_then_backspace_lines", test_type_selectissue_text_then_backspace_lines},
    {"selectissue_md_select_three_lines_backspace", test_selectissue_md_select_three_lines_backspace},
    {"mouse_drag_selection_copy", test_mouse_drag_selection_copy},
    {"alt_mouse_drag_box_selection_cut", test_alt_mouse_drag_box_selection_cut},
    {"shift_click_selection_cut", test_shift_click_selection_cut},
    {"double_click_word_selection_copy", test_double_click_word_selection_copy},
    {"ctrl_right_line_end_before_next_line", test_ctrl_right_line_end_before_next_line},
    {"ctrl_left_line_end_before_previous_word", test_ctrl_left_line_end_before_previous_word},
    {"ctrl_left_symbols_are_separate_jumps", test_ctrl_left_symbols_are_separate_jumps},
    {"find_search_selection_is_visibly_blue", test_find_search_selection_is_visibly_blue},
    {"ctrl_f_popup_ctrl_backspace_deletes_word_without_inserting_glyph", test_ctrl_f_popup_ctrl_backspace_deletes_word_without_inserting_glyph},
    {"ctrl_f_popup_backspace_deletes_previous_character", test_ctrl_f_popup_backspace_deletes_previous_character},
    {"ctrl_end_jumps_to_last_char_on_last_line", test_ctrl_end_jumps_to_last_char_on_last_line},
    {"line_paste_keeps_caret_horizontal_position", test_line_paste_keeps_caret_horizontal_position},
    {"ctrl_x_line_cut_keeps_caret_horizontal_position", test_ctrl_x_line_cut_keeps_caret_horizontal_position},
    {"select_all_cut_paste_keeps_caret_at_end_of_pasted_text", test_select_all_cut_paste_keeps_caret_at_end_of_pasted_text},
    {"ctrl_zoom_shortcuts_minus_plus_zero", test_ctrl_zoom_shortcuts_minus_plus_zero},
    {"double_click_drag_selects_whole_words", test_double_click_drag_selects_whole_words},
    {"find_dialog_typing_does_not_crash", test_find_dialog_typing_does_not_crash},
    {"find_dialog_hover_and_findnext_work", test_find_dialog_hover_and_findnext_work},
    {"find_dialog_enter_triggers_findnext", test_find_dialog_enter_triggers_findnext},
    {"find_dialog_enter_twice_advances_to_next_match", test_find_dialog_enter_twice_advances_to_next_match},
    {"find_dialog_escape_closes_window", test_find_dialog_escape_closes_window},
    {"save_prompt_close_button_cancels_close", test_save_prompt_close_button_cancels_close},
    {"alt_f4_closes_clean_window", test_alt_f4_closes_clean_window},
    {"escape_closes_clean_window", test_escape_closes_clean_window},
    {"close_untitled_empty_dirty_skips_save_prompt", test_close_untitled_empty_dirty_skips_save_prompt},
    {"untitled_empty_dirty_hides_star_title", test_untitled_empty_dirty_hides_star_title},
    {"duplicate_open_clean_peer_reloads_after_save", test_duplicate_open_clean_peer_reloads_after_save},
    {"duplicate_open_dirty_peer_requires_explicit_overwrite", test_duplicate_open_dirty_peer_requires_explicit_overwrite},
    {"deleted_file_keeps_editor_copy_until_explicit_recreate", test_deleted_file_keeps_editor_copy_until_explicit_recreate},
    {"save_mapped_file_leaves_no_replacement_temp", test_save_mapped_file_leaves_no_replacement_temp},
};

static BOOL should_run_test(const TestCase *test, int argc, wchar_t **argv) {
    if (argc <= 2) {
        return strcmp(test->name, "launches_new_window_class") == 0 ||
               strcmp(test->name, "basic_text_regressions") == 0;
    }
    for (int i = 2; i < argc; ++i) {
        wchar_t wide_name[128];
        size_t j;

        for (j = 0; test->name[j] && j + 1 < COUNT_OF(wide_name); ++j) {
            wide_name[j] = (wchar_t)(unsigned char)test->name[j];
        }
        wide_name[j] = 0;
        if (wcscmp(argv[i], wide_name) == 0) return TRUE;
    }
    return FALSE;
}

int wmain(int argc, wchar_t **argv) {
    size_t selected = 0;
    size_t count = sizeof(g_tests) / sizeof(g_tests[0]);

    if (argc > 1 && argv[1] && argv[1][0]) {
        wcsncpy(g_exe_path, argv[1], COUNT_OF(g_exe_path) - 1);
        g_exe_path[COUNT_OF(g_exe_path) - 1] = 0;
    } else {
        wcsncpy(g_exe_path, L"text.exe", COUNT_OF(g_exe_path) - 1);
    }
    {
        DWORD env_len = GetEnvironmentVariableW(L"TEXT_LOOK_DIR", g_look_dir, (DWORD)COUNT_OF(g_look_dir));
        if (env_len == 0 || env_len >= COUNT_OF(g_look_dir)) {
            g_look_dir[0] = 0;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        if (should_run_test(&g_tests[i], argc, argv)) ++selected;
    }

    printf("running %zu black-box tests\n", selected);
    for (size_t i = 0; i < count; ++i) {
        BOOL ok;
        ULONGLONG started;
        double elapsed_ms;

        if (!should_run_test(&g_tests[i], argc, argv)) continue;
        started = monotonic_tick_ms();
        ok = g_tests[i].fn();
        elapsed_ms = (double)(monotonic_tick_ms() - started);
        if (ok) printf("\nPASS %s (%.0f ms)\n", g_tests[i].name, elapsed_ms);
    }

    if (g_failures) {
        printf("%d test(s) failed\n", g_failures);
        return 1;
    }

    printf("all tests passed\n");
    return 0;
}
