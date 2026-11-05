// SPDX-License-Identifier: GPL-2.0-or-later
namespace {
bool PlatformMemoryPages(unsigned&physical,unsigned&free){
 unsigned long long total=0,available=0;
#if defined(__APPLE__)
 size_t size=sizeof total;if(sysctlbyname("hw.memsize",&total,&size,nullptr,0))return false;
 mach_port_t host=mach_host_self();vm_size_t page=0;vm_statistics64_data_t statistics{};mach_msg_type_number_t count=HOST_VM_INFO64_COUNT;
 const bool okay=host_page_size(host,&page)==KERN_SUCCESS&&host_statistics64(host,HOST_VM_INFO64,reinterpret_cast<host_info64_t>(&statistics),&count)==KERN_SUCCESS;
 mach_port_deallocate(mach_task_self(),host);if(!okay)return false;available=static_cast<unsigned long long>(statistics.free_count)*page;
#elif defined(__linux__)
 struct sysinfo info{};if(sysinfo(&info))return false;total=static_cast<unsigned long long>(info.totalram)*info.mem_unit;available=static_cast<unsigned long long>(info.freeram)*info.mem_unit;
#else
 auto*memory=CMemorySystem::Get();total=memory->GetMemSize();available=memory->GetHeapFreeSpace(HEAP_ANY);
 // Circle reports unallocated heap tail, excluding reusable bucket free lists.
 // This is a conservative count, not a claim to include every reusable block.
#endif
 if(available>total)available=total;total>>=12;available>>=12;if(!total||total>0xffffffffull)return false;
 physical=unsigned(total);free=unsigned(available);return true;
}
}
// The guest's _GetDemandPageInfo report, from which Win98 builds
// both GlobalMemoryStatus and the answer to DPMI 0500h,
// so the 32-bit and the 16-bit view agree.
// NEO98 has no pager; like a default Win98, whose swap file Windows manages,
// it reports one (the configured memory model): VMM flag 1115E9Ah bit 2 set,
// PageFile_Get_Size_Info (VxDCall 00210007h) answering as many pages as the
// machine has, and the physical pages held to 3FFFFh, so that the two halves
// stay below the 7FFFFh pages at which KERNEL32 clamps them. Every mapped
// guest page (PE images, stacks, TEBs, VirtualAlloc) counts as a pageable
// commit (1112654h); nothing is locked (1112658h, 11126F8h). The mask at
// 1112760h keeps its initial FFFFFFFFh, so the paging file in use is what the
// commits exceed the physical pages by, as on a Win98 that has not run out of
// commit space yet. The commits are held to physical plus paging file, the
// limit VMM's commit check keeps them under.
bool Application::GuestDemandInfo(unsigned *info)
{
 Win98MemoryStatus::Counters counters{};unsigned mapped=0;
 if(!PlatformMemoryPages(counters.physical,counters.free_physical)||!VirtualMemory::Space::GlobalMappedPages(mapped))return false;
 if(counters.physical>0x3ffff)counters.physical=0x3ffff;if(counters.free_physical>counters.physical)counters.free_physical=counters.physical;
 const unsigned paging=counters.physical;
 counters.commit0=mapped<counters.physical+paging?mapped:counters.physical+paging;counters.paging=true;counters.paging_mask=0xffffffffu;
 struct E{Application&app;unsigned paging;unsigned FreeArena(unsigned){return app.virtual_memory.FreePrivatePages();}unsigned PagingPages(){return paging;}}e{*this,paging};
 Win98MemoryStatus::Demand(e,counters,info);return true;
}
unsigned Application::MemoryStatusApi(const unsigned*a){
 if(!virtual_memory.Access(a[0],32,true)){last_error=87;return 0;}
 unsigned info[14]{},out[8]{};
 if(!GuestDemandInfo(info)||!Win98MemoryStatus::Status(info,out)){last_error=8;return 0;}
 Store(a[0],out,sizeof out);return 0; // Original API is void.
}
// DPMI 0500h, Get Free Memory Information, as VMM Windows 98 answers it for
// the System VM from _PageGetAllocInfo and _GetDemandPageInfo:
// +00h the largest free block in bytes and +04h in pages, the commit room
// less an eighth, at most twice the physical pages and the free linear space
// (NEO98's stand-in for the DPMI quota of the VM, [EBX+34h]); +08h the most
// lockable pages; +0Ch paging file plus physical pages; +10h the unlocked
// pages; +14h the free pages; +18h the physical pages; +1Ch the free linear
// space, at most the largest block; +20h the paging file in pages (KRNL386
// sets WF_PAGING from it ); the rest FFFFFFFFh.
bool Application::DpmiFreeMemoryInfo(unsigned *info)
{
 unsigned demand[14]{};if(!GuestDemandInfo(demand))return false;
 const unsigned physical=demand[1],free_physical=demand[2],paging=demand[11];
 const unsigned commit_room=physical+paging-demand[13],quota=virtual_memory.FreePrivatePages();
 unsigned largest=commit_room-(commit_room>>3);if(largest>physical*2)largest=physical*2;if(largest>quota)largest=quota;
 unsigned lockable=free_physical;if(lockable>quota)lockable=quota;
 const unsigned free_linear=demand[5]<largest?demand[5]:largest;
 info[0]=largest<<12;info[1]=largest;info[2]=lockable;info[3]=paging+physical;info[4]=demand[3];
 info[5]=free_physical;info[6]=physical;info[7]=free_linear;info[8]=paging;
 info[9]=info[10]=info[11]=0xffffffffu;
 return true;
}
