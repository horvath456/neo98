// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport)
#define C __stdcall
API void C ExitProcess(unsigned);API unsigned C GetModuleHandleA(const char*);API void* C GetProcAddress(unsigned,const char*);API const char*C GetCommandLineA(void);
API unsigned C Policy(unsigned);API unsigned C ResetPolicy(unsigned,const char*);
API unsigned C RegCreateKeyExA(unsigned,const char*,unsigned,char*,unsigned,unsigned,void*,unsigned*,unsigned*);
API unsigned C RegSetValueExA(unsigned,const char*,unsigned,unsigned,const void*,unsigned);API unsigned C RegDeleteValueA(unsigned,const char*);API unsigned C RegCloseKey(unsigned);
static const char root[]="Software\\Microsoft\\Windows\\CurrentVersion\\Policies";
static void check(int ok,unsigned code){if(!ok)ExitProcess(code);}static unsigned key(unsigned root,const char*path){unsigned h=0,d=0;check(!RegCreateKeyExA(root,path,0,0,0,0x20019,0,&h,&d),10900);return h;}static void set(unsigned h,const char*n,unsigned type,const void*p,unsigned len){check(!RegSetValueExA(h,n,0,type,p,len),10901);}static void number(unsigned h,const char*n,unsigned value){set(h,n,4,&value,4);}
void mainCRTStartup(void){
 unsigned module=GetModuleHandleA("SHELL32.DLL");check(module&&GetProcAddress(module,(const char*)100)&&GetProcAddress(module,(const char*)244),10902);check(!GetProcAddress(module,"SHRestricted")&&!GetProcAddress(module,"SHInitRestricted"),10903);
 unsigned user=key(0x80000001,"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer"),machine=key(0x80000002,"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer");
 if(GetCommandLineA()[0]=='3'){set(user,"NoRun",2,"abcd",4);ResetPolicy(0,0);Policy(1);ExitProcess(10924);}
 if(GetCommandLineA()[0]=='2'){check(Policy(1)==77,10904);check(ResetPolicy(0,root)==~0u&&Policy(1)==88,10905);RegCloseKey(user);RegCloseKey(machine);ExitProcess(0);}
 check(ResetPolicy(0xdeadbeef,0)==~0u&&!Policy(1)&&!Policy(3),10906);
 number(user,"NoRun",23);check(Policy(1)==23,10907);number(user,"NoRun",24);check(Policy(1)==23,10908);check(ResetPolicy(42,"Explorer")!=0&&Policy(1)==23,10909);
 check(ResetPolicy(0,"software\\MICROSOFT\\windows\\currentversion\\policies")==~0u&&Policy(1)==24,10910);
 number(machine,"NoRun",0);ResetPolicy(0,0);check(!Policy(1),10911);number(machine,"NoRun",~0u);ResetPolicy(0,0);check(Policy(1)==24,10912);
 check(!RegDeleteValueA(user,"NoRun"),10913);ResetPolicy(0,0);check(Policy(1)==~0u,10914);number(user,"NoRun",31);check(Policy(1)==31,10915);
 check(!RegDeleteValueA(machine,"NoRun"),10916);unsigned char byte=0xaa;set(user,"NoClose",3,&byte,1);check(Policy(2)==0xffffffaa,10917);
 set(user,"NoSaveSettings",1,"x",2);check(Policy(4)==0xff000078,10918);set(user,"NoFileMenu",2,"7",2);check(Policy(8)==0xffff0037,10919);
 char large[8]={1,2,3,4,5,6,7,8};set(user,"NoSetFolders",3,large,8);check(!Policy(16),10920);number(user,"NoSetFolders",42);check(Policy(16)==42,10921);
 unsigned old=key(0x80000001,"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\WinOldApp"),active=key(0x80000001,"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\ActiveDesktop");number(old,"NoRealMode",5);number(active,"NoHTMLWallPaper",6);check(Policy(0x80000)==5&&Policy(0x40000010)==6,10922);RegCloseKey(old);RegCloseKey(active);
 number(user,"NoRun",77);ResetPolicy(0,0);check(Policy(1)==77,10923);number(user,"NoRun",88);RegCloseKey(user);RegCloseKey(machine);ExitProcess(0);
}
