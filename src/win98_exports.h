// SPDX-License-Identifier: GPL-2.0-or-later
// Native KERNEL32/ export-table lookup.
#pragma once
namespace Win98Exports
{
using U = unsigned;
class Memory
{
  public:
    virtual bool Read(U address, void *data, U size) = 0;
    virtual void Error(U error) = 0;

  protected:
    ~Memory() = default;
};
// Returns the export VA, including a forwarder string VA. The module manager
// resolves forwarders; this function owns the original table-search algorithm.
U Find(Memory &, U base, U directory, U size, const char *name, U ordinal, U hint = 0);
bool Forwarded(U base, U directory, U size, U address);
// KERNEL32's ANSI copy, after module lookup and filename conversion.
U FileName(char *out, const char *path, U length, U capacity);
} // namespace Win98Exports
