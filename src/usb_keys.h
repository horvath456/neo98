// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// New USB boot-keyboard adapter: USB usages to Win32 virtual keys and PC set-1
// scan codes. This table is native host code. DE and US layouts.
namespace UsbKeys
{
struct Key
{
    unsigned vk = 0, scan = 0;
    bool extended = false;
};
inline Key Map(unsigned usage, bool german, bool numlock)
{
    if (usage >= 4 && usage <= 0x1d)
    {
        static const unsigned char scans[] = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                              0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                              0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
        unsigned vk = 'A' + usage - 4;
        if (german)
        {
            if (vk == 'Y')
                vk = 'Z';
            else if (vk == 'Z')
                vk = 'Y';
        }
        return {vk, scans[usage - 4], false};
    }
    if (usage >= 0x1e && usage <= 0x27)
        return {usage == 0x27 ? '0' : '1' + usage - 0x1e, 2 + usage - 0x1e, false};
    if (usage >= 0x3a && usage <= 0x45)
        return {0x70 + usage - 0x3a, usage < 0x44 ? 0x3b + usage - 0x3a : 0x57 + usage - 0x44, false};
    if (usage >= 0x59 && usage <= 0x63)
    {
        static const unsigned char scan[] = {0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d,
                                             0x47, 0x48, 0x49, 0x52, 0x53};
        static const unsigned char nav[] = {0x23, 0x28, 0x22, 0x25, 0x0c, 0x27, 0x24, 0x26, 0x21, 0x2d, 0x2e};
        return {numlock ? (usage <= 0x61   ? 0x61 + usage - 0x59
                           : usage == 0x62 ? 0x60
                                           : 0x6e)
                        : nav[usage - 0x59],
                scan[usage - 0x59], false};
    }
    if (usage >= 0x2d && usage <= 0x38)
    {
        static const unsigned char us[] = {0xbd, 0xbb, 0xdb, 0xdd, 0xdc, 0xdc,
                                           0xba, 0xde, 0xc0, 0xbc, 0xbe, 0xbf};
        static const unsigned char de[] = {0xdb, 0xdd, 0xba, 0xbb, 0xbf, 0xbf,
                                           0xc0, 0xde, 0xdc, 0xbc, 0xbe, 0xbd};
        static const unsigned char scan[] = {0x0c, 0x0d, 0x1a, 0x1b, 0x2b, 0x2b,
                                             0x27, 0x28, 0x29, 0x33, 0x34, 0x35};
        return {(german ? de : us)[usage - 0x2d], scan[usage - 0x2d], false};
    }
    switch (usage)
    {
    case 0x28:
        return {13, 0x1c, false};
    case 0x29:
        return {27, 1, false};
    case 0x2a:
        return {8, 0xe, false};
    case 0x2b:
        return {9, 0xf, false};
    case 0x2c:
        return {32, 0x39, false};
    case 0x39:
        return {0x14, 0x3a, false};
    case 0x46:
        return {0x2c, 0x37, true};
    case 0x47:
        return {0x91, 0x46, false};
    case 0x48:
        return {0x13, 0x45, false};
    case 0x49:
        return {0x2d, 0x52, true};
    case 0x4a:
        return {0x24, 0x47, true};
    case 0x4b:
        return {0x21, 0x49, true};
    case 0x4c:
        return {0x2e, 0x53, true};
    case 0x4d:
        return {0x23, 0x4f, true};
    case 0x4e:
        return {0x22, 0x51, true};
    case 0x4f:
        return {0x27, 0x4d, true};
    case 0x50:
        return {0x25, 0x4b, true};
    case 0x51:
        return {0x28, 0x50, true};
    case 0x52:
        return {0x26, 0x48, true};
    case 0x53:
        return {0x90, 0x45, false};
    case 0x54:
        return {0x6f, 0x35, true};
    case 0x55:
        return {0x6a, 0x37, false};
    case 0x56:
        return {0x6d, 0x4a, false};
    case 0x57:
        return {0x6b, 0x4e, false};
    case 0x58:
        return {13, 0x1c, true};
    case 0x64:
        return {0xe2, 0x56, false};
    case 0x65:
        return {0x5d, 0x5d, true};
    default:
        return {};
    }
}
} // namespace UsbKeys
