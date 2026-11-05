/* SPDX-License-Identifier: GPL-2.0-or-later */
/* textured execute-buffer pixels through the original Direct3D runtime. */
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
#ifdef REFERENCE_RGB
    check(IDirectDrawSurface_QueryInterface(target, &IID_IDirect3DRGBDevice, (void **)&device), 5);
#else
    check(IDirectDrawSurface_QueryInterface(target, &IID_IDirect3DHALDevice, (void **)&device), 5);
#endif
    check(IDirect3D_CreateViewport(root, &viewport, NULL), 6);
    check(IDirect3DDevice_AddViewport(device, viewport), 7);
    D3DVIEWPORT vp = {0};
    vp.dwSize = sizeof vp; vp.dwWidth = 320; vp.dwHeight = 240;
    vp.dvScaleX = 160; vp.dvScaleY = 120;
    vp.dvMaxX = vp.dvMaxY = vp.dvMaxZ = 1;
    check(IDirect3DViewport_SetViewport(viewport, &vp), 8);
    IDirectDrawSurface *texture_surface = NULL;
    IDirect3DTexture *texture = NULL;
    DDSURFACEDESC tex = surface;
    tex.dwWidth = tex.dwHeight = 2;
    tex.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;
    check(IDirectDraw_CreateSurface(dd, &tex, &texture_surface, NULL), 20);
    check(IDirectDrawSurface_QueryInterface(texture_surface, &IID_IDirect3DTexture, (void **)&texture), 21);
    DDSURFACEDESC locked = {0}; locked.dwSize = sizeof locked;
    check(IDirectDrawSurface_Lock(texture_surface, NULL, &locked, DDLOCK_WAIT, NULL), 22);
    for (unsigned y=0;y<2;y++) {
        WORD *row=(void*)((BYTE*)locked.lpSurface+y*locked.lPitch);
        row[0]=row[1]=0x07e0;
    }
    check(IDirectDrawSurface_Unlock(texture_surface, locked.lpSurface), 23);
    D3DTEXTUREHANDLE handle=0;
    check(IDirect3DTexture_GetHandle(texture, device, &handle), 24);
    D3DEXECUTEBUFFERDESC desc = {0};
    desc.dwSize = sizeof desc; desc.dwFlags = D3DDEB_BUFSIZE; desc.dwBufferSize = 1024;
    check(IDirect3DDevice_CreateExecuteBuffer(device, &desc, &buffer, NULL), 9);
    check(IDirect3DExecuteBuffer_Lock(buffer, &desc), 10);
    D3DTLVERTEX vertices[3] = {
        { .sx=0, .sy=0, .sz=.5f, .rhw=1, .color=0xffff0000, .tu=0, .tv=0 },
        { .sx=320, .sy=0, .sz=.5f, .rhw=1, .color=0xffff0000, .tu=1, .tv=0 },
        { .sx=0, .sy=240, .sz=.5f, .rhw=1, .color=0xffff0000, .tu=0, .tv=1 }
    };
    CopyMemory(desc.lpData,vertices,sizeof vertices);
    D3DINSTRUCTION *instruction = (void*)((BYTE*)desc.lpData+sizeof vertices);
    *instruction = (D3DINSTRUCTION){D3DOP_STATERENDER, sizeof(D3DSTATE), 7};
    D3DSTATE *states = (void *)(instruction + 1);
    for (unsigned i = 0; i < 7; ++i) states[i] = (D3DSTATE){0};
    states[0].drstRenderStateType = D3DRENDERSTATE_ZENABLE;
    states[1].drstRenderStateType = D3DRENDERSTATE_CULLMODE; states[1].dwArg[0] = D3DCULL_NONE;
    states[2].drstRenderStateType = D3DRENDERSTATE_TEXTUREMAPBLEND; states[2].dwArg[0] = D3DTBLEND_DECAL;
    states[3].drstRenderStateType = D3DRENDERSTATE_TEXTUREHANDLE; states[3].dwArg[0] = handle;
    states[4].drstRenderStateType=D3DRENDERSTATE_ALPHABLENDENABLE;
    states[5].drstRenderStateType=D3DRENDERSTATE_SRCBLEND; states[5].dwArg[0]=D3DBLEND_SRCALPHA;
    states[6].drstRenderStateType=D3DRENDERSTATE_DESTBLEND; states[6].dwArg[0]=D3DBLEND_INVSRCALPHA;
    instruction = (void *)(states + 7);
    *instruction = (D3DINSTRUCTION){D3DOP_PROCESSVERTICES, sizeof(D3DPROCESSVERTICES), 1};
    D3DPROCESSVERTICES *process=(void*)(instruction+1);
    *process=(D3DPROCESSVERTICES){0}; process->dwFlags=D3DPROCESSVERTICES_COPY; process->dwCount=3;
    instruction=(void*)(process+1);
    *instruction=(D3DINSTRUCTION){D3DOP_TRIANGLE,sizeof(D3DTRIANGLE),1};
    D3DTRIANGLE *triangle=(void*)(instruction+1);
    *triangle=(D3DTRIANGLE){0};triangle->v1=0;triangle->v2=1;triangle->v3=2;
    instruction=(void*)(triangle+1);
    *instruction = (D3DINSTRUCTION){D3DOP_EXIT, 0, 0};
    DWORD instruction_bytes=(BYTE*)(instruction+1)-((BYTE*)desc.lpData+sizeof vertices);
    check(IDirect3DExecuteBuffer_Unlock(buffer), 11);
    D3DEXECUTEDATA data = {0};
    data.dwSize = sizeof data;
    data.dwVertexCount = 3; data.dwInstructionOffset=sizeof vertices;
    data.dwInstructionLength = instruction_bytes;
    check(IDirect3DExecuteBuffer_SetExecuteData(buffer, &data), 12);
    const DWORD blends[] = {D3DTBLEND_DECAL,D3DTBLEND_MODULATE,D3DTBLEND_DECALALPHA,
        D3DTBLEND_MODULATEALPHA,D3DTBLEND_COPY,D3DTBLEND_ADD,D3DTBLEND_DECAL,D3DTBLEND_MODULATE};
    const WORD expected[] = {0x07e0,0,0x07e0,0,0x07e0,0xffe0,0xf800,0x001f};
    for (unsigned pass=0;pass<8;pass++) {
        unsigned step=100+pass*10;
        check(IDirect3DExecuteBuffer_Lock(buffer,&desc),step);
        D3DSTATE *change=(void*)((BYTE*)desc.lpData+sizeof vertices+sizeof(D3DINSTRUCTION));
        change[2].dwArg[0]=blends[pass]; change[3].dwArg[0]=pass==6?0:handle;
        change[4].dwArg[0]=pass==7;
        if(pass==7) {
            D3DTLVERTEX *v=desc.lpData;
            for(unsigned i=0;i<3;i++) v[i].color=0x00ffffff;
            DDBLTFX fx={0};fx.dwSize=sizeof fx;fx.dwFillColor=0x001f;
            check(IDirectDrawSurface_Blt(target,NULL,NULL,NULL,DDBLT_COLORFILL|DDBLT_WAIT,&fx),step+2);
        }
        check(IDirect3DExecuteBuffer_Unlock(buffer),step+1);
        check(IDirect3DDevice_BeginScene(device),step+3);
        check(IDirect3DDevice_Execute(device, buffer, viewport, D3DEXECUTE_UNCLIPPED),step+4);
        check(IDirect3DDevice_EndScene(device),step+5);
        locked=(DDSURFACEDESC){0};locked.dwSize=sizeof locked;
        check(IDirectDrawSurface_Lock(target,NULL,&locked,DDLOCK_WAIT,NULL),step+6);
        WORD pixel=*(WORD*)((BYTE*)locked.lpSurface+60*locked.lPitch+80*2);
        DWORD row[2]={step+7,pixel},written;WriteFile(report,row,sizeof row,&written,NULL);
        check(IDirectDrawSurface_Unlock(target,locked.lpSurface),step+8);
        WORD want=expected[pass];
        int dr=(pixel>>11)-(want>>11), dg=((pixel>>5)&63)-((want>>5)&63), db=(pixel&31)-(want&31);
        /* RGB's alpha blend truncates 255/256; hardware may round to 1. */
        int tolerance=(pass==2||pass==5||pass==7)?1:0;
        check(dr>=-tolerance&&dr<=tolerance&&dg>=-tolerance&&dg<=tolerance&&db>=-tolerance&&db<=tolerance?S_OK:E_FAIL,step+9);
    }
    IDirect3DTexture_Release(texture);
    IDirectDrawSurface_Release(texture_surface);
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
