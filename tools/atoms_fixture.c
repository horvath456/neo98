/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U AddAtomA(const char *);
API U FindAtomA(const char *);
API U DeleteAtom(U);
API U GetAtomNameA(U, char *, U);
API U InitAtomTable(U);
API U GlobalAddAtomA(const char *);
API U GlobalFindAtomA(const char *);
API U GlobalDeleteAtom(U);
API U GlobalGetAtomNameA(U, char *, U);
API U GlobalAddAtomW(const unsigned short *);
API U GlobalFindAtomW(const unsigned short *);
API U GlobalGetAtomNameW(U, unsigned short *, U);
API U RegisterWindowMessageA(const char *);
API U RegisterClipboardFormatA(const char *);
API void SetLastError(U);
API U CreateThread(U, U, U(__stdcall *)(U), U, U, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeThread(U, U *);
API U CloseHandle(U);
API U GetLastError(void);
API void ExitProcess(U);
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
static U __stdcall worker(U atom)
{
    char text[32];
    check(FindAtomA("THREAD Shared") == atom && GetAtomNameA(atom, text, 32) == 13 &&
              equal(text, "Thread shared"),
          2100);
    U own = AddAtomA("Worker atom");
    check(own >= 0xc000 && DeleteAtom(own) == 0, 2101);
    U global = GlobalFindAtomA("NEO98 global worker");
    check(global >= 0xc000 && GlobalAddAtomA("NEO98 GLOBAL WORKER") == global && !GlobalDeleteAtom(global),
          4340);
    return 98;
}
static void globals(void)
{
    char text[260];
    U a = GlobalAddAtomA("Global MiXeD");
    check(a >= 0xc000 && GlobalFindAtomA("GLOBAL mixed") == a && GlobalAddAtomA("global mixed") == a &&
              !FindAtomA("Global mixed"),
          4341);
    check(GlobalGetAtomNameA(a, text, 260) == 12 && equal(text, "Global MiXeD"), 4342);
    check(!GlobalDeleteAtom(a) && GlobalFindAtomA("Global mixed") == a && !GlobalDeleteAtom(a) &&
              !GlobalFindAtomA("Global mixed"),
          4343);
    check(GlobalAddAtomA((const char *)23) == 23 && GlobalFindAtomA("#23") == 23 &&
              !GlobalAddAtomA((const char *)0xc000) && !GlobalAddAtomA(""),
          4344);
    text[0] = '!';
    SetLastError(98);
    check(!GlobalGetAtomNameA(23, text, 0) && text[0] == '!' && GetLastError() == 98, 4345);
    check(!GlobalGetAtomNameA(23, 0, 0) && GetLastError() == 87, 4346);
    SetLastError(98);
    check(!GlobalGetAtomNameA(23, text, 0x10000) && text[0] == '!' && GetLastError() == 87, 4347);
    check(GlobalGetAtomNameA(12345, text, 4) == 3 && equal(text, "#45"), 4348);
    unsigned short wide[2] = {'A', 0};
    check(!GlobalAddAtomW(wide) && GetLastError() == 120 && !GlobalFindAtomW(wide) && GetLastError() == 120 &&
              !GlobalGetAtomNameW(23, wide, 2) && GetLastError() == 120 && wide[0] == 'A',
          4349);
    U message = RegisterWindowMessageA("NEO98 shared registration");
    check(message >= 0xc000 && RegisterWindowMessageA("neo98 SHARED registration") == message &&
              RegisterClipboardFormatA("NEO98 Shared Registration") == message &&
              !GlobalFindAtomA("NEO98 shared registration") && !FindAtomA("NEO98 shared registration"),
          4350);
    // Global deletion must not touch USER's separate registration table.
    a = GlobalAddAtomA("NEO98 shared registration");
    check(a >= 0xc000 && !GlobalDeleteAtom(a) &&
              RegisterWindowMessageA("NEO98 shared registration") == message,
          4351);
    // Original atoms accept 255 bytes, exceeding the previous 127-byte provider.
    for (U i = 0; i < 255; ++i)
        text[i] = 'z';
    text[255] = 0;
    U long_message = RegisterWindowMessageA(text);
    check(long_message >= 0xc000 && long_message != message && RegisterClipboardFormatA(text) == long_message,
          4352);
}
void run_atoms_test(void)
{
    globals();
    // One bucket exercises collision chains through the actual x86 ABI.
    check(InitAtomTable(1) != 0, 2102);
    char text[260];
    // Before any named atom exists, heap headers and interior offsets must
    // neither expose allocator bytes nor become deletable atom handles.
    for (U candidate = 0xc400; candidate < 0xc440; ++candidate)
    {
        text[0] = '!';
        check(!GetAtomNameA(candidate, text, 32) && !text[0] && DeleteAtom(candidate) == candidate, 2124);
    }
    check(AddAtomA((const char *)1) == 1 && FindAtomA((const char *)0xbfff) == 0xbfff, 2103);
    check(!AddAtomA(0) && !AddAtomA((const char *)0xc000) && !AddAtomA("") && !FindAtomA("absent"), 2104);
    check(AddAtomA("#65537") == 1 && !AddAtomA("#65536") && AddAtomA("#0012") == 12 && !AddAtomA("#49152"),
          2105);
    U atom = AddAtomA("MiXeD"), other = AddAtomA("Other"), same = AddAtomA("mixed");
    check(atom >= 0xc000 && other >= 0xc000 && other != atom && same == atom && FindAtomA("MIXED") == atom,
          2106);
    for (U i = 0; i < 260; ++i)
        text[i] = '!';
    check(GetAtomNameA(atom | 0xfedc0000, text, 3) == 2 && equal(text, "Mi") && text[3] == '!', 2107);
    check(GetAtomNameA(12345, text, 4) == 3 && equal(text, "#45") && text[4] == '!', 2108);
    check(GetAtomNameA(12345, text, 2) == 1 && equal(text, "#"), 2109);
    text[0] = '!';
    check(!GetAtomNameA(atom, text, 0) && text[0] == '!', 2110);
    check(!DeleteAtom(atom) && FindAtomA("mixed") == atom && !DeleteAtom(atom) && !FindAtomA("mixed"), 2111);
    text[0] = '!';
    check(!GetAtomNameA(atom, text, 32) && !text[0] && FindAtomA("Other") == other, 2112);
    check(DeleteAtom(atom) == atom && !DeleteAtom(other) && !DeleteAtom(123), 2113);
    // The original local heap must reuse released storage across app actions.
    for (U i = 0; i < 500; ++i)
    {
        U a = AddAtomA("Reused");
        check(a >= 0xc000 && !DeleteAtom(a), 2114);
    }
    U global_shared = GlobalAddAtomA("NEO98 global worker");
    U shared = AddAtomA("Thread shared"), id = 0, thread = CreateThread(0, 0, worker, shared, 0, &id),
      result = 0;
    check(shared >= 0xc000 && thread && WaitForSingleObject(thread, 5000) == 0, 2115);
    check(GetExitCodeThread(thread, &result) && result == 98 && CloseHandle(thread) && !DeleteAtom(shared),
          2116);
    check(!GlobalDeleteAtom(global_shared) && !GlobalFindAtomA("NEO98 global worker"), 4353);
    U latin = AddAtomA("\xe4");
    check(latin >= 0xc000 && AddAtomA("\xc4") == latin && !DeleteAtom(latin) && !DeleteAtom(latin), 2117);
    char long_name[257];
    for (U i = 0; i < 256; ++i)
        long_name[i] = 'x';
    long_name[256] = 0;
    check(!AddAtomA(long_name), 2118);
    long_name[255] = 0;
    U maximum = AddAtomA(long_name);
    check(maximum >= 0xc000 && GetAtomNameA(maximum, text, 260) == 255 && equal(text, long_name) &&
              !DeleteAtom(maximum),
          2119);
    // Fill the private native arena, retain existing entries through failure, then reclaim it.
    U atoms[512], count = 0;
    for (; count < 512; ++count)
    {
        long_name[0] = 'A' + count / 26;
        long_name[1] = 'A' + count % 26;
        atoms[count] = AddAtomA(long_name);
        if (!atoms[count])
            break;
    }
    check(count > 100 && count < 512 && GetLastError() == 8, 2120);
    check(GetAtomNameA(atoms[0], text, 260) == 255 && text[0] == 'A' && text[1] == 'A', 2121);
    for (U i = 0; i < count; ++i)
        check(!DeleteAtom(atoms[i]), 2122);
    U recovered = AddAtomA("Recovered");
    check(recovered >= 0xc000 && FindAtomA("recovered") == recovered && !DeleteAtom(recovered), 2123);
}
