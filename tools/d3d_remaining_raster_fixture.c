/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* the same input and readback through original D3D and the SDK. */
#define main previous_main
#include "arm64_d3d_fixture.c"
#undef main

static NV facing[6] = {{-.75f,.75f,.5f,0,0,-1,0,0},{.75f,.75f,.5f,0,0,-1,1,0},
    {-.75f,-.75f,.5f,0,0,-1,0,1},{.75f,.75f,.5f,0,0,-1,1,0},
    {.75f,-.75f,.5f,0,0,-1,1,1},{-.75f,-.75f,.5f,0,0,-1,0,1}};
static void ManyLights(void)
{
    const unsigned counts[]={1,8,9,32,33,64};
    D3DLIGHT7 light={0}; D3DMATERIAL7 material={0};
    light.dltType=D3DLIGHT_DIRECTIONAL;light.dvDirection.z=1;
    light.dcvDiffuse.r=light.dcvDiffuse.g=light.dcvDiffuse.b=1.0f/128;
    material.diffuse.r=material.diffuse.g=material.diffuse.b=material.diffuse.a=1;
    for(unsigned n=0;n<sizeof counts/sizeof *counts;++n){
        char name[20];sprintf(name,"LIGHT%u",counts[n]);
        Begin(0);Untextured();RS(D3DRENDERSTATE_LIGHTING,TRUE);
        RS(D3DRENDERSTATE_AMBIENT,0);RS(D3DRENDERSTATE_DIFFUSEMATERIALSOURCE,D3DMCS_MATERIAL);
        IDirect3DDevice7_SetMaterial(device,&material);
        for(unsigned i=0;i<counts[n];++i){Hr("many SetLight",IDirect3DDevice7_SetLight(device,i,&light));Hr("many LightEnable",IDirect3DDevice7_LightEnable(device,i,TRUE));}
        Hr("many draw",IDirect3DDevice7_DrawPrimitive(device,D3DPT_TRIANGLELIST,D3DFVF_VERTEX,facing,6,0));
        End(name);Pixel(name,128,128);
        for(unsigned i=0;i<counts[n];++i)IDirect3DDevice7_LightEnable(device,i,FALSE);
    }
    Begin(0);Untextured();RS(D3DRENDERSTATE_LIGHTING,TRUE);RS(D3DRENDERSTATE_AMBIENT,0);
    RS(D3DRENDERSTATE_DIFFUSEMATERIALSOURCE,D3DMCS_MATERIAL);IDirect3DDevice7_SetMaterial(device,&material);
    for(unsigned i=0;i<64;++i)IDirect3DDevice7_LightEnable(device,i,TRUE);
    for(unsigned i=0;i<63;++i)IDirect3DDevice7_LightEnable(device,i,FALSE);
    IDirect3DDevice7_DrawPrimitive(device,D3DPT_TRIANGLELIST,D3DFVF_VERTEX,facing,6,0);
    End("LIGHT63");Pixel("LIGHT63",128,128);IDirect3DDevice7_LightEnable(device,63,FALSE);
}
static void ComputedFog(void)
{
    const unsigned modes[]={D3DFOG_LINEAR,D3DFOG_EXP,D3DFOG_EXP2};
    for(unsigned n=0;n<3;++n){
        char name[20];sprintf(name,"VFOG%u",n);
        Begin(0);Untextured();RS(D3DRENDERSTATE_FOGENABLE,TRUE);RS(D3DRENDERSTATE_FOGCOLOR,0xffffff);
        RS(D3DRENDERSTATE_FOGVERTEXMODE,modes[n]);RS(D3DRENDERSTATE_FOGTABLEMODE,D3DFOG_NONE);
        RS(D3DRENDERSTATE_FOGSTART,F(.2f));RS(D3DRENDERSTATE_FOGEND,F(.8f));RS(D3DRENDERSTATE_FOGDENSITY,F(1.25f));
        ModelQuad(-.875f,-.875f,-.375f,.875f,.1f,0xff000000);
        ModelQuad(-.25f,-.875f,.25f,.875f,.5f,0xff000000);
        ModelQuad(.375f,-.875f,.875f,.875f,.9f,0xff000000);
        End(name);Pixel(name,48,128);Pixel(name,128,128);Pixel(name,208,128);
    }
}
static void GeneratedCoordinates(void)
{
    const unsigned modes[]={D3DTSS_TCI_CAMERASPACEPOSITION,D3DTSS_TCI_CAMERASPACENORMAL,D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR};
    LPDIRECTDRAWSURFACE7 texture=Texture(ARGB8888,8,1,0,DDSCAPS2_TEXTUREMANAGE,Pattern);
    D3DMATRIX matrix=identity;matrix._11=matrix._22=.5f;matrix._41=matrix._42=.5f;
    for(unsigned n=0;n<3;++n){
        char name[20];sprintf(name,"TGEN%u",n);Begin(0);Textured(texture);
        TSS(0,D3DTSS_TEXCOORDINDEX,modes[n]);TSS(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2);
        IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_TEXTURE0,&matrix);
        IDirect3DDevice7_DrawPrimitive(device,D3DPT_TRIANGLELIST,D3DFVF_VERTEX,facing,6,0);
        End(name);Pixel(name,64,64);Pixel(name,192,192);
    }
    IDirect3DDevice7_SetTexture(device,0,NULL);IDirectDrawSurface7_Release(texture);
}
static unsigned Checker(unsigned x,unsigned y,unsigned side,unsigned level)
{(void)side;(void)y;return level?0xff808080:(x&1)?0xffffffff:0xff000000;}
static void Anisotropic(void)
{
    LPDIRECTDRAWSURFACE7 texture=Texture(ARGB8888,64,7,0,DDSCAPS2_TEXTUREMANAGE,Checker);
    for(unsigned n=0;n<2;++n){
        TL v[6];char name[20];sprintf(name,"ANISO%u",n);Begin(0x400000);Textured(texture);
        TSS(0,D3DTSS_MINFILTER,n?D3DTFN_ANISOTROPIC:D3DTFN_LINEAR);TSS(0,D3DTSS_MAGFILTER,D3DTFG_LINEAR);
        TSS(0,D3DTSS_MAXANISOTROPY,8);TSS(0,D3DTSS_MIPFILTER,D3DTFP_LINEAR);
        QuadVertices(v,32,96,224,160,.5f,0xffffffff);
        for(unsigned i=0;i<6;++i){v[i].v*=8;v[i].rhw=v[i].u?.25f:1;}
        TSS(0,D3DTSS_ADDRESS,D3DTADDRESS_WRAP);
        IDirect3DDevice7_DrawPrimitive(device,D3DPT_TRIANGLELIST,D3DFVF_TLVERTEX,v,6,0);
        End(name);Pixel(name,64,128);Pixel(name,192,128);
    }
    IDirect3DDevice7_SetTexture(device,0,NULL);IDirectDrawSurface7_Release(texture);
}
static void Bump(void)
{
    DDSURFACEDESC2 desc={0};LPDIRECTDRAWSURFACE7 bump=NULL;
    LPDIRECTDRAWSURFACE7 texture=Texture(ARGB8888,8,1,0,DDSCAPS2_TEXTUREMANAGE,Pattern);
    desc.dwSize=sizeof desc;desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
    desc.dwWidth=desc.dwHeight=4;desc.ddsCaps.dwCaps=DDSCAPS_TEXTURE;desc.ddsCaps.dwCaps2=DDSCAPS2_TEXTUREMANAGE;
    desc.ddpfPixelFormat.dwSize=sizeof(DDPIXELFORMAT);desc.ddpfPixelFormat.dwFlags=DDPF_BUMPDUDV;
    desc.ddpfPixelFormat.dwBumpBitCount=16;desc.ddpfPixelFormat.dwBumpDuBitMask=255;desc.ddpfPixelFormat.dwBumpDvBitMask=0xff00;
    Hr("bump create",IDirectDraw7_CreateSurface(dd,&desc,&bump,NULL));
    if(bump){
        memset(&desc,0,sizeof desc);desc.dwSize=sizeof desc;
        if(SUCCEEDED(IDirectDrawSurface7_Lock(bump,NULL,&desc,DDLOCK_WAIT,NULL))){
            for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)((WORD*)((BYTE*)desc.lpSurface+y*desc.lPitch))[x]=0x0040;
            IDirectDrawSurface7_Unlock(bump,NULL);
        }
        Begin(0x40);Textured(bump);TSS(0,D3DTSS_COLOROP,D3DTOP_BUMPENVMAP);
        TSS(0,D3DTSS_BUMPENVMAT00,F(.5f));TSS(0,D3DTSS_BUMPENVMAT11,F(.5f));
        IDirect3DDevice7_SetTexture(device,1,texture);TSS(1,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        TSS(1,D3DTSS_COLORARG1,D3DTA_TEXTURE);TSS(1,D3DTSS_TEXCOORDINDEX,0);
        TSS(1,D3DTSS_MINFILTER,D3DTFN_POINT);TSS(1,D3DTSS_MAGFILTER,D3DTFG_POINT);
        TSS(1,D3DTSS_ADDRESS,D3DTADDRESS_WRAP);Quad(32,32,224,224,.5f,0xffffffff);
        End("BUMP");Pixel("BUMP",80,80);Pixel("BUMP",128,80);
        IDirect3DDevice7_SetTexture(device,0,NULL);IDirectDrawSurface7_Release(bump);
    }
    IDirect3DDevice7_SetTexture(device,1,NULL);IDirectDrawSurface7_Release(texture);
}
int main(int argc,char **argv)
{
    if(argc<3)return 2;report=fopen(argv[1],"w");if(!report)return 2;strcpy(folder,argv[2]);
    if(argc>3&&!strcmp(argv[3],"tnl"))device_class=&IID_IDirect3DTnLHalDevice;
    if(Open()){
        RS(D3DRENDERSTATE_ZENABLE,D3DZB_FALSE);RS(D3DRENDERSTATE_LIGHTING,FALSE);RS(D3DRENDERSTATE_CULLMODE,D3DCULL_NONE);
        IDirect3DDevice7_CreateStateBlock(device,D3DSBT_ALL,&base_block);
        ManyLights();ComputedFog();GeneratedCoordinates();Anisotropic();Bump();
        IDirect3DDevice7_DeleteStateBlock(device,base_block);IDirect3DDevice7_Release(device);
        if(depth)IDirectDrawSurface7_Release(depth);IDirectDrawSurface7_Release(target);
        IDirect3D7_Release(d3d);if(primary)IDirectDrawSurface7_Release(primary);IDirectDraw7_Release(dd);
    }
    Say("D3D scenes=%d wrong=%d\n",scenes,wrong);fclose(report);return wrong!=0;
}
