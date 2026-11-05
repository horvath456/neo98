// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport) __stdcall
unsigned API CompareStringA(unsigned,unsigned,const char*,int,const char*,int);
unsigned API CompareStringW(unsigned,unsigned,const unsigned short*,int,const unsigned short*,int);
unsigned API GetLastError(void);
void API SetLastError(unsigned);
void API ExitProcess(unsigned);
API void* GetModuleHandleA(const char*);
API void* GetProcAddress(void*,const char*);
static void check(int okay,unsigned code){if(!okay)ExitProcess(code);}
unsigned run_compare_string(void){
 const struct {const char*a,*b;unsigned flags,result;}cases[]={
  {"","",0,2},{"","a",0,1},{"a","",0,3},{"a","A",0,1},{"a","A",1,2},
  {"a-b","ab",0,3},{"ab","a-b",0,1},{"a-bc","ab-c",0,1},
  {"\xe9","e",0,3},{"\xe9""a","eb",0,1},{"\xc6","AE",0,2},{"\xdf","ss",0,2},
  {"\x9c","oe",1,2},{"\xc9","\xe9",1,2},{"\xe6","Ae",1,2},
  {"(GMT+01:00) Amsterdam, Berlin, Bern, Rome, Stockholm, Vienna","(GMT+01:00) Brussels, Copenhagen, Madrid, Paris",0,1},
  {"(GMT) Casablanca, Monrovia","(GMT) Greenwich Mean Time: Dublin, Edinburgh, Lisbon, London",0,1}
 };
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i){SetLastError(0x24681357);check(CompareStringA(0x409,cases[i].flags,cases[i].a,-1,cases[i].b,-1)==cases[i].result,32000+i);check(GetLastError()==0x24681357,32050+i);}
 const unsigned aliases[]={0,0x400,0x800,9};
 for(unsigned i=0;i<4;++i)check(CompareStringA(aliases[i],0,"a",-1,"b",-1)==1,32100+i);
 check(CompareStringA(0x409,0x40000001,"a",-99,"A",-2)==2,32110);
 check(!CompareStringA(0x409,0x80000000,"a",-1,"b",-1)&&GetLastError()==1004,32111);
 check(!CompareStringA(0xf0000409,0,"a",-1,"b",-1)&&GetLastError()==87,32112);
 check(!CompareStringA(0x409,0,0,-1,"b",-1)&&GetLastError()==87,32113);
 check(!CompareStringW(0x409,0,0,0,0,0)&&GetLastError()==120,32114);
 typedef unsigned(__stdcall*Fn)(unsigned,unsigned,const char*,int,const char*,int);
 Fn ordinal=(Fn)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"),(const char*)166);
 check(ordinal&&ordinal(0x409,1,"hello",-1,"HELLO",-1)==2,32115);
 return 0;
}
#ifdef COMPARE_STRING_STANDALONE
void mainCRTStartup(void){ExitProcess(run_compare_string());}
#endif
