/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The export contract of the natively provided Win98 DLLs, seen from an x86
 * guest. For KERNEL32, USER32, GDI32, ADVAPI32, WINMM and COMDLG32 the fixture
 *
 * - walks the export directory of the module's own header page and compares
 * every entry with GetProcAddress by name and by ordinal,
 * - asks GetProcAddress for every name the original DLL exports, out of the
 * generated table of tools/export_contract.py,
 * - calls a sample of the exports Windows 98 itself leaves unimplemented and
 * records the return value, the last error and the stack pointer,
 * - and, when the command line says so, calls one export this port does not
 * implement, which must stop the run with a diagnosis.
 *
 * tools/test_export_contract_guest.py builds and runs it. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void *LoadLibraryA(const char *);
API void *GetModuleHandleA(const char *);
API void *GetProcAddress(void *, const char *);
API U GetLastError(void);
API void SetLastError(U);
API void *VirtualAlloc(void *, U, U, U);
API char *GetCommandLineA(void);
API void ExitProcess(U);
API void SetLastErrorEx(U,U);
API void SetDebugErrorLevel(U);
API U GdiGetBatchLimit(void);
API U GdiSetBatchLimit(U);
API U SetGraphicsMode(U,U);
API U GetICMProfileA(U,U*,char*);
API U ColorMatchToTarget(U,U,U);
API U GetRasterizerCaps(void*,U);
API U UpdateColors(U);
API U GetSystemDefaultLangID(void);
API U GetDC(U);
API U ReleaseDC(U,U);

#include "../build/generated/export_contract_names.h"

#define RECORDS 0x30000000u
/* The counter block is at RECORDS, the indices of the first failures at
 * RECORDS+128, and the entry the call-missing run chose at RECORDS+512. */
#define FAILURES 32u
#define CHOSEN 128u
/* The counters the runner reads. Keep the order; the runner names them. */
enum
{
    R_MODULES = 0,       /* modules whose header page carries an export directory */
    R_DIRECTORY,         /* export directory entries walked */
    R_DIRECTORY_BAD,     /* directory entry whose address GetProcAddress does not give */
    R_NAMES,             /* names of the original asked for */
    R_NAME_NULL,         /* names GetProcAddress answered with zero */
    R_ORDINALS,          /* ordinals asked for */
    R_ORDINAL_NULL,      /* ordinals GetProcAddress answered with zero */
    R_ORDINAL_DIFFERS,   /* name and ordinal gave different addresses */
    R_ORIGINAL_CALLS,    /* original unimplemented exports actually called */
    R_ORIGINAL_VALUE,    /* wrong return value */
    R_ORIGINAL_ERROR,    /* last error was not 120 */
    R_ORIGINAL_ESP,      /* the stack pointer did not come back */
    R_DATA,              /* data exports asked for */
    R_DATA_NULL,         /* data exports answered with zero */
    R_MISSING_FOUND,     /* exports this port lacks that still have an address */
    R_NAME_ORDER_BAD,    /* names of a directory not in ascending byte order */
    R_SMALL_CHECKS,      /* checks of the small exports the census asked for */
    R_SMALL_BAD,         /* of those, the ones that answered wrong */
    R_SMALL_MASK,        /* bit n-1 set: check n of the small exports answered wrong */
    R_COUNTERS
};
static U *records;
static U failures[64];
static U failure_count;

static void note(U index)
{
    if (failure_count < 64)
        failures[failure_count++] = index;
}

/* The call of one export with the number of argument dwords the original
 * removes. The stack pointer is taken before and after and put back, so a
 * contract that removes the wrong number of bytes is a finding and not a
 * crash. */
static U call_fn, call_argc, call_before, call_after;
static U call_stdcall(void *function, U argc)
{
    U value;
    call_fn = (U)function;
    call_argc = argc;
    __asm__ volatile("movl %%esp, %[before]\n\t"
                     "movl %[argc], %%ecx\n\t"
                     "1: testl %%ecx, %%ecx\n\t"
                     "jz 2f\n\t"
                     "pushl $0\n\t"
                     "decl %%ecx\n\t"
                     "jmp 1b\n\t"
                     "2: call *%[fn]\n\t"
                     "movl %%esp, %[after]\n\t"
                     "movl %[before], %%esp\n\t"
                     : "=a"(value), [before] "+m"(call_before), [after] "+m"(call_after)
                     : [fn] "m"(call_fn), [argc] "m"(call_argc)
                     : "ecx", "memory");
    return value;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

/* GetProcAddress by ordinal: the low word of the pointer argument, as
 * MAKEINTRESOURCE builds it. */
static void *by_ordinal(void *module, U ordinal)
{
    return GetProcAddress(module, (const char *)ordinal);
}

/* Walk the export directory the module's header page carries and compare every
 * entry with what GetProcAddress answers. */
static void walk_directory(void *module, const char *dll)
{
    const unsigned char *image = (const unsigned char *)module;
    U lfanew, directory, base, names, name_table, ordinal_table, function_table, i;
    if (image[0] != 'M' || image[1] != 'Z')
        return;
    lfanew = *(const U *)(image + 0x3c);
    if (*(const U *)(image + lfanew) != 0x00004550u)
        return;
    directory = *(const U *)(image + lfanew + 24 + 96);
    if (!directory)
        return;
    ++records[R_MODULES];
    base = *(const U *)(image + directory + 16);
    names = *(const U *)(image + directory + 24);
    function_table = *(const U *)(image + directory + 28);
    name_table = *(const U *)(image + directory + 32);
    ordinal_table = *(const U *)(image + directory + 36);
    for (i = 0; i < names; ++i)
    {
        const char *name = (const char *)(image + *(const U *)(image + name_table + i * 4));
        /* The PE contract wants AddressOfNames in ascending byte order, or a
         * program that searches the table itself binarily misses names. */
        if (i)
        {
            const char *before = (const char *)(image + *(const U *)(image + name_table + (i - 1) * 4));
            const unsigned char *x = (const unsigned char *)before, *y = (const unsigned char *)name;
            while (*x && *x == *y) { ++x; ++y; }
            if (*x >= *y)
                ++records[R_NAME_ORDER_BAD];
        }
        U index = *(const unsigned short *)(image + ordinal_table + i * 2);
        U rva = *(const U *)(image + function_table + index * 4);
        void *walked = (void *)((U)module + rva);
        ++records[R_DIRECTORY];
        if (GetProcAddress(module, name) != walked || by_ordinal(module, base + index) != walked)
        {
            ++records[R_DIRECTORY_BAD];
            note(i);
        }
    }
    (void)dll;
}

int main(void)
{
    static const char *const modules[] = {"KERNEL32.DLL", "USER32.DLL", "GDI32.DLL",
                                          "ADVAPI32.DLL", "WINMM.DLL", "COMDLG32.DLL"};
    static void *handles[6];
    const U total = sizeof ContractExports / sizeof ContractExports[0];
    U i, m, sampled[6];
    const char *command = GetCommandLineA();
    int call_missing = 0;
    for (const char *p = command; p && *p; ++p)
        if (p[0] == 'c' && p[1] == 'a' && p[2] == 'l' && p[3] == 'l' && p[4] == '-')
            call_missing = 1;
    records = VirtualAlloc((void *)RECORDS, 65536, 0x3000, 4);
    if (records != (U *)RECORDS)
        ExitProcess(1);
    for (m = 0; m < 6; ++m)
    {
        handles[m] = GetModuleHandleA(modules[m]);
        if (!handles[m])
            handles[m] = LoadLibraryA(modules[m]);
        if (!handles[m])
            ExitProcess(2);
        sampled[m] = 0;
    }
    for (m = 0; m < 6; ++m)
        walk_directory(handles[m], modules[m]);
    for (i = 0; i < total; ++i)
    {
        const struct ContractExport *e = &ContractExports[i];
        void *module = 0, *address, *ordinal_address;
        for (m = 0; m < 6; ++m)
            if (same(e->dll, modules[m]))
                module = handles[m];
        if (!module)
            continue;
        if (e->name)
        {
            ++records[R_NAMES];
            address = GetProcAddress(module, e->name);
            if (!address)
            {
                ++records[R_NAME_NULL];
                note(i);
                continue;
            }
        }
        else
            address = 0;
        ++records[R_ORDINALS];
        ordinal_address = by_ordinal(module, e->ordinal);
        if (!ordinal_address)
        {
            ++records[R_ORDINAL_NULL];
            note(i);
            continue;
        }
        if (address && address != ordinal_address)
        {
            ++records[R_ORDINAL_DIFFERS];
            note(i);
        }
        if (e->kind == 3)
        {
            ++records[R_DATA];
            if (!ordinal_address)
                ++records[R_DATA_NULL];
        }
        if (e->kind == 4)
            ++records[R_MISSING_FOUND];
    }
    /* A sample of at least ten unimplemented original exports per DLL, called
     * for real: the return value, last error 120 and the stack pointer. */
    for (i = 0; i < total; ++i)
    {
        const struct ContractExport *e = &ContractExports[i];
        void *module = 0, *address;
        U value;
        U which = 6;
        for (m = 0; m < 6; ++m)
            if (same(e->dll, modules[m]))
            {
                module = handles[m];
                which = m;
            }
        if (!module || e->kind != 1 || e->argc > 12 || sampled[which] >= 12)
            continue;
        address = e->name ? GetProcAddress(module, e->name) : by_ordinal(module, e->ordinal);
        if (!address)
            continue;
        ++sampled[which];
        SetLastError(0);
        value = call_stdcall(address, e->argc);
        ++records[R_ORIGINAL_CALLS];
        if (value != e->value)
        {
            ++records[R_ORIGINAL_VALUE];
            note(i);
        }
        if (GetLastError() != 120)
        {
            ++records[R_ORIGINAL_ERROR];
            note(i);
        }
        /* The callee removed its own argument bytes, so the stack pointer is
         * back where the pushes began. */
        if (call_after != call_before)
        {
            ++records[R_ORIGINAL_ESP];
            note(i);
        }
    }
    /* These small exports decide their results entirely from their arguments. */
    {
        U bad = 0, checks = 0, mask = 0;
        SetLastError(0);
        SetLastErrorEx(4711, 1);
        ++checks; if (GetLastError() != 4711) { ++bad; mask |= 1u << (checks - 1); }
        SetDebugErrorLevel(0x8000);
        ++checks; if (GetLastError() != 4711) { ++bad; mask |= 1u << (checks - 1); }   /* it touches nothing else */
        ++checks; if (GdiGetBatchLimit() != 1) { ++bad; mask |= 1u << (checks - 1); }
        ++checks; if (GdiSetBatchLimit(20) != 1) { ++bad; mask |= 1u << (checks - 1); }
        ++checks; if (GdiGetBatchLimit() != 1) { ++bad; mask |= 1u << (checks - 1); }
        SetLastError(0);
        ++checks; if (SetGraphicsMode(0, 1) != 1 || GetLastError()) { ++bad; mask |= 1u << (checks - 1); }
        ++checks; if (SetGraphicsMode(0, 2) || GetLastError() != 87) { ++bad; mask |= 1u << (checks - 1); }
        /* The full Win98 SE installation has ICM profile lookup. Invalid
         * handles fail before the worker; the screen reports its default
         * profile and the required buffer size. */
        {
            U size = 260, screen = GetDC(0);
            char profile[260];
            SetLastError(0);
            ++checks; if (GetICMProfileA(0, &size, profile) || GetLastError() != 6) { ++bad; mask |= 1u << (checks - 1); }
            ++checks; if (ColorMatchToTarget(0, 0, 1) || GetLastError() != 6) { ++bad; mask |= 1u << (checks - 1); }
            ++checks; if (!screen) { ++bad; mask |= 1u << (checks - 1); }
            SetLastError(0);
            ++checks; if (!GetICMProfileA(screen, &size, profile) || GetLastError()) { ++bad; mask |= 1u << (checks - 1); }
            ++checks; if (ColorMatchToTarget(screen, screen, 1) || GetLastError() != 8) { ++bad; mask |= 1u << (checks - 1); }
            const char expected_profile[] = "C:\\WINDOWS\\SYSTEM\\COLOR\\sRGB Color Space Profile.icm";
            ++checks; if (size != sizeof expected_profile || !same(profile, expected_profile)) { ++bad; mask |= 1u << (checks - 1); }
            ++checks; if (!ReleaseDC(0, screen)) { ++bad; mask |= 1u << (checks - 1); }
            /* GetRasterizerCaps, GDI. Size six, TT_AVAILABLE |
             * TT_ENABLED because the Win98 folder installs TrueType files and
             * the font driver loads them, and the language
             * of the system; a size of zero writes nothing. */
            {
                unsigned short status[4];
                status[0] = status[1] = status[2] = 0xdead;
                ++checks; if (GetRasterizerCaps(status, 0)) { ++bad; mask |= 1u << (checks - 1); }
                ++checks; if (status[0] != 0xdead) { ++bad; mask |= 1u << (checks - 1); }
                ++checks; if (!GetRasterizerCaps(status, 6)) { ++bad; mask |= 1u << (checks - 1); }
                ++checks; if (status[0] != 6 || status[1] != 3) { ++bad; mask |= 1u << (checks - 1); }
                ++checks; if (status[2] != (unsigned short)GetSystemDefaultLangID()) { ++bad; mask |= 1u << (checks - 1); }
                status[1] = 0xdead;
                ++checks; if (!GetRasterizerCaps(status, 2) || status[1] != 0xdead) { ++bad; mask |= 1u << (checks - 1); }
            }
            /* UpdateColors, GDI. A device without a palette leaves the
             * worker at once with the zero it cleared first, and a handle that
             * is no device context gets the invalid-handle answer. */
            SetLastError(0);
            ++checks; if (UpdateColors(0) || GetLastError() != 6) { ++bad; mask |= 1u << (checks - 1); }
            screen = GetDC(0);
            SetLastError(0);
            ++checks; if (UpdateColors(screen)) { ++bad; mask |= 1u << (checks - 1); }
            ++checks; if (!ReleaseDC(0, screen)) { ++bad; mask |= 1u << (checks - 1); }
        }
        records[R_SMALL_CHECKS] = checks;
        records[R_SMALL_BAD] = bad;
        records[R_SMALL_MASK] = mask;
    }
    records[FAILURES] = failure_count;
    for (i = 0; i < failure_count && i < 64; ++i)
        records[FAILURES + 1 + i] = failures[i];
    if (call_missing)
    {
        /* One export Windows 98 has and this port does not. The run must stop
         * here with the diagnosis; nothing below may run. */
        for (i = 0; i < total; ++i)
        {
            const struct ContractExport *e = &ContractExports[i];
            void *module = 0, *address;
            for (m = 0; m < 6; ++m)
                if (same(e->dll, modules[m]))
                    module = handles[m];
            if (!module || e->kind != 4 || e->argc > 12 || !e->name)
                continue;
            address = GetProcAddress(module, e->name);
            if (!address)
                continue;
            records[CHOSEN] = i;
            call_stdcall(address, e->argc);
            ExitProcess(3);
        }
        ExitProcess(4);
    }
    ExitProcess(0);
    return 0;
}
