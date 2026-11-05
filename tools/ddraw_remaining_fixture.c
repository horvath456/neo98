/* SPDX-License-Identifier: GPL-2.0-or-later */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
static HANDLE report;
static DWORD callbacks;
static DWORD detailed;
static DDSURFACEDESC2 *enumerated;
static void Row(DWORD test,DWORD item,HRESULT hr,DWORD a,DWORD b,DWORD c)
{
    DWORD row[6]={test,item,(DWORD)hr,a,b,c},written;
    if(!WriteFile(report,row,sizeof row,&written,NULL)||written!=sizeof row)ExitProcess(3);
}
static HRESULT CALLBACK Enum(LPDIRECTDRAWSURFACE7 surface,LPDDSURFACEDESC2 desc,void *context)
{
    ++callbacks;Row(1,(DWORD)(ULONG_PTR)context,S_OK,desc->dwWidth,desc->dwHeight,desc->ddsCaps.dwCaps);
    if(detailed) {
        Row(7,(DWORD)(ULONG_PTR)context,S_OK,surface!=NULL,desc->dwFlags,desc->ddpfPixelFormat.dwRGBBitCount);
        Row(8,(DWORD)(ULONG_PTR)context,S_OK,desc->lPitch,desc->ddpfPixelFormat.dwRBitMask,desc->ddpfPixelFormat.dwGBitMask);
        Row(13,(DWORD)(ULONG_PTR)context,S_OK,enumerated->dwFlags,enumerated->ddpfPixelFormat.dwRGBBitCount,enumerated->dwWidth);
    }
    if(surface)IDirectDrawSurface7_Release(surface);
    return DDENUMRET_CANCEL;
}
int WINAPI WinMain(HINSTANCE i,HINSTANCE p,LPSTR c,int s)
{
    IDirectDraw7 *dd=NULL;DDSURFACEDESC2 d={0};IDirectDrawSurface7 *surface=NULL;
    (void)i;(void)p;(void)c;(void)s;
    report=CreateFileA("E:\\REMAIN.BIN",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);if(report==INVALID_HANDLE_VALUE)return 2;
    if(DirectDrawCreateEx(NULL,(void **)&dd,&IID_IDirectDraw7,NULL)!=DD_OK)return 3;
    IDirectDraw7_SetCooperativeLevel(dd,NULL,DDSCL_NORMAL);
    d.dwSize=sizeof d;d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
    d.dwWidth=d.dwHeight=8;d.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;
    d.ddpfPixelFormat.dwSize=sizeof d.ddpfPixelFormat;d.ddpfPixelFormat.dwFlags=DDPF_RGB;
    d.ddpfPixelFormat.dwRGBBitCount=32;d.ddpfPixelFormat.dwRBitMask=0xff0000;d.ddpfPixelFormat.dwGBitMask=0xff00;d.ddpfPixelFormat.dwBBitMask=0xff;
    for(DWORD flags=0;flags<32;++flags)if(flags&DDENUMSURFACES_CANBECREATED) {
        callbacks=0;HRESULT hr=IDirectDraw7_EnumSurfaces(dd,flags,&d,(void *)(ULONG_PTR)flags,Enum);
        Row(0,flags,hr,callbacks,0,0);
    }
    detailed=1;
    for(unsigned variant=0;variant<8;++variant) {
        DDSURFACEDESC2 want=d;
        if(variant>=4)want.dwWidth=want.dwHeight=1;
        want.dwFlags=DDSD_CAPS|(variant&1?DDSD_WIDTH|DDSD_HEIGHT:0)|(variant&2?DDSD_PIXELFORMAT:0);
        enumerated=&want;
        callbacks=0;HRESULT hr=IDirectDraw7_EnumSurfaces(dd,DDENUMSURFACES_CANBECREATED|DDENUMSURFACES_MATCH,&want,(void *)(ULONG_PTR)(100+variant),Enum);
        Row(0,100+variant,hr,callbacks,0,0);
        Row(11,variant,S_OK,want.dwFlags,want.ddpfPixelFormat.dwRGBBitCount,want.dwWidth);
    }
    Row(0,108,IDirectDraw7_EnumSurfaces(dd,DDENUMSURFACES_CANBECREATED|DDENUMSURFACES_MATCH,NULL,NULL,Enum),0,0,0);
    DWORD caps[]={0,DDSCAPS_OFFSCREENPLAIN,DDSCAPS_VIDEOMEMORY,DDSCAPS_SYSTEMMEMORY,DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY,DDSCAPS_OVERLAY,DDSCAPS_PRIMARYSURFACE};
    for(unsigned variant=0;variant<8;++variant) {
        DDSURFACEDESC2 want=d;want.ddsCaps.dwCaps=caps[variant%7];
        if(variant==7)want.dwFlags=0;
        enumerated=&want;callbacks=0;
        HRESULT hr=IDirectDraw7_EnumSurfaces(dd,DDENUMSURFACES_CANBECREATED|DDENUMSURFACES_MATCH,&want,(void *)(ULONG_PTR)(200+variant),Enum);
        Row(0,200+variant,hr,callbacks,0,0);
    }
    DWORD fourcc[]={MAKEFOURCC('Y','U','Y','2'),MAKEFOURCC('U','Y','V','Y'),MAKEFOURCC('Y','V','1','2'),MAKEFOURCC('I','4','2','0'),MAKEFOURCC('N','V','1','2'),MAKEFOURCC('B','A','D','!')};
    for(unsigned mem=0;mem<2;++mem)for(unsigned f=0;f<6;++f) {
        d.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|(mem?DDSCAPS_SYSTEMMEMORY:DDSCAPS_VIDEOMEMORY);
        d.ddpfPixelFormat.dwFlags=DDPF_FOURCC;d.ddpfPixelFormat.dwFourCC=fourcc[f];
        d.ddpfPixelFormat.dwRGBBitCount=d.ddpfPixelFormat.dwRBitMask=d.ddpfPixelFormat.dwGBitMask=d.ddpfPixelFormat.dwBBitMask=0;
        HRESULT hr=IDirectDraw7_CreateSurface(dd,&d,&surface,NULL);Row(2,mem*6+f,hr,0,0,0);
        if(surface){IDirectDrawSurface7_Release(surface);surface=NULL;}
        IDirectDraw *old=NULL;IDirectDrawSurface *old_surface=NULL;DDSURFACEDESC small={0};
        small.dwSize=sizeof small;small.dwFlags=d.dwFlags;small.dwWidth=d.dwWidth;small.dwHeight=d.dwHeight;
        small.ddsCaps.dwCaps=d.ddsCaps.dwCaps;small.ddpfPixelFormat=d.ddpfPixelFormat;
        IDirectDraw7_QueryInterface(dd,&IID_IDirectDraw,(void **)&old);
        hr=IDirectDraw_CreateSurface(old,&small,&old_surface,NULL);Row(12,mem*6+f,hr,0,0,0);
        if(old_surface)IDirectDrawSurface_Release(old_surface);IDirectDraw_Release(old);
    }
    d.ddpfPixelFormat.dwFlags=DDPF_RGB;d.ddpfPixelFormat.dwFourCC=0;
    d.ddpfPixelFormat.dwRGBBitCount=32;d.ddpfPixelFormat.dwRBitMask=0xff0000;d.ddpfPixelFormat.dwGBitMask=0xff00;d.ddpfPixelFormat.dwBBitMask=0xff;
    for(unsigned mem=0;mem<3;++mem) {
        d.ddsCaps.dwCaps=DDSCAPS_OVERLAY|(mem==1?DDSCAPS_SYSTEMMEMORY:mem==2?DDSCAPS_VIDEOMEMORY:0);
        HRESULT hr=IDirectDraw7_CreateSurface(dd,&d,&surface,NULL);Row(3,mem,hr,0,0,0);
        if(surface){IDirectDrawSurface7_Release(surface);surface=NULL;}
    }
    for(DWORD flags=0;flags<4;++flags) {
        DWORD timeout=0xdeadbeef;HRESULT hr=IDirectDraw7_EvaluateMode(dd,flags,&timeout);Row(4,flags,hr,timeout,0,0);
        SIZE modes[2]={{640,480},{800,600}};
        for(DWORD count=0;count<3;++count)Row(5,flags*3+count,IDirectDraw7_StartModeTest(dd,modes,count,flags),0,0,0);
    }
    Row(6,0,IDirectDraw7_StartModeTest(dd,NULL,0,0),0,0,0);
    Row(6,1,IDirectDraw7_EvaluateMode(dd,0,NULL),0,0,0);
    IDirectDraw7_Release(dd);CloseHandle(report);return 0;
}
void mainCRTStartup(void){ExitProcess(WinMain(GetModuleHandleA(NULL),NULL,NULL,0));}
