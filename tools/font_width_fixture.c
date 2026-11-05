/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);
API U CreateCompatibleDC(U);API U DeleteDC(U);API U SaveDC(U);API U RestoreDC(U,int);API U SelectObject(U,U);API U GetStockObject(U);API U CreateFontIndirectA(const void*);API U DeleteObject(U);
API U GetCharWidthA(U,U,U,void*);API U GetCharWidthW(U,U,U,void*);API U GetCharABCWidthsA(U,U,U,void*);
API U GetCharABCWidthsFloatA(U,U,U,void*);API U GetCharABCWidthsFloatW(U,U,U,void*);API U GetCharABCWidthsW(U,U,U,void*);API U GetCharWidth32W(U,U,U,void*);API U GetCharWidthFloatA(U,U,U,void*);API U GetCharWidthFloatW(U,U,U,void*);
API U EnumFontFamiliesA(U,const char*,void*,U);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);
static void check(int v,U n){if(!v)ExitProcess(n);}static U dc,seen;
static void widths(void){
 U narrow[96],wide[97];wide[96]=0xabcdef01;check(GetCharWidthA(dc,32,127,narrow)&&GetCharWidthW(dc,32,127,wide),11700);for(U i=0;i<96;++i)check(narrow[i]==wide[i],11701);check(wide[96]==0xabcdef01,11702);
 const U chars[]={0x20ac,0x2219,0xe4,0x2014,0x00b7,0xd800,0x0080},bytes[]={0x80,0xb7,0xe4,0x97,0x1f,0x1f,0x1f};
 for(U i=0;i<7;++i){U a=0,w=0;check(GetCharWidthA(dc,bytes[i],bytes[i],&a)&&GetCharWidthW(dc,chars[i],chars[i],&w)&&a==w,11703+i);}
 U one=0;check(GetCharWidthW(dc,0x10041,0x10041,&one)&&one==narrow[33],11710);
}
static U __stdcall font(const unsigned char*lf,const void*tm,U type,U param){
 (void)tm;check(type==1&&param==0x1234,11711);unsigned char local[60];for(U i=0;i<60;++i)local[i]=lf[i];for(U i=4;i<16;++i)local[i]=0;
 U f=CreateFontIndirectA(local),old=SelectObject(dc,f);check(f&&old,11712);widths();check(SelectObject(dc,old)==f&&DeleteObject(f),11713);++seen;return 7;
}
void run_font_width_test(void){
 dc=CreateCompatibleDC(0);check(dc,11714);widths();check(SaveDC(dc)&&SelectObject(dc,GetStockObject(16)),11715);widths();check(RestoreDC(dc,-1),11716);widths();
 check(EnumFontFamiliesA(dc,"MS Sans Serif",(void*)font,0x1234)==7&&seen==6,11717);
 U packed[7]={0xffff8001,0x0002fffe,0x80008000,0xabcdef01,0xabcdef02,0xabcdef03,0x12345678};
 SetLastError(99);check(!GetCharABCWidthsA(dc,32,33,packed)&&GetLastError()==99,11718);
 const U abc[]={0xffff8001,65535,0xfffffffe,2,32768,0xffff8000,0x12345678};for(U i=0;i<7;++i)check(packed[i]==abc[i],11719);
 U failed[3]={0x12348001,0x5678ffff,0xabcdef01};check(!GetCharWidthW(0,0,1,failed)&&failed[0]==0xffff8001&&failed[1]==0x1234&&failed[2]==0xabcdef01,11720);
 check(!GetCharWidthW(dc,32,32,0),11721);U sentinel=0x12345678;SetLastError(99);check(!GetCharWidthW(dc,0,65535,&sentinel)&&GetLastError()==87&&sentinel==0x12345678,11722);
 typedef U(__stdcall *Fn)(U,U,U,void*);Fn failures[]={GetCharABCWidthsFloatA,GetCharABCWidthsFloatW,GetCharABCWidthsW,GetCharWidth32W,GetCharWidthFloatA,GetCharWidthFloatW};
 for(U i=0;i<6;++i){SetLastError(99);check(!failures[i](0xffffffff,0xffffffff,0xffffffff,(void*)1)&&GetLastError()==120,11723+i);}
 U module=GetModuleHandleA("GDI32.DLL");Fn ordinal=(Fn)GetProcAddress(module,(const char*)238);check(ordinal&&ordinal(dc,65,65,&sentinel)&&sentinel,11729);
 ordinal=(Fn)GetProcAddress(module,(const char*)230);SetLastError(99);check(ordinal&&!ordinal(1,2,3,(void*)1)&&GetLastError()==120,11730);
 check(DeleteDC(dc),11731);
}
#ifdef FONT_WIDTH_STANDALONE
void mainCRTStartup(void){run_font_width_test();ExitProcess(0);}
#endif
