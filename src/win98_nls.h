// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_arrays.h"
namespace Win98NlsComposite { class UnicodeTable; }
namespace Win98NlsDecompose { class UnicodeTable; }
namespace Win98Nls
{
using U = unsigned;
struct Table
{
    const unsigned char *data = nullptr;
    U size = 0;
    U Half(U offset) const;
    U Offset(U index) const;
    U Wide(U byte, bool glyphs = false) const;
    U Narrow(U character) const;
    bool Valid() const;
};
struct Environment : Win98Arrays::Memory
{
    virtual U Ansi() = 0;
    virtual U Oem() = 0;
    virtual const Table *CodePage(U) = 0;
    virtual const Win98NlsComposite::UnicodeTable *Unicode() { return nullptr; }
    virtual const Win98NlsDecompose::UnicodeTable *Decomposition() { return nullptr; }
    virtual void Error(U) = 0;
    virtual void Unsupported(const char *) = 0;
};
// Native KERNEL32 SBCS and historical Win98 UTF-8 conversion paths.
U Call(Environment &, U ordinal, const U *args);
} // namespace Win98Nls
