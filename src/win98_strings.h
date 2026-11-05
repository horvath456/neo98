// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_arrays.h"
namespace Win98Strings
{
// Native COMCTL32 ordinals 233..236: Str_GetPtrA/W, Str_SetPtrA/W.
unsigned Call(Win98Arrays::Memory &, unsigned ordinal, const unsigned *args);
struct Environment : Win98Arrays::Memory
{
    virtual bool LeadByte(unsigned) = 0;
    virtual unsigned Next(unsigned) = 0;
    virtual bool Readable(unsigned) = 0;
    virtual unsigned ToWide(unsigned flags, unsigned source, unsigned output, unsigned capacity) = 0;
    virtual unsigned Local(unsigned bytes) = 0;
    virtual void ReleaseLocal(unsigned) = 0;
};
// COMCTL32 71/72/73 (allocator wrappers), 350 (StrChrA), 357/365 (StrToIntA/W).
unsigned Helper(Environment &, unsigned ordinal, const unsigned *args);
} // namespace Win98Strings
