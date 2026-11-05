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
static void BlendLight(IDirect3D7 *d3d, IDirect3DDevice7 *device)
{
    D3DMATRIX m=Identity();D3DMATERIAL7 material={0};D3DLIGHT7 light={0};
    D3DDRAWPRIMITIVESTRIDEDDATA source={0};float vertex[9]={0,0,.5f,.25f,.25f,.125f,0,0,1};
    D3DVERTEXBUFFERDESC vd={sizeof vd,D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZRHW|D3DFVF_DIFFUSE,1};
    IDirect3DVertexBuffer7 *out=NULL;HRESULT hr;unsigned normalize,blend,weight,axis,rotation;
    material.diffuse.r=material.diffuse.g=material.diffuse.b=material.diffuse.a=1;
    light.dltType=D3DLIGHT_DIRECTIONAL;light.dcvDiffuse.r=light.dcvDiffuse.g=light.dcvDiffuse.b=1;light.dvDirection.z=-1;
    hr=IDirect3DDevice7_SetMaterial(device,&material);Row(1000,0,hr,0,0,0);if(FAILED(hr))ExitProcess(10);
    hr=IDirect3DDevice7_SetLight(device,0,&light);Row(1000,1,hr,0,0,0);if(FAILED(hr))ExitProcess(11);
    hr=IDirect3DDevice7_LightEnable(device,0,TRUE);Row(1000,2,hr,0,0,0);if(FAILED(hr))ExitProcess(12);
    IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_LIGHTING,TRUE);
    IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_AMBIENT,0);
    IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_DIFFUSEMATERIALSOURCE,D3DMCS_MATERIAL);
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_WORLD,&m);
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_VIEW,&m);
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_PROJECTION,&m);
    m._11=m._33=0;m._13=-1;m._31=1;
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_WORLD1,&m);
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_WORLD2,&m);
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_WORLD3,&m);
    source.position.lpvData=vertex;source.position.dwStride=sizeof vertex;
    source.normal.lpvData=vertex+6;source.normal.dwStride=sizeof vertex;
    hr=IDirect3D7_CreateVertexBuffer(d3d,&vd,&out,0);Row(1000,3,hr,0,0,0);if(FAILED(hr))ExitProcess(13);
    for(rotation=0;rotation<3;++rotation) {
    m=Identity();if(rotation==1){m._11=m._33=0;m._13=-1;m._31=1;}else if(rotation==2){m._11=2;m._33=.5f;}
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_WORLD1,&m);
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_WORLD2,&m);
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_WORLD3,&m);
    for(normalize=0;normalize<2;++normalize)for(blend=0;blend<4;++blend)for(weight=0;weight<4;++weight)for(axis=0;axis<3;++axis) {
        DWORD test=1001+rotation*96+((normalize*4+blend)*4+weight)*3+axis,bits[5],size;void *data=NULL;
        static const float weights[]={0,.25f,.5f,1};
        vertex[3]=weights[weight];vertex[6]=axis==0?0:axis==1?1:.5f;vertex[7]=0;vertex[8]=axis==0?1:axis==1?0:.5f;
        IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_NORMALIZENORMALS,normalize);
        IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_VERTEXBLEND,blend);
        hr=IDirect3DVertexBuffer7_ProcessVerticesStrided(out,D3DVOP_TRANSFORM|D3DVOP_LIGHT,0,1,&source,D3DFVF_XYZB3|D3DFVF_NORMAL,device,0);
        Row(test,0,hr,0,0,0);if(FAILED(hr))ExitProcess(14);
        hr=IDirect3DVertexBuffer7_Lock(out,DDLOCK_WAIT,&data,&size);Row(test,1,hr,0,0,0);if(FAILED(hr))ExitProcess(15);
        memcpy(bits,data,sizeof bits);Row(test,2,S_OK,bits[0],bits[1],bits[2]);Row(test,3,S_OK,bits[3],bits[4],0);
        IDirect3DVertexBuffer7_Unlock(out);
    }
    }
    IDirect3DVertexBuffer7_Release(out);
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show)
{
    IDirectDraw7 *dd=NULL;IDirectDrawSurface7 *target=NULL;IDirect3D7 *d3d=NULL;IDirect3DDevice7 *device=NULL;
    DDSURFACEDESC2 desc={0};D3DMATRIX world=Identity();D3DVIEWPORT7 vp={0,0,64,64,0,1};unsigned i,b,blend,op,scenario,process_case;
    const D3DTRANSFORMSTATETYPE worlds[]={D3DTRANSFORMSTATE_WORLD,D3DTRANSFORMSTATE_WORLD1,D3DTRANSFORMSTATE_WORLD2,D3DTRANSFORMSTATE_WORLD3};
    (void)instance;(void)previous;(void)command;(void)show;
    report=CreateFileA("E:\\VERTEX.BIN",GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);if(report==INVALID_HANDLE_VALUE)return 2;
    if(DirectDrawCreateEx(NULL,(void **)&dd,&IID_IDirectDraw7,NULL)!=DD_OK)return 3;
    IDirectDraw7_SetCooperativeLevel(dd,NULL,DDSCL_NORMAL);
    desc.dwSize=sizeof desc;desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
    desc.dwWidth=desc.dwHeight=64;desc.ddsCaps.dwCaps=DDSCAPS_3DDEVICE|DDSCAPS_OFFSCREENPLAIN|DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwSize=sizeof desc.ddpfPixelFormat;desc.ddpfPixelFormat.dwFlags=DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount=32;desc.ddpfPixelFormat.dwRBitMask=0xff0000;desc.ddpfPixelFormat.dwGBitMask=0xff00;desc.ddpfPixelFormat.dwBBitMask=0xff;
    if(IDirectDraw7_CreateSurface(dd,&desc,&target,NULL)!=DD_OK)return 4;
    if(IDirectDraw7_QueryInterface(dd,&IID_IDirect3D7,(void **)&d3d)!=DD_OK)return 5;
    if(IDirect3D7_CreateDevice(d3d,&IID_IDirect3DHALDevice,target,&device)!=DD_OK)return 6;
    IDirect3DDevice7_SetViewport(device,&vp);IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_LIGHTING,FALSE);
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_VIEW,&world);IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_PROJECTION,&world);
    for(i=0;i<4;++i) {world=Identity();world._41=i*.25f;IDirect3DDevice7_SetTransform(device,worlds[i],&world);}
    for(process_case=0;process_case<2;++process_case) {
    vp.dwWidth=process_case?32:64;vp.dwX=process_case?10:0;vp.dwY=process_case?20:0;vp.dwHeight=process_case?32:64;
    vp.dvMinZ=process_case?.25f:0;vp.dvMaxZ=process_case?.75f:1;
    {HRESULT hr=IDirect3DDevice7_SetViewport(device,&vp);Row(900+process_case,0,hr,0,0,0);if(FAILED(hr))return 9;}
    world=Identity();if(process_case)world._44=2;
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_PROJECTION,&world);
    for(b=0;b<6;++b)for(blend=0;blend<4&&blend<=b;++blend)for(op=0;op<4;++op) {
        const DWORD test=process_case*200+b*16+blend*4+op;
        D3DVERTEXBUFFERDESC vd={sizeof vd,D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZRHW|D3DFVF_DIFFUSE,3};
        IDirect3DVertexBuffer7 *out=NULL;D3DDRAWPRIMITIVESTRIDEDDATA source={0};DWORD *data=NULL,size=0;
        float vertices[3][8]={{.0078125f,.0078125f,.5f,.25f,.25f,.25f,0,0},{2,0,.5f,.25f,.25f,.25f,0,0},{-2,1,-.5f,.25f,.25f,.25f,0,0}};
        D3DCLIPSTATUS cs={0};DWORD bits[15], clipbits[6];HRESULT hr;DWORD fvf=b?D3DFVF_XYZB1+(b-1)*2:D3DFVF_XYZ;
        IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_VERTEXBLEND,blend);
        cs.dwFlags=D3DCLIPSTATUS_STATUS|D3DCLIPSTATUS_EXTENTS2;cs.minx=cs.miny=cs.minz=1000;cs.maxx=cs.maxy=cs.maxz=-1000;
        Row(test,0,IDirect3DDevice7_SetClipStatus(device,&cs),0,0,0);
        hr=IDirect3D7_CreateVertexBuffer(d3d,&vd,&out,0);Row(test,1,hr,0,0,0);if(FAILED(hr))continue;
        if(process_case) {vertices[1][0]=4;vertices[2][0]=-4;}
        source.position.lpvData=vertices;source.position.dwStride=sizeof vertices[0];
        hr=IDirect3DVertexBuffer7_ProcessVerticesStrided(out,D3DVOP_TRANSFORM|(op&1?D3DVOP_CLIP:0)|(op&2?D3DVOP_EXTENTS:0),0,3,&source,fvf,device,0);
        Row(test,2,hr,0,0,0);
        if(SUCCEEDED(hr)&&IDirect3DVertexBuffer7_Lock(out,DDLOCK_WAIT,(void **)&data,&size)==D3D_OK) {
            memcpy(bits,data,sizeof bits);
            for(i=0;i<3;++i) {Row(test,10+i,S_OK,bits[i*5],bits[i*5+1],bits[i*5+2]);Row(test,20+i,S_OK,bits[i*5+3],bits[i*5+4],0);}
            IDirect3DVertexBuffer7_Unlock(out);
        }
        hr=IDirect3DDevice7_GetClipStatus(device,&cs);Row(test,30,hr,cs.dwFlags,cs.dwStatus,0);
        memcpy(clipbits,&cs.minx,sizeof clipbits);Row(test,31,S_OK,clipbits[0],clipbits[1],clipbits[2]);Row(test,32,S_OK,clipbits[3],clipbits[4],clipbits[5]);
        IDirect3DVertexBuffer7_Release(out);
    }
    }
    for(scenario=0;scenario<4;++scenario) {
    world=Identity();if(scenario==1)world._41=.5f;
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_WORLD,&world);
    world=Identity();if(scenario==2)world._41=.5f;
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_VIEW,&world);
    world=Identity();if(scenario==3)world._11=2;
    IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_PROJECTION,&world);
    for(i=0;i<6;++i)for(b=0;b<7;++b) {
        D3DVECTOR center={0,0,.5f};D3DVALUE radius=.25f;DWORD result=0xcccccccc;HRESULT hr;
        float distance=(int)b*.25f-.75f; /* signed distance to the selected plane */
        switch(i) {case 0:center.x=-1+distance;break;case 1:center.x=1-distance;break;case 2:center.y=1-distance;break;case 3:center.y=-1+distance;break;case 4:center.z=distance;break;case 5:center.z=1-distance;break;}
        hr=IDirect3DDevice7_ComputeSphereVisibility(device,&center,&radius,1,0,&result);Row(100+scenario*6+i,b,hr,result,0,0);
    }
    }
    {
    D3DVERTEXBUFFERDESC vd={sizeof vd,D3DVBCAPS_SYSTEMMEMORY,D3DFVF_XYZRHW,1};
    IDirect3DVertexBuffer7 *out=NULL;D3DDRAWPRIMITIVESTRIDEDDATA source={0};float outside[3]={-4,0,.5f};
    world=Identity();IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_PROJECTION,&world);
    IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_VERTEXBLEND,0);
    if(IDirect3D7_CreateVertexBuffer(d3d,&vd,&out,0)!=DD_OK)return 7;
    source.position.lpvData=outside;source.position.dwStride=sizeof outside;
    for(i=0;i<8;++i) {
        D3DCLIPSTATUS cs={i,0x3f000,12.25f,38.5f,15.5f,40.0f,.25f,.75f};
        DWORD bits[6];HRESULT hr=IDirect3DDevice7_SetClipStatus(device,&cs);
        Row(500+i,0,hr,cs.dwFlags,cs.dwStatus,0);
        hr=IDirect3DDevice7_GetClipStatus(device,&cs);Row(500+i,1,hr,cs.dwFlags,cs.dwStatus,0);
        memcpy(bits,&cs.minx,sizeof bits);Row(500+i,2,S_OK,bits[0],bits[1],bits[2]);Row(500+i,3,S_OK,bits[3],bits[4],bits[5]);
        hr=IDirect3DVertexBuffer7_ProcessVerticesStrided(out,D3DVOP_TRANSFORM|D3DVOP_CLIP|D3DVOP_EXTENTS,0,1,&source,D3DFVF_XYZ,device,0);
        Row(500+i,4,hr,0,0,0);
        hr=IDirect3DDevice7_GetClipStatus(device,&cs);Row(500+i,5,hr,cs.dwFlags,cs.dwStatus,0);
        memcpy(bits,&cs.minx,sizeof bits);Row(500+i,6,S_OK,bits[0],bits[1],bits[2]);Row(500+i,7,S_OK,bits[3],bits[4],bits[5]);
        {void *data=NULL;DWORD size=0;if(IDirect3DVertexBuffer7_Lock(out,DDLOCK_WAIT,&data,&size)!=DD_OK)return 8;
        memcpy(bits,data,16);Row(500+i,8,S_OK,bits[0],bits[1],bits[2]);Row(500+i,9,S_OK,bits[3],0,0);IDirect3DVertexBuffer7_Unlock(out);}
    }
    IDirect3DVertexBuffer7_Release(out);
    }
    BlendLight(d3d,device);
    IDirect3DDevice7_Release(device);IDirect3D7_Release(d3d);IDirectDrawSurface7_Release(target);IDirectDraw7_Release(dd);CloseHandle(report);return 0;
}
void mainCRTStartup(void) {ExitProcess(WinMain(GetModuleHandleA(NULL),NULL,NULL,0));}
