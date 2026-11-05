/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U); API U GetLastError(void);
API U CreatePalette(void*); API U CreateHalftonePalette(U); API U GetPaletteEntries(U,U,U,void*); API U SetPaletteEntries(U,U,U,void*); API U ResizePalette(U,U); API U GetNearestPaletteIndex(U,U); API U SelectPalette(U,U,U); API U RealizePalette(U); API U AnimatePalette(U,U,U,const void*); API U GetSystemPaletteEntries(U,U,U,void*); API U GetSystemPaletteUse(U); API U SetSystemPaletteUse(U,U);
API U CreateCompatibleDC(U); API U DeleteDC(U); API U DeleteObject(U); API U SelectObject(U,U); API U GetObjectA(U,U,void*); API U CreateDIBSection(U,const void*,U,void**,U,U); API U CreateSolidBrush(U); API U CreatePen(U,int,U); API U PatBlt(U,int,int,int,int,U); API U MoveToEx(U,int,int,void*); API U LineTo(U,int,int); API U Rectangle(U,int,int,int,int); API U GetStockObject(U); API U SaveDC(U); API U RestoreDC(U,int); API U SetPixel(U,int,int,U); API U SetPixelV(U,int,int,U); API U GetPixel(U,int,int); API U GetNearestColor(U,U);
static void check(int value,U code){if(!value)ExitProcess(code);}
static U rawpixel(const unsigned char*p,U depth,U x,U y){p+=y*((16*depth+31)/32)*4;if(depth==1)return(p[x/8]>>(7-x%8))&1;if(depth==4)return(p[x/2]>>((1-x%2)*4))&15;p+=x*(depth/8);U v=0;for(U i=0;i<depth/8;++i)v|=(U)p[i]<<(i*8);return v;}
static U v[256];
void run_gdi_palette_test(void){
 U log[7]={0x00060300,0,0x010000ff,0x0000ff00,0x00ff0000,0x02003507,0xffffff},p=CreatePalette(log),q=0,dc=CreateCompatibleDC(0),stock=GetStockObject(15);
 check(p&&dc&&stock,18300);check(log[5]==0x02000007,18301);
 check(GetPaletteEntries(stock,99,99,0)==20&&GetPaletteEntries(p,77,9,0)==6,18302);
 check(GetPaletteEntries(p,4,10,v)==2&&v[0]==0x02000007&&v[1]==0xffffff,18303);
 check(GetObjectA(p,0,0)==4&&GetObjectA(p,4,v)==4&&v[0]==6,18304);
 v[0]=0xaabbccdd;check(GetObjectA(p,1,v)==1&&v[0]==0xaabbcc06,18305);
 check(GetNearestPaletteIndex(p,0x0100ffff)==65535&&GetNearestPaletteIndex(p,0x0200fe01)==2,18306);
 check(SelectPalette(dc,p,0)==stock&&RealizePalette(dc)==0,18307);
 check(GetSystemPaletteEntries(dc,0,0,0)==6&&GetSystemPaletteEntries(dc,250,12,v)==6&&v[5]==0xffffff,18308);
 check(GetSystemPaletteUse(dc)==0&&SetSystemPaletteUse(dc,2)==0,18309);
 check(CreateHalftonePalette(dc)==stock,18310);q=CreateHalftonePalette(0);check(q&&q!=stock&&GetPaletteEntries(q,0,0,0)==256&&DeleteObject(q),18311);
 U brush=CreateSolidBrush(0x01000001),pen=CreatePen(0,1,0x01000001),nullpen=GetStockObject(8);check(brush&&pen&&nullpen,18312);
 for(U i=0;i<6;++i){
  U depth=i==0?1:i==1?4:i==2?8:i==3?16:i==4?24:32;
  U h[16]={40,16,(U)-12,(depth<<16)|1,0,0,0,0,0,0,0,0xff0000,0x00ff00,0x0000ff,0xffffff};
  if(depth<=8)h[8]=depth==1?2:5;if(depth==1)h[11]=0xffffff;
  void*bits=0;U b=CreateDIBSection(0,h,0,&bits,0,0),old=SelectObject(dc,b),ob=SelectObject(dc,brush),op=SelectObject(dc,pen);check(b&&bits&&old&&ob&&op,18320);
  check(PatBlt(dc,0,0,16,12,0x42),18321);U red=GetNearestColor(dc,0xff),physical=depth<=8?(depth==1?0:1):depth==16?0x7c00:0xff0000;
  check(SetPixel(dc,1,1,0x01000001)==red&&rawpixel(bits,depth,1,1)==physical,18322);
  check(SetPixelV(dc,2,1,0x01000001)&&rawpixel(bits,depth,2,1)==physical,18323);
  check(PatBlt(dc,3,1,2,2,0xf00021)&&rawpixel(bits,depth,3,1)==physical,18324);
  check(MoveToEx(dc,1,4,0)&&LineTo(dc,8,4)&&rawpixel(bits,depth,3,4)==physical,18325);
  check(SelectObject(dc,nullpen)==pen&&Rectangle(dc,9,1,14,6)&&rawpixel(bits,depth,10,2)==physical,18326);SelectObject(dc,pen);
  U entry=0x0100ff00;check(SetPaletteEntries(p,1,1,&entry)==1,18327);U green=depth<=8?(depth==1?0:2):depth==16?0x3e0:0xff00;
  check(PatBlt(dc,3,7,2,2,0xf00021)&&rawpixel(bits,depth,3,7)==green,18328);
  check(SetPixel(dc,2,8,0x01000001)==GetNearestColor(dc,0xff00)&&rawpixel(bits,depth,2,8)==green,18329);
  entry=0x010000ff;check(SetPaletteEntries(p,1,1,&entry)==1,18330);
  U before=rawpixel(bits,depth,7,9),r=SetPixel(dc,7,9,0x01000004);
  /* PC_EXPLICIT entry 4 holds physical index 7. GDI hands an indexed DIB
     FFFF0007, DIBENG passes it through, and GDI returns the caller's
     own COLORREF because the realized color carries the FF tag; the raster masks
     the index to the bitmap depth. Measured on the original binaries. */
  if(depth<=8)check(!before&&r==0x01000004&&rawpixel(bits,depth,7,9)==(depth==1?1u:7u),18331);
  else check(r==GetNearestColor(dc,0xc0c0c0),18332);
  if(depth==32)check(SetPixel(dc,8,9,0x02123456)==0x123456&&rawpixel(bits,depth,8,9)==0x563412,18333);
  check(SelectObject(dc,old)==b&&SelectObject(dc,ob)==brush&&SelectObject(dc,op)==pen&&DeleteObject(b),18334);
 }
 U entries[2]={0x00012345,0x00098765};check(AnimatePalette(p,1,2,entries),18340);check(GetPaletteEntries(p,1,2,v)==2&&v[0]==entries[0]&&v[1]==0xff00,18341);
 check(ResizePalette(p,9)&&GetPaletteEntries(p,6,3,v)==3&&!v[0]&&!v[1]&&!v[2],18342);
 check(SaveDC(dc)==1&&SelectPalette(dc,stock,1)==p&&DeleteObject(p)&&GetPaletteEntries(p,0,0,0)==9,18343);
 check(RestoreDC(dc,-1)&&SelectPalette(dc,stock,1)==p&&DeleteObject(p)&&!GetPaletteEntries(p,0,0,0),18344);
 check(!ResizePalette(stock,21)&&!SetPaletteEntries(stock,0,1,entries)&&DeleteObject(stock),18345);
 check(DeleteObject(brush)&&DeleteObject(pen)&&DeleteDC(dc),18346);
}
#ifdef GDI_PALETTE_STANDALONE
void mainCRTStartup(void){run_gdi_palette_test();ExitProcess(0);}
#endif
