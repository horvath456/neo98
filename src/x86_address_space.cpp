// SPDX-License-Identifier: GPL-2.0-or-later
// CpuBackend::None: guest memory without a processor. See
// src/x86_address_space.h.
#include "x86_address_space.h"
#include <stdlib.h>
#include <string.h>

namespace X86
{
namespace
{
constexpr unsigned PageBits = 12, PageBytes = 1u << PageBits, TablePages = 1024, Tables = 1024;

// The bytes of one Map call. Its pages go back one by one with Unmap; the
// block is freed with its last page.
struct Block
{
    unsigned char *bytes;
    unsigned live;
};
// One guest page: its host bytes, the block that owns them (null for the
// bytes of a MapHost caller) and its rights.
struct Page
{
    unsigned char *host;
    Block *block;
    unsigned prot;
};
struct Table
{
    Page pages[TablePages];
    unsigned used;
};

class AddressSpace final : public Cpu
{
  public:
    ~AddressSpace() override
    {
        for (unsigned t = 0; t < Tables; ++t)
        {
            if (!tables[t]) continue;
            for (auto &page : tables[t]->pages)
                if (page.block) Release(page.block);
            free(tables[t]);
        }
    }
    CpuBackend Backend() const override { return CpuBackend::None; }
    const char *StateRefusal() const override { return nullptr; }
    const char *ContextStateRefusal(const CpuContext *context) const override
    { return context == reinterpret_cast<const CpuContext *>(&empty_context) ? nullptr : "an invalid empty context"; }
    unsigned ContextBytes() override { return 8; }
    bool ContextExport(const CpuContext *context, void *out) override
    {
        if (ContextStateRefusal(context)) return false;
        const unsigned long long tag = 0x31585443454e4f4eull; // NONECTX1
        memcpy(out, &tag, sizeof tag); return true;
    }
    bool ContextImport(CpuContext *context, const void *in) override
    {
        unsigned long long tag = 0; memcpy(&tag, in, sizeof tag);
        return !ContextStateRefusal(context) && tag == 0x31585443454e4f4eull;
    }
    bool Regions(RegionVisitor visitor, void *context) override
    {
        unsigned first = 0, length = 0, prot = 0;
        Block *block = nullptr; unsigned char *backing = nullptr;
        auto flush = [&]() { if (length) visitor(context, first, length, prot, block ? nullptr : backing); };
        Each([&](unsigned address, const Page &page) {
            if (length && (length > 0xffffffffu - PageBytes || static_cast<unsigned long long>(first) + length != address || prot != page.prot ||
                           block != page.block || backing + length != page.host)) { flush(); length = 0; }
            if (!length) { first = address; prot = page.prot; block = page.block; backing = page.host; }
            length += PageBytes;
        });
        flush(); return true;
    }

    // No registers, no run, no hooks: the process has no processor. The
    // contexts are empty, one shared token for all of them.
    unsigned Get(int, unsigned &value) override { value = 0; return CpuError::Arch; }
    unsigned Set(int, unsigned) override { return CpuError::Arch; }
    unsigned GetWide(int, void *) override { return CpuError::Arch; }
    unsigned SetWide(int, const void *) override { return CpuError::Arch; }
    unsigned NewContext(CpuContext *&context) override
    {
        context = reinterpret_cast<CpuContext *>(&empty_context);
        return CpuError::Ok;
    }
    void FreeContext(CpuContext *) override {}
    unsigned Save(CpuContext *) override { return CpuError::Ok; }
    unsigned Restore(const CpuContext *) override { return CpuError::Ok; }
    unsigned ContextGet(const CpuContext *, int, unsigned &value) override { value = 0; return CpuError::Arch; }
    unsigned Run(unsigned, unsigned, unsigned) override { return CpuError::Arch; }
    void Stop() override {}
    void RequestStop() override {}
    void Budget(int) override {}
    unsigned SetGateHook(unsigned, unsigned, GateHook, void *) override { return CpuError::Arch; }
    unsigned SetFaultHook(FaultHook, void *) override { return CpuError::Arch; }
    unsigned SetInterruptHook(InterruptHook, void *) override { return CpuError::Arch; }
    unsigned SetPortHooks(PortInputHook, PortOutputHook, void *) override { return CpuError::Arch; }
    unsigned SetBudgetHook(BudgetHook, void *, bool &counted) override { counted = false; return CpuError::Arch; }
    unsigned WatchWrites(WriteHook, void *) override { return CpuError::Hook; }
    void UnwatchWrites() override {}
    unsigned AddTrace(TraceKind, unsigned, unsigned, TraceHook, void *) override { return CpuError::Hook; }

    // Memory, with the answers Unicorn gives: a range that is already mapped
    // cannot be mapped again, one with a hole cannot be unmapped or protected.
    unsigned Map(unsigned address, unsigned size, unsigned prot) override
    {
        unsigned error = Free(address, size);
        if (error) return error;
        auto *block = static_cast<Block *>(malloc(sizeof(Block)));
        unsigned char *bytes = block ? static_cast<unsigned char *>(calloc(size, 1)) : nullptr;
        if (!bytes) { free(block); return CpuError::NoMemory; }
        block->bytes = bytes;
        block->live = 0;
        error = Enter(address, size, prot, bytes, block);
        if (!block->live) { free(bytes); free(block); }
        return error;
    }
    unsigned MapHost(unsigned address, unsigned size, unsigned prot, void *host) override
    {
        if (!host) return CpuError::Argument;
        const unsigned error = Free(address, size);
        return error ? error : Enter(address, size, prot, static_cast<unsigned char *>(host), nullptr);
    }
    unsigned Unmap(unsigned address, unsigned size) override
    {
        const unsigned error = Mapped(address, size);
        if (error) return error;
        for (unsigned long long at = address; at < (unsigned long long)address + size; at += PageBytes)
        {
            const unsigned number = (unsigned)(at >> PageBits);
            Table *&table = tables[number / TablePages];
            Page &page = table->pages[number % TablePages];
            if (page.block) Release(page.block);
            page = Page{};
            if (!--table->used) { free(table); table = nullptr; }
        }
        return CpuError::Ok;
    }
    unsigned Protect(unsigned address, unsigned size, unsigned prot) override
    {
        const unsigned error = Mapped(address, size);
        if (error) return error;
        for (unsigned long long at = address; at < (unsigned long long)address + size; at += PageBytes)
            Find((unsigned)at)->prot = prot;
        return CpuError::Ok;
    }
    unsigned Read(unsigned address, void *out, unsigned size) override
    {
        return Copy(address, static_cast<unsigned char *>(out), size, false);
    }
    unsigned Write(unsigned address, const void *data, unsigned size) override
    {
        return Copy(address, static_cast<unsigned char *>(const_cast<void *>(data)), size, true);
    }
    // There is no translated code to drop.
    unsigned InvalidateCode(unsigned, unsigned) override { return CpuError::Ok; }
    unsigned FlushCode() override { return CpuError::Ok; }
    // The bytes of a Map call move to a processor when the process gets one
    // (CopyAddressSpace), so nobody may keep a pointer to them.
    void *HostPointer(unsigned, unsigned) override { return nullptr; }

    // Every mapped page in address order, for CopyAddressSpace.
    template <class F> void Each(F visit) const
    {
        for (unsigned t = 0; t < Tables; ++t)
            if (tables[t])
                for (unsigned i = 0; i < TablePages; ++i)
                    if (tables[t]->pages[i].host) visit((t * TablePages + i) << PageBits, tables[t]->pages[i]);
    }

  private:
    Table *tables[Tables] = {};
    static char empty_context;

    Page *Find(unsigned address) const
    {
        const Table *table = tables[address >> (PageBits + 10)];
        if (!table) return nullptr;
        Page *page = const_cast<Page *>(&table->pages[(address >> PageBits) % TablePages]);
        return page->host ? page : nullptr;
    }
    static unsigned Range(unsigned address, unsigned size)
    {
        if (!size || (address & (PageBytes - 1)) || (size & (PageBytes - 1)) ||
            (unsigned long long)address + size > (1ull << 32))
            return CpuError::Argument;
        return CpuError::Ok;
    }
    // Ok when no page of the range is mapped.
    unsigned Free(unsigned address, unsigned size) const
    {
        const unsigned error = Range(address, size);
        if (error) return error;
        for (unsigned long long at = address; at < (unsigned long long)address + size; at += PageBytes)
            if (Find((unsigned)at)) return CpuError::Map;
        return CpuError::Ok;
    }
    // Ok when every page of the range is mapped.
    unsigned Mapped(unsigned address, unsigned size) const
    {
        const unsigned error = Range(address, size);
        if (error) return error;
        for (unsigned long long at = address; at < (unsigned long long)address + size; at += PageBytes)
            if (!Find((unsigned)at)) return CpuError::NoMemory;
        return CpuError::Ok;
    }
    // Enters the pages of a free range. The tables come first, so that
    // nothing is entered when one of them cannot be allocated.
    unsigned Enter(unsigned address, unsigned size, unsigned prot, unsigned char *host, Block *block)
    {
        const unsigned first = address >> PageBits, last = (unsigned)(((unsigned long long)address + size - 1) >> PageBits);
        for (unsigned t = first / TablePages; t <= last / TablePages; ++t)
            if (!tables[t] && !(tables[t] = static_cast<Table *>(calloc(1, sizeof(Table))))) return CpuError::NoMemory;
        for (unsigned number = first; number <= last; ++number)
        {
            Table *table = tables[number / TablePages];
            table->pages[number % TablePages] = Page{host + ((number - first) << PageBits), block, prot};
            ++table->used;
            if (block) ++block->live;
        }
        return CpuError::Ok;
    }
    static void Release(Block *block)
    {
        if (--block->live) return;
        free(block->bytes);
        free(block);
    }
    unsigned Copy(unsigned address, unsigned char *bytes, unsigned size, bool write) const
    {
        while (size)
        {
            Page *page = Find(address);
            if (!page) return write ? CpuError::WriteUnmapped : CpuError::ReadUnmapped;
            const unsigned offset = address & (PageBytes - 1);
            const unsigned chunk = size < PageBytes - offset ? size : PageBytes - offset;
            if (write) memcpy(page->host + offset, bytes, chunk);
            else memcpy(bytes, page->host + offset, chunk);
            bytes += chunk;
            size -= chunk;
            address += chunk;
            if (size && !address) return write ? CpuError::WriteUnmapped : CpuError::ReadUnmapped;
        }
        return CpuError::Ok;
    }
};
char AddressSpace::empty_context;
} // namespace

unsigned CreateAddressSpace(Cpu *&cpu)
{
    cpu = new AddressSpace;
    return cpu ? CpuError::Ok : (unsigned)CpuError::NoMemory;
}

unsigned CopyAddressSpace(Cpu &space, Cpu &target)
{
    if (space.Backend() != CpuBackend::None) return CpuError::Argument;
    auto &from = static_cast<AddressSpace &>(space);
    // Runs of neighbouring pages of the same kind and rights go over as one
    // mapping; caller bytes only while they are neighbours on the host too.
    struct Run
    {
        unsigned begin = 0, size = 0, prot = 0;
        unsigned char *host = nullptr;
        bool owned = false;
    } run;
    unsigned error = CpuError::Ok;
    auto flush = [&]() {
        if (!run.size || error) return;
        if (!run.owned)
            error = target.MapHost(run.begin, run.size, run.prot, run.host);
        else if (!(error = target.Map(run.begin, run.size, run.prot)))
            for (unsigned at = 0; at < run.size && !error; at += PageBytes)
            {
                unsigned char page[PageBytes];
                error = from.Read(run.begin + at, page, PageBytes);
                if (!error) error = target.Write(run.begin + at, page, PageBytes);
            }
        run.size = 0;
    };
    from.Each([&](unsigned address, const Page &page) {
        const bool owned = page.block != nullptr;
        const bool joins = run.size && run.begin + run.size == address && run.prot == page.prot && run.owned == owned &&
                           (owned || run.host + run.size == page.host);
        if (!joins)
        {
            flush();
            run.begin = address;
            run.prot = page.prot;
            run.host = page.host;
            run.owned = owned;
        }
        run.size += PageBytes;
    });
    flush();
    return error;
}
} // namespace X86
