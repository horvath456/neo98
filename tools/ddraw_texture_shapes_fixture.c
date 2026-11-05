/* SPDX-License-Identifier: GPL-2.0-or-later */
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>

static HANDLE report;
static void Row(DWORD test, DWORD item, HRESULT hr, DWORD a, DWORD b, DWORD c)
{
    DWORD row[6] = {test, item, (DWORD)hr, a, b, c}, written;
    if (!WriteFile(report, row, sizeof row, &written, NULL) || written != sizeof row) ExitProcess(3);
}
static HRESULT FillFace(IDirectDrawSurface7 *surface, DWORD color)
{
    DDSURFACEDESC2 lock = {0};
    HRESULT hr;
    DWORD x, y;
    lock.dwSize = sizeof lock;
    hr = IDirectDrawSurface7_Lock(surface, NULL, &lock, DDLOCK_WAIT, NULL);
    if (hr != DD_OK) return hr;
    for (y = 0; y < lock.dwHeight; ++y)
        for (x = 0; x < lock.dwWidth; ++x)
            ((DWORD *)((BYTE *)lock.lpSurface + y * lock.lPitch))[x] = color;
    return IDirectDrawSurface7_Unlock(surface, NULL);
}
static void RenderCube(IDirectDraw7 *dd)
{
    IDirect3D7 *d3d = NULL;
    IDirect3DDevice7 *device = NULL;
    IDirectDrawSurface7 *target = NULL, *cube = NULL, *faces[6] = {0};
    DDSURFACEDESC2 desc = {0};
    D3DVIEWPORT7 viewport = {0, 0, 32, 8, 0.0f, 1.0f};
    static const DWORD colors[6] = {0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0xff00ff, 0x00ffff};
    static const float direction[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    HRESULT hr;
    unsigned f, pass;
    desc.dwSize = sizeof desc;
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = 32; desc.dwHeight = 8;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwSize = sizeof desc.ddpfPixelFormat;
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 32;
    desc.ddpfPixelFormat.dwRBitMask = 0xff0000; desc.ddpfPixelFormat.dwGBitMask = 0xff00; desc.ddpfPixelFormat.dwBBitMask = 0xff;
    hr = IDirectDraw7_CreateSurface(dd, &desc, &target, NULL);
    Row(20, 100, hr, 0, 0, 0); if (hr != DD_OK) goto done;
    hr = IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void **)&d3d);
    Row(20, 101, hr, 0, 0, 0); if (hr != DD_OK) goto done;
    hr = IDirect3D7_CreateDevice(d3d, &IID_IDirect3DHALDevice, target, &device);
    Row(20, 102, hr, 0, 0, 0); if (hr != DD_OK) goto done;
    IDirect3DDevice7_SetViewport(device, &viewport);
    desc.dwWidth = desc.dwHeight = 8;
    desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_COMPLEX;
    desc.ddsCaps.dwCaps2 = DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_ALLFACES;
#ifdef TEST_MIPMAPS
    // the root attaches both other faces and smaller mip levels.
    // Uploading a face as a lower mip must fail this test under Metal validation.
    desc.ddsCaps.dwCaps |= DDSCAPS_MIPMAP;
    desc.dwFlags |= DDSD_MIPMAPCOUNT;
    desc.dwMipMapCount = 4;
#endif
    hr = IDirectDraw7_CreateSurface(dd, &desc, &cube, NULL);
    Row(20, 103, hr, 0, 0, 0); if (hr != DD_OK) goto done;
    faces[0] = cube; IDirectDrawSurface7_AddRef(cube);
    for (f = 0; f < 6; ++f) {
        if (f) {
            DDSCAPS2 caps = {DDSCAPS_TEXTURE, DDSCAPS2_CUBEMAP | (DDSCAPS2_CUBEMAP_POSITIVEX << f), 0, 0};
            hr = IDirectDrawSurface7_GetAttachedSurface(cube, &caps, &faces[f]);
            if (hr != DD_OK) { Row(20, 104 + f, hr, 0, 0, 0); goto done; }
        }
        hr = FillFace(faces[f], colors[f]);
        Row(20, 110 + f, hr, 0, 0, 0); if (hr != DD_OK) goto done;
    }
    IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_ZENABLE, FALSE);
    IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_LIGHTING, FALSE);
    IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice7_SetTexture(device, 0, cube);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_MAGFILTER, D3DTFG_POINT);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_MINFILTER, D3DTFN_POINT);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_MIPFILTER, D3DTFP_NONE);
    for (pass = 0; pass < 5; ++pass) {
        DDSURFACEDESC2 lock = {0};
        if (pass == 1) Row(20, 120, FillFace(faces[4], 0x804020), 0, 0, 0);
        if (pass >= 2) {
            IDirectDrawSurface7 *source = NULL;
            desc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
            hr = IDirectDraw7_CreateSurface(dd, &desc, &source, NULL);
            Row(20 + pass, 130, hr, 0, 0, 0);
            if (hr == DD_OK) {
                for (f = 0; f < 6; ++f) {
                    IDirectDrawSurface7 *part = NULL;
                    if (!f) { part = source; IDirectDrawSurface7_AddRef(part); }
                    else {
                        DDSCAPS2 caps = {DDSCAPS_TEXTURE, DDSCAPS2_CUBEMAP | (DDSCAPS2_CUBEMAP_POSITIVEX << f), 0, 0};
                        IDirectDrawSurface7_GetAttachedSurface(source, &caps, &part);
                    }
                    if (!part) { Row(20 + pass, 131 + f, DDERR_NOTFOUND, 0, 0, 0); continue; }
                    Row(20 + pass, 131 + f, FillFace(part, pass != 3 ? colors[f] ^ 0xffffff : 0x403020), 0, 0, 0);
                    IDirectDrawSurface7_Release(part);
                }
                Row(20 + pass, 140, IDirect3DDevice7_Load(device, cube, NULL, source, NULL,
                    pass == 2 ? 0 : pass == 3 ? DDSCAPS2_CUBEMAP_POSITIVEY : DDSCAPS2_CUBEMAP_ALLFACES), 0, 0, 0);
                IDirectDrawSurface7_Release(source);
            }
        }
        IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
        IDirect3DDevice7_BeginScene(device);
        for (f = 0; f < 6; ++f) {
            struct Vertex { float x,y,z,rhw; DWORD color; float u,v,w; } v[4];
            unsigned i;
            for (i = 0; i < 4; ++i) {
                v[i].x = (float)(f * 5 + ((i & 1) ? 4 : 0));
                v[i].y = (i & 2) ? 8.0f : 0.0f;
                v[i].z = 0.5f; v[i].rhw = 1.0f; v[i].color = 0xffffffff;
                v[i].u = direction[f][0]; v[i].v = direction[f][1]; v[i].w = direction[f][2];
            }
            hr = IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLESTRIP,
                 D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0), v, 4, 0);
            Row(20 + pass, 200 + f, hr, 0, 0, 0);
        }
        IDirect3DDevice7_EndScene(device);
        lock.dwSize = sizeof lock;
        hr = IDirectDrawSurface7_Lock(target, NULL, &lock, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
        Row(20 + pass, 210, hr, 0, 0, 0);
        if (hr == DD_OK) {
            for (f = 0; f < 6; ++f) {
                DWORD pixel = ((DWORD *)((BYTE *)lock.lpSurface + 4 * lock.lPitch))[f * 5 + 2] & 0xffffff;
                Row(20 + pass, 220 + f, DD_OK, pixel, 0, 0);
            }
            IDirectDrawSurface7_Unlock(target, NULL);
        }
    }
done:
    if (device) { IDirect3DDevice7_SetTexture(device, 0, NULL); IDirect3DDevice7_Release(device); }
    for (f = 0; f < 6; ++f) if (faces[f]) IDirectDrawSurface7_Release(faces[f]);
    if (cube) IDirectDrawSurface7_Release(cube);
    if (d3d) IDirect3D7_Release(d3d);
    if (target) IDirectDrawSurface7_Release(target);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmd, int show)
{
    IDirectDraw7 *dd = NULL;
    HWND window;
    WNDCLASSA wc = {0};
    unsigned test, face;
    (void)prev; (void)cmd; (void)show;
    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = instance; wc.lpszClassName = "TextureShapes";
    RegisterClassA(&wc);
    window = CreateWindowExA(0, wc.lpszClassName, "TextureShapes", WS_POPUP, 0, 0, 100, 100, NULL, NULL, instance, NULL);
    if (!window || DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL) != DD_OK) return 1;
    IDirectDraw7_SetCooperativeLevel(dd, window, DDSCL_NORMAL);
    report = CreateFileA("E:\\SHAPES.BIN", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (report == INVALID_HANDLE_VALUE) return 2;
    for (test = 0; test < 14; ++test) {
        IDirectDrawSurface7 *surface = NULL;
        DDSURFACEDESC2 desc = {0}, got = {0};
        HRESULT hr;
        desc.dwSize = sizeof desc;
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        desc.dwWidth = desc.dwHeight = 8;
        desc.ddpfPixelFormat.dwSize = sizeof desc.ddpfPixelFormat;
        desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
        desc.ddpfPixelFormat.dwRGBBitCount = 32;
        desc.ddpfPixelFormat.dwRBitMask = 0xff0000;
        desc.ddpfPixelFormat.dwGBitMask = 0xff00;
        desc.ddpfPixelFormat.dwBBitMask = 0xff;
        desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_COMPLEX;
        if (test & 1) desc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
        if (test < 8 && (test & 2)) {
            desc.ddsCaps.dwCaps |= DDSCAPS_MIPMAP;
            desc.dwFlags |= DDSD_MIPMAPCOUNT;
            desc.dwMipMapCount = 4;
        }
        if (test < 8 && (test & 4)) {
            desc.ddsCaps.dwCaps2 = DDSCAPS2_VOLUME;
            desc.dwFlags |= DDSD_DEPTH;
            desc.dwDepth = 8;
        } else desc.ddsCaps.dwCaps2 = DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_ALLFACES;
        if (test == 8) desc.ddsCaps.dwCaps2 = DDSCAPS2_CUBEMAP;
        if (test == 9) desc.dwHeight = 4;
        if (test == 10) desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_COMPLEX;
        if (test == 11 || test == 12) desc.ddsCaps.dwCaps &= ~DDSCAPS_COMPLEX;
        if (test == 12) desc.ddsCaps.dwCaps2 = DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEX;
        if (test == 13) desc.ddsCaps.dwCaps2 = DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEY | DDSCAPS2_CUBEMAP_NEGATIVEZ;
        hr = IDirectDraw7_CreateSurface(dd, &desc, &surface, NULL);
        Row(test, 0, hr, 0, 0, 0);
        if (hr != DD_OK) continue;
        got.dwSize = sizeof got;
        hr = IDirectDrawSurface7_GetSurfaceDesc(surface, &got);
        Row(test, 1, hr, got.dwFlags, got.ddsCaps.dwCaps, got.ddsCaps.dwCaps2);
        Row(test, 2, DD_OK, got.dwWidth, got.dwHeight, got.dwDepth);
        if (!(test >= 4 && test < 8)) for (face = 0; face < 6; ++face) {
            DDSCAPS2 caps = {DDSCAPS_TEXTURE, DDSCAPS2_CUBEMAP | (DDSCAPS2_CUBEMAP_POSITIVEX << face), 0, 0};
            IDirectDrawSurface7 *other = NULL;
            hr = IDirectDrawSurface7_GetAttachedSurface(surface, &caps, &other);
            Row(test, 10 + face, hr, 0, 0, 0);
            if (other) IDirectDrawSurface7_Release(other);
        }
        IDirectDrawSurface7_Release(surface);
    }
    RenderCube(dd);
    IDirectDraw7_Release(dd);
    CloseHandle(report);
    DestroyWindow(window);
    return 0;
}
#ifndef _WIN64
void mainCRTStartup(void) { ExitProcess(WinMain(GetModuleHandleA(NULL), NULL, NULL, 0)); }
#endif
