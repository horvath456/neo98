/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);
API U CreatePalette(void*);API U GetPaletteEntries(U,U,U,void*);API U SetPaletteEntries(U,U,U,void*);API U SelectPalette(U,U,U);
API U CreateCompatibleDC(U);API U DeleteDC(U);API U DeleteObject(U);API U SelectObject(U,U);API U CreateDIBSection(U,const void*,U,void**,U,U);API U GetDIBColorTable(U,U,U,void*);API U SetDIBits(U,U,U,U,const void*,const void*,U);API U StretchDIBits(U,int,int,int,int,int,int,int,int,const void*,const void*,U,U);API U CreateSolidBrush(U);API U CreatePen(U,int,U);API U PatBlt(U,int,int,int,int,U);API U MoveToEx(U,int,int,void*);API U LineTo(U,int,int);API U SetPixel(U,int,int,U);API U SetPixelV(U,int,int,U);API U GetPixel(U,int,int);
static void check(int v,U code){if(!v)ExitProcess(code);}
static U swap(U c){return((c&255)<<16)|(c&0xff00)|((c>>16)&255);}
static U pix(const unsigned char*p,U depth,U x,U y){p+=y*((16*depth+31)/32)*4;if(depth==1)return(p[x/8]>>(7-x%8))&1;if(depth==4)return(p[x/2]>>((1-x%2)*4))&15;return p[x];}
void run_dib_palette_test(void){
 U log[5]={0x00040300,0x00123456,0xff,0x02000005,0xff0000},dc=CreateCompatibleDC(0),p=CreatePalette(log),oldpal=SelectPalette(dc,p,0);check(dc&&p&&oldpal,19500);
 U table[16]={0},source[14]={40,16,(U)-4,0x00080001,0,0,0,0,4,0,0x00020003,0x00000063};
 void*bits=0;U bitmap=CreateDIBSection(dc,source,1,&bits,0,0),old=SelectObject(dc,bitmap);check(bitmap&&bits&&old,19501);
 check(GetDIBColorTable(dc,0,4,table)==4&&table[0]==0xff&&table[1]==0x800080&&!table[2]&&table[3]==0x563412,19502);
 U color=0xffffff;check(SetPaletteEntries(p,3,1,&color)==1&&GetDIBColorTable(dc,0,4,table)==4&&table[0]==0xff,19503);color=0xff0000;SetPaletteEntries(p,3,1,&color);
 SelectObject(dc,old);DeleteObject(bitmap);
 // Core-header WORD palette indices share the existing original header expander.
 unsigned short core[10]={12,0,16,4,1,1,1,0};bitmap=CreateDIBSection(dc,core,1,&bits,0,0);check(bitmap&&SelectObject(dc,bitmap)==old,19504);check(GetDIBColorTable(dc,0,2,table)==2&&table[0]==0xff0000&&table[1]==0x563412,19505);SelectObject(dc,old);DeleteObject(bitmap);
 // A null construction DC skips the original palette resolver and leaves black.
 bitmap=CreateDIBSection(0,source,1,&bits,0,0);check(bitmap&&SelectObject(dc,bitmap)==old&&GetDIBColorTable(dc,0,4,table)==4&&!table[0]&&!table[1]&&!table[2]&&!table[3],19506);SelectObject(dc,old);DeleteObject(bitmap);
 // Source transfer uses the original distinct index-zero fallback and PC_EXPLICIT loop.
 U target[10]={40,16,(U)-4,0x00200001};bitmap=CreateDIBSection(0,target,0,&bits,0,0);check(bitmap&&SelectObject(dc,bitmap)==old,19507);
 unsigned char indices[64];for(U i=0;i<64;++i)indices[i]=i%4;
 check(SetDIBits(dc,bitmap,0,4,indices,source,1)==4,19508);
 U*raw=bits;check((raw[0]&0xffffff)==0xff&&(raw[1]&0xffffff)==0x800000&&(raw[2]&0xffffff)==0x563412&&(raw[3]&0xffffff)==0x563412,19509);
 check(StretchDIBits(dc,0,0,16,4,0,0,16,4,indices,source,1,0xcc0020)==4&&(raw[1]&0xffffff)==0x800000,19510);
 SelectObject(dc,old);DeleteObject(bitmap);
 // Direct physical-index writing is independent of nearest RGB matching.
 for(U n=0;n<3;++n){U depth=n==0?1:n==1?4:8,header[14]={40,16,(U)-4,(depth<<16)|1,0,0,0,0,2,0,0,0xffffff};
  bitmap=CreateDIBSection(0,header,0,&bits,0,0);check(bitmap&&SelectObject(dc,bitmap)==old,19520);
  color=0x02000001;check(SetPaletteEntries(p,2,1,&color)==1,19521);U brush=CreateSolidBrush(0x01000002),pen=CreatePen(0,1,0x01000002),ob=SelectObject(dc,brush),op=SelectObject(dc,pen);check(brush&&pen&&ob&&op,19522);
  check(SetPixelV(dc,1,0,0x01000002)&&pix(bits,depth,1,0)==1&&GetPixel(dc,1,0)==0xffffff,19523);
  check(SetPixel(dc,2,0,0x01000002)!=~0u&&pix(bits,depth,2,0)==1,19524);
  check(PatBlt(dc,3,1,2,2,0xf00021)&&pix(bits,depth,3,1)==1,19525);
  check(MoveToEx(dc,7,2,0)&&LineTo(dc,11,2)&&pix(bits,depth,8,2)==1,19526);
  SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(brush);DeleteObject(pen);SelectObject(dc,old);DeleteObject(bitmap);
 }
 check(SelectPalette(dc,oldpal,0)==p&&DeleteObject(p)&&DeleteDC(dc),19530);
}
#ifdef DIB_PALETTE_STANDALONE
void mainCRTStartup(void){run_dib_palette_test();ExitProcess(0);}
#endif
