/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

/* real Win98 loader and VirtualProtect, including shared RO. */
START_TEST(repertoire_pe)
{
    unsigned char image[1536]; char temp[MAX_PATH], file[MAX_PATH]; unsigned n;
    static const struct {DWORD alignment,flags;} cases[]={
        {4096,0x40000040},{4096,0x50000040},{65536,0x40000040},{65536,0xc0000040}};
    GetTempPathA(sizeof(temp),temp);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    for(n=0;n<4;++n){
        IMAGE_DOS_HEADER *dos=(void*)image; IMAGE_NT_HEADERS32 *nt=(void*)(image+64);
        IMAGE_SECTION_HEADER *s=(void*)(image+64+248); DWORD written,old=0xdeadbeef,error;
        HANDLE h; HMODULE module; BOOL changed; MEMORY_BASIC_INFORMATION info;
        ZeroMemory(image,sizeof(image)); dos->e_magic=0x5a4d; dos->e_lfanew=64;
        nt->Signature=0x4550; nt->FileHeader.Machine=0x14c;nt->FileHeader.NumberOfSections=2;
        nt->FileHeader.SizeOfOptionalHeader=224;nt->FileHeader.Characteristics=0x2102;
        nt->OptionalHeader.Magic=0x10b;nt->OptionalHeader.ImageBase=0x10000000;
        nt->OptionalHeader.SectionAlignment=cases[n].alignment;nt->OptionalHeader.FileAlignment=512;
        nt->OptionalHeader.MajorSubsystemVersion=4;nt->OptionalHeader.Subsystem=3;
        nt->OptionalHeader.SizeOfImage=2*cases[n].alignment+(cases[n].alignment>8192?cases[n].alignment:8192);nt->OptionalHeader.SizeOfHeaders=512;
        memcpy(s[0].Name,".rdata",6);s[0].VirtualAddress=cases[n].alignment;
        s[0].Misc.VirtualSize=24;s[0].SizeOfRawData=512;s[0].PointerToRawData=512;s[0].Characteristics=cases[n].flags;
        memcpy(s[1].Name,".data",5);s[1].VirtualAddress=2*cases[n].alignment;
        s[1].Misc.VirtualSize=8192;s[1].SizeOfRawData=512;s[1].PointerToRawData=1024;s[1].Characteristics=0xc0000040;
        image[512]=0x42;image[1024]=0x24;
        GetTempFileNameA(temp,"rpe",0,file);h=CreateFileA(file,GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
        WriteFile(h,image,sizeof(image),&written,0);CloseHandle(h);
        module=LoadLibraryA(file);error=module?0:GetLastError();
        trace("REPERTOIRE case=%u loaded=%u error=%lu\n",n,!!module,error);
        ok(module!=0,"load case %u\n",n);
        if(module){
            BYTE *p=(BYTE*)module+cases[n].alignment; unsigned offsets[]={0,cases[n].alignment,cases[n].alignment+4096,2*cases[n].alignment,2*cases[n].alignment+4096}; unsigned k;
            for(k=0;k<5;++k){ZeroMemory(&info,sizeof(info));VirtualQuery((BYTE*)module+offsets[k],&info,sizeof(info));
                trace("REPERTOIRE case=%u offset=%lx state=%lx protect=%lx badread=%u badwrite=%u\n",n,offsets[k],info.State,info.Protect,!!IsBadReadPtr((BYTE*)module+offsets[k],1),!!IsBadWritePtr((BYTE*)module+offsets[k],1));}
            if(cases[n].alignment==65536){
                VirtualQuery(p+4096,&info,sizeof(info));
                ok(info.State==MEM_RESERVE && info.Protect==PAGE_NOACCESS,"reserved image gap %u state=%lx protect=%lx\n",n,info.State,info.Protect);
            }
            ok(*p==0x42,"raw bytes %u\n",n);
            ok(*((BYTE*)module+2*cases[n].alignment+4096)==0,"zero fill %u\n",n);
            SetLastError(0xdeadbeef);changed=VirtualProtect(p,24,PAGE_EXECUTE_READWRITE,&old);error=changed?0:GetLastError();
            trace("REPERTOIRE case=%u changed=%u old=%lx error=%lu\n",n,!!changed,old,error);
            if(changed){__asm__ __volatile__("lock; orl $0,(%0)"::"r"(p):"memory","cc");VirtualProtect(p,24,old,&written);}
            FreeLibrary(module);
        }
        DeleteFileA(file);
    }
}
