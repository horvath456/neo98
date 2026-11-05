/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The bits of a DIB section are the same memory in every process.
   The program starts itself a second time. Each of the two creates a section
   and reaches the other's through the handle and through the pointer alone.
   The exit code names the first step that failed; 0 is success. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct { U size; int width, height; unsigned short planes, bits; U compression, image, xppm, yppm, used, important; } Header;
typedef struct { int type, width, height, stride; unsigned short planes, depth; U *bits; } Bitmap;
API void ExitProcess(U);
API U GetModuleFileNameA(U, char *, U);
API U CreateProcessA(const char *, char *, void *, void *, U, U, void *, const char *, void *, void *);
API U CreateFileMappingA(U, void *, U, U, U, const char *);
API U OpenFileMappingA(U, U, const char *);
API U *MapViewOfFile(U, U, U, U, U);
API U CreateEventA(void *, U, U, const char *);
API U SetEvent(U);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);
API U CreateDIBSection(U, const Header *, U, U **, U, U);
API U GetObjectA(U, U, void *);
API U CreateCompatibleDC(U);
API U SelectObject(U, U);
API U GetPixel(U, int, int);
enum { ParentHandle, ParentBits, ChildHandle, ChildBits };
static U Section(int side, U **bits)
{
    /* Top-down, so the first pixel in memory is the one at 0,0. */
    Header header = {40, side, -side, 1, 32, 0, 0, 0, 0, 0, 0};
    return CreateDIBSection(0, &header, 0, bits, 0, 0);
}
static void Child(U *shared)
{
    Bitmap bitmap = {0};
    U *own = 0, ready, go, section;
    /* The parent's section: GetObject names the address the parent has. */
    if (GetObjectA(shared[ParentHandle], sizeof bitmap, &bitmap) != sizeof bitmap) ExitProcess(21);
    if ((U)bitmap.bits != shared[ParentBits]) ExitProcess(22);
    if (bitmap.bits[0] != 0x00111111) ExitProcess(23);
    bitmap.bits[0] = 0x00222222;
    section = Section(64, &own);
    if (!section || !own) ExitProcess(24);
    for (U i = 0; i < 64 * 64; ++i) own[i] = 0x00333333;
    shared[ChildHandle] = section;
    shared[ChildBits] = (U)own;
    ready = CreateEventA(0, 1, 0, "NEO98DibShareReady");
    go = CreateEventA(0, 1, 0, "NEO98DibShareGo");
    if (!ready || !go || !SetEvent(ready)) ExitProcess(25);
    if (WaitForSingleObject(go, 20000)) ExitProcess(26);
    /* The parent wrote through the pointer alone. */
    if (own[1] != 0x00444444 || own[64 * 64 - 1] != 0x00555555) ExitProcess(27);
    ExitProcess(0);
}
void mainCRTStartup(void)
{
    char path[260];
    U startup[17] = {68}, process[4] = {0}, *bits = 0, code = 99;
    U mapping = OpenFileMappingA(0xf001f, 0, "NEO98DibShare");
    if (mapping) {
        U *shared = MapViewOfFile(mapping, 0xf001f, 0, 0, 0);
        if (!shared) ExitProcess(20);
        Child(shared);
    }
    mapping = CreateFileMappingA(0xffffffff, 0, 4, 0, 4096, "NEO98DibShare");
    U *shared = mapping ? MapViewOfFile(mapping, 0xf001f, 0, 0, 0) : 0;
    U ready = CreateEventA(0, 1, 0, "NEO98DibShareReady"), go = CreateEventA(0, 1, 0, "NEO98DibShareGo");
    U section = Section(16, &bits);
    if (!shared || !ready || !go) ExitProcess(1);
    if (!section || !bits) ExitProcess(2);
    for (U i = 0; i < 256; ++i) bits[i] = 0x00111111;
    shared[ParentHandle] = section;
    shared[ParentBits] = (U)bits;
    if (!GetModuleFileNameA(0, path, sizeof path)) ExitProcess(3);
    if (!CreateProcessA(path, 0, 0, 0, 0, 0, 0, 0, startup, process)) ExitProcess(4);
    if (WaitForSingleObject(ready, 20000)) {
        /* The child may have ended with its own code. */
        if (!WaitForSingleObject(process[0], 0) && GetExitCodeProcess(process[0], &code) && code) ExitProcess(code);
        ExitProcess(5);
    }
    /* The child's write is in this process's bytes and in GDI's view of them. */
    if (bits[0] != 0x00222222) ExitProcess(6);
    U dc = CreateCompatibleDC(0);
    if (!dc || !SelectObject(dc, section)) ExitProcess(7);
    if (GetPixel(dc, 0, 0) != 0x00222222 || GetPixel(dc, 1, 0) != 0x00111111) ExitProcess(8);
    /* The child's section, through the pointer alone: no call names it here
       before the first access. */
    U *theirs = (U *)shared[ChildBits];
    if (!theirs) ExitProcess(9);
    if (theirs[0] != 0x00333333 || theirs[64 * 64 - 1] != 0x00333333) ExitProcess(10);
    theirs[1] = 0x00444444;
    theirs[64 * 64 - 1] = 0x00555555;
    Bitmap bitmap = {0};
    if (GetObjectA(shared[ChildHandle], sizeof bitmap, &bitmap) != sizeof bitmap) ExitProcess(11);
    if (bitmap.bits != theirs || bitmap.width != 64) ExitProcess(12);
    if (!SelectObject(dc, shared[ChildHandle])) ExitProcess(13);
    if (GetPixel(dc, 1, 0) != 0x00444444 || GetPixel(dc, 0, 0) != 0x00333333) ExitProcess(14);
    if (!SetEvent(go)) ExitProcess(15);
    if (WaitForSingleObject(process[0], 20000)) ExitProcess(16);
    if (!GetExitCodeProcess(process[0], &code)) ExitProcess(17);
    ExitProcess(code);
}
