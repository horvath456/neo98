// SPDX-License-Identifier: GPL-2.0-or-later
#include "module_fixture.h"
API U W AlphaAnswer(U);
typedef U(W *Function)(U);
static U events[256][3], count;
static U enum_module, enum_seen, enum_freed;
static void check(int ok, U code);
static U W EnumTypes(U module, U type, U parameter)
{
    (void)type;
    (void)parameter;
    check(module == enum_module, 63);
    ++enum_seen;
    if (!enum_freed)
    {
        enum_freed = 1;
        check(FreeLibrary(enum_module) != 0, 64);
    }
    return 1;
}

static U last_thread_detach, static_detach;
void W Report(U id, U reason, U reserved)
{
    if (count >= 256)
        ExitProcess(1);
    if (reason == 3)
    {
        if (id == 2 && last_thread_detach != 1)
            ExitProcess(86);
        last_thread_detach = id;
    }
    if (reason == 0 && id <= 2)
    {
        if (!reserved || id != (static_detach ? 2u : 1u))
            ExitProcess(87);
        ++static_detach;
    }
    events[count][0] = id;
    events[count][1] = reason;
    events[count++][2] = reserved;
}
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
static U W Worker(void *unused)
{
    (void)unused;
    check(GetModuleHandleA("alpha") != 0, 80);
    return 98;
}
static U occurrences(U id, U reason)
{
    U n = 0;
    for (U i = 0; i < count; ++i)
        if (events[i][0] == id && events[i][1] == reason)
            ++n;
    return n;
}
static U resource_test(U module, U index)
{
    SetLastError(98);
    U info = FindResourceA(module, (const char *)33, (const char *)10);
    check(info && GetLastError() == 98 && SizeofResource(module, info) == 6, 100);
    check(FindResourceA(module, "#33", "#10") == info, 101);
    U data = LoadResource(module, info);
    const unsigned char *bytes = (const unsigned char *)LockResource(data);
    check(bytes && bytes[0] == index && bytes[2] == 98 && bytes[4] == 0xac && bytes[5] == 0x20, 102);
    check(!FreeResource(data) && LockResource(data) == data && GetLastError() == 98, 103);
    check(LockResource(0xdeadbeef) == 0xdeadbeef, 104); /* Original unconditional identity. */
    U named = FindResourceA(module, "PAYLOAD", "CUSTOM");
    check(named && named != info && SizeofResource(module, named) == 6, 105);
    U german = FindResourceExA(module, (const char *)10, (const char *)33, 0x407);
    check(german && german != info && SizeofResource(module, german) == 4, 106);
    bytes = (const unsigned char *)LockResource(LoadResource(module, german));
    check(bytes && bytes[0] == index + 100 && bytes[2] == 7, 107);
    check(!FindResourceExA(module, (const char *)10, (const char *)33, 0x411) && GetLastError() == 1815, 108);
    check(!FindResourceA(module, (const char *)34, (const char *)10) && GetLastError() == 1814, 109);
    check(!FindResourceA(module, "PAYLOAD", "MISSING") && GetLastError() == 1813, 110);
    char text[32], expected[] = "Module 0 \x80";
    expected[7] += index;
    check(LoadStringA(module, 7, text, sizeof text) == 10 && equal(text, expected), 111);
    return info;
}

/* a native service module has a readable PE header page at its handle,
   and its export directory names the same addresses GetProcAddress returns.
   This is a second GetProcAddress, written out of the image alone. */
static int same_text(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static U read32(U address) { return *(volatile U *)address; }
static unsigned short read16(U address) { return *(volatile unsigned short *)address; }
static U export_directory(U module, U *bytes)
{
    U lfanew, sections, optional;
    if (!module || (read32(module) & 0xffff) != 0x5a4d) return 0;
    lfanew = read32(module + 0x3c);
    if (read32(module + lfanew) != 0x00004550) return 0;
    sections = read16(module + lfanew + 6);
    optional = read16(module + lfanew + 20);
    if (!sections || optional < 0xe0) return 0;
    /* The one section must cover the export directory it describes. */
    if (read32(module + lfanew + 24 + optional + 12) != 0x1000) return 0;
    if (bytes) *bytes = read32(module + lfanew + 24 + 100);
    return read32(module + lfanew + 24 + 96);
}
static U export_lookup(U module, const char *name)
{
    U directory = export_directory(module, 0), count, names, ordinals, functions, i;
    if (!directory) return 0;
    count = read32(module + directory + 24);
    functions = read32(module + directory + 28);
    names = read32(module + directory + 32);
    ordinals = read32(module + directory + 36);
    for (i = 0; i < count; ++i)
    {
        if (!same_text((const char *)(module + read32(module + names + i * 4)), name)) continue;
        return module + read32(module + functions + read16(module + ordinals + i * 2) * 4);
    }
    return 0;
}
static void module_image_test(void)
{
    static const char *const wanted[10] = {
        "GetVersion", "GetLastError", "ExitProcess", "LoadLibraryA", "GetProcAddress",
        "Sleep", "CloseHandle", "SetLastError", "GetModuleHandleA", "FreeLibrary"};
    U kernel = GetModuleHandleA("kernel32"), bytes = 0, directory, i, walked = 0;
    U version;
    check(kernel != 0, 200);
    /* The read that used to fault: e_lfanew of a native module handle. */
    check(read32(kernel + 0x3c) != 0 && read32(kernel + read32(kernel + 0x3c)) == 0x00004550, 201);
    directory = export_directory(kernel, &bytes);
    check(directory == 0x1000 && bytes > 40, 202);
    /* Not an empty table, and the module names itself. */
    check(read32(kernel + directory + 24) > 100 && read32(kernel + directory + 20) > 100, 203);
    check(same_text((const char *)(kernel + read32(kernel + directory + 12)), "KERNEL32.DLL"), 204);
    for (i = 0; i < 10; ++i)
    {
        U walkable = export_lookup(kernel, wanted[i]);
        check(walkable != 0 && walkable == GetProcAddress(kernel, wanted[i]), 205);
        ++walked;
    }
    check(walked == 10, 206);
    /* The address found in the table is callable. */
    version = export_lookup(kernel, "GetVersion");
    check(((U(W *)(void))version)() == GetVersion(), 207);
    /* Negative probes: a name the table does not carry is refused by both
       lookups, and a handle without a header page has no directory. */
    check(!export_lookup(kernel, "Nonexistent") && !GetProcAddress(kernel, "Nonexistent"), 208);
    {
        MemoryInfo header = {0}, empty = {0};
        check(VirtualQuery((const void *)kernel, &header, sizeof header) &&
                  header.state == 0x1000 && !(header.protection & 0xcc),
              209);
        check(VirtualQuery((const void *)0x72ff0000, &empty, sizeof empty) &&
                  empty.state != 0x1000,
              213);
    }
    /* USER32 and GDI32 carry their own page, with their own name. */
    {
        U user = GetModuleHandleA("user32"), gdi = LoadLibraryA("GDI32.DLL");
        check(user && export_directory(user, 0) == 0x1000 &&
                  same_text((const char *)(user + read32(user + 0x1000 + 12)), "USER32.DLL"),
              210);
        check(gdi && export_lookup(gdi, "CreateSolidBrush") == GetProcAddress(gdi, "CreateSolidBrush"), 211);
        check(FreeLibrary(gdi), 212);
    }
}

U mainCRTStartup(void)
{
    check(count >= 2 && events[0][0] == 2 && events[1][0] == 1 && events[0][1] == 1 && events[0][2] != 0, 2);
    check(AlphaAnswer(39) == 42, 3);
    U exe = GetModuleHandleA(0), alpha = GetModuleHandleA("ALPHA"), beta = GetModuleHandleA("beta.dll");
    U alpha_resource = resource_test(alpha, 1), beta_resource = resource_test(beta, 2);
    check(alpha_resource != beta_resource && !LoadResource(beta, alpha_resource) && GetLastError() == 6, 112);
    U previous_resource = 0;
    U *early = (U *)GetProcAddress(beta, "worker"), early_exit = 0;
    check(early != 0 && *early && WaitForSingleObject(*early, 0xffffffff) == 0 &&
              GetExitCodeThread(*early, &early_exit) && early_exit == 98 && CloseHandle(*early),
          79);
    check(exe == 0x400000 && alpha != exe && beta != exe && alpha != beta, 4);
    check(GetModuleHandleA("C:\\WINDOWS\\ALPHA.DLL") == alpha && GetModuleHandleA("ALPHA.DLL.") == alpha, 5);
    check(GetProcAddress(exe, "Report") == (U)Report, 6);
    char text[260];
    SetLastError(981);
    check(GetModuleFileNameA(0, text, sizeof text) == 21 && equal(text, "C:\\WINDOWS\\MODULE.EXE") &&
              GetLastError() == 981,
          7);
    text[0] = '!';
    check(GetModuleFileNameA(alpha, text, 0) == 0 && text[0] == '!', 8);
    check(GetModuleFileNameA(alpha, text, 1) == 0 && text[0] == 0, 9);
    check(GetModuleFileNameA(alpha, text, 5) == 4 && equal(text, "C:\\W"), 10);
    check(!GetModuleFileNameA(0xdead, text, 9) && !text[0] && GetLastError() == 87, 11);
    check(!GetProcAddress(0xdead, "Value") && GetLastError() == 6, 12);
    check(!GetProcAddress(alpha, "value") && GetLastError() == 127, 13);
    Function value = (Function)GetProcAddress(alpha, (const char *)17);
    check(value != 0 && value(39) == 42, 14);
    U kernel = GetModuleHandleA("kernel32");
    check(kernel != 0, 15);
    U v = GetProcAddress(kernel, "GetVersion");
    check(v != 0 && v == GetProcAddress(kernel, "GetVersion") &&
              v == GetProcAddress(kernel, (const char *)476),
          16);
    check(!GetProcAddress(kernel, "Nonexistent") && GetLastError() == 127, 17);
    check(!LoadLibraryA("absent") && GetLastError() == 126, 18);
    check(!LoadLibraryA("BAD.DLL") && GetLastError() == 193, 19);
    check(!LoadLibraryA("FIXED.DLL") && GetLastError() == 487, 20);
    /* A static TLS directory does not stop LoadLibrary. The loader ignores it
       and the module keeps the index its file carries. */
    U tls_module = LoadLibraryA("TLS.DLL");
    check(tls_module != 0 && occurrences(7, 1) == 1, 21);
    check(FreeLibrary(tls_module) && occurrences(7, 0) == 1 && !GetModuleHandleA("TLS.DLL"), 25);
    check(!LoadLibraryA("FAILED.DLL") && GetLastError() == 1114 && !GetModuleHandleA("FAILED"), 22);
    /* A refused DLL_PROCESS_ATTACH gets no DLL_PROCESS_DETACH. KERNEL32
       clears the initialized bit and the detach walk
       skips the module. */
    check(occurrences(4, 1) == 1 && occurrences(4, 0) == 0, 23);
    check(!LoadLibraryA("BROKEN.DLL") && GetLastError() == 127 && !GetModuleHandleA("BROKEN"), 24);
    for (U pass = 0; pass < 20; ++pass)
    {
        U gamma = LoadLibraryA("gamma");
        check(gamma != 0 && GetModuleHandleA("GAMMA.DLL") == gamma, 30);
        U current_resource = resource_test(gamma, 3);
        check(current_resource != previous_resource, 113);
        if (previous_resource) check(!LoadResource(gamma, previous_resource) && GetLastError() == 6, 114);
        previous_resource = current_resource;
        U again = LoadLibraryA("C:\\WINDOWS\\GAMMA.DLL");
        check(again == gamma, 31);
        U *counter = (U *)GetProcAddress(gamma, "counter");
        check(counter != 0 && *counter == 0, 32);
        value = (Function)GetProcAddress(gamma, "Value");
        check(value != 0 && value(39) == 42 && *counter == 1, 33);
        Function forward = (Function)GetProcAddress(gamma, "ForwardName");
        check(forward != 0 && forward(39) == 42, 34);
        forward = (Function)GetProcAddress(gamma, "ForwardOrdinal");
        check(forward != 0 && forward(39) == 42, 35);
        check(!GetProcAddress(gamma, "ForwardMissing") && GetLastError() == 127, 36);
        check(!GetProcAddress(gamma, "ForwardCycle") && GetLastError() == 127, 37);
        if (!pass)
        {
            U thread = CreateThread(0, 0, Worker, 0, 0, 0), exit = 0;
            check(thread != 0 && WaitForSingleObject(thread, 0xffffffff) == 0 &&
                      GetExitCodeThread(thread, &exit) && exit == 98,
                  38);
            check(CloseHandle(thread) && occurrences(3, 2) == 1 && occurrences(3, 3) == 1, 39);
            check(DisableThreadLibraryCalls(gamma), 40);
            thread = CreateThread(0, 0, Worker, 0, 0, 0);
            check(thread != 0 && WaitForSingleObject(thread, 0xffffffff) == 0 && CloseHandle(thread), 41);
            check(occurrences(3, 2) == 1 && occurrences(3, 3) == 1, 42);
        }
        check(FreeLibrary(gamma) && GetModuleHandleA("gamma") == gamma && value(1) == 4, 43);
        check(FreeLibrary(again) && !GetModuleHandleA("gamma"), 44);
        check(!FreeLibrary(gamma) && GetLastError() == 6, 45);
    }
    check(occurrences(3, 1) == 20 && occurrences(3, 0) == 20, 46);


    check(GetModuleHandleA("ALPHA") == alpha && GetModuleHandleA("BETA") == beta, 47);
    U cycle_a = LoadLibraryA("CYCLEA"), cycle_b = GetModuleHandleA("CYCLEB");
    check(cycle_a != 0 && cycle_b != 0, 50);
    value = (Function)GetProcAddress(cycle_a, "Value");
    check(value != 0 && value(2) == 11, 51);
    check(LoadLibraryA("CYCLEB") == cycle_b && FreeLibrary(cycle_a) && GetModuleHandleA("CYCLEA") == cycle_a,
          52);
    check(FreeLibrary(cycle_b) && !GetModuleHandleA("CYCLEA") && !GetModuleHandleA("CYCLEB"), 53);
    check(occurrences(5, 1) == 1 && occurrences(6, 1) == 1 && occurrences(5, 0) == 1 &&
              occurrences(6, 0) == 1,
          54);
    check(!LoadLibraryA("ALPHA.") && GetLastError() == 126, 55);
    // The EnumResourceTypes callback unloads the enumerated module. Resolve
    // it again after callbacks and stop with ERROR_INVALID_HANDLE if it
    // disappeared.
    enum_module = LoadLibraryA("TLS");
    check(enum_module != 0 && GetModuleHandleA("TLS.DLL") == enum_module, 60);
    enum_seen = 0;
    enum_freed = 0;
    SetLastError(0);
    check(EnumResourceTypesA(enum_module, (U)EnumTypes, 0) == 0 && GetLastError() == 6, 61);
    check(enum_seen == 1 && enum_freed == 1 && !GetModuleHandleA("TLS.DLL"), 62);

    module_image_test();

    // Dynamic DLL still loaded at process exit: its detach releases its own
    // reference and then reads resources and returns through its own code.
    check(LoadLibraryA("GAMMA") != 0, 120);

    // Ordinary return from the EXE also runs process detach for static DLLs.
    return 0;
}
