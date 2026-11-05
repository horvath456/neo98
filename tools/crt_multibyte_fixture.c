/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define CRT __declspec(dllimport) __cdecl
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U CreateThread(U,U,U,U,U,U*);API U WaitForSingleObject(U,U);API U CloseHandle(U);
CRT int _setmbcp(int);CRT int _getmbcp(void);CRT int*_errno(void);CRT char*setlocale(int,const char*);CRT int*__p___mb_cur_max(void);
CRT int _mbscmp(const unsigned char*,const unsigned char*);CRT int _mbsncmp(const unsigned char*,const unsigned char*,U);CRT int _mbsnbcmp(const unsigned char*,const unsigned char*,U);
CRT U _mbslen(const unsigned char*);CRT unsigned char*_mbsinc(const unsigned char*);CRT unsigned char*_mbsdec(const unsigned char*,const unsigned char*);CRT unsigned char*_mbsninc(const unsigned char*,U);CRT U _mbsnextc(const unsigned char*);CRT U _mbclen(const unsigned char*);CRT U _mbsnbcnt(const unsigned char*,U);CRT U _mbsnccnt(const unsigned char*,U);CRT int _ismbblead(U);CRT int _ismbbtrail(U);CRT int _ismbslead(const unsigned char*,const unsigned char*);CRT int _ismbstrail(const unsigned char*,const unsigned char*);CRT unsigned char*_mbschr(const unsigned char*,U);CRT unsigned char*_mbsrchr(const unsigned char*,U);
static void check(int ok,U code){if(!ok)ExitProcess(code);}
static U __stdcall worker(U unused){check(_getmbcp()==932,12500);check(!_setmbcp(936)&&_getmbcp()==936,12501);return 0;}
void run_crt_multibyte_test(void){
 static const unsigned char s[]={0x82,0xa0,'A',0x82,0xa2,0},t[]={0x82,0xa0,'B',0},broken[]={0x82,0},invalid[]={0x82,0x20,0},other[]={0x81,0x41,0},empty[]={0};
 check(setlocale(0,"C")&&*__p___mb_cur_max()==1,12502);*_errno()=97;check(!_setmbcp(932)&&_getmbcp()==932&&*_errno()==97&&*__p___mb_cur_max()==1,12503);
 check(_ismbblead(0x81)&&_ismbblead(0xfc)&&!_ismbblead(0xa1)&&_ismbblead(0x10081),12504);check(_ismbbtrail(0x40)&&_ismbbtrail(0xfc)&&!_ismbbtrail(0x7f)&&!_ismbbtrail(0xff),12505);
 check(_mbscmp(s,t)==-1&&_mbscmp(t,s)==1&&!_mbscmp(s,s)&&_mbscmp((const unsigned char*)"A",(const unsigned char*)"a")==-1,12506);
 check(!_mbsncmp(s,t,0)&&!_mbsncmp(s,t,1)&&_mbsncmp(s,t,2)==-1,12507);check(!_mbsnbcmp(s,t,0)&&!_mbsnbcmp(s,t,1)&&!_mbsnbcmp(s,t,2)&&_mbsnbcmp(s,t,3)==-1,12508);check(!_mbsnbcmp(s,other,1)&&_mbsnbcmp(s,other,2)==1,12509);
 check(_mbslen(s)==3&&_mbslen(broken)==0&&_mbslen(invalid)==1,12510);check(_mbsinc(s)==s+2&&_mbsinc(s+2)==s+3&&(U)_mbsinc(broken)==(U)broken+2,12511);check(_mbclen(s)==2&&_mbclen(s+2)==1&&_mbclen(empty)==1,12512);check(_mbsnextc(s)==0x82a0&&_mbsnextc(broken)==0x8200&&_mbsnextc(invalid)==0x8220,12513);
 check(_mbsnbcnt(s,0)==0&&_mbsnbcnt(s,1)==2&&_mbsnbcnt(s,2)==3&&_mbsnbcnt(s,3)==5&&_mbsnbcnt(s,99)==5&&_mbsnbcnt(broken,1)==0,12514);check(_mbsnccnt(s,0)==0&&_mbsnccnt(s,1)==0&&_mbsnccnt(s,2)==1&&_mbsnccnt(s,3)==2&&_mbsnccnt(s,4)==2&&_mbsnccnt(s,5)==3,12515);check(_mbsninc(s,2)==s+3&&!_mbsninc(0,99)&&_mbsninc(broken,2)==broken,12516);
 check(!_mbsdec(s,s)&&_mbsdec(s,s+2)==s&&_mbsdec(s,s+3)==s+2&&(U)_mbsdec(s,s+1)==(U)s-1,12517);check(_ismbslead(s,s)==-1&&!_ismbslead(s,s+1)&&_ismbstrail(s,s+1)==-1&&!_ismbstrail(s,s+2),12518);check(_ismbslead(broken,broken)==-1&&_ismbstrail(broken,broken+1)==-1,12519);
 check(!_mbscmp(broken,empty)&&!_mbsncmp(broken,empty,1)&&!_mbsnbcmp(broken,empty,1),12520);check(_mbschr(s,0x82a2)==s+3&&_mbsrchr(s,0x82a0)==s&&_mbschr(s,0)==s+5&&_mbsrchr(broken,'z')==broken+1,12521);
 U id=0,thread=CreateThread(0,0,(U)worker,0,0,&id);check(thread&&WaitForSingleObject(thread,1000)==0&&CloseHandle(thread)&&_getmbcp()==936,12522);check(_ismbblead(0xfe)&&_ismbbtrail(0x80),12523);
 const int pages[]={949,950,1361};for(U i=0;i<3;++i)check(!_setmbcp(pages[i])&&_getmbcp()==pages[i],12524);check(_setmbcp(777)==-1&&_getmbcp()==1361&&*_errno()==97,12525);
 check(!_setmbcp(0)&&!_getmbcp()&&!_ismbblead(0x82)&&_mbslen(s)==5&&_mbsinc(s)==s+1,12526);check(!_ismbslead(s,s)&&!_ismbstrail(s,s+1)&&_mbsdec(s,s+1)==s,12527);check(_mbscmp((const unsigned char*)"a",(const unsigned char*)"b")==-1&&!_mbsncmp(s,t,2),12528);
 check(!_setmbcp(-3)&&!_getmbcp()&&!_setmbcp(-2)&&!_getmbcp()&&!_setmbcp(-4)&&!_getmbcp(),12529);check(*__p___mb_cur_max()==1&&*_errno()==97,12530);
}
#ifdef CRT_MULTIBYTE_STANDALONE
void mainCRTStartup(void){run_crt_multibyte_test();ExitProcess(0);}
#endif
