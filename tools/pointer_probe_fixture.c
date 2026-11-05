/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U IsBadCodePtr(const void *);
API U IsBadReadPtr(const void *, U);
API U IsBadHugeReadPtr(const void *, U);
API U IsBadWritePtr(void *, U);
API U IsBadHugeWritePtr(void *, U);
API U IsBadStringPtrA(const char *, U);
API U IsBadStringPtrW(const unsigned short *, U);
API void SetLastError(U);
API U GetLastError(void);
API void *VirtualAlloc(void *, U, U, U);
API U VirtualFree(void *, U, U);
API U VirtualProtect(void *, U, U, U *);
API void ExitProcess(U);
static void check(int okay,U code){if(!okay)ExitProcess(code);}
void run_pointer_probe_test(void)
{
    U old=0;char *p=VirtualAlloc(0,16384,0x3000,4);check(p!=0,7200);
    for(U i=0;i<16384;++i)p[i]='q';
    SetLastError(0x98bad);
    check(!IsBadReadPtr(0,0) && !IsBadWritePtr(0,0) && !IsBadHugeReadPtr(0,0) && !IsBadHugeWritePtr(0,0),7201);
    check(IsBadReadPtr(0,1)==1 && IsBadWritePtr(0,1)==1 && IsBadCodePtr(0)==1,7202);
    check(!IsBadReadPtr(p,16384) && !IsBadHugeReadPtr(p,16384) && !IsBadWritePtr(p,16384) && !IsBadHugeWritePtr(p,16384),7203);
    check(!IsBadCodePtr(p),7204); /* Readable data need not be executable. */
    check(GetLastError()==0x98bad,7205);
    for(U i=0;i<16384;++i)check(p[i]=='q',7206); /* Original default ADD operand is zero. */
    check(!IsBadStringPtrA(p,16384),7207); /* Maximum exhaustion does not require NUL. */
    check(!IsBadStringPtrA(0,0) && IsBadStringPtrA(0,1)==1,7208);
    check(!IsBadStringPtrW(0,0xffffffff) && !IsBadStringPtrW((void *)1,0xffffffff),7209);
    check(VirtualProtect(p,4096,2,&old),7210);
    SetLastError(0x98bad);
    check(!IsBadReadPtr(p,1) && !IsBadCodePtr(p) && IsBadWritePtr(p,1)==1 && IsBadHugeWritePtr(p,1)==1,7211);
    check(GetLastError()==0x98bad && p[0]=='q',7212);
    check(VirtualProtect(p,4096,4,&old) && VirtualFree(p+4096,4096,0x4000),7213);
    SetLastError(0x98bad);
    check(IsBadReadPtr(p,8193)==1 && IsBadHugeReadPtr(p,8193)==1,7214); /* Endpoints valid, interior page absent. */
    check(IsBadWritePtr(p,8193)==1 && IsBadHugeWritePtr(p,8193)==1,7215);
    check(!IsBadReadPtr(p,4096) && !IsBadWritePtr(p,4096),7216);
    check(IsBadReadPtr(p,4097)==1 && IsBadWritePtr(p,4097)==1,7217);
    check(IsBadReadPtr(p+4096,1)==1 && IsBadCodePtr(p+4096)==1,7218);
    check(IsBadStringPtrA(p,4097)==1 && !IsBadStringPtrA(p,4096),7219);
    p[4095]=0;check(!IsBadStringPtrA(p,0xffffffff),7220);
    p[4095]='q';check(IsBadStringPtrA(p,0xffffffff)==1,7221);
    check(GetLastError()==0x98bad,7222);
    check(VirtualAlloc(p+4096,4096,0x1000,4)==p+4096,7223);
    p[4096]='q';check(VirtualProtect(p+8192,4096,1,&old),7224);
    SetLastError(0x98bad);
    check(IsBadReadPtr(p+8192,0)==0 && IsBadReadPtr(p+8192,1)==1 && IsBadWritePtr(p+8192,1)==1 && IsBadCodePtr(p+8192)==1,7225);
    check(IsBadReadPtr(p,12289)==1 && IsBadWritePtr(p,12289)==1,7226);
    check(!IsBadStringPtrW((void *)(p+8192),0xffffffff),7227);
    check(IsBadReadPtr((void *)0xffffffff,2)==1 && IsBadWritePtr((void *)0xffffffff,2)==1,7228);
    check(GetLastError()==0x98bad,7229);
    check(VirtualFree(p,0,0x8000),7230);
    SetLastError(0x98bad);
    check(IsBadReadPtr(p,1)==1 && IsBadWritePtr(p,1)==1 && GetLastError()==0x98bad,7231);
    p=VirtualAlloc(0,4096,0x2000,4);check(p!=0,7232);
    check(IsBadReadPtr(p,1)==1 && IsBadWritePtr(p,1)==1 && !IsBadStringPtrW((void *)p,1),7233);
    check(VirtualFree(p,0,0x8000),7234);
}
#ifdef POINTER_PROBE_STANDALONE
void mainCRTStartup(void){run_pointer_probe_test();ExitProcess(0);}
#endif
