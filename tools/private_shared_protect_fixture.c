/* SPDX-License-Identifier: GPL-2.0-or-later */
/* private-address MEM_SHARED sections can change protection on Win98. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U VirtualProtect(void *,U,U,U *);
API U VirtualQuery(void *,void *,U);
API U IsBadWritePtr(void *,U);
__declspec(allocate(".shared")) volatile const U shared[4096]={0x12345678};
static void check(U condition,U code){if(!condition)ExitProcess(code);}
void mainCRTStartup(void)
{
    volatile U *p=(volatile U *)shared; U info[7],old=0;
    check(VirtualQuery((void*)p,info,sizeof(info))==sizeof(info)&&info[5]==2,61821);
    check(IsBadWritePtr((void*)p,4),61822);
    check(VirtualProtect((void*)p,24,0x40,&old)&&old==2,61823);
    __asm__ __volatile__("lock; orl $0,(%0)"::"r"(p):"memory","cc");
    *p=0x87654321;check(*p==0x87654321,61824);
    check(VirtualProtect((void*)p,24,2,&old)&&old==4,61825);
    check(IsBadWritePtr((void*)p,4),61826);
    check(VirtualQuery((void*)p,info,sizeof(info))==sizeof(info)&&info[5]==2,61827);
    ExitProcess(0);
}
