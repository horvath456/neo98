// SPDX-License-Identifier: GPL-2.0-or-later
unsigned Application::KernelMiscApi(ApiName name,const unsigned *a){
    switch (name.id)
    {
    API_CASE("GetErrorMode")return legacy_error_mode;
    API_CASE("RegisterServiceProcess") API_CASE("GetProcessFlags"){
        auto *target=(!a[0]||a[0]==process_id)?this:static_cast<Application*>(User::ProcessContextOf(a[0]));
        if(API_IS(name,"GetProcessFlags"))return target?target->process_flags:~0u;
        if(!target||a[1]>1)return 0;
        target->process_flags=(target->process_flags&~0x100u)|(a[1]?0x100u:0);return 1;
    }
    API_CASE("GetSystemPowerStatus"){
        if(!virtual_memory.Access(a[0],12,true)){last_error=87;return 0;}
        const auto power = HostDevices::Power();
        return Store(a[0], &power, sizeof power);
    }
    API_CASE("IsSystemResumeAutomatic")return 0; // No guest suspend/resume has occurred.
    API_CASE("SetThreadExecutionState"){
        auto &current=threads[current_thread].execution_state;unsigned previous=current;
        if(a[0]&0x80000000u)current=0x80000000u|(a[0]&3);return previous;
    }
    default:
        break;
    }
    Fail(name);return 0;
}
unsigned Application::HeapExtendedApi(ApiName name,const unsigned *a){
    if(API_IS(name,"LocalCompact"))return 0; // Original.
    if(API_IS(name,"LocalShrink"))return 0x2000000; // Resident native Local/Global heap reservation.
    if((API_IS(name,"GlobalFix")||API_IS(name,"GlobalUnfix"))&&a[0]==~0u)return 0;
    // Public moveable handles and their data pointers share this namespace.
    for (const auto &b : AllocationBlocks()) if (b.used && b.moveable &&
            (a[0] == b.address + 2 || (b.data && a[0] == b.data))) {
        if (API_IS(name,"LocalHandle") || API_IS(name,"GlobalHandle")) return b.address + 2;
        const unsigned handle[] = {b.address + 2};
        return LocalMemoryApi(name, handle);
    }
    bool valid=AllocationSize(a[0])!=0;
    if(!valid&&heap_system&&heap_system->process_heap&&a[0]>=4&&virtual_memory.Access(a[0]-4,12,false)){
        unsigned args[]={heap_system->process_heap,0,a[0]},saved=last_error;
        valid=HeapApi("HeapSize",args)!=0;last_error=saved;
    }
    if(!valid){last_error=6;return API_IS(name,"LocalFlags")?0x8000:0;}
    if(API_IS(name,"LocalFlags")||API_IS(name,"GlobalFix")||API_IS(name,"GlobalUnfix")||API_IS(name,"GlobalUnWire"))return 0;
    // Local/Global allocations in this provider are fixed resident blocks.
    return a[0];
}
