/* SPDX-License-Identifier: GPL-2.0-or-later */
/* IDirectDrawSurface::GetDC and ReleaseDC, many times over.
   DDRAW.DLL 4.06 reaches DD16_GetDC and DD16_ReleaseDC; the second gets the HDC as a word, as every GDI handle is one
   under Win9x. While one surface holds the cached context, a second surface
   gets a context of its own, created and destroyed each time. Its handle
   grows past a word once the generation of its GDI slot passes 15; the port
   then no longer found it at the release, the busy count stayed up, and after
   a few hundred rounds GetDC failed for good (the menu of Smileyshoot stood
   still). The fixture runs 200 rounds and fails with the code of the step. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DirectDrawCreate(void *, void **, void *);
API U SetPixel(U, int, int, U);
API U GetPixel(U, int, int);

static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static U __stdcall proc(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
#define CALL(object, index, ...) (((U(__stdcall *)())((*(void ***)(object))[index]))(object, ##__VA_ARGS__))

static void *surface(void *ddraw)
{
    U desc[27] = {0};
    void *s = 0;
    desc[0] = 108; desc[1] = 7; desc[2] = 32; desc[3] = 32; desc[26] = 0x840;   /* OFFSCREENPLAIN | SYSTEMMEMORY */
    check(CALL(ddraw, 6, desc, &s, 0) == 0, 12);
    return s;
}

void mainCRTStartup(void)
{
    U cls[10] = {0, (U)proc, 0, 0, 0, 0, 0, 0, 0, (U)"SurfaceDc98"};
    void *ddraw = 0, *first, *second;
    U window, round;
    check(RegisterClassA(cls) != 0, 10);
    window = CreateWindowExA(0, "SurfaceDc98", "dc", 0x10cf0000u, 0, 0, 320, 240, 0, 0, 0, 0);
    check(window && DirectDrawCreate(0, &ddraw, 0) == 0, 11);
    check(CALL(ddraw, 20, window, 8) == 0, 11);                  /* DDSCL_NORMAL */
    first = surface(ddraw);
    second = surface(ddraw);
    for (round = 0; round < 200; ++round)
    {
        U a = 0, b = 0;
        check(CALL(first, 17, &a) == 0 && a, 20);                 /* GetDC: the cached context */
        check(CALL(second, 17, &b) == 0 && b, 21);                /* GetDC: a context of its own */
        SetPixel(b, 1, 1, 0x0000ff);
        check((GetPixel(b, 1, 1) & 0xffffff) == 0x0000ff, 22);
        check(CALL(second, 26, b) == 0, 23);                      /* ReleaseDC */
        check(CALL(first, 26, a) == 0, 24);
    }
    CALL(second, 2);
    CALL(first, 2);
    CALL(ddraw, 2);
    ExitProcess(0);
}
