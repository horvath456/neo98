/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U CreatePropertySheetPageA(const void*);API U CreatePropertySheetPageW(const void*);API U CreatePropertySheetPage(const void*);API U DestroyPropertySheetPage(U);API U GetLastError(void);API void SetLastError(U);
static void check(int yes,U code){if(!yes)ExitProcess(code);}
static int same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static int samew(const W*a,const W*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static U ref,adds,releases,nested,wide,outer;
static char title[]="Owned title",name[]="PAGE";static W wtitle[]={'W','i','d','e',0},wname[]={'P','A','G','E',0};
static U __stdcall callback(U hwnd,U reason,U*p){
 check(!hwnd&&p[0]==48,21530);
 if(!reason){++adds;check(ref==1,21531);check(p[7]==0x12345678,21532);p[7]=0xabcdef01;
  // The callback pointer is a real page handle. Same-page destruction during
  // a callback is a guarded native provider boundary; nested other pages work.
  check(!DestroyPropertySheetPage((U)p)&&GetLastError()==6,21533);
  U q[10]={40,0,0,9};nested=CreatePropertySheetPageA(q);check(nested&&DestroyPropertySheetPage(nested),21534);
 }else if(reason==1){++releases;check(ref==1&&p[7]==0xabcdef01,21535);if(wide)check(samew((W*)p[5],wtitle),21536);else check(same((char*)p[5],"Owned title")&&p[5]!=(U)title,21537);}
 return 0; // AddRef and Release callback results do not veto these exports.
}
void run_property_pages_test(void){
 U p[12]={48,0xc8,0x777,17,0,(U)title,0x100500,0x12345678,(U)callback,(U)&ref,0,0};
 for(wide=0;wide<2;++wide){ref=adds=releases=0;p[5]=wide?(U)wtitle:(U)title;SetLastError(0x1234);
  outer=wide?CreatePropertySheetPageW(p):CreatePropertySheetPageA(p);check(outer&&ref==1&&adds==1&&!releases,21500);check(p[7]==0x12345678&&p[5]==(wide?(U)wtitle:(U)title),21501);
  U*public_page=(U*)outer,*core=(U*)public_page[-2];check(core&&core[2]==0x777&&core[3]==17,21502);
  if(!wide){check(public_page!=core&&core[-1]==outer&&public_page[-1]==outer,21503);check(same((char*)public_page[5],"Owned title")&&((W*)core[5])[0]=='O',21504);}
  else check(public_page==core&&!core[-1]&&samew((W*)core[5],wtitle),21505);
  check(DestroyPropertySheetPage(outer)&&ref==0&&releases==1,21506);check(!DestroyPropertySheetPage(outer)&&GetLastError()==6,21507);
 }
 U simple[10]={40,0,1,(U)name};U h=CreatePropertySheetPage(simple);check(h&&same((char*)((U*)h)[3],"PAGE")&&((U*)h)[3]!=(U)name,21508);check(DestroyPropertySheetPage(h),21509);
 simple[1]=1;simple[3]=(U)name;h=CreatePropertySheetPageW(simple);check(h&&((U*)h)[3]==(U)name&&DestroyPropertySheetPage(h),21510);
 simple[0]=39;SetLastError(0x1234);check(!CreatePropertySheetPageA(simple)&&GetLastError()==0x1234,21511);
 simple[0]=40;simple[1]=0x10000;check(!CreatePropertySheetPageW(simple)&&GetLastError()==0x1234,21512);
 simple[1]=0x8000;check(!CreatePropertySheetPageA(simple)&&GetLastError()==50,21513);
 check(!DestroyPropertySheetPage(0)&&GetLastError()==6,21514);
}
#ifdef PROPERTY_PAGES_STANDALONE
void mainCRTStartup(void){run_property_pages_test();ExitProcess(0);}
#endif
