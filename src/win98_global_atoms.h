// SPDX-License-Identifier: GPL-2.0-or-later
// Original USER global/registration dispatch over the original KRNL386 atom core.
// Table storage and selector lifetime are native providers.
#pragma once
#include "win98_atoms.h"
namespace Win98GlobalAtoms
{
using U = unsigned;
// USER selects DS from USER data+06EE for these global entries.
// A missing global selector returns AX=0 before the KERNEL import is called.
// source_value is the original Win32 DWORD: <=FFFF denotes an integer atom.
inline U Search(Win98Atoms::Environment *global, U source_value, const unsigned char *text, bool add)
{
    if (!global)
        return 0;
    // USER -> KERNEL.70 / KERNEL.69.
    return source_value <= 0xffff ? Win98Atoms::Integer(source_value)
                                 : Win98Atoms::Search(*global, text, add ? 1 : 0);
}
inline U Delete(Win98Atoms::Environment *global, U atom)
{
    // USER -> KERNEL.71; K32 Windows 98 passes only the WORD.
    return global ? Win98Atoms::Delete(*global, atom & 0xffff) : 0;
}
inline U Name(Win98Atoms::Environment *global, U atom, unsigned char *out, U capacity)
{
    // USER -> KERNEL.72. The Win32 pointer/size probe precedes
    // this WORD-level routine; it must not silently discard a nonzero high WORD.
    return global ? Win98Atoms::Name(*global, atom & 0xffff, out, capacity & 0xffff) : 0;
}
inline U Register(Win98Atoms::Environment *registration, U source_value, const unsigned char *text)
{
    // USER is shared by RegisterWindowMessage and
    // RegisterClipboardFormat. DS is USER's own data segment, not data+06EE.
    // There is no additional reference pinning in this original wrapper.
    return Search(registration, source_value, text, true);
}
template <class E> U UnsupportedWide(E &e)
{
    // K32 / ->, return class0 and argc1/3.
    e.Error(120);
    return 0;
}
} // namespace Win98GlobalAtoms
