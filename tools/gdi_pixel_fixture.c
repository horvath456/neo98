/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);
API U CreateCompatibleDC(U);API U CreateICA(const char*,const char*,const char*,void*);API U DeleteDC(U);API U DeleteObject(U);API U SelectObject(U,U);API U CreateDIBSection(U,const void*,U,void**,U,U);
API U SetPixel(U,int,int,U);API U SetPixelV(U,int,int,U);API U GetPixel(U,int,int);API U GetNearestColor(U,U);
API U SetROP2(U,U);API U SaveDC(U);API U RestoreDC(U,int);API U IntersectClipRect(U,int,int,int,int);API U SetWindowOrgEx(U,int,int,void*);API U SetViewportOrgEx(U,int,int,void*);API U MoveToEx(U,int,int,void*);API U GetCurrentPositionEx(U,void*);
static void check(int v,U n){if(!v)ExitProcess(n);}
static U swap(U v){return((v&255)<<16)|(v&0xff00)|((v>>16)&255);}
void run_gdi_pixel_test(void){
 U dc=CreateCompatibleDC(0);check(dc,13500);check(SetPixel(dc,0,0,0xffffff)==0xffffff&&GetPixel(dc,0,0)==0xffffff,13501);check(SetPixel(dc,0,0,0x0000ff)==0&&GetPixel(dc,0,0)==0,13502);
 U header[14]={40,16,(U)-8,0x00200001,0,0,0,0,0,0};void*raw=0;U bitmap=CreateDIBSection(0,header,0,&raw,0,0),old=SelectObject(dc,bitmap);check(bitmap&&raw&&old,13503);U*bits=raw;
 check(SetPixel(dc,2,3,0x00332211)==0x00332211&&bits[3*16+2]==0x00112233&&GetPixel(dc,2,3)==0x00332211,13504);
 check(SetPixelV(dc,2,3,0x00665544)&&bits[3*16+2]==0x00445566,13505);check(GetNearestColor(dc,0x00332211)==0x00332211&&GetNearestColor(dc,0x02332211)==0x00332211,13506);
 check(SaveDC(dc)==1&&IntersectClipRect(dc,2,3,4,5),13507);U before=bits[2*16+2];check(SetPixel(dc,2,2,0xffffff)==~0u&&!SetPixelV(dc,2,2,0xffffff)&&GetPixel(dc,2,2)==~0u&&bits[2*16+2]==before,13508);check(RestoreDC(dc,-1),13509);
 SetWindowOrgEx(dc,2,3,0);SetViewportOrgEx(dc,4,5,0);check(SetPixel(dc,1,1,0x123456)==0x123456&&bits[3*16+3]==0x563412,13510);SetWindowOrgEx(dc,0,0,0);SetViewportOrgEx(dc,0,0,0);
 check(SetPixel(dc,65538,65539,0x123456)==0x123456&&bits[3*16+2]==0x563412,13511);
 MoveToEx(dc,7,8,0);int point[2];SetROP2(dc,7);before=bits[3*16+2];check(SetPixel(dc,2,3,0x0f0f0f)==0x0f0f0f&&bits[3*16+2]==(before^0x0f0f0f)&&GetPixel(dc,2,3)==swap(before^0x0f0f0f),13512);check(GetCurrentPositionEx(dc,point)&&point[0]==7&&point[1]==8,13513);SetROP2(dc,13);
 /* Since the palette family was ported, every DC starts with the original
    20-entry stock palette (GDI), so PALETTEINDEX(1) resolves to its
    second entry instead of failing. The former expectation of error 50 dates
    from the time when a DC carried no palette object at all. */
 SetLastError(0);check(SetPixel(dc,1,1,0x01000001)==0x80&&GetPixel(dc,1,1)==0x80&&!GetLastError(),13514);check(!SetPixelV(0,1,1,0)&&SetPixel(0,1,1,0)==~0u&&GetPixel(0,1,1)==~0u&&GetNearestColor(0,0)==~0u,13515);
 SelectObject(dc,old);check(DeleteObject(bitmap),13516);
 for(U i=0;i<5;++i){U depth=i==0?24:i==1?16:i==2?8:i==3?4:1;header[3]=(depth<<16)|1;header[8]=depth<=8?2:0;header[10]=0;header[11]=0x00ff0000;raw=0;bitmap=CreateDIBSection(0,header,0,&raw,0,0);check(bitmap&&raw,13517);old=SelectObject(dc,bitmap);
  U nearest=depth<=8?0x0000ff:depth==16?0x0000ff:0x0000fe;check(GetNearestColor(dc,0x0000fe)==nearest&&SetPixel(dc,0,0,0x0000fe)==nearest&&GetPixel(dc,0,0)==nearest,13518);
  if(depth==24)check(((unsigned char*)raw)[0]==0&&((unsigned char*)raw)[1]==0&&((unsigned char*)raw)[2]==254,13519);
  if(depth==16)check(*(unsigned short*)raw==0x7c00,13520);
  if(depth==8)check(*(unsigned char*)raw==1,13521);
  if(depth==4)check(*(unsigned char*)raw==0x10,13522);
  if(depth==1)check(*(unsigned char*)raw==0x80,13523);
  SelectObject(dc,old);check(DeleteObject(bitmap),13524);
 }
 U ic=CreateICA("DISPLAY",0,0,0);check(ic&&GetNearestColor(ic,0x123456)==0x123456&&GetPixel(ic,0,0)==~0u&&SetPixel(ic,0,0,0)==~0u&&!SetPixelV(ic,0,0,0),13525);check(DeleteDC(ic)&&DeleteDC(dc),13526);
}
#ifdef GDI_PIXEL_STANDALONE
void mainCRTStartup(void){run_gdi_pixel_test();ExitProcess(0);}
#endif
