/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Window properties across processes. USER keeps the property list
   at the window, in its own heap; any process may read, set, enumerate and
   remove the properties of any window (GetProp USER, SetProp :
   neither looks at the calling task). Without an argument the program is the
   owner of a window and starts itself again with "child"; each side works on
   the other's window and writes what it saw.
   tools/test_cross_process_props_native.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API char *GetCommandLineA(void);
API U GetLastError(void);
API void SetLastError(U);
API U WinExec(const char *, U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API int lstrcmpA(const char *, const char *);
API U GlobalAddAtomA(const char *);
API U GlobalFindAtomA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int DestroyWindow(U);
API int IsWindow(U);
API U FindWindowA(const char *, const char *);
API int GetMessageA(U *, U, U, U);
API int DispatchMessageA(const U *);
API int PostMessageA(U, U, U, U);
API void PostQuitMessage(int);
API int SetPropA(U, const char *, U);
API U GetPropA(U, const char *);
API U RemovePropA(U, const char *);
API int EnumPropsA(U, void *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static int child, rows, named, atoms, stop_after;
static U owner, mine, other, checks;
static char report[1200];

static void Note(const char *name, int okay)
{
    wsprintfA(report + lstrlenA(report), "%s=%d\r\n", name, okay != 0);
}
static void Write(const char *path)
{
    U written = 0, file = CreateFileA(path, 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, report, (U)lstrlenA(report), &written, 0);
    CloseHandle(file);
}
static int __stdcall Row(U window, const char *name, U data)
{
    ++rows;
    if ((U)name >> 16)
    {
        if (!lstrcmpA(name, "OwnerProp") || !lstrcmpA(name, "PhoneParked") || !lstrcmpA(name, "ChildOwn") ||
            !lstrcmpA(name, "FromParent") || !lstrcmpA(name, "AtomProp")) ++named;
    }
    else ++atoms;
    (void)window; (void)data;
    return !stop_after || rows < stop_after;
}
static U __stdcall Procedure(U window, U message, U wparam, U lparam)
{
    if (!child && message == 0x401)
    {
        /* The child is done with this window; now its window. */
        const U atom = GlobalFindAtomA("AtomProp");
        other = wparam;
        Note("owner_sees_set", GetPropA(window, "PhoneParked") == 1);
        Note("owner_sees_removed", GetPropA(window, "OwnerProp") == 0);
        Note("owner_sees_atom_value", atom && GetPropA(window, (const char *)atom) == 0x2223);
        Note("owner_removes_foreign_set", RemovePropA(window, "PhoneParked") == 1 && !GetPropA(window, "PhoneParked"));
        Note("set_on_child", SetPropA(other, "FromParent", 0x3333) != 0);
        Note("get_on_child", GetPropA(other, "ChildOwn") == 0x4444 && GetPropA(other, "FromParent") == 0x3333);
        rows = named = atoms = 0;
        Note("enum_on_child", EnumPropsA(other, Row) == 1 && rows == 2 && named == 2);
        Note("remove_on_child", RemovePropA(other, "ChildOwn") == 0x4444);
        PostMessageA(other, 0x402, 0, 0);
        return 0;
    }
    if (!child && message == 0x403)
    {
        PostQuitMessage(0);
        return 0;
    }
    if (child && message == 0x402)
    {
        Note("child_sees_set", GetPropA(window, "FromParent") == 0x3333);
        Note("child_sees_removed", GetPropA(window, "ChildOwn") == 0);
        Write("D:\\CHILD.TXT");
        DestroyWindow(window); /* releases the atom of the row the parent set */
        PostMessageA(owner, 0x403, 0, 0);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}
void mainCRTStartup(void)
{
    struct Class klass = {0};
    U message[7];
    const char *line = GetCommandLineA();
    for (const char *p = line; *p; ++p)
        if (p[0] == 'c' && p[1] == 'h' && p[2] == 'i' && p[3] == 'l' && p[4] == 'd') child = 1;
    klass.proc = (U)Procedure;
    klass.instance = 0x400000;
    klass.name = child ? "Neo98PropChild" : "Neo98PropOwner";
    RegisterClassA(&klass);
    mine = CreateWindowExA(0, klass.name, "props", 0x00cf0000u, 10, 10, 200, 100, 0, 0, 0x400000, 0);
    if (!child)
    {
        const U atom = GlobalAddAtomA("AtomProp");
        Note("owner_set", SetPropA(mine, "OwnerProp", 0x1111) && SetPropA(mine, (const char *)atom, 0x2222));
        Note("child_started", WinExec("D:\\PROPS.EXE child", 1) > 31);
        while (GetMessageA(message, 0, 0, 0) > 0)
            DispatchMessageA(message);
        for (checks = 0; other && IsWindow(other) && checks < 200; ++checks) Sleep(20);
        Note("child_window_gone", other && !IsWindow(other));
        /* The row a foreign process set was a string: its atom went with the window. */
        Note("atom_released", GlobalFindAtomA("FromParent") == 0);
        Note("own_rows_left", GetPropA(mine, (const char *)atom) == 0x2223);
        Write("D:\\OWNER.TXT");
        DestroyWindow(mine);
        ExitProcess(0);
    }
    owner = FindWindowA("Neo98PropOwner", 0);
    Note("found_owner", owner != 0);
    SetPropA(mine, "ChildOwn", 0x4444);
    SetLastError(0);
    Note("get_foreign", GetPropA(owner, "OwnerProp") == 0x1111);
    Note("set_foreign", SetPropA(owner, "PhoneParked", 1) != 0);
    Note("no_error_50", GetLastError() != 50);
    Note("get_foreign_set", GetPropA(owner, "PhoneParked") == 1);
    {
        const U atom = GlobalFindAtomA("AtomProp");
        Note("get_foreign_atom", atom && GetPropA(owner, (const char *)atom) == 0x2222);
        Note("set_foreign_atom", atom && SetPropA(owner, (const char *)atom, 0x2223));
    }
    rows = named = atoms = 0;
    Note("enum_foreign", EnumPropsA(owner, Row) == 1 && rows == 3 && named == 3);
    rows = 0; stop_after = 2;
    Note("enum_foreign_stops", EnumPropsA(owner, Row) == 0 && rows == 2);
    stop_after = 0;
    Note("remove_foreign", RemovePropA(owner, "OwnerProp") == 0x1111 && !GetPropA(owner, "OwnerProp"));
    Note("missing_is_zero", GetPropA(owner, "NeverSet") == 0 && RemovePropA(owner, "NeverSet") == 0);
    Note("no_window_is_zero", GetPropA(0x00ffff00u, "OwnerProp") == 0 && !SetPropA(0x00ffff00u, "OwnerProp", 1));
    PostMessageA(owner, 0x401, mine, 0);
    while (GetMessageA(message, 0, 0, 0) > 0) DispatchMessageA(message);
    ExitProcess(0);
}
