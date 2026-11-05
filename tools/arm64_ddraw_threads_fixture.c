/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* another thread cannot enter a surface method during enumeration. */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
static IDirectDrawSurface7 *surface;
static HANDLE start, attempting, done;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %d\n", __LINE__); } } while (0)
static DWORD WINAPI Worker(void *unused)
{
    DDSURFACEDESC2 desc = {0};
    (void)unused;
    WaitForSingleObject(start, INFINITE);
    SetEvent(attempting);
    desc.dwSize = sizeof desc;
    HRESULT hr = IDirectDrawSurface7_GetSurfaceDesc(surface, &desc);
    SetEvent(done);
    return hr != DD_OK;
}
static HRESULT WINAPI Enumerate(IDirectDrawSurface7 *child, DDSURFACEDESC2 *description, void *context)
{
    (void)description; (void)context;
    SetEvent(start);
    CHECK(WaitForSingleObject(attempting, 5000) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(done, 30) == WAIT_TIMEOUT);
    /* Reentry on this thread still succeeds. */
    DDSURFACEDESC2 desc = {0};
    desc.dwSize = sizeof desc;
    CHECK(IDirectDrawSurface7_GetSurfaceDesc(child, &desc) == DD_OK);
    IDirectDrawSurface7_Release(child);
    return DDENUMRET_OK;
}
int main(void)
{
    IDirectDraw7 *dd = NULL;
    IDirectDrawSurface7 *child = NULL;
    DDSURFACEDESC2 desc = {0};
    CHECK(DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL) == DD_OK);
    if (!dd) return 1;
    CHECK(IDirectDraw7_SetCooperativeLevel(dd, NULL, DDSCL_NORMAL | DDSCL_MULTITHREADED) == DD_OK);
    desc.dwSize = sizeof desc;
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = desc.dwHeight = 32;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    CHECK(IDirectDraw7_CreateSurface(dd, &desc, &surface, NULL) == DD_OK);
    desc.dwFlags |= DDSD_PIXELFORMAT;
    desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_SYSTEMMEMORY;
    desc.ddpfPixelFormat.dwSize = sizeof desc.ddpfPixelFormat;
    desc.ddpfPixelFormat.dwFlags = DDPF_ZBUFFER;
    desc.ddpfPixelFormat.dwZBufferBitDepth = 16;
    desc.ddpfPixelFormat.dwZBitMask = 0xffff;
    CHECK(IDirectDraw7_CreateSurface(dd, &desc, &child, NULL) == DD_OK);
    if (!surface || !child) return 2;
    CHECK(IDirectDrawSurface7_AddAttachedSurface(surface, child) == DD_OK);
    start = CreateEventA(NULL, TRUE, FALSE, NULL);
    attempting = CreateEventA(NULL, TRUE, FALSE, NULL);
    done = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE thread = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
    if (!start || !attempting || !done || !thread) return 3;
    CHECK(IDirectDrawSurface7_EnumAttachedSurfaces(surface, NULL, Enumerate) == DD_OK);
    CHECK(WaitForSingleObject(done, 5000) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    DWORD code = 1;
    GetExitCodeThread(thread, &code);
    CHECK(code == 0);
    CloseHandle(thread); CloseHandle(start); CloseHandle(attempting); CloseHandle(done);
    IDirectDrawSurface7_Release(child);
    IDirectDrawSurface7_Release(surface);
    IDirectDraw7_Release(dd);
    printf("DDRAW_THREADS checks=%d failures=%d\n", checks, failures);
    return failures;
}
