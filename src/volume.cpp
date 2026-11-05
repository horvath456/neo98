// SPDX-License-Identifier: GPL-2.0-or-later
#include "volume.h"
namespace Volume
{
namespace
{
struct Slot
{
    Backend *backend = nullptr;
    U type = Fixed;
    bool writable = true;
    bool drive = false;
};
Slot slots[26];
U Index(char letter)
{
    const unsigned c = (unsigned char)letter;
    const unsigned upper = c >= 'a' && c <= 'z' ? c - 32 : c;
    return upper >= 'A' && upper <= 'Z' ? upper - 'A' : 26;
}
} // namespace
void Mount(char letter, Backend *backend, U type, bool writable)
{
    const U index = Index(letter);
    if (index < 26)
        slots[index] = Slot{backend, type, writable, backend != nullptr || type == Cdrom};
}
Backend *Mounted(char letter)
{
    const U index = Index(letter);
    return index < 26 ? slots[index].backend : nullptr;
}
bool Exists(char letter)
{
    const U index = Index(letter);
    return index < 26 && slots[index].drive;
}
U Type(char letter)
{
    const U index = Index(letter);
    return index < 26 ? slots[index].type : Fixed;
}
bool Writable(char letter)
{
    const U index = Index(letter);
    return index < 26 && slots[index].writable;
}
void Clear()
{
    for (auto &slot : slots)
        slot = Slot{};
}
} // namespace Volume
