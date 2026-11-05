/* SPDX-License-Identifier: GPL-2.0-or-later
 * x86 test program for the resource functions take a file the
 * program mapped itself, named with bit 0 set, as Win98's KERNEL32 does.
 * DSS.DLL of IE 5.01 checks its own signature that way in
 * DllMain. The program maps C:\WINDOWS\NOTEPAD.EXE and reads its version
 * resource through FindResourceA(view | 1, 1, RT_VERSION). */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U OpenFile(const char *, void *, U);
API U CreateFileMappingA(U, void *, U, U, U, const char *);
API void *MapViewOfFile(U, U, U, U, U);
API U UnmapViewOfFile(void *);
API U CloseHandle(U);
API U FindResourceA(U, const char *, const char *);
API U LoadResource(U, U);
API U SizeofResource(U, U);
API void *LockResource(U);
static void check(int value, U code)
{
    if (!value)
        ExitProcess(code);
}
void mainCRTStartup(void)
{
    unsigned char reopen[136];
    U file = OpenFile("C:\\WINDOWS\\NOTEPAD.EXE", reopen, 0);
    check(file != 0xffffffff, 1);
    U mapping = CreateFileMappingA(file, 0, 2, 0, 0, 0);
    check(mapping != 0, 2);
    unsigned char *view = MapViewOfFile(mapping, 4, 0, 0, 0);
    check(view != 0, 3);
    check(view[0] == 'M' && view[1] == 'Z', 4);
    U module = (U)view | 1;
    U found = FindResourceA(module, (const char *)1, (const char *)16);
    check(found != 0, 5);
    U size = SizeofResource(module, found);
    check(size == 856, 6);
    U loaded = LoadResource(module, found);
    check(loaded != 0, 7);
    const unsigned char *data = LockResource(loaded);
    check(data != 0, 8);
    /* VS_VERSIONINFO: length, value length, type, then L"VS_VERSION_INFO". */
    check(data[6] == 'V' && data[8] == 'S' && data[10] == '_', 9);
    check(data >= view && data < view + 0x100000, 10);
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    CloseHandle(file);
    /* The next file mapped in its place is read afresh: ADVPACK checks the
     * versions of one file after the other this way, and the view of the
     * second one usually gets the address of the first. */
    file = OpenFile("C:\\WINDOWS\\CALC.EXE", reopen, 0);
    check(file != 0xffffffff, 11);
    mapping = CreateFileMappingA(file, 0, 2, 0, 0, 0);
    check(mapping != 0, 12);
    view = MapViewOfFile(mapping, 4, 0, 0, 0);
    check(view != 0, 13);
    module = (U)view | 1;
    found = FindResourceA(module, (const char *)1, (const char *)16);
    check(found != 0, 14);
    check(SizeofResource(module, found) == 864, 15);
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    CloseHandle(file);
    ExitProcess(0);
}
