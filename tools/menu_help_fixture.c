// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport)
#define CALL __stdcall
API void CALL ExitProcess(unsigned);API unsigned CALL GetModuleHandleA(const char*);
API unsigned CALL CreateWindowExA(unsigned,const char*,const char*,unsigned,int,int,int,int,unsigned,unsigned,unsigned,void*);
API unsigned CALL SendMessageA(unsigned,unsigned,unsigned,void*);API int CALL DestroyWindow(unsigned);
API unsigned CALL CreateMenu(void);API unsigned CALL AppendMenuA(unsigned,unsigned,unsigned,const char*);API unsigned CALL DestroyMenu(unsigned);
API void CALL MenuHelp(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,const unsigned*);
static void check(int okay,unsigned code){if(!okay)ExitProcess(code);}
void mainCRTStartup(void){
 unsigned module=GetModuleHandleA(0),owner=CreateWindowExA(0,"STATIC","Menu help",0x10cf0000,20,20,300,200,0,0,module,0);check(owner,9350);
 unsigned status=CreateWindowExA(0,"msctls_statusbar32","",0x50000000,0,0,300,20,owner,9,module,0);check(status,9351);
 unsigned short wide[]={0x20ac,0xe4,0};char text[256]={0};check(SendMessageA(status,0x40b,255,wide),9352);check((SendMessageA(status,0x402,255,text)&65535)==2&&(unsigned char)text[0]==0x80&&(unsigned char)text[1]==0xe4,9353);
 MenuHelp(0x400,0,0,0,module,status,0);check((SendMessageA(status,0x402,255,text)&65535)==2,9354);
 unsigned short longtext[257];for(unsigned i=0;i<256;++i)longtext[i]='A';longtext[256]=0;check(!SendMessageA(status,0x40b,255,longtext),9355);check((SendMessageA(status,0x402,255,text)&65535)==2,9356);
 longtext[255]=0;check(SendMessageA(status,0x40b,255,longtext),9357);check((SendMessageA(status,0x402,255,text)&65535)==255,9358);
 unsigned menu=CreateMenu();check(menu&&AppendMenuA(menu,0,7,"Help"),9359);unsigned ids[]={0,0,0,0};MenuHelp(0x11f,0x08000000,menu,menu,module,status,ids);check(SendMessageA(status,0x40e,0,0)==1,9360);check(!(SendMessageA(status,0x402,255,text)&65535),9361);
 MenuHelp(0x11f,0xffff0000,0,menu,module,status,ids);check(!SendMessageA(status,0x40e,0,0),9362);check(DestroyMenu(menu)&&DestroyWindow(owner),9363);ExitProcess(0);
}
