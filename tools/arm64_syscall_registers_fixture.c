/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv)
{
    unsigned count=argc>1?(unsigned)atoi(argv[1]):100000,failures=0;
    DWORD thread=GetCurrentThreadId();LARGE_INTEGER start,end,frequency;
    QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
    for(unsigned i=0;i<count;++i)if(GetCurrentThreadId()!=thread)++failures;
    QueryPerformanceCounter(&end);
    printf("SYSCALL_REGISTERS count=%u failures=%u ticks=%lld frequency=%lld\n",count,failures,end.QuadPart-start.QuadPart,frequency.QuadPart);
    return failures?1:0;
}
