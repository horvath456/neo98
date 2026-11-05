/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
static unsigned bad,checks;
static void check(int yes){++checks;if(!yes)++bad;}
void mainCRTStartup(void){
 HANDLE heap=GetProcessHeap();static const unsigned sizes[]={0,1,12,13,132,4096,0x20000};
 for(unsigned api=0;api<2;++api)for(unsigned n=0;n<7;++n){
  BYTE *p=api?LocalAlloc(0,sizes[n]):GlobalAlloc(0,sizes[n]);check(p!=0);if(!p)continue;
  unsigned size=HeapSize(heap,0,p);check(size>=sizes[n]&&size>=12&&size!=0xffffffff);
  check(size==(api?LocalSize(p):GlobalSize(p)));check(HeapFree(heap,0,p));
 }
 BYTE *p=LocalAlloc(0,4096);check(p!=0);
 if(p){for(unsigned i=0;i<4096;++i)p[i]='Z';check(!LocalFree(p));p=LocalAlloc(0,4096);check(p!=0);
  if(p){unsigned i;for(i=32;i<96&&p[i]=='Z';++i){}check(i==96);check(!LocalFree(p));}}
 p=GlobalAlloc(GMEM_ZEROINIT,4096);check(p!=0);if(p){unsigned i;for(i=0;i<4096&&!p[i];++i){}check(i==4096);check(!GlobalFree(p));}
 /* Win98 rejects MODIFY for a fixed block and leaves its bytes intact. */
 p=GlobalAlloc(0,132);check(p!=0);if(p){p[0]=0x34;p[131]=0x98;
  for(unsigned f=GMEM_MODIFY;f<=(GMEM_MODIFY|GMEM_MOVEABLE);f+=2){
   SetLastError(0xdeadbeef);check(!GlobalReAlloc(p,0,f));check(GetLastError()==87);
   check(p[0]==0x34&&p[131]==0x98);
  }check(!GlobalFree(p));}
 DWORD row[]={checks,bad},done;HANDLE f=CreateFileA("C:\\LOCALHEAP.BIN",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);WriteFile(f,row,sizeof row,&done,0);CloseHandle(f);ExitProcess(bad?1:0);
}
