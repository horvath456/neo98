// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API void GlobalMemoryStatus(void*);API void SetLastError(U);API U GetLastError(void);API U VirtualAlloc(U,U,U,U);API U VirtualFree(U,U,U);
struct Status{U length,load,physical,freephysical,pagefile,freepagefile,virtualbytes,freevirtual;};
static void check(int v,U e){if(!v)ExitProcess(e);}
void run_memory_status_test(void){struct Status before,reserved,committed,after;before.length=1;SetLastError(777);GlobalMemoryStatus(&before);check(before.length==32&&before.load<=100&&before.physical&&before.physical<=0x3ffff000&&before.freephysical<=before.physical,35600);check(before.virtualbytes==0x7fc00000&&before.freevirtual<before.virtualbytes,35601);
 // the paging file Windows manages, as large as the memory, as DPMI 0500h names it; both halves together below 2 GiB.
 check(before.pagefile==before.physical&&before.freepagefile<=before.pagefile&&before.physical+before.pagefile<0x80000000u&&before.freephysical+before.freepagefile<0x80000000u,35611);check(GetLastError()==777,35602);
 U block=VirtualAlloc(0,65536,0x2000,4);check(block,35603);GlobalMemoryStatus(&reserved);check(reserved.freevirtual+65536==before.freevirtual&&reserved.virtualbytes==before.virtualbytes,35604);check(VirtualAlloc(block,4096,0x1000,4)==block,35605);GlobalMemoryStatus(&committed);check(committed.freevirtual==reserved.freevirtual,35606);check(VirtualFree(block,4096,0x4000),35607);check(VirtualFree(block,0,0x8000),35608);GlobalMemoryStatus(&after);check(after.freevirtual==before.freevirtual,35609);GlobalMemoryStatus(0);check(GetLastError()==87,35610);
}
#ifdef MEMORY_STATUS_STANDALONE
void mainCRTStartup(void){run_memory_status_test();ExitProcess(0);}
#endif
