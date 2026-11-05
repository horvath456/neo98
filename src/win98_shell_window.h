// SPDX-License-Identifier: GPL-2.0-or-later
// Shell window registration and lookup.
#pragma once
namespace Win98ShellWindow
{
using U = unsigned;
// Providers adapt USER's lock, its DWORD shell record at data offset F6h and
// the WORD HWND at record+46h. An unregistered shell has record zero.
template <class E> U Get(E &e)
{
    e.Lock();
    const U window = e.ShellWindowRecord();
    const U result = window ? e.WindowHandle(window) & 0xffffu : 0;
    e.Unlock();
    return result;
}
} // namespace Win98ShellWindow
