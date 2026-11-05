// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_properties.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using U = unsigned;
namespace
{
struct Environment
{
    unsigned char memory[65536]{};
    std::vector<U> trace;
    U refs[16]{}, mode = 0, next = 0x6000;
    struct Block { U address, size; };
    std::vector<Block> blocks;
    Environment()
    {
        Write16(0x386, 0xa001); Write16(0x388, 0xa002); Write16(0x38a, 0xa003);
    }
    void Check(U p, U n) { assert(p <= sizeof memory && n <= sizeof memory - p); }
    U Read16(U p) { Check(p, 2); return memory[p] | (U(memory[p + 1]) << 8); }
    U Read32(U p) { return Read16(p) | (Read16(p + 2) << 16); }
    void Write16(U p, U n) { Check(p, 2); memory[p] = n; memory[p + 1] = n >> 8; }
    void Write32(U p, U n) { Write16(p, n); Write16(p + 2, n >> 16); }
    U PropertyList(U w) { return Read16(w + 0x44); }
    void SetPropertyList(U w, U p) { Write16(w + 0x44, p); }
    void Event(U op, U a = 0, U b = 0, U c = 0) { trace.insert(trace.end(), {op, a, b, c}); }
    bool Fail(U bit) { if (!(mode & bit)) return false; mode &= ~bit; return true; }
    U Allocate(U flags, U bytes)
    {
        const bool failed = Fail(1);
        const U result = failed || next + bytes > 0xf000 ? 0 : next;
        Event(1, flags, bytes, result);
        if (!result) return 0;
        memset(memory + result, flags & 0x40 ? 0 : 0xcc, bytes);
        next = (result + bytes + 3) & ~3u;
        blocks.push_back({result, bytes});
        return result;
    }
    U Reallocate(U p, U bytes, U flags)
    {
        assert(flags == 2);
        for (auto &block : blocks)
            if (block.address == p)
            {
                const bool failed = Fail(2);
                const U result = failed || (bytes > block.size && next + bytes > 0xf000)
                                     ? 0 : bytes > block.size ? next : p;
                Event(2, p, bytes, result);
                if (!result) return 0;
                if (result != p)
                {
                    memset(memory + result, 0xcc, bytes);
                    memcpy(memory + result, memory + p, block.size);
                    memset(memory + p, 0xdd, block.size);
                    next = (result + bytes + 3) & ~3u;
                }
                else if (bytes < block.size) memset(memory + p + bytes, 0xdd, block.size - bytes);
                block = {result, bytes};
                return result;
            }
        assert(false); return 0;
    }
    U Free(U p)
    {
        Event(3, p);
        for (auto it = blocks.begin(); it != blocks.end(); ++it)
            if (it->address == p)
            {
                memset(memory + p, 0xdd, it->size); blocks.erase(it); return 0;
            }
        assert(false); return 0;
    }
    void Move(U destination, U source, U bytes)
    {
        Event(4, destination, source, bytes);
        Check(source, bytes); Check(destination, bytes);
        memmove(memory + destination, memory + source, bytes);
    }
    U Key(U name)
    {
        assert(name >> 16 == 37 * 8);
        const U index = ((name & 0xffff) - 0x1000) / 0x100;
        assert(index < 16);
        return index == 1 ? 0 : index;
    }
    U FindAtom(U name)
    {
        const U key = Key(name);
        const U atom = key == 14 ? 7 : key == 15 ? 0 : refs[key] ? 0xc000 + key : 0;
        Event(5, name, atom); return atom;
    }
    U AddAtom(U name)
    {
        const U key = Key(name);
        U atom = key == 14 ? 7 : key == 15 ? 0 : 0xc000 + key;
        if (Fail(4)) atom = 0;
        if (atom >= 0xc000) ++refs[key];
        Event(6, name, atom); return atom;
    }
    void DeleteAtom(U atom)
    {
        Event(7, atom);
        if (atom >= 0xc000 && atom < 0xc010 && refs[atom - 0xc000]) --refs[atom - 0xc000];
    }
    void SystemCleanup(U kind, U value) { Event(8, kind, value); }
    U Run(U op, U window, U name, U string, U value, U flags, U failure)
    {
        mode = failure;
        Win98Properties::State state{Read16(0x382), Read16(0x384),
                                     {Read16(0x386), Read16(0x388), Read16(0x38a)}};
        U result = 0;
        if (op == 0) result = Win98Properties::Get(*this, state, window, name, string);
        else if (op == 1) result = Win98Properties::Set(*this, state, window, name, string, value, flags);
        else if (op == 2) result = Win98Properties::Remove(*this, state, window, name, string);
        else if (op == 3) Win98Properties::Destroy(*this, state, window);
        else if (op == 4)
        {
            state.enumerated_list = PropertyList(window);
            state.enumerated_item = state.enumerated_list + 2 + name * 8;
        }
        else if (op == 5) state.enumerated_list = state.enumerated_item = 0;
        else if (op == 6) { assert(name < 16); refs[name] = value; }
        else assert(false);
        Write16(0x382, state.enumerated_list); Write16(0x384, state.enumerated_item);
        return result;
    }
    U Fingerprint() const
    {
        U hash = 2166136261u;
        auto bytes = [&](const void *p, size_t size)
        { for (auto b = static_cast<const unsigned char *>(p); size--; ++b) hash = (hash ^ *b) * 16777619u; };
        bytes(memory, sizeof memory); bytes(refs, sizeof refs);
        bytes(trace.data(), trace.size() * sizeof(U));
        return hash;
    }
};
} // namespace
extern "C"
{
void *properties_new() { return new Environment; }
void properties_delete(void *p) { delete static_cast<Environment *>(p); }
U properties_run(void *p, U op, U window, U name, U string, U value, U flags, U failure)
{ return static_cast<Environment *>(p)->Run(op, window, name, string, value, flags, failure); }
const void *properties_memory(void *p) { return static_cast<Environment *>(p)->memory; }
const U *properties_refs(void *p) { return static_cast<Environment *>(p)->refs; }
const U *properties_trace(void *p, U *size)
{ auto &trace = static_cast<Environment *>(p)->trace; *size = trace.size(); return trace.data(); }
U properties_fingerprint(void *p) { return static_cast<Environment *>(p)->Fingerprint(); }
}
#ifdef PROPERTIES_MAIN
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb"); assert(f);
    U sequences = 0, steps = 0, count;
    while (fread(&count, sizeof count, 1, f) == 1)
    {
        Environment environment;
        ++sequences;
        while (count--)
        {
            U a[9]; assert(fread(a, sizeof a, 1, f) == 1);
            assert(environment.Run(a[0], a[1], a[2], a[3], a[4], a[5], a[6]) == a[7]);
            assert(environment.Fingerprint() == a[8]); ++steps;
        }
    }
    assert(!ferror(f)); fclose(f);
    printf("PROPERTIES ASAN/UBSAN PASS: %u sequences, %u calls\n", sequences, steps);
}
#endif
