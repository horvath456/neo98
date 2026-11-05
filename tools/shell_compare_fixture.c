// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API void SetLastError(U);API U GetLastError(void);API int CompareStringA(U,U,const char*,int,const char*,int);API U GetThreadLocale(void);
API int StrCmpW(const W*,const W*);API int StrCmpIW(const W*,const W*);API int StrCmpNW(const W*,const W*,int);API int StrCmpNIW(const W*,const W*,int);API int StrCmpNA(const char*,const char*,int);API int StrCmpNIA(const char*,const char*,int);API int ChrCmpIA(U,U);API int ChrCmpIW(U,U);
#define CHECK(c,n)do{if(!(c))ExitProcess(42400+(n));}while(0)
void mainCRTStartup(void){W a[]={'A','b','c',0},b[]={'a','B','D',0},empty[]={0},accent[]={0xe4,0},upper[]={0xc4,0};U locale=GetThreadLocale();
 SetLastError(0x123456);CHECK(StrCmpNIA("AbC","aBd",2)==0,0);CHECK(StrCmpNIA("AbC","aBd",3)==-1,1);CHECK(GetLastError()==0x123456,2);
 CHECK(StrCmpNA("abc","abd",3)==CompareStringA(0x400,0,"c",-1,"d",-1)-2,3);
 CHECK(!StrCmpNA("abc","z",0)&&!StrCmpNIA("abc","z",0),4);CHECK(!StrCmpNA("abc","z",-1)&&!StrCmpNIA("abc","z",-1),5);
 CHECK(StrCmpW(a,b)==CompareStringA(locale,0,"Abc",3,"aBD",3)-2,6);CHECK(StrCmpIW(a,b)==CompareStringA(locale,1,"Abc",3,"aBD",3)-2,7);
 CHECK(!StrCmpNIW(a,b,2),8);CHECK(StrCmpNW(a,b,1)==CompareStringA(locale,0,"A",1,"a",1)-2,9);CHECK(!StrCmpNW(a,b,0)&&!StrCmpNIW(a,b,-1),10);
 CHECK(!StrCmpW(empty,empty)&&!StrCmpIW(empty,empty),11);CHECK(ChrCmpIA('a','A')==CompareStringA(0x400,1,"a",-1,"A",-1)-2,12);CHECK(!ChrCmpIW('a','A'),13);
 CHECK(StrCmpIW(accent,upper)==CompareStringA(locale,1,"\xe4",1,"\xc4",1)-2,14);CHECK(ChrCmpIA(0xe4,0xc4)==CompareStringA(0x400,1,"\xe4",-1,"\xc4",-1)-2,15);CHECK(StrCmpNIA("\xe4","\xc4",1)==0,16);
 // Exercise the original conversion heap path beyond its 519-byte buffer.
 W x[601],y[601];for(U i=0;i<600;++i){x[i]='a';y[i]='a';}x[600]=y[600]=0;CHECK(!StrCmpW(x,y),17);y[599]='b';CHECK(StrCmpW(x,y)<0,18);
 CHECK(StrCmpW(0,a)==-2,19);CHECK(StrCmpIW(a,0)==-2,20);
 ExitProcess(0);
}
