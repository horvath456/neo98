/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DirectDraw for ARM64 programs beside other programs, for
   tools/test_arm64_dx_host.py. One program, three
   parts, chosen by the first word of the command line:

     end REPORT takes the screen (exclusive mode, 640x480x16, a flip,
                         a Lock of the primary surface) and leaves through
                         ExitProcess without giving anything back
     launch A [| B] starts the command line A, and after a second and a
                         half B, and stays until the host ends, so that the
                         machine keeps a program when A and B have left
     focus REPORT OTHER takes the screen, starts the program OTHER, which
                         takes the foreground, and writes what DirectDraw
                         answers while the screen is gone and after it is back

   A report is a text file of "key=value" rows. */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HWND window;
static FILE *report;

static void Pump(DWORD milliseconds)
{
    const DWORD end = GetTickCount() + milliseconds;
    for (;;)
    {
        MSG message;
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        if ((LONG)(GetTickCount() - end) >= 0) break;
        Sleep(10);
    }
}

static void Row(const char *key, long value)
{
    if (!report) return;
    fprintf(report, "%s=%ld\n", key, value);
    fflush(report);
}

static BOOL Start(const char *command, PROCESS_INFORMATION *process)
{
    STARTUPINFOA startup;
    char line[260];
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    strncpy(line, command, sizeof line - 1);
    line[sizeof line - 1] = 0;
    return CreateProcessA(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, process);
}

/* The screen in exclusive mode with a flipping primary surface whose back
   buffer was filled with `colour` and flipped to the front. */
static BOOL TakeScreen(IDirectDraw **dd_out, IDirectDrawSurface **primary_out, DWORD colour)
{
    IDirectDraw *dd = NULL;
    IDirectDrawSurface *primary = NULL, *back = NULL;
    DDSURFACEDESC d;
    DDSCAPS caps = {DDSCAPS_BACKBUFFER};
    DDBLTFX fx;
    if (DirectDrawCreate(NULL, &dd, NULL) != DD_OK) return FALSE;
    if (IDirectDraw_SetCooperativeLevel(dd, window, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN) != DD_OK) return FALSE;
    if (IDirectDraw_SetDisplayMode(dd, 640, 480, 16) != DD_OK) return FALSE;
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    d.dwBackBufferCount = 1;
    if (IDirectDraw_CreateSurface(dd, &d, &primary, NULL) != DD_OK) return FALSE;
    if (IDirectDrawSurface_GetAttachedSurface(primary, &caps, &back) != DD_OK) return FALSE;
    memset(&fx, 0, sizeof fx);
    fx.dwSize = sizeof fx;
    fx.dwFillColor = colour;
    IDirectDrawSurface_Blt(back, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
    IDirectDrawSurface_Flip(primary, NULL, DDFLIP_WAIT);
    IDirectDrawSurface_Release(back);
    *dd_out = dd;
    *primary_out = primary;
    return TRUE;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    WNDCLASSA klass;
    char *mode = strtok(command, " "), *first = strtok(NULL, " "), *second = strtok(NULL, "");
    IDirectDraw *dd = NULL;
    IDirectDraw4 *dd4 = NULL;
    IDirectDrawSurface *primary = NULL;
    DDSURFACEDESC d;
    PROCESS_INFORMATION process;
    (void)previous;
    (void)show;
    if (!mode || !first) return 2;
    memset(&klass, 0, sizeof klass);
    klass.lpfnWndProc = DefWindowProcA;
    klass.hInstance = instance;
    klass.lpszClassName = "Arm64DirectDrawHost";
    klass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&klass);

    if (!strcmp(mode, "launch"))
    {
        /* The rest of the command line, cut at the bar. */
        char *line = first + strlen(first), *bar;
        if (second) *line = ' ';
        bar = strchr(first, '|');
        if (bar) *bar = 0;
        if (!Start(first, &process)) return 3;
        if (bar)
        {
            Sleep(1500);
            while (*++bar == ' ') {}
            if (!Start(bar, &process)) return 4;
        }
        for (;;) Sleep(1000);
    }

    window = CreateWindowExA(0, klass.lpszClassName, "DirectDraw", WS_POPUP, 0, 0, 200, 150, NULL, NULL, instance, NULL);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    Pump(100);
    report = fopen(first, "w");

    if (!strcmp(mode, "end"))
    {
        /* Red on the screen, the primary surface locked, and gone. */
        Row("taken", TakeScreen(&dd, &primary, 0x7c00));
        Pump(2500);
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        Row("lock", primary ? IDirectDrawSurface_Lock(primary, NULL, &d, DDLOCK_WAIT, NULL) : -1);
        Row("done", 1);
        fclose(report);
        ExitProcess(0);
    }

    if (!strcmp(mode, "focus") && second)
    {
        HWND other = NULL;
        int tries;
        Row("taken", TakeScreen(&dd, &primary, 0x03e0));
        if (!dd || !primary) return 5;
        IDirectDraw_QueryInterface(dd, &IID_IDirectDraw4, (void **)&dd4);
        Row("held_test", IDirectDraw4_TestCooperativeLevel(dd4));
        Row("held_width", GetSystemMetrics(SM_CXSCREEN));
        /* Another program starts and takes the foreground. */
        Row("started", Start(second, &process));
        for (tries = 0; tries < 400 && !other; ++tries)
        {
            Pump(50);
            other = FindWindowA("Notepad", NULL);
        }
        Pump(1500);
        Row("other_window", other != NULL);
        Row("away_iconic", IsIconic(window));
        Row("away_width", GetSystemMetrics(SM_CXSCREEN));
        Row("away_test", IDirectDraw4_TestCooperativeLevel(dd4) == DDERR_NOEXCLUSIVEMODE);
        Row("away_lost", IDirectDrawSurface_IsLost(primary) == DDERR_SURFACELOST);
        Row("away_restore_wrongmode", IDirectDrawSurface_Restore(primary) == DDERR_WRONGMODE);
        Row("away_flip", IDirectDrawSurface_Flip(primary, NULL, DDFLIP_WAIT) != DD_OK);
        /* The other program leaves; this one comes back to the front. */
        if (other) PostMessageA(other, WM_CLOSE, 0, 0);
        Pump(1500);
        ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
        Pump(1000);
        Row("back_iconic", IsIconic(window));
        Row("back_width", GetSystemMetrics(SM_CXSCREEN));
        Row("back_test", IDirectDraw4_TestCooperativeLevel(dd4));
        Row("back_lost", IDirectDrawSurface_IsLost(primary) == DDERR_SURFACELOST);
        Row("back_restore", IDirectDrawSurface_Restore(primary));
        Row("back_islost", IDirectDrawSurface_IsLost(primary));
        {
            /* The restored chain flips again: blue comes to the front. */
            IDirectDrawSurface *back = NULL;
            DDSCAPS caps = {DDSCAPS_BACKBUFFER};
            DDBLTFX fx;
            memset(&fx, 0, sizeof fx);
            fx.dwSize = sizeof fx;
            fx.dwFillColor = 0x001f;
            Row("back_attached", IDirectDrawSurface_GetAttachedSurface(primary, &caps, &back));
            if (back)
            {
                Row("back_fill", IDirectDrawSurface_Blt(back, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx));
                Row("back_flip", IDirectDrawSurface_Flip(primary, NULL, DDFLIP_WAIT));
                IDirectDrawSurface_Release(back);
            }
        }
        Row("shown", 1);
        Pump(2500);
        IDirectDrawSurface_Release(primary);
        IDirectDraw4_Release(dd4);
        IDirectDraw_RestoreDisplayMode(dd);
        IDirectDraw_SetCooperativeLevel(dd, window, DDSCL_NORMAL);
        Row("released", IDirectDraw_Release(dd));
        Row("end_width", GetSystemMetrics(SM_CXSCREEN));
        Row("done", 1);
        fclose(report);
        DestroyWindow(window);
        return 0;
    }
    return 2;
}
