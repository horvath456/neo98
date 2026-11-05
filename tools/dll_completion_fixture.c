/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The exports the export contract still listed as missing on 23 September
 * 2026, through their public entries (branch dlls, phase 1). Exit code 0 is a
 * pass, otherwise the number of the failed check. Each block names the
 * original routine it holds the port to; the rules are in the comments of
 * src/x86_*_completion.cpp, src/x86_mmio.cpp, src/x86_registry_hives.cpp and
 * src/x86_win_event.cpp. */
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U GetLastError(void);
API void SetLastError(U);
API U GlobalAlloc(U, U);
API U GlobalLock(U);
API U GlobalUnlock(U);
API U GlobalFree(U);
API U DeleteFileA(const char *);
API U GetFileAttributesA(const char *);
API U RegisterClassA(void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U IsDialogMessage(U, void *);
API U IsDialogMessageA(U, void *);
API U TranslateAccelerator(U, U, void *);
API U GetWindowModuleFileNameA(U, char *, U);
API U GetWindowModuleFileNameW(U, W *, U);
API U GetMouseMovePoints(U, void *, void *, int, U);
API void mouse_event(U, U, U, U, U);
API U SetWinEventHook(U, U, U, U, U, U, U);
API U UnhookWinEvent(U);
API void NotifyWinEvent(U, U, U, U);
API U PeekMessageA(void *, U, U, U, U);
API U GetCurrentThreadId(void);
API U GetDC(U);
API U ReleaseDC(U, U);
API U CreateCompatibleDC(U);
API U CreateCompatibleBitmap(U, int, int);
API U SelectObject(U, U);
API U DeleteObject(U);
API U DeleteDC(U);
API U PatBlt(U, int, int, int, int, U);
API U GetPixel(U, int, int);
API U GetObjectA(U, int, void *);
API U CreateDIBPatternBrushPt(const void *, U);
API U CreateDIBPatternBrush(U, U);
API U CreateBrushIndirect(const void *);
API U FixBrushOrgEx(U, int, int, void *);
API U SetBrushOrgEx(U, int, int, void *);
API U GetMetaRgn(U, U);
API U CreateRectRgn(int, int, int, int);
API U CreatePolyPolygonRgn(const void *, const void *, int, int);
API U PtInRegion(U, int, int);
API U GetAspectRatioFilterEx(U, void *);
API U SetMapperFlags(U, U);
API int EnumICMProfilesA(U, void *, U);
API U PlaySound(const char *, U, U);
API U PlaySoundA(const char *, U, U);
API U mmioOpenA(const char *, void *, U);
API U mmioClose(U, U);
API int mmioRead(U, void *, int);
API int mmioWrite(U, const void *, int);
API int mmioSeek(U, int, int);
API U mmioGetInfo(U, void *, U);
API U mmioSetInfo(U, const void *, U);
API U mmioAdvance(U, void *, U);
API U mmioFlush(U, U);
API U mmioSetBuffer(U, void *, int, U);
API U mmioSendMessage(U, U, U, U);
API U mmioInstallIOProcA(U, void *, U);
API U mmioRenameA(const char *, const char *, void *, U);
API U mmioCreateChunk(U, void *, U);
API U mmioDescend(U, void *, const void *, U);
API U mmioAscend(U, void *, U);
API U auxGetVolume(U, U *);
API U auxSetVolume(U, U);
API U auxOutMessage(U, U, U, U);
API U joyConfigChanged(U);
API U joyGetThreshold(U, U *);
API U joySetThreshold(U, U);
API U joySetCapture(U, U, U, U);
API U joyReleaseCapture(U);
API U mciGetCreatorTask(U);
API U mciGetYieldProc(U, U *);
API U mciSetYieldProc(U, U, U);
API U midiInGetID(U, U *);
API U midiInAddBuffer(U, void *, U);
API U midiInMessage(U, U, U, U);
API U midiInPrepareHeader(U, void *, U);
API U midiInUnprepareHeader(U, void *, U);
API U waveInGetID(U, U *);
API U waveOutGetID(U, U *);
API U midiOutOpen(U *, U, U, U, U);
API U midiOutClose(U);
API U midiOutGetID(U, U *);
API U midiOutCachePatches(U, U, W *, U);
API U midiOutCacheDrumPatches(U, U, W *, U);
API U midiConnect(U, U, U);
API U midiDisconnect(U, U, U);
API U RegCreateKeyA(U, const char *, U *);
API U RegOpenKeyA(U, const char *, U *);
API U RegCloseKey(U);
API U RegSetValueExA(U, const char *, U, U, const void *, U);
API U RegQueryValueExA(U, const char *, U *, U *, void *, U *);
API U RegDeleteKeyA(U, const char *);
API U RegSaveKeyA(U, const char *, void *);
API U RegLoadKeyA(U, const char *, const char *);
API U RegUnLoadKeyA(U, const char *);
API U RegReplaceKeyA(U, const char *, const char *, const char *);
API U RegQueryMultipleValuesA(U, void *, U, char *, U *);
API U RegRemapPreDefKey(U, U);
API U RegFlushKey(U);
API W GetFileTitleW(const W *, W *, W);
/* Phase 2: the gates into USER.EXE, GDI.EXE and DDEML. */
API U AnyPopup(void);
API U CascadeChildWindows(U, U);
API U TileChildWindows(U, U);
API U GetKBCodePage(void);
API U GetOEMCP(void);
API U GetNextQueueWindow(U, U);
API U BlockInput(U);
API U SendInput(U, void *, int);
API U GetCursorPos(int *);
API U SetSystemCursor(U, U);
API U LoadCursorA(U, U);
API U CopyImage(U, U, int, int, U);
API U SwapMouseButton(U);
API int GetSystemMetrics(int);
API U IsHungThread(U);
API U PlaySoundEvent(U);
API U HasSystemSleepStarted(void);
API U UserIsSystemResumeAutomatic(void);
API U UserTickleTimer(U);
API U UserSetDeviceHoldState(U, U);
API U WinOldAppHackoMatic(U);
API U UserSignalProc(U, U, U, U);
API U DragDetect(U, int, int);
API U CreateMenu(void);
API U AppendMenuA(U, U, U, const char *);
API U DestroyMenu(U);
API int DrawMenuBarTemp(U, U, void *, U, U);
API U ShowWindow(U, int);
API U DdeInitializeA(U *, void *, U, U);
API U DdeUninitialize(U);
API U DdeCreateDataHandle(U, const void *, U, U, U, U, U);
API U DdeAddData(U, const void *, U, U);
API U DdeGetData(U, void *, U, U);
API U DdeEnableCallback(U, U, U);
API U DdeGetLastError(U);
API U DdeReconnect(U);
API U DdeFreeDataHandle(U);
API U CheckColorsInGamut(U, void *, void *, U);
API U ColorCorrectPalette(U, U, U, U);
API U GetStockObject(int);
API U CreatePalette(const void *);
API int DrawEscape(U, int, int, const char *);
API U GdiComment(U, U, const void *);
API U GetMiterLimit(U, void *);
API U SetMiterLimit(U, U, void *);
API U SetBoundsRect(U, const void *, U);
API U GetBoundsRect(U, void *, U);
API U LineDDA(int, int, int, int, void *, U);
API U SetMagicColors(U, U, U);
API U SetMetaRgn(U);
API int GetRandomRgn(U, U, int);
API int GetClipRgn(U, U);
API int SelectClipRgn(U, U);
API U CreateEnhMetaFileA(U, const char *, const void *, const char *);
API U CloseEnhMetaFile(U);
API U DeleteEnhMetaFile(U);
API U GetEnhMetaFileA(const char *);
API U GetKerningPairs(U, U, void *);
API U GetKerningPairsA(U, U, void *);

int _fltused = 0x9875;
static U failed;
static void check(int good, U code)
{
    if (!good && !failed) failed = code;
}
static U length(const char *s) { U n = 0; while (s[n]) ++n; return n; }
static int same(const void *a, const void *b, U n)
{
    const unsigned char *x = a, *y = b;
    for (U i = 0; i < n; ++i) if (x[i] != y[i]) return 0;
    return 1;
}
static U __stdcall window_procedure(U hwnd, U message, U wp, U lp) { return DefWindowProcA(hwnd, message, wp, lp); }

/* WinEvents. */
static U seen_event, seen_hook, seen_hwnd, seen_object, seen_child, seen_thread, calls;
static void __stdcall event_procedure(U hook, U event, U hwnd, U object, U child, U thread, U time)
{
    ++calls; seen_hook = hook; seen_event = event; seen_hwnd = hwnd; seen_object = object;
    seen_child = child; seen_thread = thread; (void)time;
}
/* A custom mmio procedure over a static block: "XYZ " files. */
static unsigned char store[64];
static U store_size;
static U __stdcall xyz_procedure(U info, U message, U a, U b)
{
    U *mi = (U *)info;
    if (message == 3) { mi[11] = 0; return 0; }                       /* OPEN: lDiskOffset */
    if (message == 4) return 0;                                          /* CLOSE */
    if (message == 0) {                                                  /* READ */
        U n = 0; unsigned char *out = (unsigned char *)a;
        while (n < b && mi[11] + n < store_size) { out[n] = store[mi[11] + n]; ++n; }
        mi[11] += n; return n;
    }
    if (message == 1 || message == 5) {                                  /* WRITE */
        const unsigned char *in = (const unsigned char *)a;
        for (U n = 0; n < b && mi[11] + n < sizeof store; ++n) store[mi[11] + n] = in[n];
        mi[11] += b; if (mi[11] > store_size) store_size = mi[11]; return b;
    }
    if (message == 2) { mi[11] = b == 2 ? store_size - a : b == 1 ? mi[11] + a : a; return mi[11]; }
    if (message == 0x4000) return a + b;                                 /* a private message */
    return 0;
}
static U profiles_seen;
static U __stdcall profile_procedure(const char *name, U parameter) { (void)name; (void)parameter; ++profiles_seen; return 1; }

static void user32(void)
{
    U fields[10] = {0, (U)window_procedure, 0, 0, GetModuleHandleA(0), 0, 0, 6, 0, (U)"DllCompletion"};
    RegisterClassA(fields);
    const U hwnd = CreateWindowExA(0, "DllCompletion", "x", 0x90000000u, 10, 10, 100, 80, 0, 0, fields[4], 0);
    check(hwnd != 0, 20001);
    /* IsDialogMessage and TranslateAccelerator are the A forms. */
    U msg[7] = {hwnd, 0x0f, 0, 0, 0, 0, 0};
    check(IsDialogMessage(hwnd, msg) == IsDialogMessageA(hwnd, msg), 20002);
    check(TranslateAccelerator(hwnd, 0, msg) == 0, 20003);
    /* GetWindowModuleFileNameW converts the A answer with -1 and counts the NUL. */
    char name[260];
    W wide[260];
    const U a = GetWindowModuleFileNameA(hwnd, name, sizeof name);
    const U w = GetWindowModuleFileNameW(hwnd, wide, 260);
    check(a > 0 && w == a + 1, 20004);
    check(wide[0] == (W)(unsigned char)name[0] && wide[a] == 0, 20005);
    /* GetMouseMovePoints: 87 in EAX for a wrong size or resolution, then the ring. */
    int point[4] = {0, 0, 0, 0}, buffer[64 * 4];
    SetLastError(1234);
    check(GetMouseMovePoints(12, point, buffer, 64, 1) == 0x57 && GetLastError() == 1234, 20010);
    check(GetMouseMovePoints(16, point, buffer, 64, 2) == 0x57, 20011);
    mouse_event(0x8001, 100 * 65536 / 800 + 1, 200 * 65536 / 600 + 1, 0, 0);
    mouse_event(0x8001, 110 * 65536 / 800 + 1, 210 * 65536 / 600 + 1, 0, 0);
    point[0] = 110; point[1] = 210;
    const int got = (int)GetMouseMovePoints(16, point, buffer, 64, 1);
    check(got >= 2 && buffer[0] == 110 && buffer[1] == 210 && buffer[4] == 100 && buffer[5] == 200, 20012);
    point[0] = 7777; point[1] = 7777;
    check(GetMouseMovePoints(16, point, buffer, 64, 1) == 0xffffffffu, 20013);
    point[0] = 110; point[1] = 210;
    check(GetMouseMovePoints(16, point, buffer, 1, 1) == 1, 20014);
    /* WinEvents: a flag above bit 2 refuses the hook, an out-of-context hook of
       this thread gets the event at the next message retrieval, SKIPOWNTHREAD
       keeps it away, an in-context hook needs its module and gets it at once. */
    check(SetWinEventHook(1, 0xffff, 0, (U)event_procedure, 0, 0, 8) == 0, 20020);
    const U outside = SetWinEventHook(0x8000, 0x80ff, 0, (U)event_procedure, 0, 0, 0);
    check(outside != 0, 20021);
    NotifyWinEvent(0x800b, hwnd, 0xfffffffcu, 5);
    U peek[7];
    PeekMessageA(peek, 0, 0, 0, 0);
    check(calls == 1 && seen_event == 0x800b && seen_hwnd == hwnd && seen_object == 0xfffffffcu &&
          seen_child == 5 && seen_hook == outside && seen_thread == GetCurrentThreadId(), 20022);
    NotifyWinEvent(0x9000, hwnd, 0, 0); /* outside the range */
    PeekMessageA(peek, 0, 0, 0, 0);
    check(calls == 1, 20023);
    check(UnhookWinEvent(outside) == 1 && UnhookWinEvent(outside) == 0, 20024);
    const U skip = SetWinEventHook(1, 0xffff, 0, (U)event_procedure, 0, 0, 1);
    NotifyWinEvent(0x8000, hwnd, 0, 0);
    PeekMessageA(peek, 0, 0, 0, 0);
    check(skip && calls == 1, 20025);
    UnhookWinEvent(skip);
    check(SetWinEventHook(1, 0xffff, 0, (U)event_procedure, 0, 0, 4) == 0, 20026);
    const U inside = SetWinEventHook(1, 0xffff, GetModuleHandleA(0), (U)event_procedure, 0, 0, 4);
    NotifyWinEvent(0x8001, hwnd, 0, 0);
    check(inside && calls == 2 && seen_event == 0x8001, 20027);
    UnhookWinEvent(inside);
    DestroyWindow(hwnd);
}

static void gdi32(void)
{
    const U screen = GetDC(0);
    const U dc = CreateCompatibleDC(screen);
    const U bitmap = CreateCompatibleBitmap(screen, 16, 16);
    SelectObject(dc, bitmap);
    /* FixBrushOrgEx is SetBrushOrgEx. */
    int origin[2] = {9, 9};
    check(FixBrushOrgEx(dc, 3, 4, origin) && origin[0] == 0 && origin[1] == 0, 20100);
    check(SetBrushOrgEx(dc, 0, 0, origin) && origin[0] == 3 && origin[1] == 4, 20101);
    /* GetMetaRgn is GetRandomRgn with 2: no meta region, zero. */
    const U region = CreateRectRgn(0, 0, 1, 1);
    check(GetMetaRgn(dc, region) == 0, 20102);
    /* A DIB pattern brush, 8 by 8, 1 bit, DIB_RGB_COLORS: a checkerboard of
       red and blue. The top row of the picture is the last row of the DIB. */
    static struct { U size; int width, height; W planes, bits; U compression, image, xppm, yppm, used, important;
                    U colors[2]; U rows[8]; } dib;
    dib.size = 40; dib.width = 8; dib.height = 8; dib.planes = 1; dib.bits = 1;
    dib.colors[0] = 0x000000ff; /* blue: an RGBQUAD is blue, green, red in memory */
    dib.colors[1] = 0x00ff0000; /* red */
    for (U y = 0; y < 8; ++y) dib.rows[y] = (y & 1) ? 0x55 : 0xaa;
    const U brush = CreateDIBPatternBrushPt(&dib, 0);
    check(brush != 0, 20110);
    U logical[3] = {9, 9, 9};
    check(GetObjectA(brush, sizeof logical, logical) == 12 && logical[0] == 5 && logical[1] == 0 &&
          logical[2] == 0, 20111);
    SelectObject(dc, brush);
    PatBlt(dc, 0, 0, 16, 16, 0x00f00021);
    /* Row 0 of the picture is DIB row 7 (odd): 0x55, so x=0 is index 0 (blue),
       x=1 index 1 (red). */
    check(GetPixel(dc, 0, 0) == 0xff0000 && GetPixel(dc, 1, 0) == 0x0000ff, 20112);
    check(GetPixel(dc, 0, 1) == 0x0000ff && GetPixel(dc, 8, 0) == 0xff0000, 20113);
    /* Too small to realize: 4 by 4 draws nothing. */
    dib.width = 4; dib.height = 4;
    const U small = CreateDIBPatternBrushPt(&dib, 0);
    check(small != 0, 20114);
    dib.width = 8; dib.height = 8;
    /* The usage must be 0 or 1; the header 40 or 12. */
    check(CreateDIBPatternBrushPt(&dib, 2) == 0, 20115);
    dib.size = 20;
    check(CreateDIBPatternBrushPt(&dib, 0) == 0, 20116);
    dib.size = 40;
    /* The global form and CreateBrushIndirect's styles 5 and 6. */
    const U global = GlobalAlloc(2, sizeof dib);
    unsigned char *copy = (unsigned char *)GlobalLock(global);
    for (U i = 0; i < sizeof dib; ++i) copy[i] = ((unsigned char *)&dib)[i];
    GlobalUnlock(global);
    const U from_global = CreateDIBPatternBrush(global, 0);
    const U lb5[3] = {5, 0, global}, lb6[3] = {6, 0, (U)&dib};
    const U indirect5 = CreateBrushIndirect(lb5), indirect6 = CreateBrushIndirect(lb6);
    check(from_global && indirect5 && indirect6, 20117);
    SelectObject(dc, indirect6);
    PatBlt(dc, 0, 0, 16, 16, 0x00f00021);
    check(GetPixel(dc, 1, 0) == 0x0000ff, 20118);
    /* CreatePolyPolygonRgn: two squares, the second one open (the port closes
       it like ), an overlap that the parity rule leaves empty and the
       winding rule fills. */
    int points[] = {0, 0, 10, 0, 10, 10, 0, 10, 5, 5, 15, 5, 15, 15, 5, 15, 5, 5};
    int counts[2] = {4, 5};
    const U alternate = CreatePolyPolygonRgn(points, counts, 2, 1);
    const U winding = CreatePolyPolygonRgn(points, counts, 2, 2);
    check(alternate && winding, 20120);
    check(PtInRegion(alternate, 2, 2) && !PtInRegion(alternate, 7, 7) && PtInRegion(alternate, 12, 12), 20121);
    check(PtInRegion(winding, 7, 7) && !PtInRegion(winding, 20, 20), 20122);
    int one[2] = {1, 4};
    check(CreatePolyPolygonRgn(points, one, 2, 1) == 0, 20123);
    check(CreatePolyPolygonRgn(points, counts, 0, 1) == 0, 20124);
    /* GetAspectRatioFilterEx: zero until SetMapperFlags(1) asks the mapper. */
    int filter[2] = {5, 5};
    check(GetAspectRatioFilterEx(dc, filter) && filter[0] == 0 && filter[1] == 0, 20130);
    check(SetMapperFlags(dc, 1) == 0 && GetAspectRatioFilterEx(dc, filter), 20131);
    check(filter[0] > 0 && filter[1] > 0, 20132);
    check(SetMapperFlags(dc, 0) == 1 && GetAspectRatioFilterEx(dc, filter) && !filter[0], 20133);
    check(!GetAspectRatioFilterEx(0, filter), 20134);
    /* EnumICMProfilesA: a bad procedure is 87 and -1. */
    SetLastError(0);
    check(EnumICMProfilesA(screen, 0, 0) == -1 && GetLastError() == 87, 20140);
    check(EnumICMProfilesA(0, profile_procedure, 0) == -1, 20141);
    DeleteObject(alternate);
    DeleteObject(winding);
    DeleteDC(dc);
    DeleteObject(bitmap);
    DeleteObject(brush);
    DeleteObject(small);
    DeleteObject(from_global);
    DeleteObject(indirect5);
    DeleteObject(indirect6);
    DeleteObject(region);
    GlobalFree(global);
    ReleaseDC(0, screen);
}

static void mmio(void)
{
    U info[18], chunk[5], riff[5];
    for (U i = 0; i < 18; ++i) info[i] = 0;
    /* A RIFF written through the DOS procedure with the chunk calls. */
    U h = mmioOpenA("C:\\MMTEST.RIF", info, 0x1001 | 0x10000); /* CREATE, WRITE, ALLOCBUF */
    check(h != 0 && info[3] == 0, 20200);
    riff[1] = 0; riff[2] = 0x45564157; /* WAVE */
    check(mmioCreateChunk(h, riff, 0x20) == 0 && riff[3] == 8, 20201);
    chunk[0] = 0x61746164; chunk[1] = 0;
    check(mmioCreateChunk(h, chunk, 0) == 0 && chunk[3] == 20 && chunk[4] == 0x10000000, 20202);
    check(mmioWrite(h, "hello", 5) == 5, 20203);
    check(mmioAscend(h, chunk, 0) == 0 && chunk[1] == 5, 20204);
    check(mmioAscend(h, riff, 0) == 0 && riff[1] == 18, 20205);
    check(mmioFlush(h, 0) == 0 && mmioClose(h, 0) == 0, 20206);
    h = mmioOpenA("C:\\MMTEST.RIF", info, 0);
    check(h != 0, 20207);
    riff[2] = 0x45564157;
    check(mmioDescend(h, riff, 0, 0x20) == 0 && riff[1] == 18, 20208);
    chunk[0] = 0x61746164;
    char text[16] = {0};
    check(mmioDescend(h, chunk, riff, 0x10) == 0 && chunk[1] == 5 && mmioRead(h, text, 16) == 6 &&
          same(text, "hello\0", 6), 20209);
    /* GetInfo hands out the MMIOINFO; opened without MMIO_ALLOCBUF the DOS
       file has no buffer, and mmioRead went straight to the procedure. */
    U got[18];
    check(mmioGetInfo(h, got, 0) == 0 && got[1] == 0x20534f44 && got[5] == 0 && got[17] == h, 20210);
    /* SEEK_END subtracts the offset. */
    check(mmioSeek(h, 0, 2) == 26 && mmioSeek(h, 2, 2) == 24, 20211);
    check(mmioSendMessage(0, 0x4000, 1, 2) == 0, 20212);
    check(mmioClose(h, 0) == 0 && mmioClose(h, 0) == 5, 20213);
    /* Rename through the DOS procedure, then a missing file. */
    check(mmioRenameA("C:\\MMTEST.RIF", "C:\\MMTEST2.RIF", 0, 0) == 0, 20214);
    check(GetFileAttributesA("C:\\MMTEST2.RIF") != 0xffffffffu, 20215);
    check(mmioRenameA("C:\\NOTHERE.RIF", "C:\\X.RIF", 0, 0) == 0x101, 20216);
    DeleteFileA("C:\\MMTEST2.RIF");
    /* A MEM file that grows by adwInfo[0]: the buffer must be a global block. */
    const U block = GlobalAlloc(2, 16);
    for (U i = 0; i < 18; ++i) info[i] = 0;
    info[1] = 0x204d454d; info[5] = 16; info[6] = GlobalLock(block); info[12] = 32;
    h = mmioOpenA(0, info, 0x1002); /* CREATE, READWRITE */
    check(h != 0, 20220);
    check(mmioWrite(h, "0123456789abcdefghij", 20) == 20, 20221);
    check(mmioGetInfo(h, got, 0) == 0 && got[5] == 48, 20222);
    check(mmioSeek(h, 0, 0) == 0 && mmioRead(h, text, 4) == 4 && same(text, "0123", 4), 20223);
    check(mmioSeek(h, 100, 0) == 0xffffffffu, 20224);
    /* mmioAdvance on a MEM file: reading is refused. */
    check(mmioAdvance(h, 0, 0) == 0x105, 20225);
    mmioClose(h, 0);
    /* A procedure of the program, installed for "XYZ " and found through the
       file name's "+" extension. */
    const U xyz = 0x205a5958;
    check(mmioInstallIOProcA(xyz, xyz_procedure, 0x10000) == (U)xyz_procedure, 20230);
    check(mmioInstallIOProcA(xyz, 0, 0x40000) == (U)xyz_procedure, 20231);
    check(mmioInstallIOProcA(xyz, xyz_procedure, 0x40000) == 0, 20232);
    for (U i = 0; i < 18; ++i) info[i] = 0;
    h = mmioOpenA("archive.xyz+member", info, 0x10001);
    check(h != 0 && mmioGetInfo(h, got, 0) == 0 && got[1] == xyz, 20233);
    check(mmioWrite(h, "abcdef", 6) == 6 && mmioFlush(h, 0) == 0 && store_size == 6 && same(store, "abcdef", 6), 20234);
    check(mmioSendMessage(h, 0x4000, 40, 2) == 42, 20235);
    check(mmioClose(h, 0) == 0, 20236);
    check(mmioInstallIOProcA(xyz, 0, 0x20000) == (U)xyz_procedure, 20237);
    check(mmioInstallIOProcA(xyz, 0, 0x40000) == 0, 20238);
    /* mmioSetBuffer and mmioSetInfo on a closed handle are 5. */
    check(mmioSetBuffer(h, 0, 0, 0) == 5 && mmioSetInfo(h, got, 0) == 5 && mmioAdvance(h, 0, 0) == 5, 20239);
    GlobalFree(block);
}

static void winmm(void)
{
    /* PlaySound is PlaySoundA. */
    check(PlaySound(0, 0, 0) == PlaySoundA(0, 0, 0), 20300);
    U value = 0xabcd1234u;
    check(auxGetVolume(0, &value) == 2 && auxGetVolume(0, 0) == 11, 20301);
    check(auxSetVolume(0, 0) == 2, 20302);
    check(auxOutMessage(0x10000, 3, 0, 0) == 5 && auxOutMessage(0, 7, 0, 0) == 11 &&
          auxOutMessage(0, 3, (U)&value, 0) == 5, 20303);
    check(joyConfigChanged(0) == 6 && joyConfigChanged(1) == 11, 20304);
    value = 0xffffffffu;
    check(joyGetThreshold(0, &value) == 11 && value == 0x0000ffffu, 20305);
    check(joySetThreshold(0, 5) == 11 && joySetCapture(0, 0, 10, 0) == 11 && joyReleaseCapture(0) == 11, 20306);
    check(mciGetCreatorTask(0) == 0 && mciGetCreatorTask(77) == 0, 20307);
    U data = 5;
    check(mciGetYieldProc(1, &data) == 0 && data == 5 && mciSetYieldProc(1, 1, 1) == 0, 20308);
    check(midiInGetID(0, &value) == 5 && waveInGetID(0, &value) == 5 && midiInAddBuffer(0, 0, 0) == 5, 20309);
    check(midiInMessage(0, 0x35, (U)&value, 0) == 5 && midiInMessage(0, 1, 0, 0) == 11, 20310);
    /* waveOutGetID tests the handle first: no identifier passes. */
    check(waveOutGetID(0, &value) == 5 && waveOutGetID(0xffffffffu, &value) == 5, 20311);
    U out = 0, second = 0;
    if (midiOutOpen(&out, 0, 0, 0, 0) == 0)
    {
        value = 99;
        check(midiOutGetID(out, &value) == 0 && value == 0 && midiOutGetID(out, 0) == 11, 20312);
        W patches[128];
        check(midiOutCachePatches(out, 0, patches, 8) == 10 && midiOutCachePatches(out, 0, patches, 0) == 8, 20313);
        check(midiOutCacheDrumPatches(out, 0, 0, 0) == 11, 20314);
        check(midiConnect(out, 0, 0) == 5 && midiDisconnect(out, 0, 0) == 8, 20315);
        char header[0x40] = {0};
        check(midiInPrepareHeader(out, header, 0x40) == 5 && midiInUnprepareHeader(out, header, 0x40) == 0, 20316);
        check(midiInPrepareHeader(0, header, 0x40) == 5, 20317);
        midiOutClose(out);
        (void)second;
    }
}

static void advapi32(void)
{
    U key = 0, loaded = 0, size = 0, type = 0;
    char buffer[64];
    check(RegCreateKeyA(0x80000001u, "Software\\DllCompletion", &key) == 0, 20400);
    check(RegSetValueExA(key, "Text", 0, 1, "hive", 5) == 0 && RegSetValueExA(key, "Number", 0, 4, "\x2a\0\0\0", 4) == 0, 20401);
    DeleteFileA("C:\\HIVE.DAT");
    /* RegSaveKey writes a CREG hive of the subtree; an existing file is overwritten. */
    check(RegSaveKeyA(key, "C:\\HIVE.DAT", 0) == 0 && RegSaveKeyA(key, "C:\\HIVE.DAT", 0) == 0, 20402);
    check(RegSaveKeyA(0x50000000u, "C:\\HIVE2.DAT", 0) == 1010, 20403);
    /* RegLoadKey only below HKLM or HKU, one name component, not yet there. */
    check(RegLoadKeyA(0x80000001u, "Loaded", "C:\\HIVE.DAT") == 1010, 20404);
    check(RegLoadKeyA(0x80000003u, "A\\B", "C:\\HIVE.DAT") == 1010, 20405);
    check(RegLoadKeyA(0x80000003u, "Loaded", "C:\\NOHIVE.DAT") == 1016, 20406);
    check(RegLoadKeyA(0x80000003u, "Loaded", "C:\\HIVE.DAT") == 0, 20407);
    check(RegLoadKeyA(0x80000003u, "Loaded", "C:\\HIVE.DAT") == 1010, 20408);
    check(RegOpenKeyA(0x80000003u, "Loaded", &loaded) == 0, 20409);
    size = sizeof buffer;
    check(RegQueryValueExA(loaded, "Text", 0, &type, buffer, &size) == 0 && type == 1 && size == 5 &&
          same(buffer, "hive", 5), 20410);
    /* RegQueryMultipleValues: the lengths go out first, 234 when they do not fit. */
    U list[8] = {(U)"Text", 0, 0, 0, (U)"Number", 0, 0, 0}, total = 4;
    check(RegQueryMultipleValuesA(loaded, list, 2, buffer, &total) == 234 && total == 9 && list[1] == 5 &&
          list[7] == 4, 20411);
    total = sizeof buffer;
    check(RegQueryMultipleValuesA(loaded, list, 2, buffer, &total) == 0 && total == 9 && list[2] == (U)buffer &&
          list[6] == (U)buffer + 5 && *(U *)(buffer + 5) == 0x2a, 20412);
    U missing[4] = {(U)"None", 0, 0, 0};
    check(RegQueryMultipleValuesA(loaded, missing, 1, buffer, &total) == 2, 20413);
    RegCloseKey(loaded);
    check(RegUnLoadKeyA(0x80000003u, "Loaded") == 0 && RegUnLoadKeyA(0x80000003u, "Loaded") == 1010, 20414);
    check(RegOpenKeyA(0x80000003u, "Loaded", &loaded) != 0, 20415);
    /* RegReplaceKey needs a direct child of HKLM or HKU. */
    check(RegReplaceKeyA(key, 0, "C:\\HIVE.DAT", "C:\\OLD.DAT") == 87, 20416);
    /* RegRemapPreDefKey: HKCU may move to a key opened through HKU; handles
       opened through the old HKCU turn stale. */
    U through_user = 0, through_users = 0;
    check(RegOpenKeyA(0x80000001u, "Software", &through_user) == 0, 20420);
    check(RegCreateKeyA(0x80000003u, ".Default", &through_users) == 0, 20421);
    check(RegRemapPreDefKey(key, 0x80000001u) == 1010, 20422);
    check(RegRemapPreDefKey(through_users, 0x80000001u) == 0, 20423);
    check(RegFlushKey(through_user) == 1010, 20424);
    RegCloseKey(through_user);
    RegCloseKey(through_users);
    RegCloseKey(key);
    RegDeleteKeyA(0x80000001u, "Software\\DllCompletion");
    DeleteFileA("C:\\HIVE.DAT");
}

static void comdlg32(void)
{
    /* GetFileTitleW is the Unicode error gate. */
    W title[8];
    SetLastError(0);
    check(GetFileTitleW(0, title, 8) == 0 && GetLastError() == 120, 20500);
}

static U dda_points, dda_last_x, dda_last_y;
static void __stdcall dda_procedure(int x, int y, U parameter)
{
    if (parameter == 0x5151) { ++dda_points; dda_last_x = (U)x; dda_last_y = (U)y; }
}
static U __stdcall dde_callback(U type, U format, U conv, U a, U b, U data, U d1, U d2)
{
    (void)type; (void)format; (void)conv; (void)a; (void)b; (void)data; (void)d1; (void)d2;
    return 0;
}

static void user32_gates(void)
{
    U fields[10] = {0, (U)window_procedure, 0, 0, GetModuleHandleA(0), 0, 0, 6, 0, (U)"DllGates"};
    RegisterClassA(fields);
    const U main = CreateWindowExA(0, "DllGates", "gates", 0x10cf0000u, 0, 0, 300, 200, 0, 0, fields[4], 0);
    check(main != 0, 20600);
    /* AnyPopup: an owned, visible top-level window answers 1. */
    const U before = AnyPopup();
    const U popup = CreateWindowExA(0, "DllGates", "popup", 0x80000000u, 10, 10, 50, 50, main, 0, fields[4], 0);
    check(AnyPopup() == before, 20601);
    ShowWindow(popup, 5);
    check(AnyPopup() == 1, 20602);
    DestroyWindow(popup);
    /* The child arrangement is CascadeWindows and TileWindows without a list. */
    const U c1 = CreateWindowExA(0, "DllGates", "c1", 0x50c00000u, 0, 0, 40, 40, main, 0, fields[4], 0);
    const U c2 = CreateWindowExA(0, "DllGates", "c2", 0x50c00000u, 0, 0, 40, 40, main, 0, fields[4], 0);
    check(c1 && c2, 20603);
    check(CascadeChildWindows(main, 0) == 2 && TileChildWindows(main, 0) == 2, 20604);
    check(GetKBCodePage() == (GetOEMCP() & 0xffff), 20605);
    /* BlockInput: one owner; SendInput with a wrong size or no records is 0. */
    check(BlockInput(1) == 1 && BlockInput(1) == 0 && BlockInput(0) == 1 && BlockInput(0) == 0, 20606);
    U inputs[7] = {0, 0, 0, 0, 0x8001, 0, 0};
    inputs[1] = 50 * 65536 / 800 + 1; inputs[2] = 60 * 65536 / 600 + 1;
    check(SendInput(1, inputs, 20) == 0 && SendInput(0, inputs, 28) == 0, 20607);
    int cursor[2] = {0, 0};
    check(SendInput(1, inputs, 28) == 1 && GetCursorPos(cursor) && cursor[0] == 50 && cursor[1] == 60, 20608);
    /* SetSystemCursor takes a copy; a bad handle or id fails. */
    const U arrow = CopyImage(LoadCursorA(0, 32512), 2, 0, 0, 0);
    check(arrow && SetSystemCursor(arrow, 32512) == 1 && SetSystemCursor(arrow, 12345) == 0 &&
          SetSystemCursor(0x1234, 32512) == 0, 20609);
    /* SwapMouseButton answers the previous word and sets SM_SWAPBUTTON. */
    check(SwapMouseButton(1) == 0 && GetSystemMetrics(23) == 1 && SwapMouseButton(0) == 1 &&
          GetSystemMetrics(23) == 0, 20610);
    check(IsHungThread(GetCurrentThreadId()) == 0, 20611);
    check(PlaySoundEvent(0x1234) == 0, 20612);
    /* The power words: WinOldAppHackoMatic 8 starts the sleep unless held. */
    check(HasSystemSleepStarted() == 0 && UserIsSystemResumeAutomatic() == 0, 20613);
    UserSetDeviceHoldState(1, 1);
    check(WinOldAppHackoMatic(8) == 1 && HasSystemSleepStarted() == 0, 20614);
    UserSetDeviceHoldState(1, 0);
    check(WinOldAppHackoMatic(8) == 0 && HasSystemSleepStarted() == 1, 20615);
    check(WinOldAppHackoMatic(12) == 0 && UserTickleTimer(3) == 0 && UserSignalProc(0x10, 0, 0, 0) == 1, 20616);
    /* DragDetect without the button down is FALSE at once. */
    check(DragDetect(main, 5, 5) == 0, 20617);
    /* DrawMenuBarTemp fills the strip with the menu colour. */
    const U menu = CreateMenu();
    AppendMenuA(menu, 0, 1, "&File");
    AppendMenuA(menu, 0, 2, "&Edit");
    const U screen = GetDC(0), dc = CreateCompatibleDC(screen), bitmap = CreateCompatibleBitmap(screen, 200, 40);
    SelectObject(dc, bitmap);
    PatBlt(dc, 0, 0, 200, 40, 0x00ff0062); /* WHITENESS */
    int rect[4] = {0, 0, 200, 20};
    DrawMenuBarTemp(main, dc, rect, menu, 0);
    check(GetPixel(dc, 190, 10) == 0xc0c0c0 && GetPixel(dc, 190, 30) == 0xffffff, 20618);
    DeleteDC(dc); DeleteObject(bitmap); ReleaseDC(0, screen); DestroyMenu(menu);
    DestroyWindow(main);
}

static void dde_gates(void)
{
    U instance = 0;
    check(DdeInitializeA(&instance, dde_callback, 0, 0) == 0, 20700);
    const U data = DdeCreateDataHandle(instance, "abcd", 4, 0, 0, 1, 0);
    check(data != 0, 20701);
    /* DdeAddData grows the block and answers the same handle. */
    check(DdeAddData(data, "wxyz", 4, 6) == data, 20702);
    char buffer[16] = {0};
    check(DdeGetData(data, buffer, 16, 0) == 10 && same(buffer, "abcd", 4) && same(buffer + 6, "wxyz", 4), 20703);
    check(DdeAddData(0x1234, "x", 1, 0) == 0, 20704);
    /* DdeEnableCallback: nothing is ever queued; a bad command is 4006h. */
    check(DdeEnableCallback(instance, 0, 2) == 0 && DdeEnableCallback(instance, 0, 8) == 1 &&
          DdeEnableCallback(instance, 0, 0) == 1, 20705);
    check(DdeEnableCallback(instance, 0, 0x100) == 0 && DdeGetLastError(instance) == 0x4006, 20706);
    check(DdeReconnect(0x1234) == 0, 20707);
    DdeFreeDataHandle(data);
    DdeUninitialize(instance);
}

static void gdi32_gates(void)
{
    const U screen = GetDC(0), dc = CreateCompatibleDC(screen), bitmap = CreateCompatibleBitmap(screen, 64, 64);
    SelectObject(dc, bitmap);
    U triples[3] = {0}, answer[3] = {0};
    check(CheckColorsInGamut(dc, triples, answer, 3) == 0 && CheckColorsInGamut(0, triples, answer, 3) == 0, 20800);
    static struct { W version, count; U entries[2]; } logical = {0x300, 2, {0x00ff0000, 0x0000ff00}};
    const U palette = CreatePalette(&logical);
    check(ColorCorrectPalette(dc, GetStockObject(15), 0, 1) == 0 && ColorCorrectPalette(dc, palette, 0, 1) == 1 &&
          ColorCorrectPalette(dc, dc, 0, 1) == 0, 20801);
    check(DrawEscape(dc, 8, 4, "abcd") == 0 && DrawEscape(0, 8, 4, "abcd") == -1, 20802);
    SetLastError(0);
    check(GdiComment(dc, 0, "x") == 0 && GetLastError() == 87 && GdiComment(dc, 4, "abcd") == 1, 20803);
    /* The miter limit: 10.0, refused below 1.0, the old value handed out. */
    float limit = 0.0f, old = 0.0f;
    const float half = 0.5f, four = 4.0f;
    check(GetMiterLimit(dc, &limit) && limit == 10.0f, 20804);
    check(SetMiterLimit(dc, *(const U *)&half, &old) == 0 && SetMiterLimit(dc, *(const U *)&four, &old) == 1 &&
          old == 10.0f && GetMiterLimit(dc, &limit) && limit == 4.0f, 20805);
    /* Bounds: reset and enable, draw, read back DCB_SET. */
    int bounds[4] = {0};
    check(SetBoundsRect(dc, 0, 1 | 4) == (1 | 8), 20806);
    check(GetBoundsRect(dc, bounds, 0) == 1, 20807);
    PatBlt(dc, 5, 6, 10, 10, 0x00ff0062);
    check(GetBoundsRect(dc, bounds, 0) == 3 && bounds[0] == 5 && bounds[1] == 6 && bounds[2] == 15 &&
          bounds[3] == 16, 20808);
    check(SetBoundsRect(dc, 0, 8) == (3 | 4) && GetBoundsRect(dc, bounds, 1) == 3 && GetBoundsRect(dc, bounds, 0) == 1, 20809);
    /* LineDDA leaves the end point out. */
    check(LineDDA(0, 0, 5, 2, dda_procedure, 0x5151) && dda_points == 5 && dda_last_x == 4, 20810);
    check(SetMagicColors(dc, 0, 8) == 1, 20811);
    /* SetMetaRgn moves the clip region into the meta region. */
    const U clip = CreateRectRgn(0, 0, 20, 20), probe = CreateRectRgn(0, 0, 1, 1);
    SelectClipRgn(dc, clip);
    check(GetRandomRgn(dc, probe, 2) == 0 && GetClipRgn(dc, probe) == 1, 20812);
    check(SetMetaRgn(dc) != 0 && GetRandomRgn(dc, probe, 2) == 1 && GetClipRgn(dc, probe) == 0 &&
          GetRandomRgn(dc, probe, 3) == 1 && PtInRegion(probe, 10, 10) && !PtInRegion(probe, 30, 30), 20813);
    /* GetEnhMetaFileA reads back what CreateEnhMetaFileA wrote. */
    DeleteFileA("C:\\GATES.EMF");
    const U emf_dc = CreateEnhMetaFileA(0, "C:\\GATES.EMF", 0, 0);
    check(emf_dc != 0 && GdiComment(emf_dc, 6, "hello!") == 1, 20814);
    const U emf = CloseEnhMetaFile(emf_dc);
    check(emf && DeleteEnhMetaFile(emf), 20815);
    const U again = GetEnhMetaFileA("C:\\GATES.EMF");
    check(again != 0 && DeleteEnhMetaFile(again) && GetEnhMetaFileA("C:\\NONE.EMF") == 0, 20816);
    DeleteFileA("C:\\GATES.EMF");
    check(GetKerningPairs(dc, 0, 0) == GetKerningPairsA(dc, 0, 0), 20817);
    DeleteObject(clip); DeleteObject(probe); DeleteObject(palette);
    DeleteDC(dc); DeleteObject(bitmap); ReleaseDC(0, screen);
}

void mainCRTStartup(void)
{
    user32();
    gdi32();
    mmio();
    winmm();
    advapi32();
    comdlg32();
    user32_gates();
    dde_gates();
    gdi32_gates();
    ExitProcess(failed);
}
