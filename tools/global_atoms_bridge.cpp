// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_global_atoms.h"
#include "../src/win98_local_heap.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using U = unsigned;
struct Table : Win98LocalHeap::Environment
{
    unsigned char bytes[65536]{};
    std::vector<U> events;
    struct Atoms final : Win98Atoms::Environment
    {
        Table &table;
        Atoms(Table &t, const unsigned char *upper) : Environment(t.bytes, upper), table(t) {}
        U Allocate(U flags, U bytes) override { return Win98LocalHeap::Allocate(table, flags, bytes); }
        void Free(U p) override { Win98LocalHeap::Free(table, p); }
        bool Valid(U p) const override
        {
            if (p < 0x1004 || p >= 0xb000 || (p & 3) || p == Word(6) || p == Word(8))
                return false;
            const U root = Word(6), tail = Word(root + 10);
            for (U block = Word(root + 6), i = 0; block < tail && i < 16384; ++i)
            {
                const U next = Word(block + 2);
                if (next <= block || next > tail)
                    return false;
                if (block + 4 == p)
                    return (Word(block) & 3) == 1 && Byte(p + 4) && Byte(p + 4) + 10 <= next - block;
                block = next;
            }
            return false;
        }
    } atoms;
    explicit Table(const unsigned char *upper) : Environment(bytes), atoms(*this, upper)
    {
        assert(Win98LocalHeap::Initialize(*this, 0x1000, 0xb000));
    }
    U Compact(U bytes, U &block) override
    {
        block = 0;
        events.insert(events.end(), {1, bytes, 0});
        return 0;
    }
    U Notify(U reason, U handle, U bytes) override
    {
        events.insert(events.end(), {2, reason, handle, bytes});
        return 0;
    }
    void Error(U error) override { events.insert(events.end(), {3, error}); }
};
struct Tables
{
    unsigned char upper[128];
    Table *table[3];
    bool present = true;
    explicit Tables(const unsigned char *mapping)
    {
        memcpy(upper, mapping, 128);
        for (auto &t : table)
            t = new Table(upper);
    }
    ~Tables()
    {
        for (auto *t : table)
            delete t;
    }
    Win98Atoms::Environment *Environment(U domain)
    {
        assert(domain < 3);
        return domain == 0 && !present ? nullptr : &table[domain]->atoms;
    }
};
extern "C"
{
    Tables *global_new(const unsigned char *upper) { return new Tables(upper); }
    void global_destroy(Tables *t) { delete t; }
    U global_search(Tables *t, U domain, U source, const unsigned char *name, U add)
    {
        return domain == 2 && add ? Win98GlobalAtoms::Register(t->Environment(domain), source, name)
                                 : Win98GlobalAtoms::Search(t->Environment(domain), source, name, add);
    }
    U global_delete(Tables *t, U domain, U atom) { return Win98GlobalAtoms::Delete(t->Environment(domain), atom); }
    U global_name(Tables *t, U domain, U atom, unsigned char *out, U size)
    {
        return Win98GlobalAtoms::Name(t->Environment(domain), atom, out, size);
    }
    const unsigned char *global_data(Tables *t, U domain) { return t->table[domain]->bytes; }
    const U *global_events(Tables *t, U domain, U *count)
    {
        *count = t->table[domain]->events.size();
        return t->table[domain]->events.data();
    }
    void global_word(Tables *t, U domain, U at, U value) { t->table[domain]->W(at, value); }
    void global_present(Tables *t, U value) { t->present = value != 0; }
    U global_wide(U *error)
    {
        struct E
        {
            U *error;
            void Error(U n) { *error = n; }
        } e{error};
        return Win98GlobalAtoms::UnsupportedWide(e);
    }
}
#ifdef GLOBAL_ATOMS_MAIN
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    unsigned char upper[128];
    assert(fread(upper, 1, sizeof upper, file) == sizeof upper);
    Tables tables(upper);
    unsigned count = 0, record[5];
    while (fread(record, sizeof record, 1, file) == 1)
    {
        const U kind = record[0], domain = record[1], value = record[2], size = record[3], expected = record[4];
        std::vector<unsigned char> data(size + 1);
        assert(fread(data.data(), 1, size, file) == size);
        U result = 0;
        if (kind <= 1)
            result = global_search(&tables, domain, value, data.data(), !kind);
        else if (kind == 2)
            result = global_delete(&tables, domain, value);
        else if (kind == 3)
        {
            assert(size == 304);
            U capacity;
            memcpy(&capacity, data.data(), 4);
            unsigned char output[300];
            memset(output, 0xa5, sizeof output);
            result = global_name(&tables, domain, value, output, capacity);
            assert(memcmp(output, data.data() + 4, sizeof output) == 0);
        }
        else if (kind == 4)
        {
            assert(size == 4);
            U word;
            memcpy(&word, data.data(), 4);
            global_word(&tables, domain, value, word);
        }
        else
        {
            assert(kind == 5);
            global_present(&tables, value);
        }
        assert(result == expected);
        ++count;
    }
    assert(feof(file));
    fclose(file);
    printf("GLOBAL ATOMS ASan/UBSan PASS: %u original oracle operations\n", count);
}
#endif
