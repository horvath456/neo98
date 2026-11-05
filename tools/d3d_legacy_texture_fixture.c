/* SPDX-License-Identifier: GPL-2.0-or-later */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
static HANDLE report;
static void Row(DWORD test, DWORD item, HRESULT hr, DWORD a, DWORD b, DWORD c)
{
    DWORD row[6]={test,item,(DWORD)hr,a,b,c}, written;
    if (!WriteFile(report,row,sizeof row,&written,NULL)||written!=sizeof row) ExitProcess(3);
}
static void Pixel(IDirectDrawSurface *s, DWORD *pixel, BOOL write)
{
    DDSURFACEDESC d={0}; d.dwSize=sizeof d;
    if (IDirectDrawSurface_Lock(s,NULL,&d,DDLOCK_WAIT,NULL)!=DD_OK) ExitProcess(4);
    if(write) *(DWORD *)d.lpSurface=*pixel; else *pixel=*(DWORD *)d.lpSurface;
    IDirectDrawSurface_Unlock(s,d.lpSurface);
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show)
{
    IDirectDraw *dd=NULL; unsigned test;
    (void)instance;(void)previous;(void)command;(void)show;
    report=CreateFileA("E:\\LEGACY.BIN",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    if(report==INVALID_HANDLE_VALUE) return 2;
    if(DirectDrawCreate(NULL,&dd,NULL)!=DD_OK) return 3;
    IDirectDraw_SetCooperativeLevel(dd,NULL,DDSCL_NORMAL);
    for(test=0;test<4;++test) {
        DDSURFACEDESC desc={0}; IDirectDrawSurface *s=NULL,*from=NULL;
        IDirect3DTexture *one=NULL; IDirect3DTexture2 *two=NULL,*source=NULL,*again=NULL;
        IUnknown *a=NULL,*b=NULL; DWORD pixel=0x123456; HRESULT hr;
        desc.dwSize=sizeof desc;desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
        desc.dwWidth=desc.dwHeight=8;
        desc.ddsCaps.dwCaps=(test&1?DDSCAPS_OFFSCREENPLAIN:DDSCAPS_TEXTURE)|(test&2?DDSCAPS_SYSTEMMEMORY:0);
        desc.ddpfPixelFormat.dwSize=sizeof desc.ddpfPixelFormat;desc.ddpfPixelFormat.dwFlags=DDPF_RGB;
        desc.ddpfPixelFormat.dwRGBBitCount=32;desc.ddpfPixelFormat.dwRBitMask=0xff0000;
        desc.ddpfPixelFormat.dwGBitMask=0xff00;desc.ddpfPixelFormat.dwBBitMask=0xff;
        hr=IDirectDraw_CreateSurface(dd,&desc,&s,NULL);Row(test,0,hr,0,0,0);if(FAILED(hr)) continue;
        hr=IDirectDrawSurface_QueryInterface(s,&IID_IDirect3DTexture,(void **)&one);Row(test,1,hr,0,0,0);
        hr=IDirectDrawSurface_QueryInterface(s,&IID_IDirect3DTexture2,(void **)&two);Row(test,2,hr,0,0,0);
        if(two) {
            IDirect3DTexture2_QueryInterface(two,&IID_IDirect3DTexture2,(void **)&again);Row(test,3,S_OK,two==again,0,0);
            if(again) IDirect3DTexture2_Release(again);
            IDirect3DTexture2_QueryInterface(two,&IID_IUnknown,(void **)&a);IDirectDrawSurface_QueryInterface(s,&IID_IUnknown,(void **)&b);
            Row(test,4,S_OK,a==b,0,0);if(a) IUnknown_Release(a);if(b) IUnknown_Release(b);
            Row(test,5,IDirect3DTexture2_PaletteChanged(two,0,256),0,0,0);
            Row(test,6,IDirect3DTexture2_Load(two,NULL),0,0,0);
            Row(test,7,IDirect3DTexture2_Load(two,two),0,0,0);
            hr=IDirectDraw_CreateSurface(dd,&desc,&from,NULL);Row(test,8,hr,0,0,0);
            if(from) {
                Pixel(from,&pixel,TRUE);
                IDirectDrawSurface_QueryInterface(from,&IID_IDirect3DTexture2,(void **)&source);
                if(source) {
                    hr=IDirect3DTexture2_Load(two,source);Pixel(s,&pixel,FALSE);Row(test,9,hr,pixel,0,0);
                    IDirect3DTexture2_Release(source);
                }
                IDirectDrawSurface_Release(from);
            }
            IDirect3DTexture2_Release(two);
        }
        if(one) {Row(test,10,IDirect3DTexture_Unload(one),0,0,0);IDirect3DTexture_Release(one);}
        Row(test,11,S_OK,IDirectDrawSurface_Release(s),0,0);
    }
    IDirectDraw_Release(dd);CloseHandle(report);return 0;
}
void mainCRTStartup(void) {ExitProcess(WinMain(GetModuleHandleA(NULL),NULL,NULL,0));}
