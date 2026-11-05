/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetLastError(void);
API void SetLastError(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(void *);
API U DefWindowProcA(U, U, U, U);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DestroyWindow(U);
API U GetWindowContextHelpId(U);
API U SetWindowContextHelpId(U, U);

static void check(U value, U code) { if (!value) ExitProcess(code); }
static U __stdcall procedure(U h, U message, U wparam, U lparam)
{
    (void)h; (void)message; (void)wparam; (void)lparam;
    return DefWindowProcA(h, message, wparam, lparam);
}
static U create(U instance, const char *title, U parent, U id)
{
    return CreateWindowExA(0, "ContextHelpProbe", title, parent ? 0x50000000 : 0x90000000,
                           10, 10, 160, 70, parent, id, instance, 0);
}

void mainCRTStartup(void)
{
    U instance = GetModuleHandleA(0);
    U fields[10] = {0, (U)procedure, 0, 0, instance, 0, 0, 0, 0, (U)"ContextHelpProbe"};
    check(RegisterClassA(fields), 18100);
    U parent = create(instance, "context parent", 0, 0);
    U child = create(instance, "context child", parent, 0x77);
    check(parent && child, 18101);

    SetLastError(0x1357);
    check(GetWindowContextHelpId(parent) == 0 && GetLastError() == 0x1357, 18102);
    check(SetWindowContextHelpId(parent, 0x12345678) &&
          GetWindowContextHelpId(parent) == 0x12345678, 18103);
    check(GetWindowContextHelpId(child) == 0, 18104);
    check(SetWindowContextHelpId(parent, 0) && GetWindowContextHelpId(parent) == 0, 18105);
    check(SetWindowContextHelpId(parent, 0xffffffff) &&
          GetWindowContextHelpId(parent) == 0xffffffff, 18106);

    check(SetWindowContextHelpId(child, 0x00abcdef) &&
          GetWindowContextHelpId(child) == 0x00abcdef &&
          GetWindowContextHelpId(parent) == 0xffffffff, 18107);
    check(DestroyWindow(child), 18108);
    SetLastError(0x2468);
    check(GetWindowContextHelpId(child) == 0 && GetLastError() == 0x2468, 18109);
    check(!SetWindowContextHelpId(child, 9) && GetLastError() == 0x2468, 18110);

    U replacement = create(instance, "context replacement", parent, 0x77);
    check(replacement && replacement != child && GetWindowContextHelpId(replacement) == 0, 18111);
    check(DestroyWindow(replacement) && DestroyWindow(parent), 18112);
    ExitProcess(0);
}
