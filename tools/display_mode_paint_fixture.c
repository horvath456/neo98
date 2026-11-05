/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ChangeDisplaySettings while another thread holds a paint open.
   Win98 changes the mode at once and answers DISP_CHANGE_SUCCESSFUL, however
   long the paint stays open and on whichever thread it is; so does the port
   (the paint draws on its window's own surface). The second thread holds its
   paint for three seconds, longer than the two the port once waited, and the
   first thread then changes the mode from inside a paint of its own. DDRAW's RestoreDisplayMode does not survive a
   failure here: it calls SetMode a second time with the DDHAL_SETMODEDATA
   whose field +8 the HEL callback has overwritten with E_FAIL (0xb02b), and
   0xaf42 reads that as a pointer. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateThread(U, U, U(__stdcall *)(U), U, U, U *);
API U CreateEventA(U, U, U, U);
API U SetEvent(U);
API U WaitForSingleObject(U, U);
API void Sleep(U);
API U CloseHandle(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U ShowWindow(U, U);
API U InvalidateRect(U, const void *, U);
API U BeginPaint(U, void *);
API U EndPaint(U, const void *);
API U EnumDisplaySettingsA(const char *, U, void *);
API U ChangeDisplaySettingsExA(const char *, void *, U, U, U);
API U GetTickCount(void);

static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static U __stdcall proc(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
static U painter_dc;
static U __stdcall painter(U event)
{
    U paint[16];
    /* Shown without activation (SW_SHOWNA): activating it would send to the
       first thread, which waits in WaitForSingleObject. */
    U window = CreateWindowExA(0, "DisplayModePaint98", "painter", 0x00cf0000, 0, 0, 120, 90, 0, 0, 0, 0);
    ShowWindow(window, 8);
    InvalidateRect(window, 0, 1);
    painter_dc = BeginPaint(window, paint);
    SetEvent(event);
    Sleep(3000);
    EndPaint(window, paint);
    DestroyWindow(window);
    return 0;
}

void mainCRTStartup(void)
{
    U cls[10] = {0, (U)proc, 0, 0, 0, 0, 0, 0, 0, (U)"DisplayModePaint98"};
    unsigned char mode[156] = {0};
    U event, thread, id = 0;
    check(RegisterClassA(cls) != 0, 10);
    event = CreateEventA(0, 1, 0, 0);
    thread = CreateThread(0, 0, painter, event, 0, &id);
    check(event && thread, 11);
    check(WaitForSingleObject(event, 5000) == 0, 12);
    check(painter_dc != 0, 13);
    *(unsigned short *)(mode + 36) = 156;
    check(EnumDisplaySettingsA(0, 0xffffffff, mode) != 0, 14);
    *(U *)(mode + 108) = 640; *(U *)(mode + 112) = 480; *(U *)(mode + 104) = 8;
    /* The paint of the second thread is still open here. */
    /* WM_DISPLAYCHANGE goes to the painter's window by SendMessage, so the
       answer may wait for its thread; it must not fail. */
    check(ChangeDisplaySettingsExA(0, mode, 0, 0, 0) == 0, 15);
    U start;
    check(EnumDisplaySettingsA(0, 0xffffffff, mode) != 0 &&
          *(U *)(mode + 108) == 640 && *(U *)(mode + 104) == 8, 16);
    check(ChangeDisplaySettingsExA(0, 0, 0, 0, 0) == 0, 17);
    check(EnumDisplaySettingsA(0, 0xffffffff, mode) != 0 &&
          *(U *)(mode + 108) == 800 && *(U *)(mode + 104) == 32, 18);
    check(WaitForSingleObject(thread, 5000) == 0, 19);
    /* The same from inside a paint of this thread, as a program that restores
       the mode while it handles WM_PAINT. */
    {
        U paint[16];
        U window = CreateWindowExA(0, "DisplayModePaint98", "own", 0x00cf0000, 0, 0, 120, 90, 0, 0, 0, 0);
        ShowWindow(window, 8);
        InvalidateRect(window, 0, 1);
        check(BeginPaint(window, paint) != 0, 21);
        start = GetTickCount();
        check(ChangeDisplaySettingsExA(0, mode, 0, 0, 0) == 0, 22);
        check(ChangeDisplaySettingsExA(0, 0, 0, 0, 0) == 0, 23);
        check(GetTickCount() - start < 1000, 24);
        EndPaint(window, paint);
        DestroyWindow(window);
    }
    CloseHandle(thread);
    CloseHandle(event);
    ExitProcess(0);
}
