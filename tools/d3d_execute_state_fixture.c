/* SPDX-License-Identifier: GPL-2.0-or-later */
/* execute-buffer render states through the original Direct3D runtime. */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
static HANDLE report;
static void check(HRESULT hr, unsigned step)
{
    DWORD row[2] = {step, (DWORD)hr}, written;
    if (!WriteFile(report, row, sizeof row, &written, NULL) || written != sizeof row) ExitProcess(99);
    if (FAILED(hr)) ExitProcess(step);
}
void mainCRTStartup(void)
{
    IDirectDraw *dd = NULL;
    IDirectDrawSurface *target = NULL;
    IDirect3D *root = NULL;
    IDirect3DDevice *device = NULL;
    IDirect3DViewport *viewport = NULL;
    IDirect3DExecuteBuffer *buffer = NULL;
    report = CreateFileA("E:\\EXECUTE.BIN", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (report == INVALID_HANDLE_VALUE) ExitProcess(98);
    check(DirectDrawCreate(NULL, &dd, NULL), 1);
    check(IDirectDraw_SetCooperativeLevel(dd, NULL, DDSCL_NORMAL), 2);
    DDSURFACEDESC surface = {0};
    surface.dwSize = sizeof surface;
    surface.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    surface.dwWidth = 320; surface.dwHeight = 240;
    surface.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;
    surface.ddpfPixelFormat.dwSize = sizeof surface.ddpfPixelFormat;
    surface.ddpfPixelFormat.dwFlags = DDPF_RGB;
    surface.ddpfPixelFormat.dwRGBBitCount = 16;
    surface.ddpfPixelFormat.dwRBitMask = 0xf800;
    surface.ddpfPixelFormat.dwGBitMask = 0x7e0;
    surface.ddpfPixelFormat.dwBBitMask = 0x1f;
    check(IDirectDraw_CreateSurface(dd, &surface, &target, NULL), 3);
    check(IDirectDraw_QueryInterface(dd, &IID_IDirect3D, (void **)&root), 4);
    check(IDirectDrawSurface_QueryInterface(target, &IID_IDirect3DHALDevice, (void **)&device), 5);
    check(IDirect3D_CreateViewport(root, &viewport, NULL), 6);
    check(IDirect3DDevice_AddViewport(device, viewport), 7);
    D3DVIEWPORT vp = {0};
    vp.dwSize = sizeof vp; vp.dwWidth = 320; vp.dwHeight = 240;
    vp.dvScaleX = 160; vp.dvScaleY = 120;
    vp.dvMaxX = vp.dvMaxY = vp.dvMaxZ = 1;
    check(IDirect3DViewport_SetViewport(viewport, &vp), 8);
    D3DEXECUTEBUFFERDESC desc = {0};
    desc.dwSize = sizeof desc; desc.dwFlags = D3DDEB_BUFSIZE; desc.dwBufferSize = 1024;
    check(IDirect3DDevice_CreateExecuteBuffer(device, &desc, &buffer, NULL), 9);
    check(IDirect3DExecuteBuffer_Lock(buffer, &desc), 10);
    D3DINSTRUCTION *instruction = desc.lpData;
    *instruction = (D3DINSTRUCTION){D3DOP_STATERENDER, sizeof(D3DSTATE), 4};
    D3DSTATE *states = (void *)(instruction + 1);
    for (unsigned i = 0; i < 4; ++i) states[i] = (D3DSTATE){0};
    states[0].drstRenderStateType = D3DRENDERSTATE_ZENABLE; states[0].dwArg[0] = 1;
    states[1].drstRenderStateType = D3DRENDERSTATE_SHADEMODE; states[1].dwArg[0] = 1;
    states[2].drstRenderStateType = D3DRENDERSTATE_TEXTUREMAPBLEND; states[2].dwArg[0] = 2;
    states[3].drstRenderStateType = D3DRENDERSTATE_ZWRITEENABLE; states[3].dwArg[0] = 1;
    instruction = (void *)(states + 4);
    *instruction = (D3DINSTRUCTION){D3DOP_EXIT, 0, 0};
    check(IDirect3DExecuteBuffer_Unlock(buffer), 11);
    D3DEXECUTEDATA data = {0};
    data.dwSize = sizeof data;
    data.dwInstructionLength = 2 * sizeof(D3DINSTRUCTION) + 4 * sizeof(D3DSTATE);
    check(IDirect3DExecuteBuffer_SetExecuteData(buffer, &data), 12);
    for (unsigned i = 0; i < 3; ++i)
        check(IDirect3DDevice_Execute(device, buffer, viewport, D3DEXECUTE_UNCLIPPED), 13 + i);
    IDirect3DExecuteBuffer_Release(buffer);
    IDirect3DDevice_DeleteViewport(device, viewport);
    IDirect3DViewport_Release(viewport);
    IDirect3DDevice_Release(device);
    IDirect3D_Release(root);
    IDirectDrawSurface_Release(target);
    IDirectDraw_Release(dd);
    CloseHandle(report);
    ExitProcess(0);
}
