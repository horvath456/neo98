/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

/* Inspect each layout without Wine loader.c's ERROR_GEN_FAILURE early return.
 * Entry-point cases execute only a fixed DllMain stub returning TRUE. */
START_TEST(loader_layout)
{
    static const struct {
        const char *name;
        WORD sections,optional;
        DWORD alignment,file_alignment,image_size,headers,file_size,flags,entry;
    } cases[]={
        {"normal",1,224,4096,512,8192,512,1024},
        {"unaligned-image",1,224,4096,512,0x1f00,512,1024},
        {"small-alignment",1,224,512,512,1024,512,1024},
        {"mismatched-alignment",1,224,512,4096,8192,512,1024},
        {"zero-image",1,224,4096,512,0,512,1024},
        {"no-optional",1,0,4096,512,8192,512,1024},
        {"no-sections",0,224,4096,512,4096,512,1024},
        {"short-optional",0,64,512,512,4096,192,1024},
        {"headers-past-eof",1,224,4096,512,8192,4096,1024},
        {"short-section",1,224,4096,512,8192,512,532},
        {"no-optional-128",1,0,4096,512,8192,512,128},
        {"no-optional-144",1,0,4096,512,8192,512,144},
        {"no-optional-511",1,0,4096,512,8192,512,511},
        {"no-optional-512",1,0,4096,512,8192,512,512},
        {"empty-section",1,224,4096,512,8192,512,512},
        {"no-sections-shortfile",0,224,4096,512,4096,512,312},
        {"entry-readonly",1,224,4096,512,8192,512,1024,0x40000040,4096},
        {"entry-writable",1,224,4096,512,8192,512,1024,0xc0000040,4096},
        {"entry-code",1,224,4096,512,8192,512,1024,0x60000020,4096},
        {"noentry-writable",1,224,4096,512,8192,512,1024,0xc0000040,0},
        {"noentry-code",1,224,4096,512,8192,512,1024,0x60000020,0},
        {"no-optional-311",1,0,4096,512,8192,512,311},
        {"no-optional-312",1,0,4096,512,8192,512,312}
    };
    unsigned char image[1024];char temp[MAX_PATH],file[MAX_PATH];unsigned i;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    ok(GetTempPathA(sizeof(temp),temp)!=0,"temporary directory\n");
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
        IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER*)image;
        IMAGE_NT_HEADERS32 *nt=(IMAGE_NT_HEADERS32*)(image+64);
        IMAGE_SECTION_HEADER *section;
        DWORD written,error;HANDLE handle;HMODULE module;
        ZeroMemory(image,sizeof(image));dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=64;
        nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;
        nt->FileHeader.NumberOfSections=cases[i].sections;nt->FileHeader.SizeOfOptionalHeader=cases[i].optional;
        nt->FileHeader.Characteristics=IMAGE_FILE_EXECUTABLE_IMAGE|IMAGE_FILE_DLL;
        nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        nt->OptionalHeader.ImageBase=0x10000000;nt->OptionalHeader.SectionAlignment=cases[i].alignment;
        nt->OptionalHeader.FileAlignment=cases[i].file_alignment;nt->OptionalHeader.MajorOperatingSystemVersion=4;
        nt->OptionalHeader.MajorSubsystemVersion=4;nt->OptionalHeader.Subsystem=IMAGE_SUBSYSTEM_WINDOWS_CUI;
        nt->OptionalHeader.SizeOfImage=cases[i].image_size;nt->OptionalHeader.SizeOfHeaders=cases[i].headers;
        nt->OptionalHeader.AddressOfEntryPoint=cases[i].entry;
        section=(IMAGE_SECTION_HEADER*)(image+64+24+cases[i].optional);
        if(cases[i].sections){
            ZeroMemory(section,sizeof(*section));CopyMemory(section->Name,".data",5);
            section->VirtualAddress=cases[i].alignment;section->Misc.VirtualSize=16;
            section->SizeOfRawData=512;section->PointerToRawData=512;
            section->Characteristics=cases[i].flags?cases[i].flags:IMAGE_SCN_CNT_INITIALIZED_DATA|IMAGE_SCN_MEM_READ;
        }
        image[512]=0x42;
        if(cases[i].entry){const unsigned char entry[]={0xb8,1,0,0,0,0xc2,12,0};CopyMemory(image+512,entry,sizeof(entry));}
        ok(GetTempFileNameA(temp,"lyo",0,file)!=0,"temporary filename %u\n",i);
        handle=CreateFileA(file,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
        ok(handle!=INVALID_HANDLE_VALUE,"create image %u\n",i);if(handle==INVALID_HANDLE_VALUE)continue;
        ok(WriteFile(handle,image,cases[i].file_size,&written,NULL)&&written==cases[i].file_size,"write image %u\n",i);
        CloseHandle(handle);SetLastError(0xdeadbeef);module=LoadLibraryA(file);error=GetLastError();
        trace("LAYOUT name=%s loaded=%u error=%lu\n",cases[i].name,module!=NULL,module?0:error);
        if(module){
            MEMORY_BASIC_INFORMATION info={0};DWORD size=VirtualQuery(module,&info,sizeof(info));
            trace("LAYOUT_MEMORY name=%s query=%u size=%lu state=%08lx protect=%08lx type=%08lx\n",
                  cases[i].name,size==sizeof(info),info.RegionSize,info.State,info.Protect,info.Type);
            {
                volatile BYTE *target=(BYTE*)module+16;
                BOOL bad=IsBadWritePtr((void*)target,1);BYTE value=*target;BOOL stored=FALSE;
                if(!bad){*target=(BYTE)(value^0x5a);stored=*target==(BYTE)(value^0x5a);}
                ZeroMemory(&info,sizeof(info));VirtualQuery((void*)target,&info,sizeof(info));
                trace("LAYOUT_HEADER_WRITE name=%s bad=%u stored=%u protect=%08lx\n",cases[i].name,!!bad,!!stored,info.Protect);
            }
            if(cases[i].sections){
                volatile BYTE *target=(BYTE*)module+cases[i].alignment+12;
                BOOL bad=IsBadWritePtr((void*)target,1);BYTE value=*target;BOOL stored=FALSE;
                if(!bad){*target=(BYTE)(value^0x5a);stored=*target==(BYTE)(value^0x5a);}
                ZeroMemory(&info,sizeof(info));VirtualQuery((void*)target,&info,sizeof(info));
                trace("LAYOUT_DATA_WRITE name=%s bad=%u stored=%u protect=%08lx\n",cases[i].name,!!bad,!!stored,info.Protect);
            }
            ok(FreeLibrary(module),"unload image %u\n",i);
        }
        ok(DeleteFileA(file),"delete image %u\n",i);
    }
}
