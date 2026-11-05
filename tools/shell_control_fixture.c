/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetModuleHandleA(const char*);API U LoadLibraryA(const char*);API U GetProcAddress(U,const char*);API U FreeLibrary(U);
API void Control_RunDLL(U,U,char*,U);API void Control_RunDLLA(U,U,char*,U);
typedef struct{U style,proc,cx,wx,instance,icon,cursor,brush;const char*menu,*name;} CLASS;
API U RegisterClassA(CLASS*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);
static void check(U c,U code){if(!c)ExitProcess(code);}static int same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static U owner,module,notifies,starts,clicks,nested;
typedef U(__stdcall*QUERY)(U,U);static QUERY query;
static U __stdcall proc(U h,U m,U w,U l){
 if(m==0x4e){U*n=(U*)l;check(n[0]==h&&n[2]==(U)-500,19900);check(same((char*)n[4],"Date")||same((char*)n[4],"Clock"),19901);++notifies;return 0;}
 if(m==0x8001){++starts;if(same((char*)l,"nested")&&!nested){check(w==1,19902);nested=1;char inner[]="FIXTURE.CPL,@0,handled";Control_RunDLLA(h,0,inner,1);check(query(1,1)==1,19903);}return 0;}
 if(m==0x8002){++clicks;check(l==0xa000+w,19904);return 0;}
 return DefWindowProcA(h,m,w,l);
}
void mainCRTStartup(void){U instance=GetModuleHandleA(0);CLASS c={0,(U)proc,0,0,instance,0,0,6,0,"CPL_OWNER_TEST"};check(RegisterClassA(&c),19905);owner=CreateWindowExA(0,c.name,"Native CPL lifecycle",0x10cf0000,80,60,320,180,0,0,instance,0);check(owner,19906);
 module=LoadLibraryA("FIXTURE.CPL");check(module,19907);query=(QUERY)GetProcAddress(module,"FixtureQuery");check((U)query,19908);check(query(2,0)==1,19909);
 char first[]="FIXTURE.CPL,@1,nested";Control_RunDLL(owner,0,first,1);check(same(first,"FIXTURE.CPL,@1,nested"),19910);check(starts==2&&clicks==1&&notifies==2,19911);check(query(1,1)==1&&query(1,2)==1&&query(1,3)==2&&query(1,8)==2&&query(1,6)==2&&query(1,7)==1,19912);check(query(3,0)==0,19913);
 query(0,0);starts=clicks=notifies=0;char named[]="\"FIXTURE.CPL\",Clock,handled";Control_RunDLLA(owner,0,named,1);check(starts==1&&!clicks&&notifies==1&&query(1,7)==1,19914);
 query(0,0);starts=clicks=0;char empty[]="FIXTURE.CPL,,/m";Control_RunDLL(owner,0,empty,1);
 // Original CPLName/CPLFlags persist on this still-live owner. Empty selection
 // activates that owner even though its per-file applet property was removed.
 check(!starts&&!clicks&&!query(1,1)&&!query(1,6),19915);
 check(DestroyWindow(owner),19921);owner=CreateWindowExA(0,c.name,"Fresh default applet owner",0x10cf0000,80,60,320,180,0,0,instance,0);check(owner,19922);
 Control_RunDLL(owner,0,empty,1);check(starts==1&&clicks==1&&query(1,6)==2,19923);
 query(0,0);starts=clicks=0;char invalid[]="FIXTURE.CPL,@77,ignored";Control_RunDLLA(owner,0,invalid,1);check(!starts&&!clicks&&query(1,6)==2&&query(1,7)==1,19916);
 query(0,1);char failed[]="FIXTURE.CPL,@0,ignored";Control_RunDLLA(owner,0,failed,1);check(query(1,1)==1&&!query(1,2)&&!query(1,7),19917);check(query(3,0)==0,19918);
 check(FreeLibrary(module),19919);check(DestroyWindow(owner),19920);ExitProcess(0);
}
void*memset(void*p,int c,unsigned n){unsigned char*d=p;while(n--)*d++=c;return p;}
