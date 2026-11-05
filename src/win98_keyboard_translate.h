// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The keyboard translation of Windows 98, compatible with KEYBOARD.DRV and
// the layout tables it loads. USER32!ToAscii, ToAsciiEx, VkKeyScanA and
// OemKeyScan are thunks of KEYBOARD ordinals 4, 10, 129 and 128; the code below
// follows KEYBOARD.DRV (ToAscii), (VkKeyScan) and
// (OemKeyScan).
//
// The layout tables of Win98 are the.KBD files. Their record starts at the
// offset the file header carries at +8 and every offset inside it is relative
// to that record:
//
// +00 flags +02 modifier count +04 shift states minus one
// +06 dead keys +0a modifier table +0c shift masks of state 1 and up
// +0e character table pointers +10 key table pointers
// +12 entry counts +14 VkKeyScan result per state
// +16 scan codes +18 virtual keys +1a their count
// +1c virtual key to character +1e dead key list
// +20 composition table +22 ligature table
// +24 the bitmap of the keys caps lock shifts
//
// `Layout` below is that record in native form. The two installed layouts are
// in x86_keyboard_translate.cpp, the entry counts and shift states of the
// original.
namespace Win98KeyboardTranslate
{
// The modifier table of every Win98 layout: three keys, bit 80h of the key
// state each. KEYBOARD.DRV Windows 98 walks it and builds the shift mask from the
// bottom bit up, so bit 0 is ALT, bit 1 is SHIFT and bit 2 is CONTROL.
constexpr unsigned ModifierAlt = 1, ModifierShift = 2, ModifierControl = 4;
// Record flags. Bit 02h lets the caps-lock bitmap shift a key, bit 40h makes
// caps lock shift every key and brings in the numeric-pad exception.
constexpr unsigned FlagCapsBitmap = 0x02, FlagCapsAll = 0x40;
// The dead key list holds four bytes per entry when bit 20h is set: the
// character in the low byte and the shift mask above it.
constexpr unsigned FlagWideDeadKeys = 0x20;

struct Shift
{
    const unsigned char *keys;  // record +10h entry: the virtual keys
    const unsigned char *chars; // record +0eh entry: their characters
    unsigned char count;        // record +12h entry
    unsigned char mask;         // record +0ch entry, the modifier bits
    unsigned char result;       // record +14h entry, the VkKeyScan high byte
};

struct Compose
{
    unsigned char dead, base, result;
};

struct Layout
{
    unsigned language;
    unsigned flags;
    unsigned char states;
    const Shift *state;
    const unsigned char *caps; // 32 bytes, one bit per virtual key
    const unsigned char *dead;
    unsigned char deads;
    const Compose *compose;
    unsigned char composes;
};

// The three globals of the driver's data segment: the scan code of the numeric
// key that is building an Alt sequence, the flag that the sequence is
// still zero and the dead key that waits for its base character

struct Pending
{
    unsigned char numeric_scan = 0;
    unsigned char numeric_zero = 0;
    unsigned char dead = 0;
    unsigned numeric_value = 0;
};

// KEYBOARD.DRV supplies fallback tables for every layout. They map control
// and numeric-pad keys to characters, selected by the shift mask.
struct Driver
{
    // RETURN, BACK, SPACE, then A to Z.
    static const unsigned char *Letters()
    {
        static const unsigned char keys[29] = {
            0x0d, 0x08, 0x20, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
            'M',  'N',  'O',  'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};
        return keys;
    }
    // LF, DEL, space, then 1 to 26.
    static const unsigned char *LetterChars()
    {
        static const unsigned char chars[29] = {
            0x0a, 0x7f, 0x20, 1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
            13,   14,   15,   16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};
        return chars;
    }
    // the numeric pad, ESCAPE, CANCEL, TAB, RETURN, BACK, SPACE and
    // the five numeric operators.
    static const unsigned char *Numeric()
    {
        static const unsigned char keys[21] = {0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
                                               0x67, 0x68, 0x69, 0x1b, 0x03, 0x09, 0x0d,
                                               0x08, 0x20, 0x6f, 0x6a, 0x6d, 0x6b, 0x6e};
        return keys;
    }

    static const unsigned char *NumericChars()
    {
        static const unsigned char chars[21] = {'0',  '1',  '2',  '3',  '4',  '5',  '6',
                                                '7',  '8',  '9',  0x1b, 0x03, 0x09, 0x0d,
                                                0x08, 0x20, '/',  '*',  '-',  '+',  '.'};
        return chars;
    }
    // The selection : the shift mask picks a key list, a character
    // list and a count. Masks 3, 5 and 7 have no entry and translate nothing.
    static bool Select(unsigned mask, const unsigned char *&keys, const unsigned char *&chars,
                       unsigned &count)
    {
        switch (mask & 7)
        {
        case 0: keys = Numeric(); chars = NumericChars(); count = 21; return true;
        case 1: keys = Numeric() + 14; chars = NumericChars() + 14; count = 7; return true;
        case 2: keys = Numeric() + 12; chars = NumericChars() + 12; count = 9; return true;
        case 4: keys = Letters(); chars = LetterChars(); count = 29; return true;
        case 6: keys = Letters() + 3; chars = LetterChars() + 3; count = 26; return true;
        default: return false;
        }
    }
    // accumulates an Alt sequence over these ten scan codes, the numeric
    // pad from 0 to 9.
    static const unsigned char *NumericScans()
    {
        static const unsigned char scans[10] = {0x52, 0x4f, 0x50, 0x51, 0x4b,
                                                0x4c, 0x4d, 0x47, 0x48, 0x49};
        return scans;
    }
};

inline int Find(const unsigned char *list, unsigned count, unsigned value)
{
    for (unsigned i = 0; i < count; ++i)
        if (list[i] == (value & 255)) return int(i);
    return -1;
}

// the modifier bits, then the two caps-lock rules of the
// record flags, then the numeric-pad exception.
inline unsigned ShiftMask(const Layout &layout, unsigned vk, const unsigned char *keystate)
{
    unsigned mask = 0;
    static const unsigned char modifier[3] = {0x12, 0x10, 0x11};
    for (unsigned i = 0; i < 3; ++i)
        if (keystate[modifier[i]] & 0x80) mask |= 1u << i;
    // the bitmap decides for this key, the toggle bit of caps lock
    // decides for this moment, and flag 02h lets the pair through.
    const unsigned capsbit = layout.caps[(vk & 255) >> 3] & (1u << (vk & 7)) ? 1u : 0u;
    mask ^= ((capsbit & keystate[0x14] & 1) << 1) & layout.flags;
    // without ALT and without CONTROL, flag 40h makes caps lock a shift
    // for every key.
    if (!(mask & (ModifierAlt | ModifierControl)) && (layout.flags & FlagCapsAll) &&
        (keystate[0x14] & 1))
        mask |= ModifierShift;
    // the same flag brings the numeric-pad exception. With num lock on
    // the pad keeps its digits and ignores the shift caps lock just added.
    if ((layout.flags & FlagCapsAll) && keystate[0x90] && vk >= 0x60 && vk <= 0x69)
        mask &= ~unsigned(ModifierShift);
    return mask & 255;
}

// the shift mask selects a state, state 0 is the mask that is zero and
// the others are the list at record +0ch.
inline const Shift *State(const Layout &layout, unsigned mask)
{
    if (!mask) return &layout.state[0];
    for (unsigned i = 1; i < layout.states; ++i)
        if (layout.state[i].mask == mask) return &layout.state[i];
    return nullptr;
}

// the character is a dead key of this layout.
inline bool IsDeadKey(const Layout &layout, unsigned mask, unsigned character)
{
    for (unsigned i = 0; i < layout.deads; ++i)
    {
        if (layout.flags & FlagWideDeadKeys)
        {
            // The four byte form carries the shift mask above the character.
            const unsigned entry = layout.dead[i * 4] | unsigned(layout.dead[i * 4 + 1]) << 8 |
                                   unsigned(layout.dead[i * 4 + 2]) << 16;
            if ((entry & 255) == character && (entry >> 16) == mask) return true;
        }
        else if (layout.dead[i] == character)
            return true;
    }
    return false;
}

// KEYBOARD.DRV. The return is the count of characters the caller gets:
// -1 for a dead key, 0 for nothing, 1 for one character and 2 for the dead key
// followed by its base character.
inline int ToAscii(const Layout &layout, Pending &pending, unsigned vk, unsigned scan,
                   const unsigned char *keystate, unsigned menu_flags, unsigned char out[2])
{
    out[0] = out[1] = 0;
    // a zero virtual key only resynchronizes the toggle keys.
    if (!(vk & 255)) return 0;
    if (scan & 0x8000)
    {
        // the key going up.
        if ((vk & 255) == 0x12)
        {
            // ALT lets go, and the Alt sequence becomes its character.
            const unsigned value = pending.numeric_value & 255;
            const unsigned leading_zero = pending.numeric_zero;
            pending.numeric_scan = 0;
            pending.numeric_zero = 0;
            pending.numeric_value = 0;
            if (!value) return 0;
            // without a leading zero the sequence is an OEM code point,
            // and the driver converts it to the ANSI page before handing it
            // out. The two pages agree below 80h; above it the conversion is
            // the caller's OemToChar, which is why the note names the limit.
            out[0] = (unsigned char)value;
            (void)leading_zero;
            return 1;
        }
        if ((scan & 255) == pending.numeric_scan) pending.numeric_scan = 0;
        return 0;
    }
    // ALT going down starts a new sequence.
    if ((vk & 255) == 0x12)
    {
        pending.numeric_zero = 0;
        pending.numeric_value = 0;
        return 0;
    }
    const unsigned mask = ShiftMask(layout, vk & 255, keystate);
    unsigned selector = mask;
    // ALT alone, no menu, and a numeric-pad scan code accumulates.
    if (mask == ModifierAlt && !(menu_flags & 1))
    {
        const int digit = Find(Driver::NumericScans(), 10, scan & 255);
        if (digit >= 0)
        {
            const unsigned previous = pending.numeric_scan;
            pending.numeric_scan = (unsigned char)(scan & 255);
            if (previous == (scan & 255)) return 0;
            if (menu_flags & 2) return 0;
            pending.numeric_value = (pending.numeric_value * 10 + unsigned(digit)) & 255;
            if (!pending.numeric_value) pending.numeric_zero = 1;
            return 0;
        }
        // ALT over any other key translates as if it were not held.
        selector = 0;
    }
    unsigned character = 0;
    const Shift *state = State(layout, selector);
    if (state)
    {
        const int index = Find(state->keys, state->count, vk & 255);
        if (index >= 0) character = state->chars[index];
    }
    if (!character)
    {
        // the layout has nothing, so the driver's own tables answer.
        if ((vk & 255) == 0x20 && pending.dead)
        {
            // Space after a dead key releases the dead key itself.
            out[0] = pending.dead;
            pending.dead = 0;
            return 1;
        }
        const unsigned char *keys = nullptr, *chars = nullptr;
        unsigned count = 0;
        if (!Driver::Select(mask, keys, chars, count)) return 0;
        const int index = Find(keys, count, vk & 255);
        if (index < 0) return 0;
        character = chars[index];
        if (pending.dead && character == 8)
        {
            // backspace after a dead key hands out both.
            out[0] = pending.dead;
            out[1] = 8;
            pending.dead = 0;
            return 2;
        }
        out[0] = (unsigned char)character;
        return 1;
    }
    if (pending.dead)
    {
        // the waiting dead key and this character look for their
        // composition; without one both characters go out.
        const unsigned dead = pending.dead;
        pending.dead = 0;
        for (unsigned i = 0; i < layout.composes; ++i)
            if (layout.compose[i].dead == dead && layout.compose[i].base == character)
            {
                out[0] = layout.compose[i].result;
                return 1;
            }
        out[0] = (unsigned char)dead;
        out[1] = (unsigned char)character;
        return 2;
    }
    if (IsDeadKey(layout, mask, character))
    {
        // the dead key waits and the caller sees minus one.
        pending.dead = (unsigned char)character;
        out[0] = (unsigned char)character;
        return -1;
    }
    out[0] = (unsigned char)character;
    return 1;
}

// KEYBOARD.DRV. The states are searched in order and the first
// character that matches wins; the answer is the state's result byte over the
// virtual key. FFFFh means the layout cannot produce the character.
inline unsigned VkKeyScan(const Layout &layout, unsigned character)
{
    for (unsigned s = 0; s < layout.states; ++s)
    {
        const Shift &state = layout.state[s];
        const int index = Find(state.chars, state.count, character & 255);
        if (index >= 0) return unsigned(state.result) << 8 | state.keys[index];
    }
    // the numeric characters of the driver's own table, then the five
    // control keys, then the control letters.
    const int numeric = Find(Driver::NumericChars(), 21, character & 255);
    if (numeric >= 0) return Driver::Numeric()[numeric];
    // over and its answers, all with CONTROL.
    static const unsigned char control[5] = {0x0a, 0x1c, 0x1d, 0x7f, 0x80};
    static const unsigned char control_keys[5] = {0x0d, 0xdc, 0xdd, 0x08, 0x32};
    const int index = Find(control, 5, character & 255);
    if (index >= 0) return 0x0200u | control_keys[index];
    // the remaining control codes are CONTROL over a letter.
    const unsigned code = (character & 255) - 1;
    if (code < 26) return 0x0200u | (code + 'A');
    return 0xffff;
}
} // namespace Win98KeyboardTranslate
