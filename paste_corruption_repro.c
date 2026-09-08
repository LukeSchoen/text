/* Correctness regressions for the investigated corruption. Public GUI input only. */
#define wmain blackbox_suite_main
#include "text_blackbox_tests.c"
#undef wmain

static BOOL check_text(TestApp *app, const wchar_t *expected, const char *stage) {
    if (wait_for_window_text(app->window, expected)) {
        printf("PASS %s\n", stage);
        return TRUE;
    }
    fail_text_mismatch(stage, app->window, expected);
    return FALSE;
}

static int check_unicode_scalars(void) {
    const wchar_t *chars[] = {L"\x00e9", L"\x20ac", L"\xd83d\xde00"};
    const char *utf8[] = {"\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80"};
    int failed = 0;
    for (int scalar = 0; scalar < 3; ++scalar) for (int mode = 0; mode < 3; ++mode) {
        TestApp app;
        wchar_t fixture[MAX_PATH], original[16], inserted[16], deleted[16];
        char file_text[20];
        wcscpy(original, L"L"); wcscat(original, chars[scalar]); wcscat(original, L"R");
        wcscpy(inserted, L"LX"); wcscat(inserted, chars[scalar]); wcscat(inserted, L"R");
        wcscpy(deleted, L"L"); wcscat(deleted, chars[scalar]);
        strcpy(file_text, "L"); strcat(file_text, utf8[scalar]); strcat(file_text, "R");
        if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), "unicode_scalar", mode == 0 ? file_text : "")) return 1;
        if (!focus_app_window(app.window)) { close_app(&app); DeleteFileW(fixture); return 1; }
        if (mode == 1) { set_clipboard_text(original); send_modified_press(VK_CONTROL, 0, 'V'); }
        if (mode == 2) send_unicode_string(original);
        printf("Unicode scalar=%d backing=%d\n", scalar, mode);
        failed += !check_text(&app, original, "original");
        if (scalar == 2 && mode == 1) capture_window_screenshot(app.window, L".build\\paste-fixed-emoji.bmp");
        send_modified_press(VK_CONTROL, 0, VK_END);
        send_key_press(VK_LEFT); send_key_press(VK_LEFT);
        set_clipboard_text(L"X"); send_modified_press(VK_CONTROL, 0, 'V');
        failed += !check_text(&app, inserted, "Left and paste at scalar boundary");
        send_modified_press(VK_CONTROL, 0, 'Z');
        failed += !check_text(&app, original, "undo paste");
        send_key_press(VK_DELETE);
        failed += !check_text(&app, L"LR", "Delete removes complete scalar");
        send_modified_press(VK_CONTROL, 0, 'Z');
        send_key_press(VK_RIGHT); send_key_press(VK_DELETE);
        failed += !check_text(&app, deleted, "Right skips complete scalar");
        send_key_press(VK_BACK);
        failed += !check_text(&app, L"L", "Backspace removes complete scalar");
        send_modified_press(VK_CONTROL, 0, 'Z');
        send_modified_press(VK_SHIFT, 0, VK_LEFT);
        send_modified_press(VK_CONTROL, 0, 'C');
        if (!wait_for_clipboard_text(chars[scalar])) { fail_clipboard_mismatch("Unicode selection copy", chars[scalar]); failed++; }
        set_clipboard_text(L"Q"); send_modified_press(VK_CONTROL, 0, 'V');
        failed += !check_text(&app, L"LQ", "replace selected scalar");
        send_modified_press(VK_CONTROL, 0, 'Z');
        failed += !check_text(&app, deleted, "undo replacement");
        send_modified_press(VK_CONTROL, 0, 'Y');
        failed += !check_text(&app, L"LQ", "redo replacement");
        send_modified_press(VK_CONTROL, 0, 'Z');
        send_modified_press(VK_CONTROL, 0, 'S');
        file_text[strlen(file_text) - 1] = 0;
        if (!wait_for_file_text(fixture, file_text)) { failed++; printf("FAIL Unicode saved bytes\n"); }
        close_app(&app); DeleteFileW(fixture);
    }
    return failed;
}

static int check_unicode_columns(void) {
    TestApp app;
    wchar_t fixture[MAX_PATH];
    POINT start, next, click;
    int failed = 0;
    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), "unicode_columns", "a\xc3\xa9z\r\nb\xe2\x82\xacz")) return 1;
    if (!focus_app_window(app.window)) { close_app(&app); DeleteFileW(fixture); return 1; }
    send_modified_press(VK_CONTROL, 0, VK_HOME);
    if (!get_caret_screen_pos(app.window, &start)) return 1;
    send_key_press(VK_RIGHT);
    if (!get_caret_screen_pos(app.window, &next)) return 1;
    click.x = start.x + 2 * (next.x - start.x); click.y = start.y + 4;
    ScreenToClient(app.window, &click);
    click_client(app.window, click.x, click.y, FALSE);
    send_unicode_string(L"X");
    failed += !check_text(&app, L"a\x00e9Xz\r\nb\x20acz", "mouse column after multibyte scalar");
    send_modified_press(VK_CONTROL, 0, 'Z');
    send_modified_press(VK_CONTROL, 0, VK_HOME);
    send_key_press(VK_RIGHT); send_key_press(VK_RIGHT); send_key_press(VK_DOWN);
    send_unicode_string(L"X");
    failed += !check_text(&app, L"a\x00e9z\r\nb\x20acXz", "vertical movement uses character columns");
    send_modified_press(VK_CONTROL, 0, 'Z');
    send_modified_press(VK_CONTROL, 0, VK_HOME);
    send_key_press(VK_RIGHT);
    send_modified_press(VK_MENU, VK_SHIFT, VK_DOWN);
    send_modified_press(VK_MENU, VK_SHIFT, VK_RIGHT);
    set_clipboard_text(L"\x00f1"); send_modified_press(VK_CONTROL, 0, 'V');
    failed += !check_text(&app, L"a\x00f1z\r\nb\x00f1z", "box paste replaces whole scalars");
    send_unicode_string(L"X");
    failed += !check_text(&app, L"a\x00f1Xz\r\nb\x00f1Xz", "box caret advances one column after Unicode paste");
    close_app(&app); DeleteFileW(fixture);
    return failed;
}

int wmain(int argc, wchar_t **argv) {
    TestApp app;
    wchar_t fixture[MAX_PATH];
    int failed = 0;
    wcsncpy(g_exe_path, argc > 1 ? argv[1] : L".build\\text_under_test.exe", COUNT_OF(g_exe_path)-1);
    for (int mode = 0; mode < 2; ++mode) {
        if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), "ascii", mode ? "" : "LEFT|RIGHT")) return 2;
        if (!focus_app_window(app.window)) return 2;
        if (mode) send_unicode_string(L"LEFT|RIGHT");
        send_modified_press(VK_CONTROL, 0, VK_HOME);
        for (int i = 0; i < 5; ++i) send_key_press(VK_RIGHT);
        for (int size = 1; size <= 131072; size = size == 1 ? 65535 : size + 1) {
            wchar_t *payload = calloc(size + 1, sizeof(wchar_t));
            wchar_t *expected = calloc(size + 11, sizeof(wchar_t));
            for (int i = 0; i < size; ++i) payload[i] = L'a' + i % 26;
            wcscpy(expected, L"LEFT|"); wcscat(expected, payload); wcscat(expected, L"RIGHT");
            if (!set_clipboard_text(payload)) return 2;
            send_modified_press(VK_CONTROL, 0, 'V');
            printf("ASCII backing=%s paste=%d: ", mode ? "typed" : "mapped", size);
            if (!check_text(&app, expected, "paste")) { failed++; break; }
            send_modified_press(VK_CONTROL, 0, 'Z');
            if (!check_text(&app, L"LEFT|RIGHT", "undo")) { failed++; break; }
            send_modified_press(VK_CONTROL, 0, 'Y');
            if (!check_text(&app, expected, "redo")) { failed++; break; }
            send_modified_press(VK_CONTROL, 0, 'Z');
            free(payload); free(expected);
            if (size == 65537) size = 131071;
        }
        close_app(&app); DeleteFileW(fixture);
    }
    for (int mode = 0; mode < 3; ++mode) {
        if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), "unicode", mode == 2 ? "LEFT caf\xc3\xa9" : (mode == 0 ? "LEFT " : ""))) return 2;
        if (!focus_app_window(app.window)) return 2;
        if (mode == 1) send_unicode_string(L"LEFT ");
        send_modified_press(VK_CONTROL, 0, VK_END);
        if (mode != 2) {
            set_clipboard_text(L"caf\x00e9");
            send_modified_press(VK_CONTROL, 0, 'V');
        }
        failed += !check_text(&app, L"LEFT caf\x00e9", "valid Unicode before edit");
        if (mode == 0) capture_window_screenshot(app.window, L".build\\paste-fixed-before.bmp");
        send_key_press(VK_BACK);
        wchar_t *actual = NULL;
        get_window_text_dup(app.window, &actual);
        printf("UNICODE backing=%d after Backspace codepoints:", mode);
        if (actual) for (size_t i = 0; actual[i]; ++i) printf(" %04X", (unsigned)actual[i]);
        printf("\n"); free(actual);
        failed += !check_text(&app, L"LEFT caf", "Backspace removes complete character");
        if (mode == 0) capture_window_screenshot(app.window, L".build\\paste-fixed-after.bmp");
        send_modified_press(VK_CONTROL, 0, 'S');
        if (wait_for_file_text(fixture, "LEFT caf")) printf("PASS saved bytes after Backspace\n");
        else { printf("Unexpected saved bytes\n"); failed++; }
        if (mode == 0) CopyFileW(fixture, L".build\\paste-fixed.txt", FALSE);
        close_app(&app); DeleteFileW(fixture);
    }
    if (!launch_with_text_fixture(&app, fixture, COUNT_OF(fixture), "paste_into_unicode", "LEFT caf\xc3\xa9")) return 2;
    if (!focus_app_window(app.window)) return 2;
    send_modified_press(VK_CONTROL, 0, VK_END);
    send_key_press(VK_LEFT);
    set_clipboard_text(L"X");
    send_modified_press(VK_CONTROL, 0, 'V');
    failed += !check_text(&app, L"LEFT cafX\x00e9", "ASCII paste preserves Unicode character");
    capture_window_screenshot(app.window, L".build\\paste-fixed-insert.bmp");
    send_modified_press(VK_CONTROL, 0, 'S');
    if (wait_for_file_text(fixture, "LEFT cafX\xc3\xa9")) printf("PASS saved bytes after ASCII paste\n");
    else { printf("Unexpected saved bytes after ASCII paste\n"); failed++; }
    CopyFileW(fixture, L".build\\paste-fixed-insert.txt", FALSE);
    close_app(&app); DeleteFileW(fixture);
    failed += check_unicode_scalars();
    failed += check_unicode_columns();
    return failed ? 1 : 0;
}
