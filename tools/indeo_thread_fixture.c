/* SPDX-License-Identifier: GPL-2.0-or-later */
/* open the original Indeo driver on a CreateThread stack. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateThread(void *,U,U (__stdcall *)(void *),void *,U,U *);
API U WaitForSingleObject(U,U);
API U GetExitCodeThread(U,U *);
API U CloseHandle(U);
API U ICOpen(U,U,U);
API U ICClose(U);
API U IsBadReadPtr(void *,U);
static U __stdcall probe(void *unused)
{
    U frame; U *scope; U codec;
    (void)unused;
    __asm__ __volatile__("movl %%fs:0,%0":"=r"(frame));
    if(IsBadReadPtr((void *)frame,16)) return 62021;
    if(*(U *)(frame+12)!=0) return 62022;
    scope=*(U **)(frame+8);
    if(IsBadReadPtr(scope,12) || scope[0]!=~0u || !scope[1]) return 62023;
    codec=ICOpen(0x63646976,0x32337669,2); /* vidc, iv32, ICMODE_DECOMPRESS */
    if(!codec) return 62024;
    if(ICClose(codec)) return 62025;
    return 0;
}
void mainCRTStartup(void)
{
    U code,id,thread;
    thread=CreateThread(0,0,probe,0,0,&id);
    if(!thread || WaitForSingleObject(thread,5000) || !GetExitCodeThread(thread,&code))ExitProcess(62026);
    CloseHandle(thread);
    if(code)ExitProcess(code);
    ExitProcess(probe(0));
}
