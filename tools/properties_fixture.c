/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DestroyWindow(U);
API U DefWindowProcA(U, U, U, U);
API U GetDesktopWindow(void);
API U GetShellWindow(void);
API U SetPropA(U, const char *, U);
API U GetPropA(U, const char *);
API U RemovePropA(U, const char *);
API U GlobalAddAtomA(const char *);
API U GlobalFindAtomA(const char *);
API U GlobalDeleteAtom(U);
API U GetLastError(void);
API void ExitProcess(U);
static U instance, cleanup_seen;
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U __stdcall procedure(U h, U m, U w, U l)
{
    if (m == 0x81)
    {
        check(SetPropA(h, "NEO98 creation property", 0x123498ab), 4360);
        // Creation failure still reaches the native final destruction path.
        if (*(const U *)l == 1)
            return 0;
    }
    if (m == 0x82)
    {
        check(GetPropA(h, "NEO98 creation property") == 0x123498ab, 4361);
        ++cleanup_seen;
    }
    return DefWindowProcA(h, m, w, l);
}
static U window(U parent, U parameter)
{
    return CreateWindowExA(0, "NEO98Properties", "", parent ? 0x40000000 : 0, 0, 0, 10, 10, parent, 0,
                           instance, parameter);
}
void run_properties_test(U module)
{
    instance = module;
    check(GetDesktopWindow() && !GetShellWindow(), 4359);
    U klass[] = {0, (U)procedure, 0, 0, module, 0, 0, 0, 0, (U) "NEO98Properties"};
    check(RegisterClassA(klass), 4362);
    U a = window(0, 0), b = window(0, 0);
    check(a && b, 4363);
    check(!GetPropA(a, "missing") && !RemovePropA(a, "missing") && !GlobalFindAtomA("missing"), 4364);
    check(SetPropA(a, "NEO98 property value", 0x87654321) &&
              GetPropA(a, "neo98 PROPERTY VALUE") == 0x87654321 && !GetPropA(b, "NEO98 property value"),
          4365);
    U atom = GlobalFindAtomA("NEO98 property value");
    check(atom >= 0xc000 && GetPropA(a, (const char *)atom) == 0x87654321 &&
              SetPropA(a, "NEO98 property value", 98) && RemovePropA(a, "NEO98 property value") == 98 &&
              !GlobalFindAtomA("NEO98 property value"),
          4366);
    // The caller's name form controls Remove's reference release in Win98.
    check(SetPropA(a, "NEO98 integer removal", 27), 4367);
    atom = GlobalFindAtomA("NEO98 integer removal");
    check(RemovePropA(a, (const char *)atom) == 27 && GlobalFindAtomA("NEO98 integer removal") == atom &&
              !GlobalDeleteAtom(atom),
          4368);
    // Updating an integer-created row by string keeps the row's integer flag.
    atom = GlobalAddAtomA("NEO98 integer ownership");
    check(atom && SetPropA(b, (const char *)atom, 3) && SetPropA(b, "NEO98 integer ownership", 4), 4369);
    check(SetPropA(a, (const char *)0xffff, 0xfedcba98) && GetPropA(a, (const char *)0xffff) == 0xfedcba98 &&
              RemovePropA(a, (const char *)0xffff) == 0xfedcba98,
          4370);
    check(SetPropA(a, "NEO98 zero property", 0) && GlobalFindAtomA("NEO98 zero property") >= 0xc000 &&
              !RemovePropA(a, "NEO98 zero property") && !GlobalFindAtomA("NEO98 zero property"),
          4371);
    // Array growth, removal from the middle and shrink must preserve neighbors.
    for (U i = 1; i <= 128; ++i)
        check(SetPropA(a, (const char *)i, i ^ 0x98000000), 4372);
    for (U i = 2; i <= 128; i += 2)
        check(RemovePropA(a, (const char *)i) == (i ^ 0x98000000), 4373);
    for (U i = 1; i <= 128; ++i)
        check(GetPropA(a, (const char *)i) == (i & 1 ? i ^ 0x98000000 : 0), 4374);
    U child = window(a, 0);
    check(child && SetPropA(child, "NEO98 child property", 33), 4375);
    U before = cleanup_seen;
    check(DestroyWindow(a) && cleanup_seen == before + 2 && !GlobalFindAtomA("NEO98 child property"), 4376);
    check(DestroyWindow(b) && !GlobalFindAtomA("NEO98 creation property") &&
              GlobalFindAtomA("NEO98 integer ownership") == atom && !GlobalDeleteAtom(atom),
          4377);
    check(!window(0, 1) && !GlobalFindAtomA("NEO98 creation property"), 4378);
    U next = window(0, 0);
    check(next && next != a && !GetPropA(next, (const char *)1) && DestroyWindow(next), 4379);
    // Native provider bounds: reject wrapped 16-bit growth, retain existing rows.
    U full = CreateWindowExA(0, "STATIC", "", 0, 0, 0, 10, 10, 0, 0, module, 0);
    check(full, 4380);
    for (U i = 1; i <= 8191; ++i)
        check(SetPropA(full, (const char *)i, i), 4381);
    check(!SetPropA(full, (const char *)8192, 8192) && GetLastError() == 8 &&
              GetPropA(full, (const char *)8191) == 8191 && SetPropA(full, (const char *)8191, 98) &&
              GetPropA(full, (const char *)8191) == 98 && DestroyWindow(full),
          4382);
    U desktop = GetDesktopWindow();
    check(SetPropA(desktop, "NEO98 desktop property", 98) &&
              GetPropA(desktop, "NEO98 desktop property") == 98 &&
              RemovePropA(desktop, "NEO98 desktop property") == 98 &&
              !GlobalFindAtomA("NEO98 desktop property"),
          4383);
    // Leave one desktop row to exercise Application/USER teardown cleanup.
    check(SetPropA(desktop, "NEO98 desktop teardown", 98), 4384);
}
