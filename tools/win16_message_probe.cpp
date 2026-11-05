// SPDX-License-Identifier: GPL-2.0-or-later
// Prints the message translation decisions of src/win98_win16_user.h so
// tools/test_win16_messages_native.py can check them against the packing the
// two sides use. The probe holds no expectation of its own: it reads the rule
// table, applies it in both directions and writes what came out.
#include "../src/win98_win16_user.h"
#include <stdio.h>
namespace W = Win98Win16User;
int main()
{
    unsigned count = 0;
    const W::Rule *rules = W::Rules(count);
    printf("rules %u\n", count);
    // Two bit patterns that cannot be confused with each other in any half.
    // The high half of the 32-bit lParam is zero in both, because every rule
    // that repacks takes its three 16-bit words out of the 32-bit wParam and
    // the low half of the 32-bit lParam; a window handle never fills the
    // remaining half in a process of 16-bit windows.
    const unsigned pairs[][2] = {{0x12345678u, 0x0000def0u}, {0x0001fffeu, 0x00000042u}};
    for (unsigned i = 0; i < count; ++i)
    {
        const W::Rule &rule = rules[i];
        printf("rule %04x kind %u structure %u\n", rule.message, rule.kind, rule.structure);
        if (rule.kind != W::KindSwap)
            continue;
        for (const auto &pair : pairs)
        {
            unsigned word = 0, lparam16 = 0;
            W::SwapTo16(rule, pair[0], pair[1], word, lparam16);
            unsigned wparam32 = 0, lparam32 = 0;
            W::SwapTo32(rule, word, lparam16, wparam32, lparam32);
            printf("swap %04x in %08x %08x to16 %04x %08x back %08x %08x\n", rule.message, pair[0],
                   pair[1], word, lparam16, wparam32, lparam32);
        }
    }
    for (unsigned message = 0x132; message <= 0x139; ++message)
        printf("ctlcolor %04x is32 %u type %u back %04x\n", message, W::IsControlColor32(message) ? 1u : 0u,
               W::ControlColorType(message), W::ControlColor32(W::ControlColorType(message)));
    const unsigned probes[] = {0x0001, 0x0006, 0x000c, 0x000d, 0x0019, 0x0024, 0x002b, 0x002c,
                               0x002d, 0x002e, 0x002f, 0x0039,
                               0x0046, 0x0047, 0x0083, 0x0100, 0x0111, 0x0114, 0x0115, 0x011f,
                               0x0120, 0x0210, 0x0220, 0x0222, 0x0229, 0x0230, 0x03e0, 0x03e1, 0x03e4, 0x03ef, 0x03f0};
    for (unsigned message : probes)
        printf("kind %04x from32 %u from16 %u\n", message, W::KindOf(message, true),
               W::KindOf(message, false));
    // The ordinal contract, for the test to compare against the Win16 entries
    // the programs of the tree import.
    const W::Ordinal *user = W::UserOrdinals(count);
    for (unsigned i = 0; i < count; ++i)
        printf("user %u words %u name %s\n", user[i].ordinal, user[i].words, user[i].name);
    const W::Ordinal *gdi = W::GdiOrdinals(count);
    for (unsigned i = 0; i < count; ++i)
        printf("gdi %u words %u name %s\n", gdi[i].ordinal, gdi[i].words, gdi[i].name);
    printf("string block %u index %u\n", W::StringBlock(0x136), W::StringIndex(0x136));
    return 0;
}
