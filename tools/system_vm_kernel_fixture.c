/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original-Win98 measurement: locate the live KERNEL data, then compare it
   with the low-memory copy used by Windows 98. Rows are tag/value/reserved;
   values are observations, not fixed expected addresses. */
#include <windows.h>
typedef DWORD (WINAPI *load16_fn)(const char *);
typedef DWORD (WINAPI *proc16_fn)(DWORD,DWORD);
typedef DWORD (WINAPI *mapsl_fn)(DWORD);
static DWORD rows[60], used, failed;
static void save(void)
{
    HANDLE f=CreateFileA("C:\\UPDRGN.BIN",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    DWORD written; WriteFile(f,&failed,4,&written,0); WriteFile(f,rows,used*4,&written,0); CloseHandle(f);
}
static void record(DWORD tag,DWORD value) { rows[used++]=tag; rows[used++]=value; rows[used++]=0; }
static DWORD read(DWORD address,DWORD bytes)
{
    DWORD value=0, count=0;
    if (!ReadProcessMemory(GetCurrentProcess(),(void*)address,&value,bytes,&count)||count!=bytes) ++failed;
    return value;
}
/* Win98 GetProcAddress rejects ordinal lookup specifically for KERNEL32. Read its PE export table as
   the import loader does; the ordinal-only entries have no public names. */
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
    failed=99; save(); failed=0;
    HMODULE k32=GetModuleHandleA("KERNEL32.DLL");
    load16_fn load16=(load16_fn)ordinal(k32,35);
    proc16_fn proc16=(proc16_fn)ordinal(k32,37);
    mapsl_fn mapsl=(mapsl_fn)GetProcAddress(k32,"MapSL");
    if (!load16||!proc16||!mapsl) {
        record(11,(DWORD)k32); record(12,(DWORD)load16);
        record(13,(DWORD)proc16); record(14,(DWORD)mapsl);
        failed=1; save(); ExitProcess(failed);
    }
    DWORD module=load16("KERNEL"), entry=proc16(module,132);
    DWORD code=mapsl(entry&0xffff0000), selector=read(code+2,2), data=mapsl(selector<<16);
    record(1,module); record(2,entry); record(3,code); record(4,selector); record(5,data);
    record(6,read(data+0x13cc,4)); record(7,read(0x1c9a0+0x13cc,4));
    record(8,read(data+0x1ee0,4)); record(9,read(0x1e880,4));
    record(10,read(0,4));
    save(); ExitProcess(failed);
}
