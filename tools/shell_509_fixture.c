// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport)
#define C __stdcall
API const char*C GetCommandLineA(void);API unsigned C LocalAlloc(unsigned,unsigned);API unsigned C LocalFree(unsigned);API void C ExitProcess(unsigned);API unsigned C GetModuleHandleA(const char*);API void*C GetProcAddress(unsigned,const char*);API unsigned C GetLastError(void);API void C SetLastError(unsigned);
API unsigned C RegCreateKeyExA(unsigned,const char*,unsigned,char*,unsigned,unsigned,void*,unsigned*,unsigned*);API unsigned C RegSetValueExA(unsigned,const char*,unsigned,unsigned,const void*,unsigned);
API unsigned C O505(unsigned);API unsigned C O506(unsigned,const char*,unsigned*);API unsigned C O507(unsigned,const unsigned short*,unsigned*);API unsigned C O508(unsigned,const char*,void*,unsigned*);API unsigned C O509(unsigned,const char*,unsigned,unsigned*,void*,unsigned*);API unsigned C O510(unsigned,const unsigned short*,void*,unsigned*);API unsigned C O511(unsigned,const unsigned short*,unsigned,unsigned*,void*,unsigned*);API unsigned C SHQueryValueExW(unsigned,const unsigned short*,unsigned,unsigned*,void*,unsigned*);
static void check(int ok,unsigned code){if(!ok)ExitProcess(code);}static void set(unsigned h,const char*name,unsigned type,const void*data,unsigned n){check(!RegSetValueExA(h,name,0,type,data,n),11500);}static unsigned length(const unsigned short*p){unsigned n=0;while(p[n])++n;return n;}
void mainCRTStartup(void){
 unsigned h=0,d=0;check(!RegCreateKeyExA(0x80000001,"Software\\Neo98\\Ordinal509",0,0,0,0x20019,0,&h,&d),11501);
 if(GetCommandLineA()[0]=='2'){unsigned short long_name[261];for(unsigned i=0;i<260;++i)long_name[i]='a';long_name[260]=0;unsigned bytes=0;SHQueryValueExW(h,long_name,0,0,0,&bytes);ExitProcess(11524);}
 unsigned k=0xdeadbeef;check(!O506(0x80000001,"Software\\Neo98\\Ordinal509",&k)&&k!=0xdeadbeef,11502);check(!O505(k),11503);k=0xdeadbeef;check(O506(h,"absent",&k)==2&&!k,11504);
 static const unsigned short text[]={ 't','e','x','t',0 },empty[]={0},path[]={'c','h','i','l','d',0};k=0xdeadbeef;SetLastError(42);check(!O507(h,path,&k)&&k==0xdeadbeef&&GetLastError()==120,11505);
 char buffer[64];unsigned short wide[64];unsigned bytes=0,type=0;buffer[0]='X';check(!O508(h,0,buffer,&bytes)&&bytes==1&&!buffer[0],11506);bytes=0;buffer[0]='X';check(O508(h,"missing",buffer,&bytes)==2&&!bytes&&buffer[0]=='X',11507);
 bytes=0;wide[0]='X';check(!O510(h,empty,wide,&bytes)&&bytes==2&&!wide[0],11508);bytes=64;check(O510(h,path,wide,&bytes)==6,11509);
 set(h,"",1,"default",8);bytes=64;check(!O508(h,"",buffer,&bytes)&&bytes==8&&buffer[0]=='d',11510);bytes=64;check(!O510(h,0,wide,&bytes)&&bytes==16&&length(wide)==7,11511);
 set(h,"text",1,"abc",4);bytes=64;type=0;check(!O509(h,"text",0,&type,buffer,&bytes)&&type==1&&bytes==4&&buffer[2]=='c',11512);
 bytes=64;type=0;check(!O511(h,text,0,&type,wide,&bytes)&&type==1&&bytes==8&&length(wide)==3,11513);
 bytes=4;wide[0]=0xeeee;check(SHQueryValueExW(h,text,0,0,wide,&bytes)==234&&bytes==8&&((char*)wide)[0]=='a'&&((char*)wide)[1]=='b',11514);
 bytes=0;check(!SHQueryValueExW(h,text,0,0,0,&bytes)&&bytes==8,11515);
 unsigned number=0x12345678;set(h,"number",4,&number,4);static const unsigned short nname[]={'n','u','m','b','e','r',0};bytes=4;d=0;check(!O511(h,nname,0,&type,&d,&bytes)&&type==4&&bytes==4&&d==number,11516);
 set(h,"text",2,"abc",4);bytes=64;check(!O511(h,text,0,&type,wide,&bytes)&&type==1&&bytes==8&&length(wide)==3,11517);
 static const unsigned short accented[]={0xe4,0};set(h,"\xe4",1,"\xe4\x80",3);bytes=64;check(!SHQueryValueExW(h,accented,0,&type,wide,&bytes)&&bytes==6&&wide[0]==0xe4&&wide[1]==0x20ac&&!wide[2],11518);
 set(h,"text",7,"a\0b\0",5);bytes=64;wide[0]=wide[1]=wide[2]=wide[3]=0xeeee;check(!O511(h,text,0,&type,wide,&bytes)&&type==7&&bytes==10&&wide[0]=='a'&&!wide[1],11519); // Original lstrcpyW copies only the first MULTI_SZ component.
 unsigned shell=GetModuleHandleA("SHELL32.DLL"),shlw=GetModuleHandleA("SHLWAPI.DLL");for(unsigned o=505;o<=511;++o)check(GetProcAddress(shell,(const char*)o)!=0,11520);check(GetProcAddress(shlw,"SHQueryValueExW")==GetProcAddress(shlw,(const char*)592),11521);
 unsigned reusable=LocalAlloc(0,260);check(reusable&&!LocalFree(reusable),11525);
 for(unsigned i=0;i<150;++i){bytes=64;check(!O511(h,accented,0,0,wide,&bytes)&&wide[1]==0x20ac,11522);}
 unsigned again=LocalAlloc(0,260);check(again==reusable&&!LocalFree(again),11526);
 check(!O505(h)&&O505(h)==6,11523);ExitProcess(0);
}
