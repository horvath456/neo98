/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 client of the Windows 98-compatible COMDLG32 Find/Replace dialog. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U RegisterWindowMessageA(const char *);
API U GetWindowLongA(U, int);
API U SetWindowLongA(U, int, U);
API U CallWindowProcA(U, U, U, U, U);
API U FindTextA(void *);
API U ReplaceTextA(void *);
API U CommDlgExtendedError(void);
API U IsWindow(U);
API U IsWindowEnabled(U);
API U IsWindowVisible(U);
API U GetDlgItem(U, U);
API U SetDlgItemTextA(U, U, const char *);
API U GetWindowTextA(U, char *, U);
API U SendMessageA(U, U, U, U);
API U CheckDlgButton(U, U, U);
API U CheckRadioButton(U, U, U, U);
API U LocalAlloc(U, U);
API U LocalFree(U);
struct FindReplace
{
    U size, owner, instance, flags;
    char *find, *replacement;
    unsigned short find_capacity, replacement_capacity;
    U data;
    U(__stdcall *hook)(U, U, U, U);
    const char *template_name;
};
static U message, help_message, owner, notifications, help_calls, hook_calls, init_calls, veto;
static struct FindReplace *active;
static U expected_action, expected_checks;
static const char *expected_find, *expected_replace;
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return *a == *b;
}
static U __stdcall hook(U hwnd, U msg, U wp, U lp)
{
    ++hook_calls;
    if (msg == 0x110)
    {
        check(lp == (U)active && active->data == 0x9800, 700);
        check(GetDlgItem(hwnd, 1152) != 0, 701);
        Sleep(0); // Native dialog -> x86 hook -> native scheduler -> x86 hook -> native dialog.
        ++init_calls;
        return 1;
    }
    if (msg == 0x111 && (wp & 65535) == 1 && veto)
        return 23;
    return 0;
}
static U __stdcall window(U hwnd, U msg, U wp, U lp)
{
    if (msg == message)
    {
        check(hwnd == owner && wp == 0 && lp == (U)active, 702);
        check((active->flags & 0x78) == expected_action, 703);
        if (expected_action != 0x40)
        {
            check((active->flags & 7) == expected_checks, 704);
            check(equal(active->find, expected_find), 705);
            if (expected_replace)
                check(equal(active->replacement, expected_replace), 706);
        }
        ++notifications;
        return expected_action == 0x10 || expected_action == 0x20;
    }
    if (msg == help_message)
    {
        check(hwnd == owner && IsWindow(wp) && lp == (U)active, 707);
        ++help_calls;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}
static U previous_dialog, subclass_calls;
static U __stdcall subclass(U hwnd, U msg, U wp, U lp)
{
    ++subclass_calls;
    return CallWindowProcA(previous_dialog, hwnd, msg, wp, lp);
}
void run_find_test(U instance)
{
    U klass[] = {0, (U)window, 0, 0, instance, 0, 0, 16, 0, (U) "NEO98FindContract"};
    check(RegisterClassA(klass) != 0, 708);
    owner = CreateWindowExA(0, "NEO98FindContract", "Find and Replace", 0x90cf0000, 40, 40, 680, 460, 0, 0,
                            instance, 0);
    check(owner != 0, 709);
    message = RegisterWindowMessageA("commdlg_FindReplace");
    help_message = RegisterWindowMessageA("commdlg_help");
    check(message && help_message && message != help_message, 710);
    check(!FindTextA(0) && CommDlgExtendedError() == 2, 711);
    char find[64] = "alpha", replacement[64] = "beta";
    struct FindReplace request = {sizeof request, owner, 0, 1, find, replacement, 64, 64, 0x9800, hook, 0};
    active = &request;
    request.size = 39;
    check(!FindTextA(&request) && CommDlgExtendedError() == 1, 712);
    request.size = sizeof request;
    request.owner = 0;
    check(!FindTextA(&request) && CommDlgExtendedError() == 0xffff, 713);
    request.owner = owner;
    request.flags = 0x100;
    request.hook = 0;
    check(!ReplaceTextA(&request) && CommDlgExtendedError() == 11, 714);
    request.flags = 0x2000;
    check(!FindTextA(&request) && CommDlgExtendedError() == 4, 715);
    request.flags = 0x200;
    request.instance = instance;
    request.template_name = (const char *)9999;
    check(!FindTextA(&request) && CommDlgExtendedError() == 6, 716);
    request.instance = 0;
    request.template_name = 0;
    for (U mode = 0; mode < 2; ++mode)
    {
        request.flags = 0x181;
        request.hook = hook;
        U dialog = mode ? ReplaceTextA(&request) : FindTextA(&request);
        check(dialog && IsWindow(dialog) && IsWindowEnabled(owner) && !CommDlgExtendedError(), 717);
        check(IsWindowVisible(GetDlgItem(dialog, 1038)), 718);
        check(SetDlgItemTextA(dialog, 1152, ""), 719);
        // Real EN_CHANGE delivered through the native USER message queue.
        SendMessageA(dialog, 0x111, 0x3000480, 0);
        check(!IsWindowEnabled(GetDlgItem(dialog, 1)), 720);
        SetDlgItemTextA(dialog, 1152, "find text");
        if (mode)
            SetDlgItemTextA(dialog, 1153, "replace text");
        SendMessageA(dialog, 0x111, 0x3000480, 0);
        check(IsWindowEnabled(GetDlgItem(dialog, 1)), 721);
        CheckDlgButton(dialog, 1040, 1);
        CheckDlgButton(dialog, 1041, 1);
        if (!mode)
            CheckRadioButton(dialog, 1056, 1057, 1056);
        expected_checks = mode ? 7 : 6;
        expected_find = "find text";
        expected_replace = mode ? "replace text" : 0;
        expected_action = 8;
        U before = notifications;
        veto = 1;
        SendMessageA(dialog, 0x111, 1, 0);
        veto = 0;
        check(notifications == before, 722);
        SendMessageA(dialog, 0x111, 1, 0);
        check(notifications == before + 1, 723);
        if (mode)
        {
            request.find_capacity = 5;
            request.replacement_capacity = 4;
            expected_find = "find";
            expected_replace = "rep";
            for (U action = 0; action < 2; ++action)
            {
                expected_action = action ? 0x20 : 0x10;
                SendMessageA(dialog, 0x111, 1024 + action, 0);
                char caption[16];
                GetWindowTextA(GetDlgItem(dialog, 2), caption, sizeof caption);
                check(equal(caption, "Close"), 724);
            }
        }
        U help_before = help_calls;
        SendMessageA(dialog, 0x111, 1038, 0);
        check(help_calls == help_before + 1, 725);
        expected_action = 0x40;
        SendMessageA(dialog, 0x10, 0, 0);
        check(!IsWindow(dialog) && IsWindowEnabled(owner), 726);
        request.find_capacity = request.replacement_capacity = 64;
    }
    check(init_calls == 2 && hook_calls > 12 && notifications == 6, 727);
    // No hook flag: Win98 clears the caller's hook pointer, and hides Help.
    request.flags = 1;
    request.hook = hook;
    U dialog = FindTextA(&request);
    check(dialog && !request.hook && !IsWindowVisible(GetDlgItem(dialog, 1038)), 728);
    previous_dialog = GetWindowLongA(dialog, 4);
    check(previous_dialog && SetWindowLongA(dialog, 4, (U)subclass) == previous_dialog, 735);
    expected_action = 0x40;
    SendMessageA(dialog, 0x111, 2, 0);
    check(subclass_calls >= 3, 736);
    check(!IsWindow(dialog), 729);
    // Line/character queries use guest CRLF offsets, including the LF half of CRLF.
    U edit = CreateWindowExA(0, "EDIT", "a\r\nbc\r\n", 0x50300104, 0, 0, 200, 100, owner, 99, instance, 0);
    // CreateWindowEx currently accepts native text for EDIT; WM_SETTEXT normalizes Win32 CRLF.
    SendMessageA(edit, 12, 0, (U) "a\r\nbc\r\n");
    check(SendMessageA(edit, 0xba, 0, 0) == 3, 730);
    U expected_lines[] = {0, 0, 0, 1, 1, 1, 1, 2};
    for (U i = 0; i < 8; ++i)
        check(SendMessageA(edit, 0xc9, i, 0) == expected_lines[i], 731);
    check(SendMessageA(edit, 0xbb, 0, 0) == 0 && SendMessageA(edit, 0xbb, 1, 0) == 3 &&
              SendMessageA(edit, 0xbb, 2, 0) == 7 && SendMessageA(edit, 0xbb, 3, 0) == ~0u,
          732);
    SendMessageA(edit, 0xb1, 3, 4);
    check(SendMessageA(edit, 0xc9, ~0u, 0) == 1 && SendMessageA(edit, 0xbb, ~0u, 0) == 3, 733);
    check(DestroyWindow(owner), 734);
}
