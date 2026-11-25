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
// low one when it turns off. StickyKeys, FilterKeys, MouseKeys, the time-out,
// SoundSentry and SerialKeys keep their settings without acting on input.
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
} // namespace

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
