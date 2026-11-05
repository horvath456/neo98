// SPDX-License-Identifier: GPL-2.0-or-later
// The font belongs to
// the caller; only its auxiliary global block is released. FONTINFO fields
// below are packed, including the unaligned DWORDs at 1dh and 52h.
#pragma once
namespace Win98EngineFont
{
// GDI: degree table (1/10000), linear tenth-degree
// interpolation and two rounded 16-bit MulDiv operations.
inline int Sine(int radius, int angle)
{
    static const unsigned short values[91] = {
        0,175,349,523,698,872,1045,1219,1392,1564,1736,1908,2079,2250,2419,2588,
        2756,2924,3090,3256,3420,3584,3746,3907,4067,4226,4384,4540,4695,4848,
        5000,5150,5299,5446,5592,5736,5878,6018,6157,6293,6428,6561,6691,6820,
        6947,7071,7193,7314,7431,7547,7660,7771,7880,7986,8090,8192,8290,8387,
        8480,8572,8660,8746,8829,8910,8988,9063,9135,9205,9272,9336,9397,9455,
        9511,9563,9613,9659,9703,9744,9781,9816,9848,9877,9903,9925,9945,9962,
        9976,9986,9994,9998,10000
    };
    angle %= 3600;
    if (angle < 0) angle += 3600;
    const bool negative = angle > 1800;
    if (negative) angle -= 1800;
    if (angle > 900) angle = 1800 - angle;
    const unsigned index = unsigned(angle) / 10, remainder = unsigned(angle) % 10;
    int value = values[index];
    if (remainder) value += ((values[index + 1] - value) * int(remainder) + 5) / 10;
    radius = short(radius);
    const int magnitude = radius < 0 ? -radius : radius;
    const int scaled = (magnitude * value + 5000) / 10000;
    return (negative != (radius < 0)) ? -scaled : scaled;
}

template<class Memory> unsigned Delete(Memory &m, unsigned offset)
{
    unsigned flags = m.Word(0);
    if (!(flags & 0x20) && (flags & 3) == 3 && !(m.Byte(0x33) & 4))
    {
        m.Word(0, flags | 0x20);
        const unsigned bits = m.Long(0x52);
        m.Long(0x52, m.Long(0x1d));
        m.Long(0x1d, bits);
    }
    if ((m.Byte(0) & 3) == 3 && m.Word(0x27) == ((offset + 0x83) & 0xffff))
    {
        const unsigned handle = m.Word(0x5a);
        if (handle)
        {
            m.Word(0x5a, 0);
            m.Free(handle);
        }
    }
    return 1;
}
}
