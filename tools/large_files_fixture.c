/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Fixture of tools/test_large_files_native.py: Win98 has no size ceiling for
   programs, libraries, file transfers, mappings, memory moves or the
   registry; only the
   address space and the memory there is limit. The test appends overlays to
   LARGECHILD.EXE (over 32 MiB) and LARGELIB.DLL (over 8 MiB) after linking.
   LARGETEST.EXE exits with 0, or with the number of the step that failed
   (plus 0x10000 and the error when an API refused). */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetLastError(void);

#if defined(LARGE_CHILD)
/* An image of over 64 MiB:.bss alone takes 80 MiB. */
static volatile unsigned char big[80u << 20];

void mainCRTStartup(void)
{
    big[sizeof big - 1] = 42;
    ExitProcess(big[sizeof big - 1]);
}

#elif defined(LARGE_LIBRARY)
U __stdcall Answer(void) { return 7; }
U __stdcall DllMainCRTStartup(U instance, U reason, U reserved) { return 1; }

#else
API U CreateProcessA(const char *, char *, U, U, U, U, U, const char *, U *, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);
API U LoadLibraryA(const char *);
API void *GetProcAddress(U, const char *);
API void *VirtualAlloc(void *, U, U, U);
API U CreateFileA(const char *, U, U, void *, U, U, U);
API U WriteFile(U, const void *, U, U *, void *);
API U ReadFile(U, void *, U, U *, void *);
API U CloseHandle(U);
API U CreateFileMappingA(U, void *, U, U, U, const char *);
API void *MapViewOfFile(U, U, U, U, U);
API U UnmapViewOfFile(void *);
API void RtlMoveMemory(void *, const void *, U);
API U RegCreateKeyA(U, const char *, U *);
API U RegSetValueExA(U, const char *, U, U, const void *, U);
API U RegQueryValueExA(U, const char *, U *, U *, void *, U *);
API U RegCloseKey(U);

#define CHUNK (40u << 20)

static void fail(U step) { ExitProcess(step); }
static void refused(U step) { ExitProcess(0x10000 + step * 0x100 + (GetLastError() & 0xff)); }

static unsigned char value(U i) { return (unsigned char)(i * 7 + (i >> 13)); }

static void value_name(char *out, U i)
{
    out[0] = 'V';
    for (U k = 0; k < 5; ++k) out[5 - k] = (char)('0' + i % 10), i /= 10;
    out[6] = 0;
}

void mainCRTStartup(void)
{
    /* 1. A program file over 32 MiB whose image is over 64 MiB. */
    char command[] = "D:\\LARGECHILD.EXE";
    U startup[17] = {68}, information[4] = {0}, code = 0;
    if (!CreateProcessA(0, command, 0, 0, 0, 0, 0, "D:\\", startup, information)) refused(1);
    WaitForSingleObject(information[0], 0xffffffffu);
    GetExitCodeProcess(information[0], &code);
    if (code != 42) fail(1);

    /* 2. A library file over 8 MiB. */
    U library = LoadLibraryA("D:\\LARGELIB.DLL");
    if (!library) refused(2);
    U (__stdcall *answer)(void) = (U (__stdcall *)(void))GetProcAddress(library, "Answer");
    if (!answer || answer() != 7) fail(2);

    /* 3 and 4. One WriteFile and one ReadFile of 40 MiB each; the file grows to
       80 MiB with a second write. */
    unsigned char *data = VirtualAlloc(0, CHUNK + 4096, 0x3000, 4);
    if (!data) refused(3);
    for (U i = 0; i < CHUNK; ++i) data[i] = value(i);
    U file = CreateFileA("D:\\LARGE.DAT", 0xc0000000u, 0, 0, 2, 0x80, 0), done = 0;
    if (file == ~0u) refused(3);
    if (!WriteFile(file, data, CHUNK, &done, 0) || done != CHUNK) refused(3);
    if (!WriteFile(file, data, CHUNK, &done, 0) || done != CHUNK) refused(3);
    CloseHandle(file);
    for (U i = 0; i < CHUNK; ++i) data[i] = 0;
    file = CreateFileA("D:\\LARGE.DAT", 0x80000000u, 1, 0, 3, 0x80, 0);
    if (file == ~0u) refused(4);
    if (!ReadFile(file, data, CHUNK, &done, 0) || done != CHUNK) refused(4);
    for (U i = 0; i < CHUNK; ++i)
        if (data[i] != value(i)) fail(4);

    /* 5. A mapping of the whole 80 MiB file. */
    U mapping = CreateFileMappingA(file, 0, 2, 0, 0, 0);
    if (!mapping) refused(5);
    const unsigned char *view = MapViewOfFile(mapping, 4, 0, 0, 0);
    if (!view) refused(5);
    if (view[0] != value(0) || view[CHUNK + 12345] != value(12345) || view[2 * CHUNK - 1] != value(CHUNK - 1))
        fail(5);
    UnmapViewOfFile((void *)view);
    CloseHandle(mapping);
    CloseHandle(file);

    /* 6 and 7. RtlMoveMemory (MSVCRT's memcpy/memmove) over 40 MiB, overlapping in
       both directions. */
    for (U i = 0; i < CHUNK; ++i) data[i] = value(i);
    RtlMoveMemory(data + 4096, data, CHUNK);
    if (data[4096] != value(0) || data[CHUNK + 4095] != value(CHUNK - 1) || data[CHUNK / 2 + 4096] != value(CHUNK / 2))
        fail(6);
    RtlMoveMemory(data, data + 4096, CHUNK);
    for (U i = 0; i < CHUNK; i += 4093)
        if (data[i] != value(i)) fail(7);

    /* 8. 5000 registry values in one key, far past the 1024 spare slots the
       table once had. */
    U key = 0, type = 0, size = 4, read = 0;
    char name[8];
    if (RegCreateKeyA(0x80000001u, "Software\\NEO98\\Large", &key)) fail(8);
    for (U i = 0; i < 5000; ++i) {
        value_name(name, i);
        if (RegSetValueExA(key, name, 0, 4, &i, 4)) fail(8);
    }
    for (U i = 0; i < 5000; i += 999) {
        value_name(name, i);
        size = 4;
        if (RegQueryValueExA(key, name, 0, &type, &read, &size) || type != 4 || read != i) fail(9);
    }
    RegCloseKey(key);
    ExitProcess(0);
}
#endif
