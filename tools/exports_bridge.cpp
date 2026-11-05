// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_exports.h"
struct Bridge final : Win98Exports::Memory
{
    using Reader = bool (*)(unsigned, void *, unsigned);
    Reader reader;
    unsigned error;
    Bridge(Reader r, unsigned e) : reader(r), error(e) {}
    bool Read(unsigned a, void *p, unsigned n) override { return reader(a, p, n); }
    void Error(unsigned e) override { error = e; }
};
extern "C" unsigned find_export(Bridge::Reader r, unsigned base, unsigned directory, unsigned size,
                                const char *name, unsigned ordinal, unsigned hint, unsigned *error)
{
    Bridge memory(r, *error);
    const auto result = Win98Exports::Find(memory, base, directory, size, name, ordinal, hint);
    *error = memory.error;
    return result;
}
extern "C" unsigned module_filename(char *out, const char *path, unsigned length, unsigned capacity)
{
    return Win98Exports::FileName(out, path, length, capacity);
}
