/* SPDX-License-Identifier: GPL-2.0-or-later */
/* REP must publish its completed iterations before a page fault. */
#include <windows.h>
static BYTE *source,*destination;
static DWORD rows[32][5],count;
static HANDLE file;
static void finish(DWORD code){DWORD n;WriteFile(file,&code,4,&n,0);WriteFile(file,&count,4,&n,0);WriteFile(file,rows,count*sizeof rows[0],&n,0);CloseHandle(file);ExitProcess(code);}
static LONG WINAPI fault(EXCEPTION_POINTERS *p){
 DWORD address=p->ExceptionRecord->ExceptionInformation[1];
 if(p->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || address<(DWORD)source || address>=(DWORD)source+0x4000)finish(10);
 if(count==32)finish(11);
 DWORD offset=address-(DWORD)source;DWORD *r=rows[count++];
 r[0]=offset;r[1]=p->ContextRecord->Esi-(DWORD)source;r[2]=p->ContextRecord->Edi-(DWORD)destination;r[3]=p->ContextRecord->Ecx;r[4]=p->ExceptionRecord->ExceptionInformation[0];
 // A stream cache retires the previous page after mapping the next page.
 if(count>1 && (offset&~4095u) <= (rows[count-2][0]&~4095u))finish(12);
 if(!VirtualAlloc(source+(offset&~4095u),4096,MEM_COMMIT,PAGE_READWRITE))finish(13);
 for(DWORD i=0;i<4096;i++)source[(offset&~4095u)+i]=(BYTE)((offset&~4095u)/4096+1);
 if(offset>=4096)VirtualFree(source+(offset&~4095u)-4096,4096,MEM_DECOMMIT);
 return EXCEPTION_CONTINUE_EXECUTION;
}
static DWORD copy_buffer[128];
static void semantics(void){
 // REP preserves arithmetic flags, handles DF and zero count, and reads
 // overlapping sources after each preceding store rather than as memcpy.
 for(DWORD width=1;width<=4;width*=2)for(DWORD reverse=0;reverse<2;reverse++){
  for(DWORD i=0;i<sizeof copy_buffer;i++)((BYTE*)copy_buffer)[i]=(BYTE)(i*3+7);
  BYTE *s=(BYTE*)copy_buffer+(reverse?63*width:0),*d=(BYTE*)copy_buffer+256+(reverse?63*width:0);
  DWORD count=64,flags=0x8d7|(reverse?0x400:0),after;
  if(width==1)__asm__ __volatile__("push %4; popfl; rep movsb; pushfl; pop %0; cld":"=r"(after),"+S"(s),"+D"(d),"+c"(count):"r"(flags):"memory","cc");
  if(width==2)__asm__ __volatile__("push %4; popfl; rep movsw; pushfl; pop %0; cld":"=r"(after),"+S"(s),"+D"(d),"+c"(count):"r"(flags):"memory","cc");
  if(width==4)__asm__ __volatile__("push %4; popfl; rep movsl; pushfl; pop %0; cld":"=r"(after),"+S"(s),"+D"(d),"+c"(count):"r"(flags):"memory","cc");
  if(count || (after&0xcd5)!=(flags&0xcd5))finish(20);
  for(DWORD i=0;i<64*width;i++)if(((BYTE*)copy_buffer)[256+i]!=(BYTE)(i*3+7))finish(21);
 }
 BYTE *s=0,*d=0;DWORD n=0;
 __asm__ __volatile__("rep movsl":"+S"(s),"+D"(d),"+c"(n)::"memory");
 if(s || d || n)finish(22);
 copy_buffer[0]=0x12345678;s=(BYTE*)copy_buffer;d=s+4;n=63;
 __asm__ __volatile__("rep movsl":"+S"(s),"+D"(d),"+c"(n)::"memory");
 for(DWORD i=0;i<64;i++)if(copy_buffer[i]!=0x12345678)finish(23);
}
void mainCRTStartup(void){
 file=CreateFileA("C:\\REPPROG.BIN",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
 source=VirtualAlloc(0,0x4000,MEM_RESERVE,PAGE_READWRITE);destination=VirtualAlloc(0,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 SetUnhandledExceptionFilter(fault);
 BYTE *s=source+44,*d=destination;DWORD words=(0x4000-44)/4;
 __asm__ __volatile__("cld; rep movsl" : "+S"(s),"+D"(d),"+c"(words)::"memory");
 for(DWORD i=0;i<0x4000-44;i++)if(destination[i]!=(BYTE)((i+44)/4096+1))finish(14);
 semantics();finish(0);
}
