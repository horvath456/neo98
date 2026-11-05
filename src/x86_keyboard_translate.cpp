// SPDX-License-Identifier: GPL-2.0-or-later
// The keyboard translation exports of USER32: ToAscii, ToAsciiEx, VkKeyScanA,
// OemKeyScan and MapVirtualKeyExA. All five are word thunks of KEYBOARD.DRV
// (ordinals 4, 10, 129, 128 and 12); the algorithms are in
// win98_keyboard_translate.h, the layout tables of the two.KBD files Windows
// 98 installs for the layouts NEO98 offers in win98_keyboard_layouts.h.
#include "x86_app.h"
#include "win98_keyboard_layouts.h"
#include "win98_kernel_ordinals_a.h"
namespace X86 {
using namespace Win98KeyboardTranslate;

// KEYBOARD.DRV resolves a layout handle to its table and answers with a
// null table when the handle is not installed. NEO98 installs exactly the one
// layout its input provider is configured for.
const Layout *Application::KeyboardTranslationLayout(unsigned hkl)
{
    const unsigned installed = keyboard_layout_state.entries[0].layout;
    if (hkl && hkl != installed && (hkl & 65535) != (installed & 65535)) return nullptr;
    return FindLayout(hkl ? hkl : installed);
}

unsigned Application::KeyScan(unsigned character)
{
    const Layout *layout = KeyboardTranslationLayout(0);
    return layout ? VkKeyScan(*layout, character) : 0xffff;
}

unsigned Application::KeyboardTranslateApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // USER32!ToAscii and ToAsciiEx (1953h) are the word thunks of
    // KEYBOARD ordinals 4 and 10. ToAscii passes the layout of the calling
    // thread, which the driver reads from its own data.
    API_CASE("ToAscii") API_CASE("ToAsciiEx")
    {
        const bool extended = API_IS(name, "ToAsciiEx");
        const Layout *layout = KeyboardTranslationLayout(extended ? a[5] : 0);
        if (!layout) return 0;
        unsigned char keystate[256] = {};
        if (!Read(a[2], keystate, sizeof keystate)) return 0;
        unsigned char characters[2] = {};
        const int count =
            ToAscii(*layout, DriverState(), a[0], a[1], keystate, a[4], characters);
        // Windows 98 writes one word. In the two-character case Windows 98 writes the
        // first character as a word and the second as the next word.
        const unsigned short words[2] = {characters[0], characters[1]};
        if (a[3] && !Store(a[3], words, count == 2 ? 4 : 2)) return 0;
        return unsigned(count);
    }
    // USER32!VkKeyScanA narrows the word the driver returns with
    // MOVZX, so a character the layout cannot produce answers FFFFh.
    // USER32!VkKeyScanExA is the same word thunk with CL 7Fh and
    // the layout of the caller's choice behind it, where VkKeyScanA (CL 7Dh)
    // always uses the one of the calling thread.
    API_CASE("VkKeyScanA") API_CASE("VkKeyScanExA")
    {
        const Layout *layout = KeyboardTranslationLayout(API_IS(name, "VkKeyScanA") ? 0 : a[1]);
        if (!layout) return 0xffff;
        return VkKeyScan(*layout, a[0]);
    }
    // KEYBOARD.DRV. A character above 7Fh has to survive the round trip
    // through the OEM and the ANSI page, the letters and the control codes take
    // the short paths, and the answer is the shift state over the scan code.
    API_CASE("OemKeyScan")
    {
        unsigned character = a[0] & 255, shift = 0;
        if (character >= 0x80)
        {
            // the OEM byte has to survive the trip to the ANSI page
            // and back, and its ANSI form has to stay above 7Fh.
            if (!BuildOemTables()) return ~0u;
            const unsigned char ansi = Win98KernelOrdinals::OemToCharByte(oem_high, (unsigned char)character);
            if (ansi < 0x80) return ~0u;
            if (Win98KernelOrdinals::CharToOemByte(ansi_high, ansi) != character) return ~0u;
            const Layout *layout = KeyboardTranslationLayout(0);
            if (!layout) return ~0u;
            const unsigned scanned = VkKeyScan(*layout, ansi);
            if (scanned == 0xffff || ((scanned >> 8) & 4)) return ~0u;
            shift = ((scanned >> 8) & 255) << 1;
            character = scanned & 255;
        }
        else if (character >= 'A' && character <= 'Z')
            shift = 2;
        else if (character == ' ' || character == 13 || character == 9 || character == 8)
            shift = 0;
        else if (character >= 'a' && character <= 'z')
            character -= 0x20;
        else if (character != 10 && character >= 1 && character <= 26)
        {
            character += 0x40;
            shift = 4;
        }
        else
        {
            const Layout *layout = KeyboardTranslationLayout(0);
            if (!layout) return ~0u;
            const unsigned scanned = VkKeyScan(*layout, character);
            if (scanned == 0xffff || (scanned >> 8) & 4) return ~0u;
            shift = ((scanned >> 8) & 255) << 1;
            character = scanned & 255;
        }
        const unsigned arguments[] = {character, 0};
        return shift << 16 | (KeyboardGameApi("MapVirtualKeyA", arguments) & 65535);
    }
    // USER32!MapVirtualKeyExA is KEYBOARD ordinal 12. The only
    // difference to MapVirtualKeyA is the table lookup in front: Windows 98 answers
    // zero when the handle names a layout that is not installed.
    API_CASE("MapVirtualKeyExA")
    {
        if (!KeyboardTranslationLayout(a[2])) return 0;
        const unsigned arguments[] = {a[0], a[1]};
        return KeyboardGameApi("MapVirtualKeyA", arguments);
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
