// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Tls
{
constexpr unsigned Slots = 80;
struct Environment
{
    virtual ~Environment() = default;
    virtual void Lock(bool global) = 0;
    virtual void Unlock(bool global) = 0;
    virtual unsigned &Bitmap(unsigned word) = 0;
    virtual unsigned Read(unsigned index) = 0;
    virtual void Write(unsigned index, unsigned value) = 0;
    virtual void ClearThreads(unsigned index) = 0;
    virtual void Error(unsigned value) = 0;
};
// KERNEL32. Allocation reserves the first free bit; it does not clear data.
inline unsigned Alloc(Environment &e)
{
    e.Lock(false);
    unsigned word = 0;
    while (word < 3 && e.Bitmap(word) == ~0u)
        ++word;
    unsigned slot = word * 32;
    if (slot < Slots)
    {
        unsigned mask = 1;
        while (slot < Slots && (e.Bitmap(word) & mask))
        {
            ++slot;
            mask <<= 1;
        }
        if (slot < Slots)
            e.Bitmap(word) |= mask;
    }
    if (slot >= Slots)
    {
        slot = ~0u;
        e.Error(259);
    }
    e.Unlock(false);
    return slot;
}
// Clear the released slot in every thread belonging to this process.
inline unsigned Free(Environment &e, unsigned slot)
{
    e.Lock(true);
    e.Lock(false);
    unsigned result = 0;
    if (slot < Slots && (e.Bitmap(slot / 32) & (1u << (slot & 31))))
    {
        e.Bitmap(slot / 32) &= ~(1u << (slot & 31));
        e.ClearThreads(slot);
        result = 1;
    }
    else
        e.Error(87);
    e.Unlock(false);
    e.Unlock(true);
    return result;
}
// /. Get/Set check the range, not the allocation bitmap.
inline unsigned Set(Environment &e, unsigned slot, unsigned value)
{
    if (slot >= Slots)
    {
        e.Error(87);
        return 0;
    }
    e.Write(slot, value);
    return 1;
}
inline unsigned Get(Environment &e, unsigned slot)
{
    if (slot >= Slots)
    {
        e.Error(87);
        return 0;
    }
    const auto value = e.Read(slot);
    e.Error(0);
    return value;
}
} // namespace Win98Tls
