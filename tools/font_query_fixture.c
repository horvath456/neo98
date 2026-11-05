/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);
API U CreateCompatibleDC(U);API U DeleteDC(U);API U SelectObject(U,U);API U GetStockObject(U);API U SaveDC(U);API U RestoreDC(U,int);API U CreateFontIndirectA(const void*);API U DeleteObject(U);
API U GetTextFaceA(U,U,char*);API U GetCharWidthA(U,U,U,void*);API U GetTextFaceW(U,U,void*);API U GetTextMetricsA(U,void*);API U GetTextMetricsW(U,void*);
API U EnumFontsA(U,const char*,void*,U);API U EnumFontFamiliesA(U,const char*,void*,U);API U EnumFontFamiliesExA(U,const void*,void*,U,U);API U EnumFontsW(U,void*,void*,U);API U EnumFontFamiliesW(U,void*,void*,U);API U EnumFontFamiliesExW(U,void*,void*,U,U);
static U dc,seen,stop,nested,inside;static void check(int ok,U code){if(!ok)ExitProcess(code);}static int equal(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}static U word(const unsigned char*p){return p[0]|(U)p[1]<<8|(U)p[2]<<16|(U)p[3]<<24;}
static U __stdcall inner(const unsigned char*lf,const unsigned char*tm,U type,U param){check(equal((const char*)lf+28,"Fixedsys")&&type==1&&param==0x12345678&&word(tm)>0,11120);++nested;return 42;}
static U __stdcall callback(const unsigned char*lf,const unsigned char*tm,U type,U param){
 check(type==1&&param==0xabcdef01&&((U)tm-(U)lf)==188,11121);check(equal((const char*)lf+28,"MS Sans Serif")||equal((const char*)lf+28,"Fixedsys"),11122);check(equal((const char*)lf+28,(const char*)lf+60)&&word(lf)==word(tm)&&word(tm)>0,11123);
 check(lf[23]==tm[52],11124);for(U i=124;i<188;++i)check(!lf[i],11125);
 unsigned char local[60];for(U i=0;i<60;++i)local[i]=lf[i];for(U i=4;i<16;++i)local[i]=0;
 U f=CreateFontIndirectA(local);check(f,11126);U old=SelectObject(dc,f);char face[32];check(old&&GetTextFaceA(dc,32,face)&&equal(face,(const char*)lf+28),11127);unsigned char actual[56];check(GetTextMetricsA(dc,actual)&&word(actual)==word(tm),11128);check(SelectObject(dc,old)==f&&DeleteObject(f),11129);
 if(!inside){inside=1;check(EnumFontsA(dc,"Fixedsys",(void*)inner,0x12345678)==42,11130);inside=0;check(equal((const char*)lf+28,(const char*)lf+60),11131);}
 ++seen;return stop?0:0x80000007;
}
void run_font_query_test(void){
 dc=CreateCompatibleDC(0);check(dc,11100);char face[40];for(U i=0;i<40;++i)face[i]='!';check(GetTextFaceA(dc,32,face)==13&&equal(face,"MS Sans Serif")&&face[14]=='!',11101);
 face[0]='!';check(!GetTextFaceA(dc,0,face)&&face[0]=='!'&&!GetTextFaceA(dc,1,face)&&face[0]=='!',11102);check(GetTextFaceA(dc,4,face)==3&&equal(face,"MS ")&&face[4]=='a',11103);check(!GetTextFaceA(dc,0,0),11104);
 U widths[4]={0,0,0,0x12345678};check(GetCharWidthA(dc,'A','C',widths)&&widths[0]&&widths[1]&&widths[2]&&widths[3]==0x12345678,11135);U one=0;check(GetCharWidthA(dc,0x10041,0x10041,&one)&&one==widths[0],11136);
 U failed[3]={0x12348001,0x5678ffff,0xabcdef01};check(!GetCharWidthA(0,0,1,failed)&&failed[0]==0xffff8001&&failed[1]==0x1234&&failed[2]==0xabcdef01,11137);check(!GetCharWidthA(dc,0,0,0),11138);
 U saved=SaveDC(dc);check(saved&&SelectObject(dc,GetStockObject(16)),11105);check(GetTextFaceA(dc,32,face)==8&&equal(face,"Fixedsys"),11106);check(RestoreDC(dc,-1)&&GetTextFaceA(dc,0x10020,face)==13&&equal(face,"MS Sans Serif"),11107);
 unsigned char lf[60]={0};const char*requested="Arial";for(U i=0;requested[i];++i)lf[28+i]=requested[i];U mapped=CreateFontIndirectA(lf),old=SelectObject(dc,mapped);check(mapped&&old&&GetTextFaceA(dc,32,face)==13&&equal(face,"MS Sans Serif"),11108);check(SelectObject(dc,old)==mapped&&DeleteObject(mapped),11109);
 seen=nested=0;check(EnumFontsA(dc,0,(void*)callback,0xabcdef01)==0x80000007&&seen==2&&nested==2,11110);
 seen=nested=0;check(EnumFontFamiliesA(dc,"MS Sans Serif",(void*)callback,0xabcdef01)==0x80000007&&seen==6&&nested==6,11111);
 for(U i=0;i<60;++i)lf[i]=0;lf[23]=1;seen=nested=0;check(EnumFontFamiliesExA(dc,lf,(void*)callback,0xabcdef01,0xffffffff)==0x80000007&&seen==2&&nested==2,11112);
 lf[23]=77;seen=0;check(EnumFontFamiliesExA(dc,lf,(void*)callback,0xabcdef01,0)==1&&!seen,11113);check(EnumFontsA(dc,"Uninstalled font",(void*)callback,0xabcdef01)==1&&!seen,11114);
 stop=1;seen=nested=0;check(!EnumFontsA(dc,0,(void*)callback,0xabcdef01)&&seen==1&&nested==1,11115);stop=0;
 SetLastError(99);check(!GetTextFaceW(1,32,(void*)1)&&GetLastError()==120,11116);SetLastError(99);check(!GetTextMetricsW(1,(void*)1)&&GetLastError()==120,11117);SetLastError(99);check(!EnumFontsW(1,(void*)1,(void*)1,1)&&GetLastError()==120,11118);SetLastError(99);check(!EnumFontFamiliesW(1,(void*)1,(void*)1,1)&&GetLastError()==120,11119);SetLastError(99);check(!EnumFontFamiliesExW(1,(void*)1,(void*)1,1,1)&&GetLastError()==120,11132);
 check(DeleteDC(dc),11133);check(!EnumFontsA(dc,0,(void*)callback,0)&&!GetTextFaceA(dc,32,face),11134);
}
#ifdef FONT_QUERY_STANDALONE
void mainCRTStartup(void){run_font_query_test();ExitProcess(0);}
#endif
