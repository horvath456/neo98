// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_parameters.h"
namespace Win98Parameters
{
namespace
{
unsigned Read32(const unsigned char *p)
{
    return unsigned(p[0]) | (unsigned(p[1]) << 8) | (unsigned(p[2]) << 16) | (unsigned(p[3]) << 24);
}
void Write32(unsigned char *p, unsigned v)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = v >> (i * 8);
}
int Signed(unsigned v) { return (v & 0x8000) ? int(v & 0xffff) - 65536 : int(v & 0xffff); }
} // namespace
void ExpandFont(const unsigned char (&source)[50], unsigned char *out)
{
    // USER.EXE: five signed WORDs, followed by forty unchanged bytes.
    for (unsigned i = 0; i < 5; ++i)
        Write32(out + i * 4, Signed(source[i * 2] | unsigned(source[i * 2 + 1]) << 8));
    for (unsigned i = 0; i < 40; ++i)
        out[20 + i] = source[10 + i];
}
int Query(const Environment &env, unsigned action, unsigned parameter, unsigned char *out, unsigned capacity,
          unsigned &written)
{
    written = 0;
    auto word = [&](unsigned offset) { return unsigned(env.Word(offset)); };
    auto put = [&](unsigned offset, unsigned value) { Write32(out + offset, value); };
    auto font = [&](unsigned offset, unsigned source)
    {
        unsigned char data[50];
        if (!env.Font(word(source), data))
            return false;
        ExpandFont(data, out + offset);
        return true;
    };
    unsigned scalar = 0;
    int result = 1;
    switch (action)
    {
    case 1:
        scalar = word(0x1a4);
        break;
    case 5:
        scalar = word(0x1014);
        break;
    case 10:
        scalar = word(0xd8) & 31;
        break;
    case 13:
        scalar = word(0x32a);
        break;
    case 14:
        scalar = word(0xda);
        if (scalar & 0x8000)
            scalar = (-scalar) & 0xffff;
        break;
    case 16:
        scalar = Signed(word(0xda)) > 0;
        break;
    case 18:
    case 35:
        scalar = 1;
        break;
    case 22:
        scalar = (word(0xd8) & 0x60) >> 5;
        break;
    case 24:
        scalar = word(0x32c);
        break;
    case 25:
        scalar = word(0x24c);
        break;
    case 27:
        scalar = word(0x32e);
        break;
    // SPI_GETWHEELSCROLLLINES, new with Win98 and NT 4: the lines one notch
    // of the wheel scrolls, 3 unless "WheelScrollLines" of the desktop
    // settings says otherwise; this installation carries no such value. A
    // program that gets FALSE here takes the wheel for absent: Gecko
    // (RetroZilla, nsWindow.cpp WM_MOUSEWHEEL) then drops every notch

    case 104:
        scalar = 3;
        break;
    case 38:
        result = word(0x264) != 0;
        scalar = result ? word(0x234) : 0;
        break;
    case 3:
    case 48:
    {
        const unsigned length = action == 3 ? 6 : 8, source = action == 3 ? 0x86a : 0x11ba;
        if (!out || capacity < length)
            return 0;
        for (unsigned i = 0; i < length; i += 2)
        {
            unsigned value = word(source + i);
            out[i] = value;
            out[i + 1] = value >> 8;
        }
        written = length;
        return 1;
    }
    case 31:
    {
        unsigned char data[50];
        if (!out || !env.Font(word(0x1b0), data))
            return 0;
        unsigned n = parameter & 0xffff;
        if (n > sizeof data)
            n = sizeof data;
        if (capacity < n)
            return 0;
        for (unsigned i = 0; i < n; ++i)
            out[i] = data[i];
        written = n;
        return 1;
    }
    case 41:
    case 43:
    case 45:
    {
        const unsigned length = action == 41 ? 340 : action == 43 ? 20 : 76;
        if (!out || capacity < length || Read32(out) != length)
            return 0;
        if (action == 41)
        {
            put(4, Signed(word(0x1014)));
            put(8, word(0x2e2));
            put(12, word(0x306));
            put(16, Signed(word(0x31a)));
            put(20, Signed(word(0x31c)));
            put(84, Signed(word(0x346)));
            put(88, Signed(word(0x348)));
            put(152, Signed(word(0x34a)));
            put(156, Signed(word(0x34c)));
            if (!font(24, 0x1ac) || !font(92, 0x1ae) || !font(160, 0x1a8) || !font(220, 0x1b2) ||
                !font(280, 0x1b4))
                return 0;
        }
        else if (action == 43)
        {
            put(4, Signed(word(0x350) - word(0x2ec) * 2));
            put(8, (word(0x33c) - word(0x350)) & 0xffff);
            put(12, (word(0x33e) - word(0x352)) & 0xffff);
            put(16, word(0x34e));
        }
        else
        {
            put(4, word(0x32a));
            put(8, word(0x32c));
            put(12, Signed(word(0x24c)));
            if (!font(16, 0x1b0))
                return 0;
        }
        written = length;
        return 1;
    }
    // The four get actions of the power features, SPI_GETLOWPOWERTIMEOUT
    // (4Fh), SPI_GETPOWEROFFTIMEOUT (50h), SPI_GETLOWPOWERACTIVE (53h) and
    // SPI_GETPOWEROFFACTIVE (54h). USER first asks the display for its power
    // states : VDD function 91h through the entry INT 2Fh
    // AX=1684h BX=0Ah hands out, mask 4 for low power and 8
    // for power off. The display of this port offers both (Environment::
    // PowerStates); the timeouts are the words DS:DCh and DS:DEh, negative
    // while inactive. Without the state the action fails
    // with FALSE, POWERCFG then leaves out "Turn off monitor"
    // The set actions are
    // Application::ScreenSaveParameters.
    case 0x4f:
    case 0x50:
    case 0x53:
    case 0x54:
    {
        const unsigned index = action == 0x4f || action == 0x53 ? 1 : 2;
        if (!(env.PowerStates() & (index == 1 ? 4u : 8u)))
            return 0;
        const int value = Signed(word(0xda + index * 2));
        scalar = action <= 0x50 ? unsigned(value < 0 ? -value : value) : unsigned(value > 0);
        break;
    }
    case 0x51:
    case 0x52:
    case 0x55:
    case 0x56:
        return env.PowerStates() & (action == 0x51 || action == 0x55 ? 4u : 8u) ? -1 : 0;
    default:
        return -1;
    }
    if (!out || capacity < 2)
        return 0;
    out[0] = scalar;
    out[1] = scalar >> 8;
    written = 2;
    return result;
}
} // namespace Win98Parameters
