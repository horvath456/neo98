// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_exports.h"
namespace Win98Exports
{
namespace
{
bool Word(Memory &m, U address, U &value, U size = 4)
{
    unsigned char bytes[4] = {};
    if (size - 1 > ~address || !m.Read(address, bytes, size))
        return false;
    value = bytes[0] | U(bytes[1]) << 8 | U(bytes[2]) << 16 | U(bytes[3]) << 24;
    return true;
}
bool Compare(Memory &m, U address, const char *name, int &comparison)
{
    // Provider boundary: bound strings independently of the original search.
    for (U i = 0; i < 256 && i <= ~address; ++i)
    {
        unsigned char ch = 0;
        if (!m.Read(address + i, &ch, 1))
            return false;
        comparison = int(ch) - int(static_cast<unsigned char>(name[i]));
        if (comparison || !ch)
            return true;
    }
    return false;
}
} // namespace
bool Forwarded(U base, U directory, U size, U address)
{
    // The original uses strict lower and upper bounds here.
    return address > base + directory && address - (base + directory) < size;
}
U Find(Memory &m, U base, U directory, U size, const char *name, U ordinal, U hint)
{
    if (!size || size < 40 || directory > ~base || size - 1 > ~(base + directory))
    {
        m.Error(126);
        return 0;
    }
    const U exp = base + directory;
    U first, functions, addresses;
    if (!Word(m, exp + 16, first) || !Word(m, exp + 20, functions) || !Word(m, exp + 28, addresses) ||
        addresses > ~base || functions > 65536)
        return 0;
    if (name)
    {
        U count, strings, ordinals;
        if (!Word(m, exp + 24, count) || !Word(m, exp + 32, strings) || !Word(m, exp + 36, ordinals) ||
            count > 65536 || strings > ~base || ordinals > ~base)
            return 0;
        auto compare = [&](U index, int &result)
        {
            U rva;
            return index <= (~(base + strings)) / 4 && Word(m, base + strings + index * 4, rva) &&
                   rva <= ~base && Compare(m, base + rva, name, result);
        };
        int result = 1;
        U index = hint;
        bool found = hint < count && compare(hint, result) && !result;
        if (!found)
        {
            int low = 0, high = int(count) - 1;
            while (low <= high)
            {
                index = (U(low) + U(high)) >> 1;
                if (!compare(index, result))
                    return 0;
                if (!result)
                {
                    found = true;
                    break;
                }
                if (result < 0)
                    low = int(index) + 1;
                else
                    high = int(index) - 1;
            }
        }
        if (!found)
        {
            m.Error(126);
            return 0;
        }
        U slot;
        if (index > (~(base + ordinals)) / 2 || !Word(m, base + ordinals + index * 2, slot, 2))
            return 0;
        // The original sign-extends the 16-bit name ordinal before adding.
        ordinal = first + (slot & 0x8000 ? slot | 0xffff0000u : slot);
    }
    const U index = ordinal - first;
    if (index >= functions || index > (~(base + addresses)) / 4)
        return 0;
    U rva;
    if (!Word(m, base + addresses + index * 4, rva) || !rva || rva > ~base)
        return 0;
    return base + rva;
}
U FileName(char *out, const char *path, U length, U capacity)
{
    if (!capacity)
        return 0;
    const U count = length < capacity - 1 ? length : capacity - 1;
    for (U i = 0; i < count; ++i)
        out[i] = path[i];
    out[count] = 0;
    return count;
}
} // namespace Win98Exports
