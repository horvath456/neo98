/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define CRT __declspec(dllimport) __cdecl
__declspec(dllimport) void __stdcall ExitProcess(U);
CRT int _setmbcp(int);
CRT U _mbscspn(const char*,const char*);
CRT U _mbsspn(const char*,const char*);
CRT char*_mbspbrk(const char*,const char*);
CRT char*_mbsspnp(const char*,const char*);
CRT U strcspn(const char*,const char*);
CRT U strspn(const char*,const char*);
CRT char*strpbrk(const char*,const char*);
static void check(int okay,U code){if(!okay)ExitProcess(code);}
void mainCRTStartup(void){
    const char name[]="file?.rtf",chars[]="\\/:*?\"<>|",set[]="ab",text[]="aaabz";
    const char multi[]={0x82,(char)0xa0,'A',0x82,(char)0xa2,0},first[]={0x82,(char)0xa0,0};
    const char broken[]={0x82,0},letter[]="A",trail[]={0x82,'A',0};
    check(!_setmbcp(0),31200);
    check(strpbrk(name,chars)==name+4 && strcspn(name,chars)==4,31201);
    check(strspn(text,set)==4 && _mbsspn(text,set)==4 && _mbsspnp(text,set)==text+4,31202);
    check(_mbspbrk(name,chars)==name+4 && _mbscspn(name,chars)==4,31203);
    check(!strpbrk(text,"") && strspn(text,"")==0 && strcspn(text,"")==5,31204);
    check(!_setmbcp(932),31205);
    check(_mbspbrk(multi,"A")==multi+2 && _mbscspn(multi,"A")==2,31206);
    check(_mbsspn(multi,first)==2 && _mbsspnp(multi,first)==multi+2,31207);
    check(!_mbspbrk(trail,"A") && _mbscspn(trail,"A")==2,31208);
    check(strpbrk(trail,"A")==trail+1 && strcspn(trail,"A")==1,31209);
    check(!_mbspbrk(broken,"A") && _mbscspn(broken,"A")==1,31210);
    check(_mbspbrk(letter,broken)==letter && _mbsspn(letter,broken)==1 && !_mbsspnp(letter,broken),31211);
    check(!_setmbcp(0) && !_mbspbrk(letter,broken) && _mbsspn(letter,broken)==0,31212);
    ExitProcess(0);
}
