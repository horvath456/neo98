// SPDX-License-Identifier: GPL-2.0-or-later
// Included by the patched libretro adapter. WINOLDAP paste produces keyboard
// events: programs such as EDIT hook IRQ1 instead of just reading BIOS keys.
extern unsigned KEYBOARD_Neo98Free();
extern unsigned BIOS_Neo98PasteRoom();
extern bool BIOS_Neo98CapsLock();
extern char DBP_DOS_KeyboardLayout_MapChar(char, bool &, bool &);
static bool DBP_Neo98PasteCharacter(unsigned char character)
{
    bool shift = false, altgr = false;
    unsigned char mapped = (unsigned char)DBP_DOS_KeyboardLayout_MapChar((char)character, shift, altgr);
    const KBD_KEYS key = character == '\n' || character == '\r' ? KBD_enter :
        mapped < RETROK_LAST ? (KBD_KEYS)dbp_keymap_retro2dos[mapped] : KBD_NONE;
    if (mapped >= 'a' && mapped <= 'z' && BIOS_Neo98CapsLock()) shift = !shift;
    // Reserve the entire make/break sequence, including modifiers, so a
    // full hardware buffer cannot leave a synthetic modifier held down.
    const unsigned events = key != KBD_NONE ? 2 + (shift ? 2 : 0) + (altgr ? 4 : 0) : 8;
    if (KEYBOARD_Neo98Free() < events) return false;
    if (key != KBD_NONE) {
        if (shift) KEYBOARD_AddKey(KBD_leftshift, true);
        if (altgr) KEYBOARD_AddKey(KBD_rightalt, true);
        KEYBOARD_AddKey(key, true);
        KEYBOARD_AddKey(key, false);
        if (altgr) KEYBOARD_AddKey(KBD_rightalt, false);
        if (shift) KEYBOARD_AddKey(KBD_leftshift, false);
    } else {
        // OEM characters without a key in the current layout use DOS's
        // Alt+numpad entry. These events pass the program's IRQ1 handler too.
        const KBD_KEYS digits[] = {KBD_kp0,KBD_kp1,KBD_kp2,KBD_kp3,KBD_kp4,
                                  KBD_kp5,KBD_kp6,KBD_kp7,KBD_kp8,KBD_kp9};
        KEYBOARD_AddKey(KBD_leftalt, true);
        for (unsigned divisor = 100; divisor; divisor /= 10) {
            const KBD_KEYS digit = digits[(character / divisor) % 10];
            KEYBOARD_AddKey(digit, true);
            KEYBOARD_AddKey(digit, false);
        }
        KEYBOARD_AddKey(KBD_leftalt, false);
    }
    return true;
}
