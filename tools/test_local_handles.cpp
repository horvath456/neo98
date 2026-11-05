// SPDX-License-Identifier: GPL-2.0-or-later
// Native checks for the implemented KERNEL32 LocalSize and
// ConvertToGlobalHandle decisions in src/win98_local_handles.h.
#include "../src/win98_local_handles.h"
#include "../build/generated/win98_message_table.h"
#include "../src/win98_format_message.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
unsigned failures = 0;
void check(bool ok, const char *what)
{
    if (!ok)
    {
        printf("FAIL %s\n", what);
        ++failures;
    }
}

struct Heap final : Win98LocalHandles::SizeEnvironment
{
    std::map<unsigned, unsigned> blocks;   // pointer -> size
    std::map<unsigned, unsigned> entries;  // handle -> block
    std::map<unsigned, unsigned> signature;
    unsigned error = 0;
    bool readable = true;
    bool Readable(unsigned) override { return readable; }
    bool Entry(unsigned handle, unsigned &sig, unsigned &block) override
    {
        auto found = entries.find(handle);
        if (found == entries.end())
            return false;
        block = found->second;
        sig = signature.count(handle) ? signature[handle] : Win98LocalHandles::MoveableSignature;
        return true;
    }
    bool BlockSize(unsigned pointer, unsigned &size) override
    {
        auto found = blocks.find(pointer);
        if (found == blocks.end())
            return false;
        size = found->second;
        return true;
    }
    void Error(unsigned code) override { error = code; }
};

struct Global final : Win98LocalHandles::GlobalEnvironment
{
    bool shared = true, mix = true;
    unsigned share_result = 0;
    bool SharedProcess() override { return shared; }
    unsigned Share(unsigned) override { return share_result; }
    bool Mix() override { return mix; }
};

void local_size()
{
    Heap heap;
    heap.blocks[0x10000] = 64;
    check(Win98LocalHandles::Size(heap, 0x10000) == 64, "a fixed pointer reports its block size");
    check(heap.error == 0, "a fixed pointer reports no error");

    heap.error = 0;
    check(Win98LocalHandles::Size(heap, 0x10004) == 0, "an unknown block is rejected");
    check(heap.error == Win98LocalHandles::InvalidHandle, "an unknown block reports error 6");

    // A moveable handle carries bit 1 and points at an entry whose signature
    // must match; the block itself starts four bytes behind the data.
    heap.error = 0;
    heap.entries[0x20000] = 0x30004;
    heap.blocks[0x30000] = 128;
    check(Win98LocalHandles::Size(heap, 0x20002) == 124, "a moveable handle drops its header");
    check(heap.error == 0, "a moveable handle reports no error");

    heap.error = 0;
    heap.signature[0x20000] = 0x1234;
    check(Win98LocalHandles::Size(heap, 0x20002) == 0, "a wrong signature is rejected");
    check(heap.error == Win98LocalHandles::InvalidHandle, "a wrong signature reports error 6");

    heap.error = 0;
    heap.signature.erase(0x20000);
    heap.entries[0x20000] = 0;
    check(Win98LocalHandles::Size(heap, 0x20002) == 0, "an empty entry is rejected");

    heap.error = 0;
    heap.readable = false;
    check(Win98LocalHandles::Size(heap, 0x10000) == 0, "an unreadable handle is rejected");
    check(heap.error == Win98LocalHandles::InvalidHandle, "an unreadable handle reports error 6");
}

void convert_to_global()
{
    Global environment;
    check(Win98LocalHandles::ToGlobal(environment, 0x1234) == (0x1234u ^ 0x453a4d3cu),
          "the shared process mixes the handle");
    environment.mix = false;
    check(Win98LocalHandles::ToGlobal(environment, 0x1234) == 0x1234u,
          "a provider without mixing keeps the handle");
    environment.shared = false;
    environment.mix = true;
    check(Win98LocalHandles::ToGlobal(environment, 0x1234) == 0, "a failed share returns zero");
    environment.share_result = 0x99;
    check(Win98LocalHandles::ToGlobal(environment, 0x1234) == (0x99u ^ 0x453a4d3cu),
          "a shared handle is mixed");
}

void format_message_flags()
{
    using namespace Win98FormatMessage;
    check(Width(0) == 0, "width zero keeps the hard breaks");
    check(Width(0x10ff) == 0xffffffffu, "the width 0xff suppresses breaks");
    check(Width(0x1040) == 0x40, "an explicit width survives the mask");
    check(Unwrap(FromSystem, 0x80070032u) == 50, "a wrapped Win32 error is unwrapped");
    check(Unwrap(FromSystem | FromModule, 0x80070032u) == 0x80070032u,
          "two sources keep the HRESULT");
    check(Unwrap(FromSystem, 0x80040154u) == 0x80040154u, "another facility is kept");
    check(Select(FromString | FromSystem) == String, "the string source wins");
    check(Select(FromModule | FromSystem) == Module, "the module source comes before the system");
    check(Select(0) == None, "no source at all is reported");
    check(Retry(FromModule | FromSystem), "both sources allow the fallback");
    check(!Retry(FromModule), "a module alone has no fallback");
    check(IndirectArguments(FromString), "a va_list parameter needs one dereference");
    check(!IndirectArguments(FromString | ArgumentArray), "an argument array is read directly");
}
void message_table()
{
    using namespace Win98FormatMessage;
    // The implemented block walk over the original resource must find every
    // identifier the generator counted with its own decoder.
    unsigned found = 0;
    const unsigned blocks = Read32(Win98MessageTable::Data, 0);
    for (unsigned index = 0; index < blocks; ++index)
    {
        const unsigned base = 4 + index * 12;
        const unsigned low = Read32(Win98MessageTable::Data, base);
        const unsigned high = Read32(Win98MessageTable::Data, base + 4);
        for (unsigned id = low; id <= high; ++id)
        {
            Entry entry;
            if (Lookup(Win98MessageTable::Data, Win98MessageTable::Bytes, id, entry))
                ++found;
        }
    }
    printf("message table blocks %u identifiers %u\n", blocks, found);
    check(blocks == 111, "the original table has 111 blocks");
    check(found == 922, "every identifier of the original table resolves");
    Entry entry;
    check(Lookup(Win98MessageTable::Data, Win98MessageTable::Bytes, 50, entry),
          "message 50 is present");
    check(!entry.wide, "message 50 is an ANSI entry");
    check(std::string((const char *)entry.text) == "The network request is not supported.\r\n",
          "message 50 matches the original text");
    check(!Lookup(Win98MessageTable::Data, Win98MessageTable::Bytes, 35, entry),
          "a gap between blocks is not found");
}
} // namespace

int main()
{
    local_size();
    convert_to_global();
    format_message_flags();
    message_table();
    if (failures)
    {
        printf("LOCAL HANDLES FAIL %u\n", failures);
        return 1;
    }
    printf("LOCAL HANDLES PASS\n");
    return 0;
}
