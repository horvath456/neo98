// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only Win98 system tree, layered under the RAM volume of src/files.h.
//
// The loader and the guest file APIs see one C: drive. A lookup that the RAM
// volume cannot answer falls through to the mounted tree; the entry then
// appears in the RAM volume as a copy of the original metadata, and its bytes
// arrive on first read. Every write lands in the RAM volume, so the tree never
// changes. PLAN.md step 4 replaces this layer with a real host file system;
// only the Backend below and the four call sites in src/files.cpp belong to it.
//
// Nothing here uses the host: a backend lives outside src/ (the macOS one is
// tools/system_tree_host.h) and hands over names, metadata and bytes.
#pragma once
#include "files.h"
namespace SystemTree
{
struct Entry
{
    char name[256] = {};   // long name, as Win98 shows it
    char alias[14] = {};   // 8.3 alias, empty when the long name already is one
    unsigned attributes = 0, size = 0;
    Files::TimeStamp times[3];
};
struct Backend
{
    virtual ~Backend() {}
    // `absolute` is a normalized guest path such as "C:\\WINDOWS\\SYSTEM\\USER32.DLL".
    // A component matches its long name or its 8.3 alias, case insensitively.
    virtual bool Find(const char *absolute, Entry &out) = 0;
    virtual bool Child(const char *directory, unsigned index, Entry &out) = 0;
    virtual unsigned Read(const char *absolute, unsigned offset, void *buffer, unsigned size) = 0;
    // Where the tree came from, for logs and reports.
    virtual const char *Description() const = 0;
};
void Mount(Backend *);
Backend *Mounted();
} // namespace SystemTree
