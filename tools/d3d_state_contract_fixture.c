/* SPDX-License-Identifier: GPL-2.0-or-later */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <string.h>
static HANDLE report;
static void Row(DWORD test,DWORD item,HRESULT hr,DWORD a,DWORD b,DWORD c)
{
    DWORD row[6]={test,item,(DWORD)hr,a,b,c},written;
    if(!WriteFile(report,row,sizeof row,&written,NULL)||written!=sizeof row) ExitProcess(3);
}
static D3DMATRIX Identity(void) {D3DMATRIX m={0};m._11=m._22=m._33=m._44=1;return m;}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show)
{
    IDirectDraw7 *dd=NULL;IDirectDrawSurface7 *target=NULL;IDirect3D7 *d3d=NULL;IDirect3DDevice7 *device=NULL;
    DDSURFACEDESC2 desc={0};
    (void)instance;(void)previous;(void)command;(void)show;
    report=CreateFileA("E:\\STATES.BIN",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);if(report==INVALID_HANDLE_VALUE)return 2;
    if(DirectDrawCreateEx(NULL,(void **)&dd,&IID_IDirectDraw7,NULL)!=DD_OK)return 3;
    IDirectDraw7_SetCooperativeLevel(dd,NULL,DDSCL_NORMAL);
    desc.dwSize=sizeof desc;desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
    desc.dwWidth=desc.dwHeight=64;desc.ddsCaps.dwCaps=DDSCAPS_3DDEVICE|DDSCAPS_OFFSCREENPLAIN|DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwSize=sizeof desc.ddpfPixelFormat;desc.ddpfPixelFormat.dwFlags=DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount=32;desc.ddpfPixelFormat.dwRBitMask=0xff0000;desc.ddpfPixelFormat.dwGBitMask=0xff00;desc.ddpfPixelFormat.dwBBitMask=0xff;
    if(IDirectDraw7_CreateSurface(dd,&desc,&target,NULL)!=DD_OK)return 4;
    if(IDirectDraw7_QueryInterface(dd,&IID_IDirect3D7,(void **)&d3d)!=DD_OK)return 5;
    if(IDirect3D7_CreateDevice(d3d,&IID_IDirect3DHALDevice,target,&device)!=DD_OK)return 6;
    for(unsigned type=2;type<=3;++type) {
        for(unsigned state=1;state<256;++state) {
            DWORD block=0,value=0xdeadbeef;HRESULT first,second,get;
            first=IDirect3DDevice7_SetRenderState(device,(D3DRENDERSTATETYPE)state,0);
            if(FAILED(first))continue;
            get=IDirect3DDevice7_GetRenderState(device,(D3DRENDERSTATETYPE)state,&value);
            if(FAILED(get))continue;
            if(FAILED(IDirect3DDevice7_CreateStateBlock(device,(D3DSTATEBLOCKTYPE)type,&block)))return 10;
            second=IDirect3DDevice7_SetRenderState(device,(D3DRENDERSTATETYPE)state,1);
            IDirect3DDevice7_ApplyStateBlock(device,block);
            get=IDirect3DDevice7_GetRenderState(device,(D3DRENDERSTATETYPE)state,&value);
            Row(type,state,get,value,(DWORD)first,(DWORD)second);
            IDirect3DDevice7_DeleteStateBlock(device,block);
        }
        for(unsigned stage=0;stage<8;++stage)for(unsigned state=1;state<=24;++state) {
            DWORD block=0,value=0xdeadbeef;
            HRESULT first=IDirect3DDevice7_SetTextureStageState(device,stage,(D3DTEXTURESTAGESTATETYPE)state,0);
            if(FAILED(first))continue;
            if(FAILED(IDirect3DDevice7_GetTextureStageState(device,stage,(D3DTEXTURESTAGESTATETYPE)state,&value)))continue;
            IDirect3DDevice7_CreateStateBlock(device,(D3DSTATEBLOCKTYPE)type,&block);
            HRESULT second=IDirect3DDevice7_SetTextureStageState(device,stage,(D3DTEXTURESTAGESTATETYPE)state,1);
            IDirect3DDevice7_ApplyStateBlock(device,block);
            HRESULT get=IDirect3DDevice7_GetTextureStageState(device,stage,(D3DTEXTURESTAGESTATETYPE)state,&value);
            Row(type,1000+stage*32+state,get,value,(DWORD)first,(DWORD)second);
            IDirect3DDevice7_DeleteStateBlock(device,block);
        }
        DWORD block=0;HRESULT hr;BOOL enabled=FALSE;
        D3DMATRIX first=Identity(),second=Identity(),matrix;
        D3DMATERIAL7 m1={0},m2={0},material;
        D3DVIEWPORT7 v1={0,0,64,64,0,1},v2={1,2,32,40,0.25f,0.75f},viewport;
        D3DLIGHT7 l1={0},l2={0},light;
        D3DVALUE p1[4]={1,2,3,4},p2[4]={5,6,7,8},plane[4];
        first._41=2;second._41=4;m1.diffuse.r=0.25f;m2.diffuse.r=0.75f;
        l1.dltType=l2.dltType=D3DLIGHT_DIRECTIONAL;l1.dvDirection.z=1;l2.dvDirection.x=1;
        IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_WORLD,&first);
        IDirect3DDevice7_SetMaterial(device,&m1);IDirect3DDevice7_SetViewport(device,&v1);
        IDirect3DDevice7_SetClipPlane(device,0,p1);IDirect3DDevice7_SetLight(device,63,&l1);IDirect3DDevice7_LightEnable(device,63,TRUE);
        IDirect3DDevice7_SetLight(device,1,&l1);IDirect3DDevice7_LightEnable(device,1,TRUE);
        IDirect3DDevice7_CreateStateBlock(device,(D3DSTATEBLOCKTYPE)type,&block);
        IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_WORLD,&second);
        IDirect3DDevice7_SetMaterial(device,&m2);IDirect3DDevice7_SetViewport(device,&v2);
        IDirect3DDevice7_SetClipPlane(device,0,p2);IDirect3DDevice7_SetLight(device,63,&l2);IDirect3DDevice7_LightEnable(device,63,FALSE);
        IDirect3DDevice7_SetLight(device,1,&l2);IDirect3DDevice7_LightEnable(device,1,FALSE);
        IDirect3DDevice7_ApplyStateBlock(device,block);
        hr=IDirect3DDevice7_GetTransform(device,D3DTRANSFORMSTATE_WORLD,&matrix);Row(type,2000,hr,!memcmp(&matrix,&first,sizeof first),!memcmp(&matrix,&second,sizeof second),0);
        hr=IDirect3DDevice7_GetMaterial(device,&material);Row(type,2001,hr,!memcmp(&material,&m1,sizeof m1),!memcmp(&material,&m2,sizeof m2),0);
        hr=IDirect3DDevice7_GetViewport(device,&viewport);Row(type,2002,hr,!memcmp(&viewport,&v1,sizeof v1),!memcmp(&viewport,&v2,sizeof v2),0);
        hr=IDirect3DDevice7_GetClipPlane(device,0,plane);Row(type,2003,hr,!memcmp(plane,p1,sizeof p1),!memcmp(plane,p2,sizeof p2),0);
        hr=IDirect3DDevice7_GetLight(device,63,&light);Row(type,2004,hr,!memcmp(&light,&l1,sizeof l1),!memcmp(&light,&l2,sizeof l2),0);
        hr=IDirect3DDevice7_GetLightEnable(device,63,&enabled);Row(type,2005,hr,enabled!=FALSE,0,0);
        hr=IDirect3DDevice7_GetLight(device,1,&light);Row(type,2006,hr,!memcmp(&light,&l1,sizeof l1),!memcmp(&light,&l2,sizeof l2),0);
        hr=IDirect3DDevice7_GetLightEnable(device,1,&enabled);Row(type,2007,hr,enabled!=FALSE,0,0);
        IDirect3DDevice7_DeleteStateBlock(device,block);
    }
    for(unsigned id=0;id<=6;++id) {
        DWORD values[4]={0xdeadbeef,0x12345678,0x90abcdef,0xaabbccdd};
        HRESULT hr=IDirect3DDevice7_GetInfo(device,id,values,sizeof values);
        Row(10,id,hr,values[0],values[1],values[2]);
    }
    desc.ddsCaps.dwCaps=DDSCAPS_TEXTURE|DDSCAPS_VIDEOMEMORY;
    IDirectDrawSurface7 *texture=NULL,*read=NULL;
    if(FAILED(IDirectDraw7_CreateSurface(dd,&desc,&texture,NULL)))return 11;
    for(unsigned type=2;type<=3;++type) {
        DWORD block=0;
        Row(type,2011,IDirect3DDevice7_SetTexture(device,0,texture),0,0,0);
        IDirect3DDevice7_CreateStateBlock(device,(D3DSTATEBLOCKTYPE)type,&block);
        IDirect3DDevice7_SetTexture(device,0,NULL);IDirect3DDevice7_ApplyStateBlock(device,block);
        HRESULT hr=IDirect3DDevice7_GetTexture(device,0,&read);Row(type,2010,hr,read==texture,read==NULL,0);
        if(read)IDirectDrawSurface7_Release(read);
        IDirect3DDevice7_DeleteStateBlock(device,block);
    }
    IDirect3DDevice7_SetTexture(device,0,NULL);IDirectDrawSurface7_Release(texture);
    IDirect3DDevice7_Release(device);IDirect3D7_Release(d3d);IDirectDrawSurface7_Release(target);IDirectDraw7_Release(dd);
    CloseHandle(report);return 0;
}
void mainCRTStartup(void){ExitProcess(WinMain(GetModuleHandleA(NULL),NULL,NULL,0));}
