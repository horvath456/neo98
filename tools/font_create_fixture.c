/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);
API U CreateFontA(int,int,int,int,int,U,U,U,U,U,U,U,U,const char*);API U CreateFontIndirectA(const void*);API U CreateFontW(int,int,int,int,int,U,U,U,U,U,U,U,U,const void*);API U CreateFontIndirectW(const void*);
API U CreateBitmap(int,int,U,U,const void*);API U PatBlt(U,int,int,int,int,U);API U TextOutA(U,int,int,const char*,int);API U GetPixel(U,int,int);
API U GetObjectA(U,U,void*);API U DeleteObject(U);API U CreateCompatibleDC(U);API U DeleteDC(U);API U SelectObject(U,U);API U GetStockObject(U);API U SaveDC(U);API U RestoreDC(U,int);API U GetTextFaceA(U,int,char*);API U GetTextMetricsA(U,void*);API U GetTextCharset(U);API U GetCharWidthA(U,U,U,void*);API U EnumFontFamiliesA(U,const char*,void*,U);
static void check(int ok,U code){if(!ok)ExitProcess(code);}static int same(const void*a,const void*b,U n){const unsigned char*x=a,*y=b;for(U i=0;i<n;++i)if(x[i]!=y[i])return 0;return 1;}
static int equal(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static U dc,seen;
static U make(int h,const char*face){return CreateFontA(h,0,0,0,400,0,0,0,0,0,0,0,0,face);}
static U __stdcall enumerate(const void*lf,const void*tm,U type,U param){
 (void)tm;check(type==1&&param==0x13900,13930);unsigned char raw[60],out[60];for(U i=0;i<60;++i)raw[i]=((const unsigned char*)lf)[i];for(U i=4;i<8;++i)raw[i]=0;
 U f=CreateFontIndirectA(raw),old=SelectObject(dc,f);char face[32];check(f&&old&&GetObjectA(f,60,out)==60&&same(raw,out,23)&&GetTextFaceA(dc,32,face)&&equal(face,(const char*)lf+28),13931);check(SelectObject(dc,old)==f&&DeleteObject(f),13932);++seen;return 1;
}
void run_font_create_test(void){
 dc=CreateCompatibleDC(0);check(dc,13900);unsigned char lf[60]={0},out[64];
 // Force the allocator to issue a real generation value above a Win16 WORD.
 U stale=0;for(U i=0;i<260;++i){U f=make(13,"MS Sans Serif");check(f&&DeleteObject(f),13901);stale=f;}
 U f=make(0x10010,"mS sAnS sErIf");check(f>65535&&f!=stale,13902);check(GetObjectA(f,0,0)==60&&GetObjectA(f,999,0)==60,13903);check(GetObjectA(f,60,out)==60&&((int*)out)[0]==16&&equal((char*)out+28,"mS sAnS sErIf"),13904);
 U old=SelectObject(dc,f);char face[32];U tm[14];check(old&&GetTextFaceA(dc,32,face)==13&&equal(face,"MS Sans Serif")&&GetTextMetricsA(dc,tm)&&tm[0]==16,13905);
 check(SaveDC(dc)==1&&SelectObject(dc,old)==f&&DeleteObject(f)==0&&RestoreDC(dc,-1)&&SelectObject(dc,old)==f&&DeleteObject(f),13906);
 SetLastError(0xdeadbeef);check(!GetObjectA(f,60,out)&&GetLastError()==87,13907);
 U direct=CreateFontA(-13,0x10000,0x20000,0x30000,0x10190,0x100,0x200,0x300,0x400,0x503,0x67f,0x700,0x802,"Arial");
 int*n=(int*)lf;n[0]=-13;n[1]=0x10000;n[2]=0x20000;n[3]=0x30000;n[4]=0x10190;lf[23]=0;lf[24]=3;lf[25]=127;lf[27]=2;const char*name="Arial";for(U i=0;name[i];++i)lf[28+i]=name[i];
 U indirect=CreateFontIndirectA(lf);unsigned char other[60];check(direct&&indirect&&GetObjectA(direct,60,out)==60&&GetObjectA(indirect,60,other)==60&&same(out,other,60),13908);check(((int*)out)[0]==-13&&!((int*)out)[1]&&!((int*)out)[2]&&!((int*)out)[3]&&((int*)out)[4]==400&&out[25]==95,13909);
 old=SelectObject(dc,direct);check(old&&GetTextFaceA(dc,32,face)==13&&equal(face,"MS Sans Serif")&&GetTextCharset(dc)==0&&SelectObject(dc,old)==direct&&DeleteObject(direct)&&DeleteObject(indirect),13910);
 for(U i=0;i<60;++i)lf[i]=0;f=CreateFontIndirectA(lf);check(f&&GetObjectA(f,60,out)==60&&out[23]==1&&DeleteObject(f),13911);
 f=CreateFontA(0,0,0,0,0,0,0,0,0,0,0,0,0,0);check(f&&GetObjectA(f,60,out)==60&&out[23]==1&&out[28]==0,13912);for(U i=0;i<64;++i)out[i]=0x77;check(GetObjectA(f,7,out)==7&&out[7]==0x77,13913);SetLastError(0xabc);check(!GetObjectA(f,0,out)&&GetLastError()==87&&DeleteObject(f),13914);
 f=make(15,"FIXEDSYS");old=SelectObject(dc,f);check(f&&old&&GetTextFaceA(dc,32,face)&&equal(face,"Fixedsys"),13915);int widths[2];check(GetCharWidthA(dc,'A','B',widths)&&widths[0]>0&&widths[0]==widths[1]&&SelectObject(dc,old)==f&&DeleteObject(f),13916);
 /* A width is part of the request; the former refusal with error 50
    was this port's, not Win98's. */
 f=CreateFontA(13,1,0,0,400,0,0,0,0,0,0,0,0,"Arial");check(f&&GetObjectA(f,60,out)==60&&out[4]==1&&DeleteObject(f),13917);
 SetLastError(0);check(!CreateFontA(13,0,1,0,400,0,0,0,0,0,0,0,0,"Arial")&&GetLastError()==50,13918);
 // Win98 creates italic, underlined and struck-out fonts; the text
 // metrics carry the three flags and the underline is drawn under the run.
 f=CreateFontA(13,0,0,0,700,1,1,1,0,0,0,0,0,"MS Sans Serif");check(f&&GetObjectA(f,60,out)==60&&out[20]==1&&out[21]==1&&out[22]==1,13919);
 old=SelectObject(dc,f);check(old&&GetTextMetricsA(dc,tm)&&((unsigned char*)tm)[48]==1&&((unsigned char*)tm)[49]==1&&((unsigned char*)tm)[50]==1&&SelectObject(dc,old)==f&&DeleteObject(f),13935);
 {U bitmap=CreateBitmap(64,32,1,32,0),previous=bitmap?SelectObject(dc,bitmap):0,u=CreateFontA(13,0,0,0,400,0,1,0,0,0,0,0,0,"MS Sans Serif");int w[1]={0};
  old=u?SelectObject(dc,u):0;check(previous&&old&&GetTextMetricsA(dc,tm)&&PatBlt(dc,0,0,64,32,0xff0062)&&GetCharWidthA(dc,'i','i',w)&&TextOutA(dc,0,0,"ii",2),13936);
  const int row=(int)tm[1]+1;check(GetPixel(dc,0,row)==0&&GetPixel(dc,2*w[0]-1,row)==0&&GetPixel(dc,2*w[0]+1,row)==0xffffff&&GetPixel(dc,0,row+1)==0xffffff,13937);
  check(SelectObject(dc,old)==u&&DeleteObject(u)&&SelectObject(dc,previous)==bitmap&&DeleteObject(bitmap),13938);}
 SetLastError(0);check(!CreateFontIndirectA(0)&&GetLastError()==87,13920);for(U i=28;i<60;++i)lf[i]='A';check(!CreateFontIndirectA(lf)&&GetLastError()==87,13921);
 SetLastError(0);check(!CreateFontIndirectW((void*)0xffffffff)&&GetLastError()==120,13922);SetLastError(0);check(!CreateFontW(0,0,0,0,0,0,0,0,0,0,0,0,0,(void*)0xffffffff)&&GetLastError()==120,13923);
 U module=GetModuleHandleA("GDI32.DLL");typedef U(__stdcall*Create)(int,int,int,int,int,U,U,U,U,U,U,U,U,const char*);Create ordinal=(Create)GetProcAddress(module,(const char*)149);check(ordinal!=0,13924);f=ordinal(13,0,0,0,400,0,0,0,0,0,0,0,0,"MS Sans Serif");check(f&&DeleteObject(f),13925);
 check(EnumFontFamiliesA(dc,0,(void*)enumerate,0x13900)==1&&seen==2,13933);check(DeleteDC(dc),13934);
}
#ifdef FONT_CREATE_STANDALONE
void mainCRTStartup(void){run_font_create_test();ExitProcess(0);}
#endif
