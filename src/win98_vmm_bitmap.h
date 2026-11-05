// SPDX-License-Identifier: GPL-2.0-or-later
// Bitmap allocation helpers for one-based identifiers and zero-based indexes.
#pragma once
namespace Win98VmmBitmap
{
using U = unsigned;
static_assert(sizeof(U) == 4, "VMM bitmap words must have 32 bits");

// Return a one-based identifier, or zero on exhaustion.
inline U Claim(U *words, U bit_count)
{
    const U word_count = (bit_count + 31u) >> 5;
    for (U word = 0; word < word_count; ++word)
    {
        const U value = words[word];
        if (value == ~0u)
            continue;
        U bit = 0;
        while (value & (1u << bit))
            ++bit;
        const U identifier = word * 32u + bit + 1u;
        if (identifier > bit_count)
            return 0;
        words[word] = value | (1u << bit);
        return identifier;
    }
    return 0;
}

// The caller validates a nonzero identifier and holds the lock.
inline void Release(U *words, U identifier)
{
    const U bit = identifier - 1u;
    words[bit >> 5] &= ~(1u << (bit & 31u));
}

// Return a zero-based index, or UINT32_MAX on exhaustion.
inline U Last(const U *words, U bit_count)
{
    if (!bit_count)
        return ~0u;
    U word = (bit_count - 1u) >> 5;
    const U last_bit = (bit_count - 1u) & 31u;
    U value = words[word] & (~0u >> (31u - last_bit));
    for (;;)
    {
        if (value)
        {
            U bit = 31;
            while (!(value & (1u << bit)))
                --bit;
            return word * 32u + bit;
        }
        if (!word)
            return ~0u;
        value = words[--word];
    }
}
} // namespace Win98VmmBitmap
