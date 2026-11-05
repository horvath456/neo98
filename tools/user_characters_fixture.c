/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U IsCharLowerA(U);API U IsCharUpperA(U);API U IsCharAlphaA(U);API U IsCharAlphaNumericA(U);
API U IsCharAlphaW(U);API char *CharNextA(const char*);API char *CharPrevA(const char*,const char*);
API char *CharNextExA(U,const char*,U);API char *CharPrevExA(U,const char*,const char*,U);
API U GetStringTypeA(U,U,const char*,int,unsigned short*);API void ExitProcess(U);
API void SetLastError(U);API U GetLastError(void);API void *VirtualAlloc(void*,U,U,U);
API U VirtualProtect(void*,U,U,U*);API U VirtualFree(void*,U,U);
static void check(int ok,U code){if(!ok)ExitProcess(code);}
void run_user_characters_test(void){
 const unsigned char chars[]={0,'A','Z','a','z','0','9',' ',9,0x81,0xa0,0xaa,0xb5,0xc0,0xe9,0xff};
 for(U i=0;i<sizeof chars;++i){unsigned short flags=0;char c=chars[i];
  check(GetStringTypeA(0x800,1,&c,1,&flags),14100);SetLastError(98);
  check(IsCharLowerA(chars[i]|0xff0000)==!!(flags&2),14101);
  check(IsCharUpperA(chars[i]|0xff0000)==!!(flags&1),14102);
  check(IsCharAlphaA(chars[i]|0xff0000)==!!(flags&0x100),14103);
  check(IsCharAlphaNumericA(chars[i]|0xff0000)==!!(flags&0x104),14104);
  check(GetLastError()==98,14105);
 }
 check(!IsCharAlphaW('A')&&GetLastError()==120,14106);
 char input[]={0,'A',(char)0x81,'@',0,'Z',0};const char *s=input+1;
 check(CharNextA(s)==s+1&&CharNextA(s+1)==s+2&&CharNextA(s+3)==s+3,14107);
 check(CharPrevA(s,s+3)==s+2&&CharPrevA(s,s)==s&&CharPrevA(s,input)==input,14108);
 check(CharNextExA(0x123404e4,s,0xffff)==s+1,14109);
 check(CharPrevExA(1252,s,s+3,0xffff)==s+2&&CharPrevExA(1252,s,input,0)==input,14110);
 SetLastError(98);check(CharNextExA(65535,s,0)==s+1&&GetLastError()==87,14111);
 char *pages=VirtualAlloc(0,8192,0x3000,4);U old=0;check(pages!=0,14112);
 pages[4095]='A';check(VirtualProtect(pages+4096,4096,1,&old),14113);
 check(!CharNextA(pages+4095)&&GetLastError()==87,14114);
 check(!CharNextExA(1252,pages+4095,0)&&GetLastError()==87,14115);
 check(!CharPrevA(pages,pages+4096)&&GetLastError()==87,14116);
 pages[4095]=0;check(CharNextA(pages+4095)==pages+4095,14117);
 check(VirtualFree(pages,0,0x8000),14118);
}
#ifdef USER_CHARACTERS_STANDALONE
void mainCRTStartup(void){run_user_characters_test();ExitProcess(0);}
#endif
