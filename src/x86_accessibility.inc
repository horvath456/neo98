// SPDX-License-Identifier: GPL-2.0-or-later
// The accessibility settings of SystemParametersInfo (src/x86_user.cpp):
// FILTERKEYS, TOGGLEKEYS, MOUSEKEYS, SHOWSOUNDS, STICKYKEYS, ACCESSTIMEOUT,
// SERIALKEYS, SOUNDSENTRY, HIGHCONTRAST, KEYBOARDPREF and SCREENREADER,
// actions 32h to 47h. USER keeps them for the whole machine and Windows 98
// stores them under HKEY_CURRENT_USER\Control Panel\Accessibility, one
// string value per flag ("On", "Available", "HotKeyActive" and so on) and
// one per number, as Setup leaves them. The Accessibility Options of the
// control panel ask for each structure; without them every feature was
// greyed.
//
// What they do here: ShowSounds is the metric SM_SHOWSOUNDS that programs
// ask; High Contrast puts the colour scheme it names into effect when it is
// turned on and the scheme before it back when it is turned off; ToggleKeys
// sounds a high tone when Caps Lock, Num Lock or Scroll Lock turns on and a
// low one when it turns off. StickyKeys, MouseKeys and FilterKeys act on the
// keys in Application::RawKey (StickyKey, MouseKey, FilterKey below). The
// time-out, SoundSentry and SerialKeys keep their settings without acting.
namespace
{
struct AccessFlag
{
    unsigned bit;
    const char *name;
};
// A field of the structure after cbSize and dwFlags, kept in a number or,
// for a pointer to a string, in a text of the registry.
struct AccessField
{
    unsigned offset;
    const char *name;
    unsigned fallback;
    bool text;
    const char *fallback_text;
};
struct AccessSetting
{
    unsigned action, size;  // the get action (set is one more) and cbSize
    const char *key;        // under Control Panel\Accessibility
    const char *flags_name; // the flags as one number instead of one value each
    unsigned fallback;      // the flags of a missing key: the feature is available
    AccessFlag flags[9];
    AccessField fields[11];
};
const AccessSetting access_settings[] = {
    {0x32, 24, "KeyboardResponse", nullptr, 0x7e,
     {{1, "On"}, {2, "Available"}, {4, "HotKeyActive"}, {8, "ConfirmHotKey"}, {0x10, "OnOffFeedback"},
      {0x20, "ShowStatusIndicator"}, {0x40, "ClickOn"}},
     // The value of iWaitMSec is spelt so in the registry of Windows 98.
     {{8, "DelayBeforeAcceptancce", 1000}, {12, "AutoRepeatDelay", 1000}, {16, "AutoRepeatRate", 500},
      {20, "BounceTime", 0}}},
    {0x34, 8, "ToggleKeys", nullptr, 0x1a,
     {{1, "On"}, {2, "Available"}, {4, "HotKeyActive"}, {8, "ConfirmHotKey"}, {0x10, "OnOffFeedback"},
      {0x20, "ShowStatusIndicator"}},
     {}},
    {0x36, 28, "MouseKeys", nullptr, 0x3a,
     {{1, "On"}, {2, "Available"}, {4, "HotKeyActive"}, {8, "ConfirmHotKey"}, {0x10, "OnOffFeedback"},
      {0x20, "ShowStatusIndicator"}, {0x40, "Modifiers"}, {0x80, "ReplaceNumbers"}},
     {{8, "MaximumSpeed", 40}, {12, "TimeToMaximumSpeed", 300}, {16, "Control Speed", 80}}},
    {0x3a, 8, "Stickykeys", nullptr, 0x1fa,
     {{1, "On"}, {2, "Available"}, {4, "HotKeyActive"}, {8, "ConfirmHotKey"}, {0x10, "OnOffFeedback"},
      {0x20, "ShowStatusIndicator"}, {0x40, "AudibleFeedback"}, {0x80, "TriState"}, {0x100, "TwoKeysOff"}},
     {}},
    {0x3c, 12, "TimeOut", nullptr, 3, {{1, "On"}, {2, "OnOffFeedback"}}, {{8, "TimeToWait", 300000}}},
    {0x3e, 28, "SerialKeys", "Flags", 2, {},
     {{8, "ActivePort", 0, true, "COM1"}, {12, "Port", 0, true, ""}, {16, "BaudRate", 300},
      {20, "PortState", 1}, {24, "Active", 0}}},
    {0x40, 48, "SoundSentry", nullptr, 2, {{1, "On"}, {2, "Available"}, {4, "ShowStatusIndicator"}},
     {{8, "TextEffect", 2}, {12, "TextEffectMSec", 500}, {16, "TextEffectColorBits", 0}, {20, "GrafEffect", 3},
      {24, "GrafEffectMSec", 500}, {28, "GrafEffectColor", 0}, {32, "WindowsEffect", 1},
      {36, "WindowsEffectMSec", 500}, {40, "WindowsEffectDLL", 0, true, ""}, {44, "WindowsEffectOrdinal", 0}}},
    {0x42, 12, "HighContrast", nullptr, 0x5a,
     {{1, "On"}, {2, "Available"}, {4, "HotKeyActive"}, {8, "ConfirmHotKey"}, {0x10, "OnOffFeedback"},
      {0x20, "ShowStatusIndicator"}, {0x40, "HotKeyAvailable"}},
     {{8, "High Contrast Scheme", 0, true, "High Contrast Black"}}},
};
constexpr unsigned AccessCount = sizeof access_settings / sizeof *access_settings;
constexpr unsigned AccessTexts = 2, AccessText = 128;
// The settings of the machine: the bytes of each structure behind cbSize,
// its texts, and the three switches. Read from the registry once.
struct AccessState
{
    bool loaded = false;
    unsigned values[AccessCount][12] = {};
    char texts[AccessCount][AccessTexts][AccessText] = {};
    bool show_sounds = false, keyboard_preference = false, screen_reader = false;
} access_state;
const char AccessKey[] = "Control Panel\\Accessibility";

unsigned AccessOpen(const char *sub, bool create)
{
    auto &registry = Registry::System();
    char path[96];
    snprintf(path, sizeof path, sub ? "%s\\%s" : "%s", AccessKey, sub);
    unsigned key = Registry::Missing;
    bool created = false;
    return registry.Open(registry.roots[1], path, create, key, created) ? Registry::Missing : key;
}
// A number of the registry: a string of decimal digits, as Windows 98 keeps
// them, or a DWORD.
bool AccessNumber(unsigned key, const char *name, unsigned &number)
{
    const Registry::Value *value = nullptr;
    if (key == Registry::Missing || Registry::System().Query(key, name, value) || !value || !value->data)
        return false;
    if (value->type == 4 && value->size >= 4)
    {
        memcpy(&number, value->data, 4);
        return true;
    }
    if (value->type != 1)
        return false;
    unsigned result = 0;
    bool digits = false;
    for (unsigned i = 0; i < value->size && value->data[i] >= '0' && value->data[i] <= '9'; ++i)
    {
        result = result * 10 + unsigned(value->data[i] - '0');
        digits = true;
    }
    if (digits)
        number = result;
    return digits;
}
bool AccessString(unsigned key, const char *name, char (&out)[AccessText])
{
    const Registry::Value *value = nullptr;
    if (key == Registry::Missing || Registry::System().Query(key, name, value) || !value || !value->data ||
        value->type != 1)
        return false;
    unsigned n = 0;
    for (; n + 1 < AccessText && n < value->size && value->data[n]; ++n)
        out[n] = char(value->data[n]);
    out[n] = 0;
    return true;
}
void AccessWrite(unsigned key, const char *name, const char *text)
{
    Registry::System().Set(key, name, 1, text, unsigned(strlen(text)) + 1);
}
void AccessWriteNumber(unsigned key, const char *name, unsigned number)
{
    char text[16];
    snprintf(text, sizeof text, "%u", number);
    AccessWrite(key, name, text);
}

AccessState &AccessLoaded()
{
    auto &s = access_state;
    if (s.loaded)
        return s;
    s.loaded = true;
    for (unsigned i = 0; i < AccessCount; ++i)
    {
        const auto &setting = access_settings[i];
        const unsigned key = AccessOpen(setting.key, false);
        unsigned flags = setting.fallback;
        if (setting.flags_name)
            AccessNumber(key, setting.flags_name, flags);
        else
            for (const auto &flag : setting.flags)
            {
                unsigned on = 0;
                if (flag.name && AccessNumber(key, flag.name, on))
                    flags = on ? flags | flag.bit : flags & ~flag.bit;
            }
        s.values[i][1] = flags;
        unsigned text = 0;
        for (const auto &field : setting.fields)
        {
            if (!field.name)
                break;
            if (field.text)
            {
                char(&out)[AccessText] = s.texts[i][text++ % AccessTexts];
                if (!AccessString(key, field.name, out))
                    snprintf(out, sizeof out, "%s", field.fallback_text);
                continue;
            }
            unsigned number = field.fallback;
            AccessNumber(key, field.name, number);
            s.values[i][field.offset / 4] = number;
        }
    }
    unsigned on = 0;
    s.show_sounds = AccessNumber(AccessOpen("ShowSounds", false), "On", on) && on;
    const unsigned top = AccessOpen(nullptr, false);
    s.keyboard_preference = AccessNumber(top, "Keyboard Preference", on) && on;
    s.screen_reader = AccessNumber(top, "Blind Access", on) && on;
    // SM_SHOWSOUNDS is ShowSounds.
    User::SystemSettings().SetWord(0x2de + 70 * 2, s.show_sounds ? 1 : 0);
    return s;
}
void AccessSave(unsigned index)
{
    const auto &setting = access_settings[index];
    const auto &s = access_state;
    const unsigned key = AccessOpen(setting.key, true);
    if (key == Registry::Missing)
        return;
    const unsigned flags = s.values[index][1];
    if (setting.flags_name)
        AccessWriteNumber(key, setting.flags_name, flags);
    for (const auto &flag : setting.flags)
        if (flag.name)
            AccessWrite(key, flag.name, (flags & flag.bit) ? "1" : "0");
    unsigned text = 0;
    for (const auto &field : setting.fields)
    {
        if (!field.name)
            break;
        if (field.text)
            AccessWrite(key, field.name, s.texts[index][text++ % AccessTexts]);
        else
            AccessWriteNumber(key, field.name, s.values[index][field.offset / 4]);
    }
}
// StickyKeys of the keyboard: the modifiers it holds for the next key,
// Shift, Ctrl and Alt by bit (1, 2, 4), the left key and scan code each was
// pressed with, the modifier pressed alone so far, and the presses of Shift
// in a row for the hot key.
struct StickyState
{
    unsigned latched = 0, locked = 0, alone = 0, shifts = 0;
    unsigned vk[3] = {0xa0, 0xa2, 0xa4}, scan[3] = {0x2a, 0x1d, 0x38};
} sticky_state;
constexpr unsigned StickyIndex = 3, StickyOn = 1, StickyHotKey = 4, StickyHotKeySound = 0x10,
                   StickyAudible = 0x40, StickyTriState = 0x80, StickyTwoKeysOff = 0x100;
unsigned StickyBit(unsigned key)
{
    return key == 0x10 ? 1 : key == 0x11 ? 2 : key == 0x12 ? 4 : 0;
}
// MouseKeys: the button the keypad clicks (1 left, 2 right, 3 both), and the
// key that moves the pointer and since when.
struct MouseKeysState
{
    unsigned button = 1, key = 0, since = 0;
} mouse_keys_state;
constexpr unsigned MouseKeysIndex = 2, MouseKeysOn = 1, MouseKeysModifiers = 0x40, MouseKeysNumbers = 0x80;
// FilterKeys: when each key went up last (BounceKeys) and whether its press
// was dropped; the key held now, since when, since when it counts and its
// last repeat, and the press SlowKeys holds back.
struct FilterState
{
    unsigned up[256] = {}, key = 0, since = 0, counted = 0, last = 0;
    bool bounced[256] = {}, upped[256] = {}, accepted = false;
    User::InputPacket pending{};
} filter_state;
constexpr unsigned FilterIndex = 0, FilterOn = 1, FilterClick = 0x40;
// The hot keys of MouseKeys and FilterKeys: the flag that turns the feature
// on (MKF_MOUSEKEYSON, FKF_FILTERKEYSON), the one that lets the hot key act
// and the one that sounds it; and since when Right Shift is held, and
// whether its hot key has acted in this hold.
constexpr unsigned HotKeyActive = 4, HotKeySound = 0x10, FilterHotKeyHold = 8000;
struct HotKeyState
{
    unsigned right_shift_since = 0;
    bool right_shift_held = false, right_shift_acted = false;
} hot_key_state;
void StickyTone(unsigned frequency)
{
    if (access_state.values[StickyIndex][1] & StickyAudible)
        Sound::Speaker::Tone(frequency, 60);
}
} // namespace

// StickyKeys (SKF_STICKYKEYSON): Shift, Ctrl or Alt pressed and released
// alone stays down for the next key and goes up after it (latched); with
// SKF_TRISTATE a second press keeps it down until a third one (locked), else
// the second press lets it go. The modifier is up for the keyboard, so the
// host's modifiers agree, and down in the key state each key carries, which
// makes Ctrl and Alt the keys the program sees (Alt makes a system key). A
// key pressed while a modifier is held turns StickyKeys off with
// SKF_TWOKEYSOFF, and Shift pressed five times in a row turns it on or off
// with SKF_HOTKEYACTIVE (the confirmation of SKF_CONFIRMHOTKEY is not
// shown), a rising or a falling tone with SKF_HOTKEYSOUND. SKF_AUDIBLEFEEDBACK
// sounds a tone when a modifier latches, locks or goes. Answers whether the
// key goes no further; `release` asks for the latched modifiers to go up
// after it (StickyRelease).
bool Application::StickyKey(User::InputPacket &packet, bool down, bool repeat, bool &release)
{
    auto &s = AccessLoaded();
    auto &k = sticky_state;
    unsigned &flags = s.values[StickyIndex][1];
    const unsigned bit = packet.kind == 1 ? StickyBit(packet.key) : 0;
    if (packet.kind == 1 && down && !repeat)
    {
        k.shifts = packet.key == 0x10 ? k.shifts + 1 : 0;
        if (k.shifts == 5)
        {
            k.shifts = 0;
            if (flags & StickyHotKey)
            {
                flags ^= StickyOn;
                if (flags & StickyHotKeySound)
                    Sound::Speaker::Tone((flags & StickyOn) ? 1600 : 400, 150);
                if (!(flags & StickyOn))
                    StickyRelease(true);
                k.alone = 0;
                return false;
            }
        }
    }
    if (!(flags & StickyOn))
    {
        if (k.latched | k.locked)
            StickyRelease(true);
        return false;
    }
    if (bit)
    {
        if (down)
        {
            if (!repeat)
                k.alone = bit;
            return false;
        }
        if (k.alone != bit)
        {
            k.alone = 0;
            return false;
        }
        k.alone = 0;
        if (k.locked & bit)
        {
            k.locked &= ~bit;
            StickyTone(500);
            return false;
        }
        if (k.latched & bit)
        {
            k.latched &= ~bit;
            if (flags & StickyTriState)
            {
                k.locked |= bit;
                StickyTone(1400);
                return true;
            }
            StickyTone(500);
            return false;
        }
        k.latched |= bit;
        const unsigned index = bit == 1 ? 0 : bit == 2 ? 1 : 2;
        k.vk[index] = 0xa0 + 2 * index;
        k.scan[index] = (packet.parameter >> 16) & 255;
        StickyTone(1000);
        return true;
    }
    if (packet.kind == 1 && down && !repeat)
    {
        k.alone = 0;
        if ((flags & StickyTwoKeysOff) &&
            (User::PhysicalKeyDown(0x10) || User::PhysicalKeyDown(0x11) || User::PhysicalKeyDown(0x12)))
        {
            flags &= ~StickyOn;
            if (flags & StickyHotKeySound)
                Sound::Speaker::Tone(400, 150);
            StickyRelease(true);
            return false;
        }
    }
    const unsigned held = k.latched | k.locked;
    if (!held || packet.kind != 1)
        return false;
    for (unsigned i = 0; i < 3; ++i)
        if (held & (1u << i))
        {
            packet.state[0x10 + i] |= 0x80;
            packet.state[0xa0 + 2 * i] |= 0x80;
        }
    if (held & 4)
    {
        packet.parameter |= 0x20000000;
        if (!(held & 2) && !User::PhysicalKeyDown(0x11))
            packet.message = 0x104 + (down ? 0 : 1);
    }
    if (!down && k.latched)
        release = true;
    return false;
}
// MouseKeys (MKF_MOUSEKEYSON): the numeric keypad is the mouse, with
// MKF_REPLACENUMBERS while Num Lock is on (its digits), else while it is off
// (the keys of the keypad that are not extended). 1 to 9 but 5 move the
// pointer in their direction, one pixel at the first press and faster while
// the key repeats, up to an eighth of iMaxSpeed a step after
// iTimeToMaxSpeed milliseconds; with MKF_MODIFIERS Ctrl makes the steps four
// times as long and Shift one pixel. 5 clicks the chosen button, + double
// clicks it, / chooses the left button, * both, - the right one; 0 (Ins)
// holds the button down and the decimal point (Del) lets it go. Answers
// whether the key was MouseKeys', which then goes no further.
bool Application::MouseKey(unsigned key, bool extended, bool down, bool repeat)
{
    auto &s = AccessLoaded();
    auto &m = mouse_keys_state;
    const unsigned flags = s.values[MouseKeysIndex][1];
    if (!(flags & MouseKeysOn))
        return false;
    // The keypad by its digit: 0 to 9, 10 the decimal point, 11 to 14 for
    // / * - +; -1 for another key.
    int digit = -1;
    if (key == 0x6f) digit = 11;
    else if (key == 0x6a) digit = 12;
    else if (key == 0x6d) digit = 13;
    else if (key == 0x6b) digit = 14;
    else if (flags & MouseKeysNumbers)
    {
        if (key >= 0x60 && key <= 0x69) digit = int(key - 0x60);
        else if (key == 0x6e) digit = 10;
    }
    else if (!extended)
    {
        static const unsigned keys[11] = {0x2d, 0x23, 0x28, 0x22, 0x25, 0x0c, 0x27, 0x24, 0x26, 0x21, 0x2e};
        for (int i = 0; i < 11; ++i)
            if (keys[i] == key) digit = i;
    }
    if (digit < 0)
        return false;
    if (!down)
        return true;
    Gdi::POINT p{};
    User::CursorPosition(&p);
    const unsigned buttons = User::PhysicalMouseButtons();
    const unsigned chosen = m.button == 3 ? 3u : m.button == 2 ? 2u : 1u;
    static const int dx[10] = {0, -1, 0, 1, -1, 0, 1, -1, 0, 1}, dy[10] = {0, 1, 1, 1, 0, 0, 0, -1, -1, -1};
    if (digit >= 1 && digit <= 9 && digit != 5)
    {
        const unsigned now = unsigned(Now() / 1000);
        if (!repeat || m.key != key)
        {
            m.key = key;
            m.since = now;
        }
        const unsigned top = s.values[MouseKeysIndex][2] / 8 ? s.values[MouseKeysIndex][2] / 8 : 1;
        const unsigned time = s.values[MouseKeysIndex][3] ? s.values[MouseKeysIndex][3] : 1;
        const unsigned elapsed = now - m.since < time ? now - m.since : time;
        int step = repeat ? int(1 + (top - 1) * elapsed / time) : 1;
        if (flags & MouseKeysModifiers)
        {
            if (User::PhysicalKeyDown(0x11)) step *= 4;
            if (User::PhysicalKeyDown(0x10)) step = 1;
        }
        MouseButtons(p.x + dx[digit] * step, p.y + dy[digit] * step, buttons);
        return true;
    }
    if (repeat)
        return true;
    if (digit == 11 || digit == 12 || digit == 13)
        m.button = digit == 11 ? 1 : digit == 12 ? 3 : 2;
    else if (digit == 0)
        MouseButtons(p.x, p.y, buttons | chosen);
    else if (digit == 10)
        MouseButtons(p.x, p.y, buttons & ~chosen);
    else
        for (int i = digit == 14 ? 2 : 1; i > 0; --i)
        {
            MouseButtons(p.x, p.y, buttons | chosen);
            MouseButtons(p.x, p.y, buttons & ~chosen);
        }
    return true;
}
// FilterKeys (FKF_FILTERKEYSON). BounceKeys, with iBounceMSec: a press of a
// key within that time after it went up is dropped with its release.
// Otherwise SlowKeys, with iWaitMSec: a key counts once it is held that
// long, its press goes on at its first repeat or its release after that
// time, and a shorter press is dropped; and RepeatKeys: a held key repeats
// after iDelayMSec, every iRepeatMSec at most, and not at all when that is
// zero. FKF_CLICKON clicks when a key counts. The modifiers and the lock keys
// pass as they are; the hot key is AccessibilityHotKey's. Answers whether
// the packet goes no further.
bool Application::FilterKey(User::InputPacket &packet, bool down, bool repeat)
{
    auto &s = AccessLoaded();
    const unsigned flags = s.values[FilterIndex][1];
    const unsigned vk = packet.key & 255;
    if (!(flags & FilterOn) || packet.kind != 1 || (vk >= 0x10 && vk <= 0x14) || vk == 0x90 || vk == 0x91 ||
        (vk >= 0xa0 && vk <= 0xa5) || vk == 0x5b || vk == 0x5c)
        return false;
    auto &f = filter_state;
    const unsigned wait = s.values[FilterIndex][2], delay = s.values[FilterIndex][3],
                   rate = s.values[FilterIndex][4], bounce = s.values[FilterIndex][5];
    const unsigned now = unsigned(Now() / 1000);
    auto click = [&] {
        if (flags & FilterClick)
            Sound::Speaker::Tone(2000, 5);
    };
    if (bounce)
    {
        if (down && !repeat)
            f.bounced[vk] = f.upped[vk] && now - f.up[vk] < bounce;
        if (down)
            return f.bounced[vk];
        const bool dropped = f.bounced[vk];
        f.bounced[vk] = false;
        if (!dropped)
        {
            f.up[vk] = now;
            f.upped[vk] = true;
        }
        return dropped;
    }
    if (down && !repeat)
    {
        f.key = vk;
        f.since = f.counted = f.last = now;
        f.accepted = !wait;
        if (wait)
        {
            f.pending = packet;
            return true;
        }
        click();
        return false;
    }
    if (vk != f.key)
        return false;
    if (down)
    {
        if (!f.accepted)
        {
            if (now - f.since >= wait)
            {
                f.accepted = true;
                f.counted = f.last = now;
                EnqueueInput(f.pending);
                click();
            }
            return true;
        }
        if (!rate || now - f.counted < delay || now - f.last < rate)
            return true;
        f.last = now;
        return false;
    }
    f.key = 0;
    if (f.accepted)
        return false;
    if (now - f.since < wait)
        return true;
    EnqueueInput(f.pending);
    click();
    return false;
}
// The hot keys the shortcuts of ACCESS.CPL name and ENABLE3.DLL confirms:
// Left Alt+Left Shift+Num Lock turns MouseKeys on or off, Right Shift held
// for eight seconds FilterKeys, each with MKF_HOTKEYACTIVE or
// FKF_HOTKEYACTIVE, and with the HOTKEYSOUND flag a rising tone when it goes
// on and a falling one when it goes off, as the hot key of StickyKeys. The
// hold is seen at a repeat of the key, as the keyboard repeats it; the keys
// themselves go on as they are. The confirmation of the CONFIRMHOTKEY flags
// is not shown.
void Application::AccessibilityHotKey(const User::InputPacket &packet, bool down, bool repeat)
{
    if (packet.kind != 1)
        return;
    auto &s = AccessLoaded();
    auto toggle = [&](unsigned &flags, unsigned on) {
        if (!(flags & HotKeyActive))
            return;
        flags ^= on;
        if (flags & HotKeySound)
            Sound::Speaker::Tone((flags & on) ? 1600 : 400, 150);
    };
    if (packet.key == 0x90 && down && !repeat && User::PhysicalKeyDown(0xa4) && User::PhysicalKeyDown(0xa0))
        toggle(s.values[MouseKeysIndex][1], MouseKeysOn);
    auto &h = hot_key_state;
    const bool right_shift = packet.key == 0x10 && ((packet.parameter >> 16) & 0xff) == 0x36;
    if (!right_shift)
        return;
    const unsigned now = unsigned(Now() / 1000);
    if (!down)
    {
        h.right_shift_held = false;
        return;
    }
    if (!repeat || !h.right_shift_held)
    {
        h.right_shift_held = true;
        h.right_shift_acted = false;
        h.right_shift_since = now;
        return;
    }
    if (!h.right_shift_acted && now - h.right_shift_since >= FilterHotKeyHold)
    {
        h.right_shift_acted = true;
        toggle(s.values[FilterIndex][1], FilterOn);
    }
}
// The latched modifiers go up after the key they were held for, and with
// `all` the locked ones too. Alt goes up as a plain WM_KEYUP: a key went by
// under it.
void Application::StickyRelease(bool all)
{
    auto &k = sticky_state;
    const unsigned held = k.latched | (all ? k.locked : 0);
    k.latched = 0;
    if (all)
        k.locked = 0;
    for (unsigned i = 0; i < 3; ++i)
        if (held & (1u << i))
        {
            auto up = User::CaptureKey(k.vk[i], 0, false, k.scan[i], false);
            if (i == 2)
                up.message = 0x101;
            EnqueueInput(up);
        }
}

// ToggleKeys: a high tone when a lock key turns its light on, a low one when
// it turns it off.
void Application::AccessibilityKey(unsigned key, bool down, bool repeat)
{
    if (!down || repeat || (key != 0x14 && key != 0x90 && key != 0x91))
        return;
    const auto &s = AccessLoaded();
    if (!(s.values[1][1] & 1))
        return;
    const bool on = (User::GetKeyState(key) & 1) != 0;
    Sound::Speaker::Tone(on ? 1200 : 400, 100);
}

// High Contrast: the colours, the metrics of the frame and the icon font of
// a scheme of HKEY_CURRENT_USER\Control Panel\Appearance\Schemes, the
// structure Display Properties writes there: a version, NONCLIENTMETRICS,
// the LOGFONT of the icon titles and the colours, which then is the current
// scheme.
bool Application::ApplyAppearanceScheme(const char *name)
{
    auto &registry = Registry::System();
    unsigned key = Registry::Missing;
    bool created = false;
    const Registry::Value *value = nullptr;
    if (!name || !*name ||
        registry.Open(registry.roots[1], "Control Panel\\Appearance\\Schemes", false, key, created) ||
        registry.Query(key, name, value) || !value || !value->data || value->type != 3 ||
        value->size < 4 + 340 + 60 + 25 * 4)
        return false;
    unsigned char scheme[4 + 340 + 60 + 29 * 4] = {};
    const unsigned bytes = value->size < sizeof scheme ? value->size : unsigned(sizeof scheme);
    memcpy(scheme, value->data, bytes);
    const unsigned colours = (bytes - 404) / 4;
    const unsigned block = Allocate(340 + 60 + colours * 8);
    if (!block)
        return false;
    Store(block, scheme + 4, 340);
    Write32(block, 340);
    Store(block + 340, scheme + 344, 60);
    for (unsigned i = 0; i < colours; ++i)
    {
        unsigned colour = 0;
        memcpy(&colour, scheme + 404 + i * 4, 4);
        Write32(block + 400 + i * 4, i);
        Write32(block + 400 + colours * 4 + i * 4, colour & 0xffffff);
    }
    const unsigned metrics[] = {0x2a, 340, block, 3}, icon_font[] = {0x22, 60, block + 340, 3};
    Api("SystemParametersInfoA", metrics);
    Api("SystemParametersInfoA", icon_font);
    const unsigned set[] = {colours, block + 400, block + 400 + colours * 4};
    Api("SetSysColors", set);
    Free(block);
    unsigned appearance = Registry::Missing;
    if (!registry.Open(registry.roots[1], "Control Panel\\Appearance", true, appearance, created))
        AccessWrite(appearance, "Current", name);
    return true;
}

unsigned Application::AccessibilityParameters(const unsigned *a)
{
    auto &s = AccessLoaded();
    const unsigned action = a[0];
    const bool set = action & 1;
    auto changed = [&] {
        if (a[3] & 2)
            BroadcastMessage(Win98Broadcast::Send, 0x1a, action, 0);
        return 1u;
    };
    // The three switches: a BOOL behind pvParam, set from uiParam.
    if (action == 0x38 || action == 0x39 || (action >= 0x44 && action <= 0x47))
    {
        bool &value = action <= 0x39 ? s.show_sounds : action <= 0x45 ? s.keyboard_preference : s.screen_reader;
        if (!set)
        {
            const unsigned answer = value ? 1 : 0;
            if (!a[2] || !virtual_memory.Access(a[2], 4, true))
            {
                last_error = 87;
                return 0;
            }
            return Store(a[2], &answer, 4) ? 1 : 0;
        }
        value = a[1] != 0;
        if (action == 0x39)
            User::SystemSettings().SetWord(0x2de + 70 * 2, value ? 1 : 0);
        if (a[3] & 1)
        {
            const unsigned key = AccessOpen(action == 0x39 ? "ShowSounds" : nullptr, true);
            if (key != Registry::Missing)
                AccessWrite(key, action == 0x39 ? "On" : action == 0x45 ? "Keyboard Preference" : "Blind Access",
                            value ? "1" : "0");
        }
        return changed();
    }
    unsigned index = AccessCount;
    for (unsigned i = 0; i < AccessCount; ++i)
        if (access_settings[i].action == (action & ~1u))
            index = i;
    if (index == AccessCount)
    {
        last_error = 120;
        return 0;
    }
    const auto &setting = access_settings[index];
    // The structure says its size; another one is refused.
    if (!a[2] || !virtual_memory.Access(a[2], setting.size, set ? false : true) || Read32(a[2]) != setting.size)
    {
        last_error = 87;
        return 0;
    }
    if (!set)
    {
        // A string goes into the buffer the caller's pointer names; the
        // pointer of SoundSentry's effect library is reserved.
        unsigned buffers[AccessTexts] = {}, text = 0;
        for (const auto &field : setting.fields)
            if (field.name && field.text && text < AccessTexts)
                buffers[text++] = action == 0x40 ? 0 : Read32(a[2] + field.offset);
        for (unsigned offset = 4; offset < setting.size; offset += 4)
            Write32(a[2] + offset, s.values[index][offset / 4]);
        text = 0;
        for (const auto &field : setting.fields)
        {
            if (!field.name)
                break;
            if (!field.text || text >= AccessTexts)
                continue;
            const char *value = s.texts[index][text];
            const unsigned buffer = buffers[text++];
            Write32(a[2] + field.offset, buffer);
            const unsigned length = unsigned(strlen(value)) + 1;
            if (buffer && virtual_memory.Access(buffer, length, true))
                Store(buffer, value, length);
        }
        return 1;
    }
    const unsigned before = s.values[index][1];
    unsigned text = 0;
    for (unsigned offset = 4; offset < setting.size; offset += 4)
    {
        const AccessField *field = nullptr;
        for (const auto &f : setting.fields)
            if (f.name && f.offset == offset)
                field = &f;
        if (field && field->text)
        {
            char(&out)[AccessText] = s.texts[index][text++ % AccessTexts];
            const unsigned pointer = Read32(a[2] + offset);
            if (pointer && action != 0x41)
                snprintf(out, sizeof out, "%s", ReadString(pointer));
            continue;
        }
        s.values[index][offset / 4] = Read32(a[2] + offset);
    }
    // Whether a feature is available belongs to the machine, not the caller.
    const unsigned fixed = setting.flags_name ? 0u : 2u;
    s.values[index][1] = (s.values[index][1] & ~fixed) | (before & fixed);
    if (a[3] & 1)
        AccessSave(index);
    // High Contrast turned on puts its scheme into effect and remembers the
    // scheme before it; turned off, that one comes back.
    if (action == 0x43 && ((before ^ s.values[index][1]) & 1))
    {
        auto &registry = Registry::System();
        unsigned appearance = Registry::Missing, contrast = AccessOpen("HighContrast", true);
        bool created = false;
        char current[AccessText] = {};
        if (!registry.Open(registry.roots[1], "Control Panel\\Appearance", false, appearance, created))
            AccessString(appearance, "Current", current);
        if (s.values[index][1] & 1)
        {
            if (contrast != Registry::Missing && current[0])
                AccessWrite(contrast, "Pre-High Contrast Scheme", current);
            ApplyAppearanceScheme(s.texts[index][0]);
        }
        else
        {
            char previous[AccessText] = "Windows Standard";
            AccessString(contrast, "Pre-High Contrast Scheme", previous);
            ApplyAppearanceScheme(previous);
        }
    }
    return changed();
}
