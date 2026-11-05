// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98ResourceWrite
{
// KERNEL32, called by UnhandledExceptionFilter before
// the application's filter. The resource data-directory range, not the
// section name, determines which read-only writes can be retried.
template<class E> bool Repair(E &e, unsigned address)
{
    unsigned allocation = 0, protection = 0;
    if (!e.Query(address, allocation, protection) || protection != 2) return false;
    unsigned size = 0;
    const unsigned resource = e.ResourceDirectory(allocation, size);
    if (!resource || address < resource || resource + size <= address) return false;
    return e.MakeWritable(address);
}
}
