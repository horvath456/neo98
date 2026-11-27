// SPDX-License-Identifier: GPL-2.0-or-later
// The settings of the Control Panel that USER keeps as plain numbers: what
// it reads from the registry when it starts, and the actions of
// SystemParametersInfo that set, answer and store them (src/x86_user.cpp
// includes this file).
//
// USER reads each of them with one of two helpers of its start-up segment:
// an integer (RegOpenKey, RegQueryValueEx into 20 bytes, taken only when the
// value is a REG_SZ of some length, then the digits) and a string. Both look
// under HKEY_CURRENT_USER, in the keys named below, and nowhere else: these
// settings have no WIN.INI behind them on Windows 95 and Windows 98. The
// set actions store with the counterpart, a REG_SZ of "%d", when
// SPIF_UPDATEINIFILE is given, and the shared exit of the service broadcasts
// WM_SETTINGCHANGE when the value was stored and SPIF_SENDCHANGE is given.
namespace
{
constexpr char ControlDesktop[] = "Control Panel\\Desktop";
constexpr char ControlMouse[] = "Control Panel\\Mouse";
constexpr char ControlKeyboard[] = "Control Panel\\Keyboard";
constexpr char ControlSound[] = "Control Panel\\Sound";
constexpr char ControlColors[] = "Control Panel\\Colors";
constexpr char ControlMetrics[] = "Control Panel\\Desktop\\WindowMetrics";
// The dots per inch USER converts a stored size with (its word of the
// display's LOGPIXELS): a size of WindowMetrics is twips when negative, 1440
// to the inch, as LoadWindowMetrics in src/x86_user.cpp reads the others.
constexpr int ControlDotsPerInch = 96;
// The names of the system colours, strings 32 to 60 of USER; the colour of
// index i is stored under the name i.
const char *const ControlColorNames[29] = {
    "Scrollbar",      "Background",        "ActiveTitle",   "InactiveTitle",       "Menu",
    "Window",         "WindowFrame",       "MenuText",      "WindowText",          "TitleText",
    "ActiveBorder",   "InactiveBorder",    "AppWorkspace",  "Hilight",             "HilightText",
    "ButtonFace",     "ButtonShadow",      "GrayText",      "ButtonText",          "InactiveTitleText",
    "ButtonHilight",  "ButtonDkShadow",    "ButtonLight",   "InfoText",            "InfoWindow",
    "ButtonAlternateFace", "HotTrackingColor", "GradientActiveTitle", "GradientInactiveTitle"};
// The string of a value, at most `capacity` bytes with its end, as the
// string helper of USER reads it; empty when the key or the value is missing.
bool ControlText(const char *path, const char *name, char *text, unsigned capacity)
{
    text[0] = 0;
    auto &registry = Registry::System();
    unsigned key = Registry::Missing;
    bool created = false;
    const Registry::Value *value = nullptr;
    if (registry.Open(registry.roots[1], path, false, key, created) || registry.Query(key, name, value) ||
        !value || !value->data || !value->size || value->size > capacity)
        return false;
    memcpy(text, value->data, value->size);
    text[value->size < capacity ? value->size : capacity - 1] = 0;
    return true;
}
// The integer of a value: the default unless it is a REG_SZ of at most 20
// bytes; then its leading digits, with a sign.
int ControlNumber(const char *path, const char *name, int fallback)
{
    auto &registry = Registry::System();
    unsigned key = Registry::Missing;
    bool created = false;
    const Registry::Value *value = nullptr;
    if (registry.Open(registry.roots[1], path, false, key, created) || registry.Query(key, name, value) ||
        !value || !value->data || !value->size || value->size > 20 || value->type != 1)
        return fallback;
    int result = 0, sign = 1;
    unsigned at = 0;
    while (at < value->size && (value->data[at] == ' ' || value->data[at] == '\t'))
        ++at;
    if (at < value->size && value->data[at] == '-')
    {
        sign = -1;
        ++at;
    }
    for (; at < value->size && value->data[at] >= '0' && value->data[at] <= '9'; ++at)
        result = result * 10 + (value->data[at] - '0');
    return result * sign;
}
bool StoreControlText(const char *path, const char *name, const char *text)
{
    auto &registry = Registry::System();
    unsigned key = Registry::Missing;
    bool created = false;
    return !registry.Open(registry.roots[1], path, true, key, created) &&
           !registry.Set(key, name, 1, text, unsigned(strlen(text)) + 1);
}
bool StoreControlNumber(const char *path, const char *name, int number)
{
    char text[16];
    snprintf(text, sizeof text, "%d", number);
    return StoreControlText(path, name, text);
}
} // namespace
bool Application::BeepEnabled() { return User::SystemSettings().beep != 0; }
// The time a pointer rests on an item before its submenu opens
// (SPI_SETMENUSHOWDELAY), in milliseconds.
unsigned Application::MenuShowDelayTime() { return User::SystemSettings().menu_show_delay; }
// The typematic setting of the keyboard as the controller takes it: the
// low five bits of the word are the speed, 0 the slowest and 31 the fastest,
// bits 5 and 6 the delay in quarters of a second less one. The controller
// counts the rate the other way round, 0 the fastest: a period of
// (8 + A) * 2^B * 4.17 ms with A the low three bits and B the next two, which
// is 30 characters a second at one end and 2 at the other.
unsigned Application::TypematicDelay(unsigned word) { return (((word >> 5) & 3) + 1) * 250000u; }
unsigned Application::TypematicPeriod(unsigned word)
{
    const unsigned rate = 31 - (word & 31);
    return ((8 + (rate & 7)) << ((rate >> 3) & 3)) * 4170u;
}
// What USER reads once, when it starts.
void Application::LoadControlPanelSettings()
{
    auto &settings = User::SystemSettings();
    // The colours: the four the 3-D shades come from first, then the rest,
    // each "red green blue" in decimal under its name. The parser takes up
    // to four numbers, each a byte that wraps, and skips everything that is
    // no digit between them. A missing or empty value keeps the built-in
    // colour.
    static const unsigned char order[4] = {15, 16, 20, 25};
    for (unsigned turn = 0; turn < 4 + 29; ++turn)
    {
        const unsigned index = turn < 4 ? order[turn] : turn - 4;
        if (turn >= 4 && (index == 15 || index == 16 || index == 20 || index == 25))
            continue;
        char text[25];
        if (!ControlText(ControlColors, ControlColorNames[index], text, sizeof text) || !text[0])
            continue;
        unsigned char parts[4] = {};
        const char *at = text;
        for (unsigned part = 0; part < 4; ++part)
        {
            while (*at && (*at < '0' || *at > '9'))
                ++at;
            unsigned char number = 0;
            while (*at >= '0' && *at <= '9')
                number = (unsigned char)(number * 10 + (*at++ - '0'));
            parts[part] = number;
        }
        Win98Frame::SetSystemColor(index, unsigned(parts[0]) << 16 | unsigned(parts[1]) << 8 | parts[2]);
    }
    // The keyboard: without a stored speed the word is 15 and the keyboard is
    // left as it is; with one, the delay comes on top and the keyboard is set.
    const int speed = ControlNumber(ControlKeyboard, "KeyboardSpeed", -1);
    if (speed == -1)
        settings.keyboard = 15;
    else
    {
        settings.keyboard = (unsigned short)speed;
        const int delay = ControlNumber(ControlKeyboard, "KeyboardDelay", -1);
        if (delay != -1)
            settings.keyboard |= (unsigned short)((delay & 3) << 5);
        settings.keyboard &= 0x7f;
        settings.typematic = true;
    }
    // The mouse: the first threshold, the speed, and the second threshold
    // only at speed 2.
    settings.mouse[0] = (unsigned short)ControlNumber(ControlMouse, "MouseThreshold1", settings.mouse[0]);
    settings.mouse[2] = (unsigned short)ControlNumber(ControlMouse, "MouseSpeed", 1);
    if (settings.mouse[2] == 2)
        settings.mouse[1] = (unsigned short)ControlNumber(ControlMouse, "MouseThreshold2", settings.mouse[1]);
    settings.wheel_lines = (unsigned short)ControlNumber(ControlDesktop, "WheelScrollLines", 3);
    caret_blink = (unsigned)ControlNumber(ControlDesktop, "CursorBlinkRate", 500) & 0xffff;
    // SetDoubleClickTime takes 0 for the 500 ms of USER.
    User::SetDoubleClickTime((unsigned)ControlNumber(ControlMouse, "DoubleClickSpeed", 0) & 0xffff);
    settings.menu_show_delay = (unsigned short)ControlNumber(ControlDesktop, "MenuShowDelay", 400);
    // The sizes of the double click and of the start of a drag, and the side
    // menus drop to: system metrics 36, 37, 68, 69 and 40. A missing value
    // keeps the metric this port starts with.
    static const struct { const char *name; unsigned offset; } metrics[] = {
        {"DoubleClickWidth", 0x326}, {"DoubleClickHeight", 0x328}, {"DragWidth", 0x366},
        {"DragHeight", 0x368},       {"MenuDropAlignment", 0x32e}};
    for (const auto &metric : metrics)
        settings.SetWord(metric.offset,
                         (unsigned short)ControlNumber(ControlDesktop, metric.name, settings.Word(metric.offset)));
    // The grid of the icons, system metrics 38 and 39: twips when negative,
    // never below the size of an icon (metrics 11 and 12).
    static const struct { const char *name; unsigned offset, least; } spacing[] = {
        {"IconSpacing", 0x32a, 0x2f4}, {"IconVerticalSpacing", 0x32c, 0x2f6}};
    for (const auto &entry : spacing)
    {
        int size = ControlNumber(ControlMetrics, entry.name, 0);
        if (!size)
            continue;
        if (size < 0)
            size = (-size * ControlDotsPerInch + 720) / 1440;
        if (size < int(settings.Word(entry.least)))
            size = settings.Word(entry.least);
        if (size <= 0x7fff)
            settings.SetWord(entry.offset, (unsigned short)size);
    }
    // Whether a window shows its contents while it is dragged, whether
    // minimizing is animated, and the switches of the preferences mask.
    settings.drag_full = ControlNumber(ControlDesktop, "DragFullWindows", settings.drag_full) ? 1 : 0;
    settings.min_animate = ControlNumber(ControlMetrics, "MinAnimate", settings.min_animate) ? 1 : 0;
    {
        auto &registry = Registry::System();
        unsigned key = Registry::Missing;
        bool created = false;
        const Registry::Value *mask = nullptr;
        if (!registry.Open(registry.roots[1], ControlDesktop, false, key, created) &&
            !registry.Query(key, "UserPreferencemask", mask) && mask && mask->type == 3 && mask->data &&
            mask->size >= 4)
            memcpy(&settings.preferences, mask->data, 4);
    }
    // The beep is on unless the value starts with something else than the
    // 'Y' of the built-in "Yes", in either case.
    char beep[4];
    if (ControlText(ControlSound, "Beep", beep, sizeof beep) && beep[0])
        settings.beep = beep[0] == 'Y' || beep[0] == 'y';
}
// The actions of SystemParametersInfo this file serves; false for any other.
bool Application::ControlPanelParameters(const unsigned *a, unsigned &result)
{
    auto &settings = User::SystemSettings();
    const unsigned action = a[0], value = a[1] & 0xffff;
    const bool store = (a[3] & 1) != 0;
    // What the shared exit does: the value as "%d" under its name, and the
    // broadcast when that write succeeded and SPIF_SENDCHANGE asks for it.
    const auto finish = [&](const char *path, const char *name, int number) {
        if (store && StoreControlNumber(path, name, number) && (a[3] & 2))
            BroadcastMessage(Win98Broadcast::Send, 0x1a, action, 0);
        result = 1;
        return true;
    };
    // A scalar answer of the service is a word; the public entry widens it.
    const auto answer = [&](unsigned word) {
        if (!a[2] || !virtual_memory.Access(a[2], 4, true))
        {
            last_error = 87;
            result = 0;
            return true;
        }
        result = Store(a[2], &word, 4);
        return true;
    };
    // The switches of the preferences mask, 1000h to 100Fh: an even action
    // answers bit (action - 1000h) / 2, the odd one after it sets the bit
    // when the pointer is not null and clears it otherwise. Stored, the bit
    // goes into the four bytes of "UserPreferencemask" as they stand in
    // the registry. 1008h and 1009h, the gradient of the captions, have
    // their own branch in src/x86_user.cpp.
    if (action >= 0x1000 && action <= 0x100f && action != 0x1008 && action != 0x1009)
    {
        const unsigned bit = 1u << ((action - 0x1000) >> 1);
        if (!(action & 1))
            return answer(settings.preferences & bit ? 1 : 0);
        settings.preferences = a[2] ? settings.preferences | bit : settings.preferences & ~bit;
        result = 1;
        if (store)
        {
            auto &registry = Registry::System();
            unsigned key = Registry::Missing, mask = settings.preferences;
            bool created = false;
            const Registry::Value *stored = nullptr;
            if (registry.Open(registry.roots[1], ControlDesktop, true, key, created))
                return true;
            if (!registry.Query(key, "UserPreferencemask", stored) && stored && stored->type == 3 && stored->data &&
                stored->size >= 4)
            {
                memcpy(&mask, stored->data, 4);
                mask = a[2] ? mask | bit : mask & ~bit;
            }
            if (!registry.Set(key, "UserPreferencemask", 3, &mask, 4) && (a[3] & 2))
                BroadcastMessage(Win98Broadcast::Send, 0x1a, action, 0);
        }
        return true;
    }
    switch (action)
    {
    case 1: // SPI_GETBEEP
        return answer(settings.beep);
    case 2: // SPI_SETBEEP: stored as the "Yes" or "No" of USER's strings.
        settings.beep = value;
        if (store && StoreControlText(ControlSound, "Beep", value ? "Yes" : "No") && (a[3] & 2))
            BroadcastMessage(Win98Broadcast::Send, 0x1a, action, 0);
        result = 1;
        return true;
    case 3: // SPI_GETMOUSE: the two thresholds and the speed.
    {
        if (!a[2] || !virtual_memory.Access(a[2], 12, true))
        {
            last_error = 87;
            result = 0;
            return true;
        }
        const unsigned values[3] = {settings.mouse[0], settings.mouse[1], settings.mouse[2]};
        result = Store(a[2], values, sizeof values);
        return true;
    }
    case 4: // SPI_SETMOUSE: three values, each stored under its own name.
    {
        unsigned values[3];
        if (!a[2] || !Read(a[2], values, sizeof values))
        {
            last_error = 87;
            result = 0;
            return true;
        }
        for (unsigned i = 0; i < 3; ++i)
            settings.mouse[i] = (unsigned short)values[i];
        result = 1;
        if (store)
        {
            // The broadcast hangs on the first write, as in the original.
            const bool written = StoreControlNumber(ControlMouse, "MouseThreshold1", (short)settings.mouse[0]);
            StoreControlNumber(ControlMouse, "MouseThreshold2", (short)settings.mouse[1]);
            StoreControlNumber(ControlMouse, "MouseSpeed", (short)settings.mouse[2]);
            if (written && (a[3] & 2))
                BroadcastMessage(Win98Broadcast::Send, 0x1a, action, 0);
        }
        return true;
    }
    case 11: // SPI_SETKEYBOARDSPEED: the low five bits of the word.
        settings.keyboard = (unsigned short)((settings.keyboard & 0xffe0u) | value);
        settings.typematic = true;
        return finish(ControlKeyboard, "KeyboardSpeed", (short)value);
    case 23: // SPI_SETKEYBOARDDELAY: bits 5 and 6.
        settings.keyboard = (unsigned short)((value << 5) | (settings.keyboard & 0xff9fu));
        settings.typematic = true;
        return finish(ControlKeyboard, "KeyboardDelay", (short)value);
    case 13: // SPI_ICONHORIZONTALSPACING
    case 24: // SPI_ICONVERTICALSPACING
    {
        // With a pointer the action answers the spacing (the read branch);
        // without one it sets it: nothing for 0, never below the size of an
        // icon, and stored in twips, negative, under WindowMetrics.
        if (a[2])
            return false;
        result = 1;
        if (!value)
            return true;
        const unsigned least = settings.Word(action == 13 ? 0x2f4 : 0x2f6);
        const unsigned size = value < least ? least : value;
        settings.SetWord(action == 13 ? 0x32a : 0x32c, (unsigned short)size);
        if (store && StoreControlNumber(ControlMetrics, action == 13 ? "IconSpacing" : "IconVerticalSpacing",
                                        -int(size * 1440 / ControlDotsPerInch)) && (a[3] & 2))
            BroadcastMessage(Win98Broadcast::Send, 0x1a, action, 0);
        return true;
    }
    case 28: // SPI_SETMENUDROPALIGNMENT
        settings.SetWord(0x32e, value ? 1 : 0);
        return finish(ControlDesktop, "MenuDropAlignment", (short)value);
    case 29: // SPI_SETDOUBLECLKWIDTH
        settings.SetWord(0x326, (unsigned short)value);
        return finish(ControlDesktop, "DoubleClickWidth", (short)value);
    case 30: // SPI_SETDOUBLECLKHEIGHT
        settings.SetWord(0x328, (unsigned short)value);
        return finish(ControlDesktop, "DoubleClickHeight", (short)value);
    case 32: // SPI_SETDOUBLECLICKTIME: SetDoubleClickTime, stored with the mouse.
        User::SetDoubleClickTime(value);
        return finish(ControlMouse, "DoubleClickSpeed", (short)value);
    case 37: // SPI_SETDRAGFULLWINDOWS: FALSE without the Plus module.
        if (!settings.Word(0x264))
        {
            result = 0;
            return true;
        }
        settings.drag_full = value ? 1 : 0;
        return finish(ControlDesktop, "DragFullWindows", settings.drag_full);
    case 72: // SPI_GETANIMATION: ANIMATIONINFO, eight bytes.
    case 73: // SPI_SETANIMATION
    {
        unsigned info[2] = {};
        if (!a[2] || !Read(a[2], info, sizeof info) || info[0] != 8)
        {
            result = 0;
            return true;
        }
        if (action == 72)
        {
            info[1] = settings.min_animate;
            result = Store(a[2], info, sizeof info);
            return true;
        }
        settings.min_animate = info[1] ? 1 : 0;
        return finish(ControlMetrics, "MinAnimate", settings.min_animate);
    }
    case 76: // SPI_SETDRAGWIDTH
        settings.SetWord(0x366, (unsigned short)value);
        return finish(ControlDesktop, "DragWidth", (short)value);
    case 77: // SPI_SETDRAGHEIGHT
        settings.SetWord(0x368, (unsigned short)value);
        return finish(ControlDesktop, "DragHeight", (short)value);
    case 104: // SPI_GETWHEELSCROLLLINES
        return answer(settings.wheel_lines);
    case 105: // SPI_SETWHEELSCROLLLINES
        settings.wheel_lines = (unsigned short)value;
        return finish(ControlDesktop, "WheelScrollLines", (short)value);
    case 106: // SPI_GETMENUSHOWDELAY
        return answer(settings.menu_show_delay);
    case 107: // SPI_SETMENUSHOWDELAY
        settings.menu_show_delay = (unsigned short)value;
        return finish(ControlDesktop, "MenuShowDelay", (short)value);
    default:
        return false;
    }
}
// A PC keyboard repeats the key that went down last while it is held: after
// the delay, at the rate, both of which Windows programs into the controller
// (KEYBOARD.7 SetSpeed with the word above). The host's keyboard repeats by
// the host's own settings. Once Windows has set the keyboard, by a stored
// speed at the start or by SPI_SETKEYBOARDSPEED or SPI_SETKEYBOARDDELAY, the
// port therefore drops the repeats the host sends and makes its own on the
// clock of the machine; until then the host's repeats pass as they come, as
// the keyboard of a PC keeps what its BIOS set. True when the event of the
// host is such a repeat and goes no further.
Application::Typematic Application::typematic;
bool Application::TypematicKey(unsigned key, unsigned character, bool down, unsigned scan, bool extended)
{
    const auto &settings = User::SystemSettings();
    if (!settings.typematic || typematic.sending)
        return false;
    if (!down)
    {
        if (typematic.key == key)
            typematic.key = 0;
        return false;
    }
    if (User::PhysicalKeyDown(key))
        return true;
    typematic.key = key;
    typematic.character = character;
    typematic.scan = scan;
    typematic.extended = extended;
    typematic.next = Now() + TypematicDelay(settings.keyboard);
    return false;
}
// Once a turn of the first process of the session, which is the one the host
// hands its keys to.
void Application::TypematicPulse()
{
    if (!typematic.key || parent_process || typematic.sending)
        return;
    const auto &settings = User::SystemSettings();
    if (!settings.typematic || !User::PhysicalKeyDown(typematic.key))
    {
        typematic.key = 0;
        return;
    }
    const unsigned long long now = Now();
    if (now < typematic.next)
        return;
    const unsigned period = TypematicPeriod(settings.keyboard);
    typematic.sending = true;
    RawKey(typematic.key, typematic.character, true, typematic.scan, typematic.extended);
    typematic.sending = false;
    // One repeat a turn: a machine that stood for a while does not catch up.
    typematic.next += period;
    if (typematic.next <= now)
        typematic.next = now + period;
}
