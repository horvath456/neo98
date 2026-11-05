// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport)
#define CALL __stdcall
API unsigned CALL LocalAlloc(unsigned,unsigned);API unsigned CALL LocalFree(unsigned);API void CALL ExitProcess(unsigned);API unsigned CALL GetModuleHandleA(const char*);API void*CALL GetProcAddress(unsigned,const char*);API const char*CALL GetCommandLineA(void);
API const char*CALL StrChrA(const char*,unsigned short);API const unsigned short*CALL StrChrW(const unsigned short*,unsigned short);
API const char*CALL StrRChrA(const char*,const char*,unsigned short);API const unsigned short*CALL StrRChrW(const unsigned short*,const unsigned short*,unsigned short);
API const char*CALL StrPBrkA(const char*,const char*);API const unsigned short*CALL StrPBrkW(const unsigned short*,const unsigned short*);
API unsigned CALL StrSpnA(const char*,const char*);API unsigned CALL StrSpnW(const unsigned short*,const unsigned short*);API unsigned CALL StrCSpnA(const char*,const char*);API unsigned CALL StrCSpnW(const unsigned short*,const unsigned short*);
static void check(int okay,unsigned code){if(!okay)ExitProcess(code);}static int contains(const char*p,const char*q){for(;*p;++p){const char*a=p,*b=q;while(*b&&*a==*b)++a,++b;if(!*b)return 1;}return 0;}
void mainCRTStartup(void){static const char text[]="abca\\file.exe";static const unsigned short wide[]={'a','b','c','a','\\','f','i','l','e','.','e','x','e',0};static const unsigned short ac[]={'a','c',0},abc[]={'a','b','c',0},reject[]={'\\','.',0},empty[]={0};
 if(contains(GetCommandLineA(),"badend")){StrRChrA(text,text+15,'x');ExitProcess(10120);}
 check(StrChrA(text,'a')==text&&StrChrA(text,'\\')==text+4&&!StrChrA(text,0)&&!StrChrA(text,'z'),10100);
 check(StrChrW(wide,'a')==wide&&StrChrW(wide,'\\')==wide+4&&!StrChrW(wide,0)&&!StrChrW(wide,'z'),10101);
 check(StrRChrA(text,0,'a')==text+3&&StrRChrA(text,text+3,'a')==text&&!StrRChrA(text,text,'a')&&!StrRChrA(text,0,0),10102);
 check(StrRChrW(wide,0,'a')==wide+3&&StrRChrW(wide,wide+3,'a')==wide&&!StrRChrW(wide,wide,'a')&&!StrRChrW(wide,0,0),10103);
 check(StrPBrkA(text,"\\.")==text+4&&!StrPBrkA(text,"z")&&!StrPBrkA(text,""),10104);check(StrPBrkW(wide,reject)==wide+4&&!StrPBrkW(wide,empty),10105);
 check(StrSpnA(text,"abc")==4&&StrSpnA(text,"ac")==1&&!StrSpnA(text,""),10106);check(StrSpnW(wide,abc)==4&&StrSpnW(wide,ac)==1&&!StrSpnW(wide,empty),10107);
 check(StrCSpnA(text,"\\.")==4&&StrCSpnA(text,"")==13&&!StrCSpnA(0,"abc")&&!StrCSpnA(text,0),10108);check(StrCSpnW(wide,reject)==4&&StrCSpnW(wide,empty)==13&&!StrCSpnW(0,abc)&&!StrCSpnW(wide,0),10109);
 static const unsigned short unicode[]={0x20ac,0xd83d,0xde00,0xe4,0};check(StrChrW(unicode,0x20ac)==unicode&&StrChrW(unicode,0xde00)==unicode+2&&StrRChrW(unicode,0,0xe4)==unicode+3,10110);
 unsigned module=GetModuleHandleA("SHLWAPI.DLL");const unsigned ordinals[]={632,635,659,662,657,658,669,670,625,628};const char*names[]={"StrChrA","StrChrW","StrRChrA","StrRChrW","StrPBrkA","StrPBrkW","StrSpnA","StrSpnW","StrCSpnA","StrCSpnW"};
 for(unsigned i=0;i<10;++i){void*named=GetProcAddress(module,names[i]),*ordinal=GetProcAddress(module,(const char*)ordinals[i]);check(named&&named==ordinal,10111);}
 typedef const char*(CALL*Find)(const char*,unsigned short);Find find=(Find)GetProcAddress(module,(const char*)632);check(find(text,'.')==text+9,10112);
 unsigned free_slot=LocalAlloc(0,20);check(free_slot&&!LocalFree(free_slot),10114);
 for(unsigned i=0;i<400;++i)check(StrChrA(text,'f')==text+5&&StrCSpnA(text,".")==9,10113);ExitProcess(0);}
