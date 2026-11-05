/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U HeapCreate(U,U,U); API U HeapAlloc(U,U,U); API U HeapFree(U,U,U);
API U HeapDestroy(U); API U MessageBoxA(U,const char*,const char*,U); API void ExitProcess(U);
void mainCRTStartup(void)
{
    U heap=HeapCreate(0x04000000,0,0x100000);
    U block=heap?HeapAlloc(heap,8,0x20000):0;
    int okay=heap>=0x80000000u && block>heap;
    if(okay){
        volatile U *p=(volatile U*)block;
        for(U i=0;i<0x8000;++i) p[i]=i^0x5a5aa5a5;
        for(U i=0;i<0x8000;++i) if(p[i]!=(i^0x5a5aa5a5)) okay=0;
    }
    if(block && !HeapFree(heap,0,block)) okay=0;
    if(heap && !HeapDestroy(heap)) okay=0;
    const char *text=okay?"PASS restored session allocation":"FAIL restored session allocation";
    MessageBoxA(0,text,text,0); ExitProcess(okay?0:1);
}
