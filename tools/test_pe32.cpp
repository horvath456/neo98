// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/pe32.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static void put(std::vector<unsigned char> &file, unsigned offset, unsigned value, unsigned size = 4)
{ for (unsigned i = 0; i < size; ++i) file[offset+i] = value >> (8*i); }

static std::vector<unsigned char> fixture()
{
    std::vector<unsigned char> file(0x600);
    put(file,0,0x5a4d,2); put(file,60,0x80); put(file,0x80,0x4550);
    put(file,0x84,0x14c,2); put(file,0x86,2,2); put(file,0x94,224,2);
    put(file,0x98,0x10b,2); put(file,0xa8,0x1000); put(file,0xb4,0x400000);
    put(file,0xb8,0x1000); put(file,0xbc,0x200); put(file,0xd0,0x3000); put(file,0xd4,0x200);
    put(file,0xc8,0x1234,2); put(file,0xca,0x5678,2); // Subsystem version, separate from OS version.
    put(file,0xf4,16); put(file,0xf8,0x2000); put(file,0xfc,0x60);
    // Executable read-only code; writable data with a larger zero-filled tail.
    put(file,0x180,0x200); put(file,0x184,0x1000); put(file,0x188,0x200); put(file,0x18c,0x200); put(file,0x19c,0x60000020);
    put(file,0x1a8,0x800); put(file,0x1ac,0x2000); put(file,0x1b0,0x200); put(file,0x1b4,0x400); put(file,0x1c4,0xc0000040);
    const unsigned char code[] = {0xb8,42,0,0,0,0xc3}; memcpy(file.data()+0x200,code,sizeof code);
    put(file,0x414,1); put(file,0x418,1); put(file,0x41c,0x2040); put(file,0x420,0x2044); put(file,0x424,0x2048);
    put(file,0x440,0x1000); put(file,0x444,0x2050); memcpy(file.data()+0x450,"Answer",7);
    put(file,0x100,0x2080);put(file,0x104,40);
    put(file,0x480,0x20c0);put(file,0x48c,0x20e0);put(file,0x490,0x20d0);
    put(file,0x4c0,0x20f0);put(file,0x4c4,0x80000000);memcpy(file.data()+0x4e0,"TEST.DLL",9);memcpy(file.data()+0x4f2,"Imported",9);
    put(file,0x108,0x2100);put(file,0x10c,0x100);
    put(file,0x50e,1,2);put(file,0x510,6);put(file,0x514,0x80000020);
    put(file,0x52e,1,2);put(file,0x530,1);put(file,0x534,0x80000040);
    put(file,0x54e,1,2);put(file,0x550,0x409);put(file,0x554,0x60);
    put(file,0x560,0x2180);put(file,0x564,5);memcpy(file.data()+0x580,"hello",5);
    return file;
}

struct Memory final : Pe32::AddressSpace {
    std::vector<unsigned char> bytes;
    unsigned permissions[3] = {}, base = 0;
    bool Map(unsigned address,unsigned size,unsigned flags) override {
        if(size!=0x3000) return false;
        base=address;
        bytes.assign(size,0); for(auto &p:permissions)p=flags; return true;
    }
    bool Store(unsigned address,const void *data,Pe32::Size size) override {
        if(address<base || address-base>bytes.size() || size>bytes.size()-(address-base)) return false;
        memcpy(bytes.data()+address-base,data,size); return true;
    }
    bool Fetch(unsigned address,void *data,unsigned size) override {
        if(address<base || address-base>bytes.size() || size>bytes.size()-(address-base))return false;
        memcpy(data,bytes.data()+address-base,size);return true;
    }
    unsigned Word(unsigned offset,unsigned size=4) const {
        unsigned result=0;for(unsigned i=0;i<size;++i)result|=unsigned(bytes[offset+i])<<(8*i);return result;
    }
    bool Protect(unsigned address,unsigned size,unsigned flags) override {
        if(address<base || (address&4095) || (size&4095) || address-base>bytes.size() || size>bytes.size()-(address-base))return false;
        for(unsigned page=(address-base)/4096;size;size-=4096)permissions[page++]=flags;
        return true;
    }
};

static void relocated()
{
    auto file=fixture();
    // Strip unrelated tables. Put one relocation block in the data section.
    put(file,0x100,0);put(file,0x104,0);
    put(file,0x120,0x21a0);put(file,0x124,24);put(file,0x96,0x2000,2);
    put(file,0x5a0,0x1000);put(file,0x5a4,24);
    const unsigned entries[]={0x3010,0x1020,0x2022,0x4024,0xffff,0x3250,0,0};
    for(unsigned i=0;i<8;++i)put(file,0x5a8+i*2,entries[i],2);
    put(file,0x210,0x401100);put(file,0x220,0x40,2);put(file,0x222,0xfffe,2);put(file,0x224,0x41,2);
    const auto original=file;
    Pe32::Image image;Memory memory;
    for(unsigned base:{0x400000u,0x410000u,0x300000u,0x80000000u}) {
        assert(image.Parse(file.data(),file.size())&&image.CanRelocate()&&image.IsDll()&&!image.HasTls());
        assert(image.Load(memory,base));assert(image.Base()==base&&image.PreferredBase()==0x400000&&image.Entry()==base+0x1000);
        assert(memory.Word(0x1010)==base+0x1100);
        assert(memory.Word(0x1020,2)==base>>16);
        assert(memory.Word(0x1022,2)==0xfffe);
        assert(memory.Word(0x1024,2)==((base>>16)+1)%65536);
        assert(memory.Word(0x1250)==base-0x400000); // Relocation into zero-filled section tail.
        // Relocation writes precede the DLL section's final read-only protection.
        assert(memory.permissions[1]==(Pe32::Read|Pe32::Execute));
        assert(image.Export("Answer")==base+0x1000);
        assert(file==original);
    }
    // Multiple records targeting the same location apply in file order.
    put(file,0x5aa,0x3010,2);assert(image.Parse(file.data(),file.size())&&image.Load(memory,0x410000));
    assert(memory.Word(0x1010)==0x421100);
    file=original;put(file,0x5ae,0x4024,2);put(file,0x5b0,0x8001,2);
    assert(image.Parse(file.data(),file.size())&&image.Load(memory,0x410000));assert(memory.Word(0x1024,2)==0x42);
    // HIGHADJ at the final slot cannot consume an absent signed addend.
    file=original;put(file,0x5b6,0x4024,2);assert(image.Parse(file.data(),file.size())&&!image.Load(memory,0x410000));
    for(unsigned type=5;type<16;++type) {
        file=original;put(file,0x5a8,(type<<12)|0x10,2);
        assert(image.Parse(file.data(),file.size())&&!image.Load(memory,0x410000));
    }
    for(unsigned size:{0u,7u,9u,26u,0xffffffffu}) {
        file=original;put(file,0x5a4,size);assert(image.Parse(file.data(),file.size())&&!image.Load(memory,0x410000));
    }
    file=original;put(file,0x5a0,0xfffff000);assert(image.Parse(file.data(),file.size())&&!image.Load(memory,0x410000));
    file=original;put(file,0x96,0x2001,2);assert(image.Parse(file.data(),file.size())&&!image.CanRelocate()&&!image.Load(memory,0x410000));
    file=original;put(file,0x120,0);put(file,0x124,0);assert(image.Parse(file.data(),file.size())&&!image.Load(memory,0x410000));
    file=original;assert(image.Parse(file.data(),file.size())&&!image.Load(memory,0x410001));
    put(file,0xa8,0);assert(image.Parse(file.data(),file.size())&&!image.Entry());
    // Forwarders and ordinal-only lookup retain a distinct result type.
    file=fixture();put(file,0x410,77);put(file,0x440,0x2058);memcpy(file.data()+0x458,"X.#19",6);
    Pe32::Image::Symbol symbol;
    assert(image.Parse(file.data(),file.size())&&image.FindExport(nullptr,77,symbol));
    assert(!symbol.address&&!strcmp(symbol.forwarder,"X.#19")&&!image.Export("Answer"));
    assert(!image.FindExport(nullptr,76,symbol)&&!image.FindExport(nullptr,78,symbol));
    put(file,0x440,0x1000);assert(image.Parse(file.data(),file.size())&&image.FindExport(nullptr,77,symbol)&&symbol.address==0x401000);
}

static void resource_names()
{
    auto file = fixture();
    put(file, 0x50c, 1, 2); put(file, 0x50e, 0, 2); put(file, 0x510, 0x80000080);
    put(file, 0x52c, 1, 2); put(file, 0x52e, 0, 2); put(file, 0x530, 0x800000a0);
    put(file, 0x54e, 2, 2); put(file, 0x550, 0x407); put(file, 0x554, 0x70);
    put(file, 0x558, 0x409); put(file, 0x55c, 0x60);
    put(file, 0x560, 0x21e0); put(file, 0x564, 5); put(file, 0x568, 1252);
    put(file, 0x570, 0x21e8); put(file, 0x574, 3); put(file, 0x578, 437);
    put(file, 0x580, 4, 2);
    const unsigned short type[] = {'T','Y','P','E'}, name[] = {'A',0x20ac}, lower[] = {'t','y','p','e'};
    for (unsigned i = 0; i < 4; ++i) put(file, 0x582 + i * 2, type[i], 2);
    put(file, 0x5a0, 2, 2); put(file, 0x5a2, name[0], 2); put(file, 0x5a4, name[1], 2);
    memcpy(file.data() + 0x5e0, "hello", 5); memcpy(file.data() + 0x5e8, "bye", 3);
    Pe32::Image image; Pe32::Image::ResourceEntry entry;
    const Pe32::Image::ResourceKey tk{0,type,4}, nk{0,name,2};
    unsigned error = 0;
    assert(image.Parse(file.data(), file.size()));
    assert(image.FindResource(tk, nk, 0x409, entry, &error) && !error && entry.entry == 0x2160 &&
           entry.data == 0x21e0 && entry.size == 5 && entry.code_page == 1252 && entry.language == 0x409);
    assert(image.FindResource(tk, nk, ~0u, entry) && entry.language == 0x407 && entry.size == 3);
    // KERNEL32 compares resource names without regard to case (src/pe32.cpp).
    assert(image.FindResource({0,lower,4}, nk, ~0u, entry, &error) && !error && entry.language == 0x407);
    const unsigned short other[] = {'t','y','p','o'};
    assert(!image.FindResource({0,other,4}, nk, ~0u, entry, &error) && error == 1813);
    assert(!image.FindResource(tk, {999}, ~0u, entry, &error) && error == 1814);
    assert(!image.FindResource(tk, nk, 0x411, entry, &error) && error == 1815);
    const auto good = file;
    const unsigned positions[] = {0x510, 0x514, 0x534, 0x55c, 0x560, 0x564, 0x580, 0x5a0};
    const unsigned bad[] = {0xffffffff, 0x20, 0x40, 0x80000060, 0xffffffff, 0xffffffff, 65535, 65535};
    for (unsigned i = 0; i < 8; ++i)
    {
        file = good; put(file, positions[i], bad[i], i >= 6 ? 2 : 4);
        assert(image.Parse(file.data(), file.size()) && !image.FindResource(tk, nk, 0x409, entry));
    }
    unsigned random = 0x98abcd;
    for (unsigned i = 0; i < 2000; ++i)
    {
        file = good; random = random * 1664525 + 1013904223;
        put(file, 0x500 + (random % 0xfc), random);
        if (image.Parse(file.data(), file.size())) image.FindResource(tk, nk, 0x409, entry);
    }
}
// A packer keeps the resource tree in the file and leaves the data in a part
// of a section the file does not carry (UPX: the entries point into UPX0).
// The image then reads the data out of the address space it was loaded into,
// fresh on every request, and not at all before it is loaded.
static void unpacked_resource_data()
{
    auto file=fixture();
    put(file,0x560,0x2300); // data entry: inside the section's virtual size, beyond its raw bytes
    Memory memory; unsigned size=0;
    {
        Pe32::Image unloaded;
        assert(unloaded.Parse(file.data(),file.size()));
        assert(!unloaded.Resource(6,1,&size));
    }
    Pe32::Image image;
    assert(image.Parse(file.data(),file.size()) && image.Load(memory));
    assert(memory.Store(0x402300,"WORLD",5));
    const auto *first=image.Resource(6,1,&size);
    assert(first && size==5 && !memcmp(first,"WORLD",5));
    assert(memory.Store(0x402300,"again",5));
    const auto *second=image.Resource(6,1,&size);
    assert(second==first && !memcmp(second,"again",5));
    put(file,0x560,0x2900); // beyond the section's virtual size: no such bytes anywhere
    Pe32::Image outside; Memory other;
    assert(outside.Parse(file.data(),file.size()) && outside.Load(other) && !outside.Resource(6,1,&size));
}
// A section without file bytes: PointerToRawData 0 with a SizeOfRawData and
// a VirtualSize of 0, as the Watcom linker writes.bss. It is zero in memory
// and has no file offset; the MZ header at offset 0 must not end up there
// (Need for Speed II SE).
static void uninitialised()
{
    auto file = fixture();
    put(file, 0x180, 0); put(file, 0x18c, 0);   // the code section: VirtualSize 0, PointerToRawData 0
    Pe32::Image image; Memory memory;
    assert(image.Parse(file.data(), file.size()) && image.Load(memory));
    for (unsigned i = 0x1000; i < 0x1200; ++i) assert(memory.bytes[i] == 0);
    assert(image.FileOffset(0x1000) == ~0u);
    // It still has its size: its pages carry the rights of a code section.
    assert(memory.permissions[1] == (Pe32::Read | Pe32::Execute));
}


// a 64-KiB section boundary must not widen 4-KiB protection ranges.
static void large_alignment()
{
    struct LargeMemory : Pe32::AddressSpace {
        std::vector<unsigned char> bytes;
        std::vector<unsigned> rights;
        std::vector<bool> committed;
        unsigned relaxed = 0;
        bool Map(unsigned base,unsigned size,unsigned flags) override {
            assert(base==0x400000); bytes.assign(size,0); rights.assign(size/4096,flags); committed.assign(size/4096,true); return true;
        }
        bool Store(unsigned at,const void *data,Pe32::Size size) override {
            assert(at>=0x400000 && at-0x400000+size<=bytes.size());
            memcpy(bytes.data()+at-0x400000,data,size);return true;
        }
        bool Protect(unsigned at,unsigned size,unsigned flags) override {
            assert(!(at&4095)&&!(size&4095));
            for(unsigned p=(at-0x400000)/4096;size;size-=4096) {rights.at(p)=flags;committed.at(p++)=true;}
            return true;
        }
        bool ReserveImage(unsigned at,unsigned size) override {
            if (!Protect(at,size,0)) return false;
            for(unsigned p=(at-0x400000)/4096;size;size-=4096) committed.at(p++)=false;
            return true;
        }
        unsigned ImageDataPageSize() const override {return 16384;}
        bool RelaxImageData(unsigned,unsigned) override {++relaxed;return true;}
    } memory;
    auto file=fixture();
    put(file,0xb8,65536);put(file,0xd0,0x30000);put(file,0xa8,0x10000);
    put(file,0xf4,0); // no directories in this mapping fixture
    put(file,0x184,0x10000);put(file,0x1ac,0x20000);put(file,0x1a8,0x5000);
    Pe32::Image image; assert(image.Parse(file.data(),file.size())&&image.Load(memory));
    assert(image.Entry()==0x410000);
    assert(!memcmp(memory.bytes.data()+0x10000,file.data()+0x200,512));
    assert(!memcmp(memory.bytes.data()+0x20000,file.data()+0x400,512));
    for(unsigned p=0;p<48;++p) {
        unsigned expected=p==0?Pe32::Read:p==16?(Pe32::Read|Pe32::Execute):
            p>=32&&p<37?(Pe32::Read|Pe32::Write):0;
        assert(memory.rights[p]==expected);
        assert(memory.committed[p]==bool(expected));
    }
    for(unsigned at=0x20200;at<0x25000;++at) assert(memory.bytes[at]==0);
    assert(memory.relaxed==0); // no mixed RO/RW data page; holes never qualify
    for(unsigned alignment:{0u,512u,4095u}) {
        put(file,0xb8,alignment);assert(!image.Parse(file.data(),file.size()));
    }
}

int main()
{
    large_alignment();
    relocated();
    uninitialised();
    resource_names();
    unpacked_resource_data();
    auto file=fixture(); const auto original=file; Pe32::Image image; Memory memory;
    assert(image.Parse(file.data(),file.size())); assert(image.Base()==0x400000 && image.Entry()==0x401000 && image.SubsystemVersion()==0x12345678);
    assert(image.Export("Answer")==0x401000 && !image.Export("Missing"));
    assert(image.Load(memory)); assert(memory.permissions[0]==Pe32::Read);
    unsigned resource_size=0;const auto *resource=image.Resource(6,1,&resource_size);
    assert(resource && resource_size==5 && !memcmp(resource,"hello",5));assert(!image.Resource(6,2,&resource_size) && !resource_size);
    unsigned resolved=0;
    auto resolver=[](void *context,const char *dll,const char *name,unsigned ordinal,unsigned *address){
        auto &count=*static_cast<unsigned*>(context);assert(!strcmp(dll,"TEST.DLL"));
        if(!count)assert(name && !strcmp(name,"Imported"));else assert(!name && ordinal==0);
        *address=0x70000000+16*count++;return true;};
    assert(image.BindImports(memory,resolver,&resolved)&&resolved==2);
    unsigned first=0,second=0;memcpy(&first,memory.bytes.data()+0x20d0,4);memcpy(&second,memory.bytes.data()+0x20d4,4);
    assert(first==0x70000000 && second==0x70000010);
    // Code is read-only; the data section explicitly carries MEM_WRITE.
    assert(memory.permissions[1]==(Pe32::Read|Pe32::Execute)); assert(memory.permissions[2]==(Pe32::Read|Pe32::Write));
    assert(!memcmp(memory.bytes.data()+0x1000,file.data()+0x200,512));
    for(unsigned i=0x2200;i<0x3000;++i)assert(memory.bytes[i]==0);
    assert(file==original);
    file=original;put(file,0x96,0x2002,2);
    assert(image.Parse(file.data(),file.size())&&image.Load(memory));
    assert(memory.permissions[0]==Pe32::Read);
    assert(memory.permissions[1]==(Pe32::Read|Pe32::Execute));
    assert(memory.permissions[2]==(Pe32::Read|Pe32::Write));
    file=original;
    for(unsigned length=0;length<file.size();++length){
        const bool parsed=image.Parse(file.data(),length);
        assert(parsed==(length>=0x400)); // the final section may end at EOF
        if(parsed){
            Memory truncated;assert(image.Load(truncated));
            for(unsigned i=length-0x400;i<0x800;++i)assert(truncated.bytes[0x2000+i]==0);
        }
    }
    file=original;put(file,0xd0,0x2fff);
    assert(image.Parse(file.data(),file.size())&&image.SizeOfImage()==0x3000&&image.Load(memory));
    file=original;put(file,0x86,0,2);put(file,0xa8,0);put(file,0xf4,0);
    assert(image.Parse(file.data(),0x178)&&image.Load(memory)); // header extends beyond EOF, no sections
    for(unsigned i=0x178;i<0x3000;++i)assert(memory.bytes[i]==0);
    const unsigned offsets[]={0x84,0x98,0xb4,0xb8,0xd0,0xd4,0x184,0x18c,0x1ac,0x1b0};
    const unsigned values[]={0x8664,0x20b,0x400001,0x200,0xfffff000,0xffffffff,0x1001,0xfffffff0,0x1000,0xffffffff};
    for(unsigned i=0;i<sizeof offsets/sizeof offsets[0];++i){file=original;put(file,offsets[i],values[i]);assert(!image.Parse(file.data(),file.size()));}
    file=original;put(file,0x440,0x2020);assert(image.Parse(file.data(),file.size()));assert(!image.Export("Answer"));
    file=original;put(file,0x448,1,2);assert(image.Parse(file.data(),file.size()));assert(!image.Export("Answer"));
    file=original;put(file,0x420,0xfffffffe);assert(image.Parse(file.data(),file.size()));assert(!image.Export("Answer"));
    unsigned random=0x98;
    for(unsigned i=0;i<3000;++i){
        file=original;
        for(unsigned j=0;j<4;++j){random=random*1664525u+1013904223u;unsigned offset=random%file.size();random=random*1664525u+1013904223u;file[offset]^=random>>24;}
        if(image.Parse(file.data(),file.size())){
            image.Export("Answer");image.Export("Missing");image.Load(memory);image.Resource(6,1,&resource_size);
            image.BindImports(memory,[](void*,const char*,const char*,unsigned,unsigned *a){*a=0x70000000;return true;},nullptr);
        }
    }
    puts("PE32 PASS: relocation types 0-4, collisions, wraparound, duplicate records, rejected malformed blocks, ordinal/forwarded exports, sections, permissions, BSS, exports, named and ordinal imports, resources, truncation, invalid layouts, 3000 malformed inputs");
}
