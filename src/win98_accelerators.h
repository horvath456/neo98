// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Accelerator tables as Windows 98 keeps them. USER32 hands
// CreateAcceleratorTableA, CopyAcceleratorTableA and DestroyAcceleratorTable
// use the 16-bit USER implementation through flat thunks.
//
// A 16-bit record is five bytes, the layout of an RT_ACCELERATOR resource:
// fVirt, then the key word, then the command word. The 32-bit ACCEL is six
// bytes because the byte carries a pad; Windows 98 reads two bytes and writes one,
// Windows 98 reads one and writes two. There is no stored count: Windows 98 sets bit
// 80h in the fVirt of the last entry, and every reader stops there.
namespace Win98Accelerators
{
struct Entry
{
    unsigned char virt;
    unsigned short key, command;
};
// The flags of fVirt. Bit 80h is the terminator, not an option.
constexpr unsigned FlagVirtualKey = 0x01, FlagShift = 0x04, FlagControl = 0x08;
constexpr unsigned FlagAlt = 0x10, FlagLast = 0x80;
// Windows 98 computes the block size with a word IMUL by five, so a table above
// this many entries wraps in the original. NEO98 refuses it instead.
constexpr unsigned MaxEntries = 13106;

// Windows 98 walks the records. A virtual-key entry answers the key messages, a
// character entry the character messages, and the two modifier bits have to
// agree with the key state. Virtual keys also compare FALT with the delivered
// ALT state: CTRL+ALT arrives as WM_KEYDOWN while ALT is still held. Character
// entries distinguish WM_CHAR from WM_SYSCHAR instead.
inline bool Matches(unsigned flags, unsigned key, unsigned message, unsigned wparam,
                    unsigned modifiers)
{
    const bool virtual_key = (flags & FlagVirtualKey) != 0;
    const bool matching_message = virtual_key ? (message == 0x100 || message == 0x104)
                                              : (message == 0x102 || message == 0x106);
    const bool matching_modifiers =
        virtual_key ? (bool(flags & FlagShift) == bool(modifiers & 1) &&
                       bool(flags & FlagControl) == bool(modifiers & 2))
                    : true;
    const bool alt = virtual_key ? bool(modifiers & 4) : message == 0x106;
    return matching_message && key == wparam && matching_modifiers && bool(flags & FlagAlt) == alt;
}

// counts the entries: it stops after the one whose fVirt carries bit
// 80h, and never walks past the capacity the block size allows.
inline unsigned Count(const Entry *entries, unsigned capacity)
{
    for (unsigned i = 0; i < capacity; ++i)
        if (entries[i].virt & FlagLast) return i + 1;
    return capacity;
}
} // namespace Win98Accelerators
