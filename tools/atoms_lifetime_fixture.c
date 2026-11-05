/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U AddAtomA(const char *);
API U FindAtomA(const char *);
API U GlobalAddAtomA(const char *);
API U GlobalFindAtomA(const char *);
API U GlobalDeleteAtom(U);
API U GetDesktopWindow(void);
API U SetPropA(U, const char *, U);
API void ExitProcess(U);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
void mainCRTStartup(void)
{
    const char *global = "NEO98 lifecycle global", *local = "NEO98 lifecycle local",
               *property = "NEO98 lifecycle property";
    U atom = GlobalFindAtomA(global);
    if (!atom)
    {
        check(GlobalAddAtomA(global) >= 0xc000 && AddAtomA(local) >= 0xc000 &&
                  SetPropA(GetDesktopWindow(), property, 98),
              4390);
        ExitProcess(98);
    }
    // A second native Application sees system atoms, but no dead process's
    // local table or reference owned by its destroyed desktop property row.
    check(!FindAtomA(local) && !GlobalFindAtomA(property) && !GlobalDeleteAtom(atom), 4391);
    ExitProcess(99);
}
