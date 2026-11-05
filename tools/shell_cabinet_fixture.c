/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API char*GetCommandLineA(void);API void Sleep(U);API U GetLastError(void);
API U O654(void*,U);API U O651(void*,U);API U O652(void*);API U O244(U,U);API void SHGetSettings(U*,U);
API U RegCreateKeyExA(U,const char*,U,char*,U,U,void*,U*,U*);API U RegSetValueExA(U,const char*,U,U,const void*,U);API U RegQueryValueExA(U,const char*,U,U*,void*,U*);API U RegCloseKey(U);
typedef struct{W size,version;U flags,attributes;} RECORD;
static const char*explorer="Software\\Microsoft\\Windows\\CurrentVersion\\Explorer";
static void check(U c,U code){if(!c)ExitProcess(code);}static U key(const char*p){U h=0,d=0;check(!RegCreateKeyExA(0x80000001,p,0,0,0,0xf003f,0,&h,&d),22300);return h;}
static void set(U h,const char*n,U type,const void*p,U bytes){check(!RegSetValueExA(h,n,0,type,p,bytes),22301);}static U get(U h,const char*n){U value=0,bytes=4;return RegQueryValueExA(h,n,0,0,&value,&bytes)?0:value;}
static void phase(U h,U v){set(h,"CabinetFixturePhase",4,&v,4);}
void mainCRTStartup(void){U e=key(explorer),settings[7]={28,0x80,0,0,0,1,10},bits=0;RECORD out={0};char*command=GetCommandLineA();
 if(command[0]=='B'){
  while(get(e,"CabinetFixturePhase")!=1)Sleep(0);
  SHGetSettings(&bits,0x400);check(!bits,22302);check(!O654(&out,12)&&!(out.flags&0x20),22303);phase(e,2);RegCloseKey(e);ExitProcess(0);
 }
 set(e,"ShellState",3,settings,28);SHGetSettings(&bits,0x400);check(bits==0x80,22304);check(!O654(&out,12)&&out.size==12&&out.version==1&&out.flags==0x12a&&out.attributes==0x60,22305);
 settings[1]=0;set(e,"ShellState",3,settings,28);phase(e,1);while(get(e,"CabinetFixturePhase")!=2)Sleep(0);
 check(!O651(&out,0xffffffff)&&out.flags==0x12a,22306);SHGetSettings(&bits,0x400);check(!bits,22307);check(!O654(&out,12)&&out.flags==0x10a,22308);
 U c=key("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CabinetState");RECORD input={12,1,0x153,0x99};check(O652(&input),22309);check(get(c,"FullPath")==1,22310);check(O654(&out,12)&&out.flags==input.flags&&out.attributes==0x99,22311);
 U full=0;set(c,"FullPath",4,&full,4);check(O654(&out,12)&&out.flags==0x152,22312);
 input.size=6;input.flags=1;check(O652(&input),22313);check(O654(&out,12)&&out.size==12&&out.flags==1&&out.attributes==0x99,22314);
 input=(RECORD){12,0,0xffffffff,0x77};set(c,"Settings",3,&input,12);check(O654(&out,12)&&out.version==1&&out.flags==0xffff01df&&out.attributes==0x60,22315);
 U bytes=12;RECORD stored={0};check(!RegQueryValueExA(c,"Settings",0,0,&stored,&bytes)&&stored.version==1&&stored.size==12,22316);
 unsigned char shortout[16];for(U i=0;i<16;++i)shortout[i]=0xa5;check(O654(shortout,5),22317);check(shortout[0]==5&&shortout[1]==0&&shortout[5]==0xa5,22318);
 check(!O654(shortout,13)&&GetLastError()==87&&shortout[5]==0xa5,22319);check(!O654(0,12)&&!O654(shortout,0),22320);
 U p=key("Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer"),one=1;set(p,"ClassicShell",4,&one,4);O244(0,0);SHGetSettings(&bits,0x400);check(bits==0x80,22321);one=0;set(p,"ClassicShell",4,&one,4);O244(0,0);
 RegCloseKey(p);RegCloseKey(c);RegCloseKey(e);ExitProcess(0);
}
void*memset(void*p,int c,U n){unsigned char*d=p;while(n--)*d++=c;return p;}
