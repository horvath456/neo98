/* SPDX-License-Identifier: GPL-2.0-or-later */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
static HANDLE report;
static unsigned checks, failures;
static void Check(unsigned actual,unsigned expected)
{
    DWORD row[3]={++checks,actual,expected},written;
    if(actual!=expected)++failures;
    WriteFile(report,row,sizeof row,&written,NULL);
}
static void Hr(HRESULT hr){Check((unsigned)hr,0);}
static unsigned Pixel(IDirectDrawSurface7 *s,int x,int y)
{
    DDSURFACEDESC2 d={0};d.dwSize=sizeof d;
    HRESULT hr=IDirectDrawSurface7_Lock(s,NULL,&d,DDLOCK_WAIT|DDLOCK_READONLY,NULL);Hr(hr);
    if(FAILED(hr))return ~0u;
    unsigned color=*(DWORD *)((char *)d.lpSurface+y*d.lPitch+x*4)&0xffffff;
    Hr(IDirectDrawSurface7_Unlock(s,NULL));return color;
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show)
{
    IDirectDraw7 *dd=NULL;IDirectDrawSurface7 *target=NULL,*copy=NULL;IDirect3D7 *d3d=NULL;IDirect3DDevice7 *device=NULL;
    DDSURFACEDESC2 desc={0};D3DVIEWPORT7 viewport={0,0,64,64,0,1};
    (void)instance;(void)previous;(void)command;(void)show;
    report=CreateFileA("E:\\PASSES.BIN",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);if(report==INVALID_HANDLE_VALUE)return 2;
    if(FAILED(DirectDrawCreateEx(NULL,(void **)&dd,&IID_IDirectDraw7,NULL)))return 3;
    Hr(IDirectDraw7_SetCooperativeLevel(dd,NULL,DDSCL_NORMAL));
    desc.dwSize=sizeof desc;desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
    desc.dwWidth=desc.dwHeight=64;desc.ddsCaps.dwCaps=DDSCAPS_3DDEVICE|DDSCAPS_OFFSCREENPLAIN|DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwSize=sizeof desc.ddpfPixelFormat;desc.ddpfPixelFormat.dwFlags=DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount=32;desc.ddpfPixelFormat.dwRBitMask=0xff0000;
    desc.ddpfPixelFormat.dwGBitMask=0xff00;desc.ddpfPixelFormat.dwBBitMask=0xff;
    if(FAILED(IDirectDraw7_CreateSurface(dd,&desc,&target,NULL)))return 4;
    desc.ddsCaps.dwCaps=DDSCAPS_3DDEVICE|DDSCAPS_TEXTURE|DDSCAPS_VIDEOMEMORY;
    if(FAILED(IDirectDraw7_CreateSurface(dd,&desc,&copy,NULL)))return 5;
    if(FAILED(IDirectDraw7_QueryInterface(dd,&IID_IDirect3D7,(void **)&d3d)))return 6;
    if(FAILED(IDirect3D7_CreateDevice(d3d,&IID_IDirect3DHALDevice,target,&device)))return 7;
    Hr(IDirect3DDevice7_SetViewport(device,&viewport));
    Hr(IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_LIGHTING,FALSE));
    Hr(IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_CULLMODE,D3DCULL_NONE));
    Hr(IDirect3DDevice7_SetTextureStageState(device,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
    Hr(IDirect3DDevice7_SetTextureStageState(device,0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
    struct Vertex {float x,y,z,w;DWORD color;} vertices[129];
    for(unsigned i=0;i<129;++i){vertices[i].x=i%3==1?127.5f:-.5f;vertices[i].y=i%3==2?127.5f:-.5f;vertices[i].z=.5f;vertices[i].w=1;}
    Hr(IDirect3DDevice7_Clear(device,0,NULL,D3DCLEAR_TARGET,0,1,0));
    Hr(IDirect3DDevice7_BeginScene(device));
    for(unsigned batch=0;batch<20;++batch){
        for(unsigned i=0;i<129;++i)vertices[i].color=batch==19?0xff00ff00:0xff0000ff;
        Hr(IDirect3DDevice7_DrawPrimitive(device,D3DPT_TRIANGLELIST,D3DFVF_XYZRHW|D3DFVF_DIFFUSE,vertices,129,0));
    }
    Hr(IDirect3DDevice7_EndScene(device));Check(Pixel(target,20,20),0x00ff00);
    Hr(IDirect3DDevice7_Clear(device,0,NULL,D3DCLEAR_TARGET,0x0000ff,1,0));
    HDC dc=NULL;Hr(IDirectDrawSurface7_GetDC(target,&dc));
    if(dc){Check(GetPixel(dc,10,10),RGB(0,0,255));Check(SetPixel(dc,10,10,RGB(255,0,0)),RGB(255,0,0));Hr(IDirectDrawSurface7_ReleaseDC(target,dc));}
    D3DRECT corner;corner.x1=60;corner.y1=60;corner.x2=64;corner.y2=64;
    Hr(IDirect3DDevice7_Clear(device,1,&corner,D3DCLEAR_TARGET,0x00ff00,1,0));
    Check(Pixel(target,10,10),0xff0000);Check(Pixel(target,20,20),0x0000ff);Check(Pixel(target,62,62),0x00ff00);
    Hr(IDirect3DDevice7_Clear(device,0,NULL,D3DCLEAR_TARGET,0xffff00,1,0));
    Hr(IDirectDrawSurface7_Blt(copy,NULL,target,NULL,DDBLT_WAIT,NULL));
    Hr(IDirect3DDevice7_Clear(device,0,NULL,D3DCLEAR_TARGET,0x0000ff,1,0));
    Check(Pixel(copy,20,20),0xffff00);
    Hr(IDirectDrawSurface7_BltFast(target,0,0,copy,NULL,DDBLTFAST_WAIT));
    Hr(IDirect3DDevice7_Clear(device,1,&corner,D3DCLEAR_TARGET,0x00ff00,1,0));
    Check(Pixel(target,20,20),0xffff00);Check(Pixel(target,62,62),0x00ff00);
    Hr(IDirect3DDevice7_SetRenderTarget(device,copy,0));Hr(IDirect3DDevice7_SetViewport(device,&viewport));
    Hr(IDirect3DDevice7_Clear(device,0,NULL,D3DCLEAR_TARGET,0xff00ff,1,0));
    Hr(IDirect3DDevice7_SetRenderTarget(device,target,0));Hr(IDirect3DDevice7_SetViewport(device,&viewport));
    Hr(IDirect3DDevice7_SetTexture(device,0,copy));Hr(IDirect3DDevice7_SetTextureStageState(device,0,D3DTSS_COLORARG1,D3DTA_TEXTURE));
    Hr(IDirect3DDevice7_BeginScene(device));
    Hr(IDirect3DDevice7_DrawPrimitive(device,D3DPT_TRIANGLELIST,D3DFVF_XYZRHW|D3DFVF_DIFFUSE,vertices,129,0));
    Hr(IDirect3DDevice7_EndScene(device));Check(Pixel(target,20,20),0xff00ff);Check(Pixel(copy,20,20),0xff00ff);
    Hr(IDirect3DDevice7_SetTexture(device,0,NULL));
    IDirect3DDevice7_Release(device);IDirect3D7_Release(d3d);IDirectDrawSurface7_Release(copy);IDirectDrawSurface7_Release(target);IDirectDraw7_Release(dd);
    CloseHandle(report);return failures?1:0;
}
void mainCRTStartup(void){ExitProcess(WinMain(GetModuleHandleA(NULL),NULL,NULL,0));}
