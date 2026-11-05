// SPDX-License-Identifier: GPL-2.0-or-later
// Native Win32 mutex objects in NEO98's single-process handle namespace.
#include "x86_app.h"
#include <string.h>
namespace X86
{
void Application::AbandonMutexes(unsigned thread)
{
    for (auto &m : kernel_mutexes)
        if (m.references && m.owner == thread)
        {
            m.owner = m.recursion = 0;
            m.abandoned = true;
        }
}
unsigned Application::MutexApi(ApiName name, const unsigned *a)
{
    if (API_IS(name,"ReleaseMutex"))
    {
        auto *h = FindKernelHandle(a[0]);
        if (!h || h->kind != 3)
        {
            last_error = 6;
            return 0;
        }
        auto &m = kernel_mutexes[h->object];
        if (m.owner != ThreadId())
        {
            last_error = 288;
            return 0;
        }
        if (!--m.recursion)
            m.owner = 0;
        return 1;
    }
    const bool open = API_IS(name,"OpenMutexA");
    bool inherit = open && a[1];
    // the helper every creating export uses: a null pointer means no
    // attributes, `nLength` has to be twelve, and only `bInheritHandle` is
    // read. The descriptor is never touched, because Win98 carries no security
    // on a kernel object; CreateMutexA probes byte 0 and byte 11 of
    // the structure and nothing else. The original WS2_32 hands one in for
    // `Winsock2ProtocolCatalogMutex`.
    if (!open && a[0])
    {
        unsigned attributes[3];
        if (!Read(a[0], attributes, sizeof attributes))
            return 0;
        if (attributes[0] != 12)
        {
            last_error = 87;
            return 0;
        }
        inherit = attributes[2] != 0;
    }
    if (open && !a[2])
    {
        last_error = 87;
        return 0;
    }
    const char *object_name = ReadString(a[2]);
    if (state == Failed)
        return 0;
    unsigned name_address = a[2];
    // treats an empty name like no name at all.
    if (name_address && !*object_name)
        name_address = 0;
    if (strlen(object_name) > KernelObjectNameLimit)
    {
        last_error = 123;
        return 0;
    }
    // The name lives in the session-wide namespace, not in this process.
    unsigned found = KernelObjectSlots;
    if (name_address)
    {
        const unsigned kind = NamedObjectKind(object_name);
        if (kind && kind != 3)
        { last_error = 6; return 0; }
        if (kind == 3)
            found = FindNamedObject(3, object_name) - 1;
    }
    const bool existing = found != KernelObjectSlots;
    if (!existing)
    {
        if (open)
        {
            // an absent name is ERROR_INVALID_NAME, not ERROR_FILE_NOT_FOUND.
            last_error = 123;
            return 0;
        }
        for (unsigned i = 0; i < KernelObjectSlots; ++i)
            if (!kernel_mutexes[i].references)
            {
                found = i;
                break;
            }
        if (found == KernelObjectSlots)
        {
            last_error = 8;
            return 0;
        }
        auto &m = kernel_mutexes[found];
        m = {};
        m.named = name_address != 0;
        strcpy(m.name, object_name);
        if (a[1])
        {
            m.owner = ThreadId();
            m.recursion = 1;
        }
    }
    const unsigned result = NewKernelHandle(3, found);
    if (!result)
    {
        if (!existing)
            kernel_mutexes[found] = {};
        return 0;
    }
    auto *h = FindKernelHandle(result);
    h->inherit = inherit;
    h->access = open ? a[0] : 0x1f0001;
    if (h->access & 0x10000000)
        h->access = (h->access & ~0x10000000u) | 0x1f0001;
    if (h->access & 0x80000000)
        h->access = (h->access & ~0x80000000u) | 0x20001;
    if (h->access & 0x40000000)
        h->access = (h->access & ~0x40000000u) | 0x20000;
    if (h->access & 0x20000000)
        h->access = (h->access & ~0x20000000u) | 0x120000;
    if (!open)
        last_error = existing ? 183 : 0;
    return result;
}
} // namespace X86
