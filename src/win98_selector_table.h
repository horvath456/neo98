// SPDX-License-Identifier: GPL-2.0-or-later
// Native LDT storage for implemented KERNEL32 mapping routines.
// KERNEL32 /0x1ebf create a 64-KiB, present ring-3 data
// descriptor with the exact flat pointer as base and return selector:0.
// adds the offset, rejecting GDT and absent/system descriptors.
#pragma once
// PLAN.md step 9 turns this table into the LDT the CPU really loads. The
// entries of the flat thunk edge keep their shape: a 64-KiB read/write data
// descriptor at a flat base. A segment of a 16-bit module says so, and then
// the descriptor carries its own limit and its code or data type.
namespace Win98Selectors {
struct Table {
    // The upper half is reserved for module aliases. No x86 segment executes.
    static constexpr unsigned Capacity=4096;
    // `present` is the P bit of the descriptor. KRNL386 clears it for a block
    // of the global heap that carries no memory: GlobalAlloc builds the
    // descriptor of a zero-length moveable block with "and al,7Fh",
    // and the block lookup reads exactly that bit back to tell a
    // discarded block from a live one.
    struct Entry { unsigned base=0, next=0, limit=65535; unsigned char locks=0; bool used=false, arena=false, code=false, big=false, present=true; } entries[Capacity];
    // The first descriptors stay unused, so every selector the table hands
    // out is at least 27h. A Win16 handle below 32 is an error code by
    // contract (LoadLibrary, WinExec, LoadModule answer 0 to 31 for a
    // failure, and SHELL32 checks LoadLibrary16 against 20h); under
    // Win98 the low entries of the one LDT belong to the system long before
    // a program runs. With the first free entry at one, a library placed
    // early in a 32-bit process got selector 0Fh or 17h and its load read as
    // a failure.
    static constexpr unsigned Reserved=4;
    unsigned first=Reserved, free=0;
    unsigned Map(unsigned pointer, bool arena=false) {
        if(pointer<=0xffff)return pointer;
        return Create(pointer,arena);
    }
    unsigned Create(unsigned pointer,bool arena=false) {
        unsigned i=free;
        if(i)free=entries[i].next;
        else {if(first==Capacity)return 0;i=first++;}
        entries[i]={pointer,0,65535,0,true,arena,false,false};
        return ((i*8+7)<<16);
    }
    // A descriptor of a 16-bit module segment. Returns the selector value
    // itself, not a segmented pointer, because the caller loads it into a
    // segment register. Zero when the table is full.
    unsigned CreateSegment(unsigned base,unsigned limit,bool code,bool big=false) {
        const unsigned pointer=Create(base);
        if(!pointer)return 0;
        auto &e=entries[(pointer>>16)>>3];
        e.limit=limit;e.code=code;e.big=big;
        return pointer>>16;
    }
    // Allocate consecutive descriptors with access byte 0xF3 and selector
    // spacing eight, as required by __AHINCR. The free list cannot guarantee
    // contiguity.
    unsigned CreateRun(unsigned count) {
        if(!count||first+count>Capacity)return 0;
        const unsigned index=first;first+=count;
        for(unsigned i=0;i<count;++i)entries[index+i]={0,0,65535,0,true,false,false,false};
        return index*8+7;
    }
    // Extend a sequence only if the following slots are free. Otherwise the
    // caller must allocate a replacement. Return the unchanged first selector
    // or zero.
    unsigned ExtendRun(unsigned selector,unsigned have,unsigned want) {
        const unsigned index=selector>>3;
        if(!index||!have||want<=have)return 0;
        if(index+have!=first||first+(want-have)>Capacity)return 0;
        for(unsigned i=have;i<want;++i)entries[index+i]={0,0,65535,0,true,false,false,false};
        first+=want-have;
        return selector;
    }
    const Entry *At(unsigned selector) const {
        const unsigned index=selector>>3;
        return (selector&4)&&index&&index<first&&entries[index].used?&entries[index]:nullptr;
    }
    Entry *Find(unsigned pointer) {
        unsigned selector=pointer>>16,index=selector>>3;
        return (selector&4)&&index&&index<first&&entries[index].used?&entries[index]:nullptr;
    }
    unsigned Linear(unsigned pointer) {
        if(!(pointer>>16))return pointer;
        auto *e=Find(pointer);return e?e->base+(pointer&65535):0;
    }
    void Unmap(unsigned pointer) {
        auto *e=Find(pointer);if(!e)return;
        unsigned i=(pointer>>16)>>3;e->used=false;e->next=free;free=i;
    }
    unsigned Fix(unsigned pointer) {
        auto *e=Find(pointer);if(e&&e->arena&&e->locks!=255)++e->locks;
        return Linear(pointer);
    }
    void Unfix(unsigned pointer) {
        auto *e=Find(pointer);if(e&&e->arena&&e->locks&&e->locks!=255)--e->locks;
    }
    bool Descriptor(unsigned selector,unsigned char (&out)[8]) {
        auto *e=Find(selector<<16);if(!e)return false;
        unsigned base=e->base,limit=e->limit;
        // A limit past 1 MiB only fits as a count of pages (the G bit); DPMI
        // function 0008h demands such a limit to end on a page for this.
        const bool pages=limit>0xfffff;
        if(pages)limit>>=12;
        out[0]=limit;out[1]=limit>>8;out[2]=base;out[3]=base>>8;out[4]=base>>16;
        // Present, ring three, accessed; execute/read for code, read/write
        // for data. The granularity byte carries the top limit bits and the
        // default operand size.
        out[5]=e->code?0xfb:0xf3;
        out[6]=(unsigned char)((limit>>16)&15)|(e->big?0x40:0)|(pages?0x80:0);
        out[7]=base>>24;return true;
    }
};
}
