/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
typedef struct {U size;int width,height;unsigned short planes,depth;U compression,bytes;int xppm,yppm;U colors,important;} Header;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateCompatibleDC(U); API U CreateCompatibleBitmap(U,int,int); API U CreateDiscardableBitmap(U,int,int);
API U CreateDIBSection(U,const void*,U,void**,U,U); API U DeleteObject(U); API U DeleteDC(U); API U SelectObject(U,U);
API U GetObjectA(U,int,void*); API U GetPixel(U,int,int); API U SaveDC(U); API U RestoreDC(U,int);
API U GetStretchBltMode(U); API U SetStretchBltMode(U,U); API U SetLastError(U); API U GetLastError(void);
API U StretchBlt(U,int,int,int,int,U,int,int,int,int,U);
API U StretchDIBits(U,int,int,int,int,int,int,int,int,const void*,const void*,U,U);
static void check(int value,U code){if(!value)ExitProcess(code);}
void run_dib_stretch_test(void){
 Header h={40,2,-2,1,32,0,0,0,0,0,0};U *source=0,*destination=0;
 U sb=CreateDIBSection(0,&h,0,(void**)&source,0,0);h.width=5;h.height=-5;
 U db=CreateDIBSection(0,&h,0,(void**)&destination,0,0);check(sb&&db&&source&&destination,8800);
 source[0]=0x00124ab7;source[1]=0x00e7812a;source[2]=0x00483f12;source[3]=0x006a7c91;
 U sd=CreateCompatibleDC(0),dd=CreateCompatibleDC(0),so=SelectObject(sd,sb),old=SelectObject(dd,db);check(sd&&dd&&so&&old,8801);
 check(GetStretchBltMode(dd)==1&&SaveDC(dd)==1,8802);check(SetStretchBltMode(dd,4)==1&&GetStretchBltMode(dd)==4,8803);
 check(!SetStretchBltMode(dd,5)&&GetStretchBltMode(dd)==4,8804);
 check(StretchBlt(dd,0,0,5,5,sd,0,0,2,2,0xcc0020),8805);
 const U xm[]={0,0,0,1,1},ym[]={0,0,1,1,1};
 for(U y=0;y<5;++y)for(U x=0;x<5;++x)check(destination[y*5+x]==source[ym[y]*2+xm[x]],8806);
 check(GetPixel(dd,0,0)==0xb74a12&&GetPixel(dd,4,4)==0x917c6a,8807);
 check(RestoreDC(dd,-1)&&GetStretchBltMode(dd)==1,8808);
 check(StretchBlt(dd,1,1,-2,-2,sd,0,0,2,2,0xcc0020),8809);check(destination[0]==source[3]&&destination[1]==source[2]&&destination[5]==source[1]&&destination[6]==source[0],8810);
 h.width=2;h.height=-2;check(StretchDIBits(dd,0,0,5,5,0,0,2,2,source,&h,0,0xcc0020)==2,8811);
 for(U y=0;y<5;++y)for(U x=0;x<5;++x)check(destination[y*5+x]==source[ym[y]*2+xm[x]],8812);
 /* a stretch between different formats, and one with another
    raster operation, samples the same pixels as the same-format stretch. */
 {
  struct {Header h;U colors[4];} p8={{40,2,-2,1,8,0,0,0,0,4,0},{0x00102030,0x00405060,0x00708090,0x00a0b0c0}};
  static const unsigned char bits8[8]={0,1,0,0,2,3,0,0};
  check(StretchDIBits(dd,0,0,5,5,0,0,2,2,bits8,&p8,0,0xcc0020)==2,8830);
  for(U y=0;y<5;++y)for(U x=0;x<5;++x)check(destination[y*5+x]==p8.colors[ym[y]*2+xm[x]],8831);
  /* BI_RLE8, bottom-up: the bottom row is 0 1, the top row 2 3. */
  static const unsigned char rle[12]={1,0,1,1,0,0,1,2,1,3,0,1};
  p8.h.height=2;p8.h.compression=1;p8.h.bytes=sizeof rle;
  for(U i=0;i<25;++i)destination[i]=0;
  check(StretchDIBits(dd,0,0,5,5,0,0,2,2,rle,&p8,0,0xcc0020)==2,8832);
  for(U y=0;y<5;++y)for(U x=0;x<5;++x)check(destination[y*5+x]==p8.colors[(ym[y]?0:2)+xm[x]],8833);
  /* SRCINVERT while stretching, 32 into 32: the old pixel XOR the sample. */
  for(U i=0;i<25;++i)destination[i]=0x00ffffff;
  check(StretchDIBits(dd,0,0,5,5,0,0,2,2,source,&h,0,0x660046)==2,8834);
  for(U y=0;y<5;++y)for(U x=0;x<5;++x)check(destination[y*5+x]==(0x00ffffff^source[ym[y]*2+xm[x]]),8835);
 }
 U info[21]={0};for(U i=0;i<40;++i){U clone=CreateCompatibleBitmap(dd,7,3);check(clone&&GetObjectA(clone,84,info)==84&&info[1]==7&&info[2]==3&&info[5],8813);check(DeleteObject(clone),8814);}
 U mono=CreateCompatibleDC(0),bitmap=CreateDiscardableBitmap(mono,7,3);check(mono&&bitmap&&GetObjectA(bitmap,24,info)==24&&info[1]==7&&info[2]==3&&(info[4]>>16)==1,8815);check(DeleteObject(bitmap)&&DeleteDC(mono),8816);
 /* StretchBlt with another raster operation between two direct-colour
    bitmaps samples the same pixels and applies the operation, as StretchDIBits
    does above (it used to be refused with error 50). */
 for(U i=0;i<25;++i)destination[i]=0x00ffffff;
 check(StretchBlt(dd,0,0,5,5,sd,0,0,2,2,0x660046),8817);
 for(U y=0;y<5;++y)for(U x=0;x<5;++x)check(destination[y*5+x]==(0x00ffffff^source[ym[y]*2+xm[x]]),8836);
 check(SelectObject(sd,so)==sb&&SelectObject(dd,old)==db,8818);check(DeleteObject(sb)&&DeleteObject(db)&&DeleteDC(sd)&&DeleteDC(dd),8819);
}
#ifdef DIB_STRETCH_STANDALONE
void mainCRTStartup(void){run_dib_stretch_test();ExitProcess(0);}
#endif
