// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 printf-style formatting, decimal conversion, string reversal, and
// Unicode-to-ANSI conversion.
#include "win98_printf.h"
namespace Win98Printf
{
using U = unsigned;
U Format(Win98Arrays::Memory &m, U output, U format, U arguments, bool &supported, Ansi *ansi)
{
    supported = true;
    U position = output, remaining = 1024;
    auto byte = [&](U p)
    {
        unsigned char c = 0;
        m.Read(p, &c, 1);
        return c;
    };
    auto put = [&](unsigned char c)
    {
        if (--remaining == 0)
            return false;
        m.Write(position++, &c, 1);
        return true;
    };
    auto decimal = [&]()
    {
        U value = 0;
        while (byte(format) >= '0' && byte(format) <= '9')
            value = value * 10 + byte(format++) - '0';
        return value;
    };
    auto reverse = [&](U first, U end)
    {
        while (first < end)
        {
            unsigned char a = byte(first), b = byte(end);
            m.Write(first++, &b, 1);
            m.Write(end--, &a, 1);
        }
    };
    auto finish = [&]()
    {
        unsigned char zero = 0;
        m.Write(position, &zero, 1);
        return 1024 - remaining;
    };
    for (U budget = 0; byte(format) && budget < 0x100000; ++budget)
    {
        if (byte(format) != '%')
        {
            if (!put(byte(format++)))
                return finish();
            continue;
        }
        ++format;
        bool left = false, prefix = false;
        while (byte(format) == '-' || byte(format) == '#')
        {
            if (byte(format) == '-')
                left = true;
            else
                prefix = true;
            ++format;
        }
        unsigned char padding = ' ';
        if (byte(format) == '0')
        {
            padding = '0';
            ++format;
        }
        int width = (int)decimal(), precision = -1;
        if (byte(format) == '.')
        {
            ++format;
            precision = (int)decimal();
        }
        U modifier = 0;
        bool short_value = false;
        if (byte(format) == 'w')
        {
            modifier = 2;
            ++format;
        }
        else if (byte(format) == 'l')
        {
            modifier = 1;
            ++format;
        }
        else if (byte(format) == 't')
        {
            ++format;
        }
        else if (byte(format) == 'h')
        {
            short_value = true;
            ++format;
        }
        unsigned char kind = byte(format);
        if (!kind)
            break;
        ++format;
        if (kind == 'c' || kind == 'C' || kind == 's' || kind == 'S')
        {
            if ((kind == 'C' || kind == 'S') && !modifier && !short_value)
                modifier = 1;
            U argument = m.Word(arguments);
            arguments += 4;
            U length = 1, source = argument;
            // The wide paths convert into a 1024-byte buffer on the stack of
            // the original and print its bytes like the ANSI paths do.
            unsigned char narrowed[1024];
            bool converted = false;
            if (modifier)
            {
                if (!ansi)
                {
                    supported = false;
                    return finish();
                }
                const bool one = kind == 'c' || kind == 'C';
                auto wide = [&](U index)
                {
                    if (one)
                        return index ? 0u : (argument & 0xffffu);
                    unsigned short value = 0;
                    m.Read(source + index * 2, &value, 2);
                    return (U)value;
                };
                // Original: wcslen, on the two-word buffer that
                // %lc builds, so a zero character leaves nothing to print.
                U count = 0;
                while (wide(count))
                {
                    if (++count > 0x100000)
                    {
                        supported = false;
                        return finish();
                    }
                }
                if (precision >= 0 && (U)precision < count)
                    count = precision;
                // converts count characters and stops at count bytes.
                U bytes = 0;
                for (U i = 0; i < count && bytes < count && bytes < sizeof narrowed; ++i)
                {
                    const U character = wide(i);
                    if (!character)
                    {
                        narrowed[bytes++] = 0;
                        continue;
                    }
                    unsigned char pair[2] = {};
                    const U written = ansi->Narrow(character, pair);
                    if (!written)
                    {
                        narrowed[bytes++] = '_';
                        continue;
                    }
                    if (written == 1)
                    {
                        narrowed[bytes++] = pair[0];
                        continue;
                    }
                    if (bytes + 1 >= count || bytes + 1 >= sizeof narrowed)
                        break;
                    narrowed[bytes++] = pair[0];
                    narrowed[bytes++] = pair[1];
                }
                length = bytes;
                converted = true;
            }
            else if (kind == 's' || kind == 'S')
            {
                length = 0;
                while (byte(source + length))
                {
                    if (++length > 0x100000)
                    {
                        supported = false;
                        return finish();
                    }
                }
            }
            // The wide paths cap the precision in characters before the
            // conversion, so it does not apply to the bytes it produced.
            if (!converted && precision >= 0 && (U)precision < length)
                length = precision;
            int pad = (int)((U)width - length);
            if (!left)
                while (pad > 0)
                {
                    --pad;
                    if (!put(padding))
                        return finish();
                }
            for (U i = 0; i < length; ++i)
                if (!put(converted        ? narrowed[i]
                         : kind == 'c' || kind == 'C' ? (unsigned char)argument
                                                      : byte(source + i)))
                    return finish();
            if (left)
                while (pad > 0)
                {
                    --pad;
                    if (!put(padding))
                        return finish();
                }
            continue;
        }
        if (kind != 'd' && kind != 'i' && kind != 'u' && kind != 'x' && kind != 'X')
        {
            if (!put(kind))
                return finish();
            continue;
        }
        bool signed_value = kind == 'd' || kind == 'i', hex = kind == 'x' || kind == 'X';
        if (!hex)
            prefix = false;
        if (left || precision >= 0)
            padding = ' ';
        U value = m.Word(arguments);
        arguments += 4;
        bool negative = signed_value && (value & 0x80000000u);
        if (negative)
            value = 0 - value;
        U start = position, digits = 0, radix = hex ? 16 : 10;
        // Original conversion writes least-significant digits before reversing.
        do
        {
            U digit = value % radix;
            value /= radix;
            unsigned char c = digit < 10 ? '0' + digit : (kind == 'X' ? 'A' : 'a') + digit - 10;
            m.Write(position + digits, &c, 1);
            ++digits;
        } while (digits <= remaining && value);
        remaining -= digits;
        if (!remaining)
            return finish();
        position += digits;
        int pad = (int)((U)width - digits), zeroes = (int)((U)precision - digits);
        if (zeroes > 0)
            pad = (int)((U)pad - (U)zeroes);
        while (zeroes > 0)
        {
            --zeroes;
            if (!put('0'))
                return finish();
        }
        auto sign = [&]()
        {
            if (negative)
            {
                negative = false;
                if (!put('-'))
                    return false;
            }
            return true;
        };
        auto hex_prefix = [&]()
        {
            if (prefix)
            {
                prefix = false;
                if (!put(kind) || !put('0'))
                    return false;
            }
            return true;
        };
        if (pad < 1 || left)
        {
            if (negative)
                --pad;
            if (!sign() || !hex_prefix())
                return finish();
            reverse(start, position - 1);
            while (pad > 0)
            {
                --pad;
                if (!put(padding))
                    return finish();
            }
        }
        else
        {
            if (padding != '0')
            {
                if (negative)
                    --pad;
                if (!sign() || !hex_prefix())
                    return finish();
            }
            if (negative)
                --pad;
            while (pad > 0)
            {
                --pad;
                if (!put(padding))
                    return finish();
            }
            if (!sign() || !hex_prefix())
                return finish();
            reverse(start, position - 1);
        }
    }
    return finish();
}
} // namespace Win98Printf
