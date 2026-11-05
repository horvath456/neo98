/* SPDX-License-Identifier: GPL-2.0-or-later */
/* nonzero SetEntries ranges must reach the same display indices. */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
static IDirectDraw *dd;
static IDirectDrawSurface *primary;
static IDirectDrawPalette *palette;
static PALETTEENTRY entries[256];
static WNDCLASSA wc;
static DDSURFACEDESC desc;
static HANDLE report;
static void check(HRESULT hr, DWORD step) {
    DWORD row[2]={step,(DWORD)hr},written;
    WriteFile(report,row,sizeof row,&written,0);
    if(FAILED(hr))ExitProcess(step);
}
void mainCRTStartup(void) {
    report=CreateFileA("E:\\PALETTE.BIN",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    if(report==INVALID_HANDLE_VALUE)ExitProcess(98);
    wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(0);wc.lpszClassName="PaletteRanges";
    RegisterClassA(&wc);
    HWND w=CreateWindowA(wc.lpszClassName,"Palette ranges",WS_POPUP|WS_VISIBLE,0,0,640,480,0,0,wc.hInstance,0);
    if(!w)ExitProcess(97);
    check(DirectDrawCreate(0,&dd,0),1);
    check(IDirectDraw_SetCooperativeLevel(dd,w,DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN),2);
    check(IDirectDraw_SetDisplayMode(dd,640,480,8),3);
    desc.dwSize=sizeof desc;desc.dwFlags=DDSD_CAPS;desc.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE;
    check(IDirectDraw_CreateSurface(dd,&desc,&primary,0),4);
    for(unsigned i=0;i<256;i++){entries[i].peRed=entries[i].peGreen=255;}
    check(IDirectDraw_CreatePalette(dd,DDPCAPS_8BIT|DDPCAPS_ALLOW256,entries,&palette,0),5);
    check(IDirectDrawSurface_SetPalette(primary,palette),6);
    for(unsigned i=0;i<248;i++) {
        entries[i].peRed=i<32?255:0;
        entries[i].peGreen=i>=32&&i<144?255:0;
        entries[i].peBlue=i>=144?255:0;
    }
    check(IDirectDrawPalette_SetEntries(palette,0,32,112,entries+32),7);
    check(IDirectDrawPalette_SetEntries(palette,0,144,104,entries+144),8);
    check(IDirectDrawPalette_SetEntries(palette,0,0,32,entries),9);
    desc.dwSize=sizeof desc;
    check(IDirectDrawSurface_Lock(primary,0,&desc,DDLOCK_WAIT,0),10);
    const BYTE indices[4]={16,64,200,250};
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)
        ((BYTE*)desc.lpSurface)[y*desc.lPitch+x]=indices[x/160];
    check(IDirectDrawSurface_Unlock(primary,desc.lpSurface),11);
    FlushFileBuffers(report);
    Sleep(8000);
    CloseHandle(report);
    ExitProcess(0);
}
