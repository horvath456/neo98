/* SPDX-License-Identifier: GPL-2.0-or-later */
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <dsound.h>

static void require(int okay, UINT code) { if (!okay) ExitProcess(code); }
static LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

static void child(int clean)
{
    WNDCLASSA c = {0};
    c.lpfnWndProc = procedure;
    c.lpszClassName = "DDUnload";
    c.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&c);
    HWND h = CreateWindowA(c.lpszClassName, "DDUnload", WS_OVERLAPPEDWINDOW,
                          20, 20, 400, 260, 0, 0, 0, 0);
    require(h != 0, 1);
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    HMODULE sound_library = LoadLibraryA("DSOUND.DLL");
    typedef HRESULT (WINAPI *SoundCreate)(GUID *, LPDIRECTSOUND *, IUnknown *);
    SoundCreate sound_create = (SoundCreate)GetProcAddress(sound_library, "DirectSoundCreate");
    LPDIRECTSOUND sound = 0;
    require(sound_create && sound_create(0, &sound, 0) == 0, 15);
    require(IDirectSound_SetCooperativeLevel(sound, h, DSSCL_NORMAL) == 0, 16);
    HMODULE library = LoadLibraryA("DDRAW.DLL");
    require(library != 0, 2);
    typedef HRESULT (WINAPI *Create)(GUID *, LPDIRECTDRAW *, IUnknown *);
    Create create = (Create)GetProcAddress(library, "DirectDrawCreate");
    require(create != 0, 3);
    LPDIRECTDRAW dd = 0;
    LPDIRECTDRAWSURFACE primary = 0, back = 0;
    LPDIRECTDRAWCLIPPER clip = 0;
    require(create(0, &dd, 0) == 0, 4);
    require(IDirectDraw_SetCooperativeLevel(dd, h, DDSCL_NORMAL) == 0, 5);
    DDSURFACEDESC d = {0};
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS;
    d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    require(IDirectDraw_CreateSurface(dd, &d, &primary, 0) == 0, 6);
    require(IDirectDraw_CreateClipper(dd, 0, &clip, 0) == 0, 7);
    require(IDirectDrawClipper_SetHWnd(clip, 0, h) == 0, 8);
    require(IDirectDrawSurface_SetClipper(primary, clip) == 0, 9);
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    d.dwWidth = 64;
    d.dwHeight = 64;
    d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    require(IDirectDraw_CreateSurface(dd, &d, &back, 0) == 0, 10);
    DDBLTFX fx = {0};
    fx.dwSize = sizeof fx;
    fx.dwFillColor = 0x00ff00;
    require(IDirectDrawSurface_Blt(back, 0, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &fx) == 0, 11);
    POINT p = {0, 0};
    ClientToScreen(h, &p);
    RECT r = {p.x, p.y, p.x + 64, p.y + 64};
    require(IDirectDrawSurface_Blt(primary, &r, back, 0, DDBLT_WAIT, 0) == 0, 12);
    HDC dc = GetDC(h);
    COLORREF colour = GetPixel(dc, 20, 20);
    ReleaseDC(h, dc);
    require(colour == RGB(0, 255, 0), 13);
    OutputDebugStringA("DDUNLOAD child pixels PASS");
    IDirectDrawSurface_Release(back);
    IDirectDrawSurface_Release(primary);
    IDirectDrawClipper_Release(clip);
    IDirectDraw_Release(dd);
    require(FreeLibrary(library), 14);
    if (clean)
    {
        IDirectSound_Release(sound);
        require(FreeLibrary(sound_library), 17);
    }
    DestroyWindow(h);
    /* Deliberately leave DirectSound alive for DDHELP's process cleanup. */
    ExitProcess(0);
}

void mainCRTStartup(void)
{
    int clean = 0;
    for (const char *p = GetCommandLineA(); *p; ++p)
    {
        if (*p == '!') child(p[1] == 'c');
        if (*p == '+') clean = 1;
    }
    char name[MAX_PATH], command[MAX_PATH + 8];
    GetModuleFileNameA(0, name, sizeof name);
    for (int i = 0; i < 3; ++i)
    {
        char *out = command;
        *out++ = '"';
        for (const char *in = name; *in; ++in) *out++ = *in;
        *out++ = '"'; *out++ = ' '; *out++ = '!';
        if (clean) *out++ = 'c';
        *out = 0;
        STARTUPINFOA si = {0};
        PROCESS_INFORMATION pi = {0};
        si.cb = sizeof si;
        require(CreateProcessA(0, command, 0, 0, 0, 0, 0, 0, &si, &pi), 20 + i);
        require(WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0, 30 + i);
        DWORD code = 99;
        require(GetExitCodeProcess(pi.hProcess, &code), 40 + i);
        require(code == 0, 60 + code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Sleep(500); // DDHELP handles the ended process before the next launch.
    }
    OutputDebugStringA("DDUNLOAD done");
    ExitProcess(0);
}
