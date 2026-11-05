/* SPDX-License-Identifier: GPL-2.0-or-later */
/* packed alpha at and next to the reference, HAL and original RGB. */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
static HANDLE report;
static void row(DWORD tag,DWORD value) { DWORD data[2]={tag,value},n; WriteFile(report,data,sizeof data,&n,0); }
static void check(HRESULT hr,DWORD tag) { if (FAILED(hr)) { row(8000+tag,hr); CloseHandle(report); ExitProcess(tag); } }
void mainCRTStartup(void)
{
    report=CreateFileA("E:\\EXECUTE.BIN",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    IDirectDraw *base=0; IDirectDraw4 *dd=0; IDirect3D3 *d3d=0; IDirect3DDevice3 *dev=0;
    IDirectDrawSurface4 *target=0; IDirect3DViewport3 *viewport=0;
    check(DirectDrawCreate(0,&base,0),1);
    check(IDirectDraw_QueryInterface(base,&IID_IDirectDraw4,(void**)&dd),2);
    check(IDirectDraw4_SetCooperativeLevel(dd,0,DDSCL_NORMAL),3);
    DDSURFACEDESC2 desc={0}; desc.dwSize=sizeof desc;
    desc.dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_CAPS|DDSD_PIXELFORMAT;
    desc.dwWidth=desc.dwHeight=64;
    desc.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE|DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwSize=sizeof desc.ddpfPixelFormat;
    desc.ddpfPixelFormat.dwFlags=DDPF_RGB;desc.ddpfPixelFormat.dwRGBBitCount=16;
    desc.ddpfPixelFormat.dwRBitMask=0xf800;desc.ddpfPixelFormat.dwGBitMask=0x7e0;desc.ddpfPixelFormat.dwBBitMask=0x1f;
    check(IDirectDraw4_CreateSurface(dd,&desc,&target,0),4);
    check(IDirectDraw4_QueryInterface(dd,&IID_IDirect3D3,(void**)&d3d),5);
#ifdef REFERENCE_RGB
    check(IDirect3D3_CreateDevice(d3d,&IID_IDirect3DRGBDevice,target,&dev,0),6);
#else
    check(IDirect3D3_CreateDevice(d3d,&IID_IDirect3DHALDevice,target,&dev,0),6);
#endif
    check(IDirect3D3_CreateViewport(d3d,&viewport,0),7);
    check(IDirect3DDevice3_AddViewport(dev,viewport),8);
    D3DVIEWPORT2 vp={0};vp.dwSize=sizeof vp;vp.dwWidth=vp.dwHeight=64;
    vp.dvClipX=-1;vp.dvClipY=1;vp.dvClipWidth=vp.dvClipHeight=2;vp.dvMaxZ=1;
    check(IDirect3DViewport3_SetViewport2(viewport,&vp),9);
    check(IDirect3DDevice3_SetCurrentViewport(dev,viewport),10);
    DDSURFACEDESC2 locked={0};
    check(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_ZENABLE,FALSE),15);
    check(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_CULLMODE,D3DCULL_NONE),16);
    D3DTLVERTEX v[3]={
        {.sx=0,.sy=0,.sz=.5f,.rhw=1,.color=0xffff0000,.tu=0,.tv=0},
        {.sx=64,.sy=0,.sz=.5f,.rhw=1,.color=0xffff0000,.tu=1,.tv=0},
        {.sx=0,.sy=64,.sz=.5f,.rhw=1,.color=0xffff0000,.tu=0,.tv=1}};
    check(IDirect3DDevice3_SetTexture(dev,0,0),20);
    check(IDirect3DDevice3_SetTextureStageState(dev,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1),21);
    check(IDirect3DDevice3_SetTextureStageState(dev,0,D3DTSS_COLORARG1,D3DTA_DIFFUSE),22);
    check(IDirect3DDevice3_SetTextureStageState(dev,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1),23);
    check(IDirect3DDevice3_SetTextureStageState(dev,0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE),24);
    check(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_ALPHATESTENABLE,TRUE),25);
    const unsigned refs[]={0,1,64,127,128,254,255};
    for(unsigned r=0;r<sizeof refs/sizeof refs[0];++r)
    for(int delta=-1;delta<=1;++delta) {
        int alpha=(int)refs[r]+delta;
        if(alpha<0 || alpha>255) continue;
        for(unsigned i=0;i<3;++i)v[i].color=((DWORD)alpha<<24)|0xff0000;
        check(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_ALPHAREF,refs[r]),26);
        for(unsigned func=1;func<=8;++func) {
            DDBLTFX fx={0};fx.dwSize=sizeof fx;fx.dwFillColor=0x1f;
            check(IDirectDrawSurface4_Blt(target,0,0,0,DDBLT_COLORFILL|DDBLT_WAIT,&fx),27);
            check(IDirect3DDevice3_SetRenderState(dev,D3DRENDERSTATE_ALPHAFUNC,func),28);
            check(IDirect3DDevice3_BeginScene(dev),29);
            check(IDirect3DDevice3_DrawPrimitive(dev,D3DPT_TRIANGLELIST,D3DFVF_TLVERTEX,v,3,0),30);
            check(IDirect3DDevice3_EndScene(dev),31);
            locked=(DDSURFACEDESC2){0};locked.dwSize=sizeof locked;
            check(IDirectDrawSurface4_Lock(target,0,&locked,DDLOCK_WAIT,0),32);
            row((refs[r]<<16)|((unsigned)alpha<<8)|func,*(WORD*)((BYTE*)locked.lpSurface+10*locked.lPitch+20));
            check(IDirectDrawSurface4_Unlock(target,0),33);
        }
    }
    IDirect3DDevice3_Release(dev);IDirect3DViewport3_Release(viewport);
    IDirectDrawSurface4_Release(target);IDirect3D3_Release(d3d);IDirectDraw4_Release(dd);IDirectDraw_Release(base);
    CloseHandle(report);ExitProcess(0);
}
