/* SPDX-License-Identifier: GPL-2.0-or-later */
/* flat KERNEL tables, real-mode aliases and DOS arena ownership. */
#include <windows.h>
typedef DWORD (WINAPI *load16_fn)(const char *);
typedef DWORD (WINAPI *proc16_fn)(DWORD,DWORD);
typedef DWORD (WINAPI *mapsl_fn)(DWORD);
static DWORD rows[512], used, failed;
static BYTE image[0x30000];
static void check(DWORD actual,DWORD expected) {
    rows[used]=used/3+1; rows[used+1]=actual; rows[used+2]=expected;
    used+=3; if(actual!=expected) ++failed;
}
void *qt;
DWORD __stdcall call16(DWORD entry,DWORD arg);
__asm__(".text\n.globl _call16@8\n_call16@8:\n"
 "push %ebp\n mov %esp,%ebp\n sub $64,%esp\n mov 8(%ebp),%edx\n"
 "pushl 12(%ebp)\n call *_qt\n shll $16,%eax\n shrdl $16,%edx,%eax\n leave\n ret $8\n");
static void *ordinal(HMODULE module,DWORD number)
{
    BYTE *base=(BYTE*)module;
    IMAGE_DOS_HEADER *mz=(IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS *pe=(IMAGE_NT_HEADERS*)(base+mz->e_lfanew);
    IMAGE_EXPORT_DIRECTORY *ex=(IMAGE_EXPORT_DIRECTORY*)
        (base+pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    if (number<ex->Base || number-ex->Base>=ex->NumberOfFunctions) return 0;
    DWORD rva=((DWORD*)(base+ex->AddressOfFunctions))[number-ex->Base];
    return rva ? base+rva : 0;
}

void mainCRTStartup(void)
{
    HMODULE k32=GetModuleHandleA("KERNEL32.DLL");
    load16_fn load16=(load16_fn)ordinal(k32,35);
    proc16_fn proc16=(proc16_fn)ordinal(k32,37);
    mapsl_fn mapsl=(mapsl_fn)GetProcAddress(k32,"MapSL");
    qt=GetProcAddress(k32,"QT_Thunk");
    DWORD kernel=load16("KERNEL");
    DWORD alloc=proc16(kernel,184),freeblock=proc16(kernel,185);
    volatile DWORD *low=(volatile DWORD *)0x1e880;
    check(*low,0x20010618); /* Win98 measurement and static decoder bytes. */
    DWORD read;
    HANDLE source=CreateFileA("C:\\WINDOWS\\SYSTEM\\KRNL386.EXE",GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
    ReadFile(source,image,sizeof image,&read,0); CloseHandle(source);
    DWORD ne=*(DWORD*)(image+0x3c);
    WORD *entry=(WORD*)(image+ne+*(WORD*)(image+ne+0x22)+
        8*(*(WORD*)(image+ne+0x0e)-1));
    DWORD offset=(DWORD)entry[0]<<*(WORD*)(image+ne+0x32);
    BOOL same=TRUE;
    for (DWORD i=0x1ed4;i<0x1f32;++i)
        if (*(volatile BYTE*)(0x1c9a0+i)!=image[offset+i]) same=FALSE;
    check(same,TRUE);
    check(*(volatile DWORD*)0x1dd6c,call16(proc16(kernel,132),0));
    MEMORY_BASIC_INFORMATION info;
    check(VirtualQuery((void*)low,&info,sizeof info),sizeof info);
    check(info.State,MEM_FREE);
    /* Allocate before creating the high real-mode view. */
    DWORD a=call16(alloc,0xb000), base=(a>>16)*16;
    check(a!=0,1); check(base+0xb000<=0x1c000 || base>=0x20000,1);
    *(volatile DWORD*)base=0x12345678;
    /* MapSL exposes the same bytes as the __0000H selector. */
    DWORD zero=proc16(kernel,183);
    DWORD high=mapsl(zero&0xffff0000);
    check(high!=0,1);
    check(*(volatile DWORD*)(high+0x1e880),*low);
    *(volatile WORD*)(high+0x1dd6c)=0x1234;
    check(call16(proc16(kernel,132),0),0x1234);
    *(volatile WORD*)0x1dd6c=0x2c29;
    check(call16(proc16(kernel,132),0),0x2c29);
    *(volatile DWORD*)(high+0x1e880)=0x13579bdf;
#ifdef SYSTEM_VM_STATE
    MessageBoxA(0,"Continue checks after loading the saved state.","System VM state",MB_OK);
#endif
    check(*low,0x13579bdf);
    *low=0x20010618;
    check(*(volatile DWORD*)(high+0x1e880),0x20010618);
    check(*(volatile DWORD*)(high+base),0x12345678);
    /* An allocation crossing the resident data must move past it. */
    DWORD b=call16(alloc,0x3000), bbase=(b>>16)*16;
    check(b!=0,1); check(bbase>=0x20000,1);
    *(volatile DWORD*)(high+bbase)=0x87654321;
    check(*(volatile DWORD*)bbase,0x87654321);
    check(*low,0x20010618);
    /* Shared firmware, too; restore the changed byte. */
    volatile BYTE *bios=(volatile BYTE*)0xffffe;
    BYTE model=*bios;
    *(volatile BYTE*)(high+0xffffe)=0xfa;
    check(*bios,0xfa); *bios=model;
    check(call16(freeblock,b&65535)&65535,0);
    check(call16(freeblock,a&65535)&65535,0);
    DWORD again=call16(alloc,0xb000);
    check((again>>16)*16,base);
    check(*(volatile DWORD*)(high+base),0);
    check(call16(freeblock,again&65535)&65535,0);
    DWORD written;
    HANDLE f=CreateFileA("C:\\UPDRGN.BIN",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    WriteFile(f,&failed,4,&written,0); WriteFile(f,rows,used*4,&written,0); CloseHandle(f);
    ExitProcess(failed);
}
