// SPDX-License-Identifier: GPL-2.0-or-later
// Initialize installed fonts and stock font objects.
#include "x86_app.h"
#include "gdi_font_catalog.h"
#include "gdi_truetype.h"
#include "win98_gdi_parameters.h"
#include "win98_frame_control.h"
#include "registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86 {
namespace {
using U = unsigned;
bool ReadGuestFile(const char *path, unsigned char *&data, U &size)
{
    Files::Session session(Files::System());
    U handle = 0, status = 0, done = 0;
    data = nullptr;
    if (session.Open(path, 1, 3, 3, 0, handle, status))
        return false;
    bool ok = !session.Size(handle, size) && size && size < 16u * 1024 * 1024;
    if (ok)
    {
        data = static_cast<unsigned char *>(malloc(size));
        ok = data && !session.Read(handle, data, size, done) && done == size;
    }
    session.Close(handle);
    if (!ok)
        free(data), data = nullptr;
    return ok;
}
// A registry value for the font driver in the machine (TrueTypeMachine::Query):
// "HKEY_LOCAL_MACHINE\\Software\\..." and a value name, read from the
// system hive without creating anything.
bool QueryRegistry(const char *key, const char *name, unsigned char *data, U &size)
{
    static const char *const roots[] = {"HKEY_CLASSES_ROOT", "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE",
                                        "HKEY_USERS", "HKEY_PERFORMANCE_DATA", "HKEY_CURRENT_CONFIG"};
    auto &registry = Registry::System();
    for (U i = 0; i < 6; ++i)
    {
        const U n = U(strlen(roots[i]));
        if (strncmp(key, roots[i], n) || (key[n] && key[n] != '\\'))
            continue;
        U node = Registry::Missing;
        bool created = false;
        if (registry.Open(registry.roots[i], key[n] ? key + n + 1 : "", false, node, created))
            return false;
        const Registry::Value *value = nullptr;
        if (registry.Query(node, name, value) || !value || value->size > size)
            return false;
        if (value->size && value->data)
            memcpy(data, value->data, value->size);
        size = value->size;
        return true;
    }
    return false;
}
char Upper(char c) { return c >= 'a' && c <= 'z' ? char(c - 32) : c; }
bool Same(const char *a, const char *b, U n)
{
    for (U i = 0; i < n; ++i)
        if (Upper(a[i]) != Upper(b[i]))
            return false;
    return true;
}
// The lines "key=value" of one section of an INI file, key and value trimmed.
template <class F> void Section(const unsigned char *text, U size, const char *name, F each)
{
    const U length = U(strlen(name));
    bool inside = false;
    for (U at = 0; at < size;)
    {
        U end = at;
        while (end < size && text[end] != '\n' && text[end] != '\r')
            ++end;
        const char *line = reinterpret_cast<const char *>(text + at);
        U n = end - at;
        while (n && (*line == ' ' || *line == '\t'))
            ++line, --n;
        if (n && *line == '[')
            inside = n >= length + 2 && line[length + 1] == ']' && Same(line + 1, name, length);
        else if (inside && n && *line != ';')
        {
            U eq = 0;
            while (eq < n && line[eq] != '=')
                ++eq;
            if (eq < n)
            {
                char key[64] = {}, value[128] = {};
                U k = eq;
                while (k && (line[k - 1] == ' ' || line[k - 1] == '\t'))
                    --k;
                U v = eq + 1, w = n;
                while (v < n && (line[v] == ' ' || line[v] == '\t'))
                    ++v;
                while (w > v && (line[w - 1] == ' ' || line[w - 1] == '\t'))
                    --w;
                if (k < sizeof key && w - v < sizeof value)
                {
                    memcpy(key, line, k);
                    memcpy(value, line + v, w - v);
                    each(key, value);
                }
            }
        }
        at = end + 1;
    }
}
const char *Leaf(const char *path)
{
    const char *leaf = path;
    for (const char *p = path; *p; ++p)
        if (*p == '\\' || *p == ':' || *p == '/')
            leaf = p + 1;
    return leaf;
}
// A name without a directory is looked up where Win98 keeps fonts: the FONTS
// directory, then the system and the Windows directory.
bool Install(const char *name)
{
    if (!*name || GdiFonts::Installed(Leaf(name)))
        return false;
    const U length = U(strlen(name));
    const bool truetype = length > 4 && Same(name + length - 4, ".TTF", 4);
    // A TrueType file goes to Win98's font driver, which the first one starts
    // (src/gdi_truetype.cpp).
    if (truetype)
        return (GdiTrueType::Available() || GdiTrueType::Start(ReadGuestFile, QueryRegistry)) &&
               GdiFonts::InstallTrueType(name) != 0;
    const bool qualified = Leaf(name) != name;
    static const char *const directories[] = {"C:\\WINDOWS\\FONTS\\", "C:\\WINDOWS\\SYSTEM\\", "C:\\WINDOWS\\"};
    for (U i = 0; i < (qualified ? 1u : 3u); ++i)
    {
        char path[260];
        if (qualified)
            snprintf(path, sizeof path, "%s", name);
        else
            snprintf(path, sizeof path, "%s%s", directories[i], name);
        unsigned char *data = nullptr;
        U size = 0;
        if (!ReadGuestFile(path, data, size))
            continue;
        const U added = GdiFonts::InstallFontFile(Leaf(name), data, size);
        free(data);
        return added != 0;
    }
    return false;
}
} // namespace

// AddFontResource (GDI into the worker ): the file is opened,
// its faces go into the font list, and the answer is the number of faces the
// file holds; a file already in the list answers its faces again.
unsigned Application::AddFontResourceFile(const char *path)
{
    const char *leaf = Leaf(path);
    auto faces_of = [&] {
        unsigned n = 0;
        for (unsigned i = 0; i < GdiFonts::Count(); ++i)
            if (const auto *f = GdiFonts::At(i); f && strlen(f->file) == strlen(leaf) && Same(f->file, leaf, U(strlen(leaf))))
                ++n;
        return n;
    };
    if (GdiFonts::Installed(leaf))
        return faces_of();
    return Install(path) ? faces_of() : 0;
}

// The font size of the display (src/gdi_display_dpi.h). GDI.EXE reads the
// four numbers of HKEY_CURRENT_CONFIG\Display\Settings once, when the display
// driver is enabled; a change through Display Properties therefore holds
// from the next start of Windows. A value is a string of digits (what Setup
// and Display Properties write), a DWORD or binary; one that is missing keeps
// the driver's 96.
void Application::LoadDisplayDpi()
{
    static bool loaded = false;
    if (loaded)
        return;
    loaded = true;
    auto &registry = Registry::System();
    U key = Registry::Missing;
    bool created = false;
    if (registry.Open(registry.roots[5], "Display\\Settings", false, key, created))
        return;
    auto number = [&](const char *name) {
        const Registry::Value *value = nullptr;
        if (registry.Query(key, name, value) || !value || value->size > 0x14)
            return 96;
        if (value->type == 3 || value->type == 4)
            return value->size >= 2 && value->data ? int(value->data[0] | value->data[1] << 8) : 0;
        if (value->type != 1)
            return 96;
        int result = 0;
        for (U i = 0; i < value->size && value->data[i] >= '0' && value->data[i] <= '9'; ++i)
            result = (result * 10 + (value->data[i] - '0')) & 0xffff;
        return result;
    };
    const int logical_x = number("DPILogicalX"), logical_y = number("DPILogicalY");
    Gdi::SetDpi(logical_x, logical_y, number("DPIPhysicalX"), number("DPIPhysicalY"));
    // Neo98's own setting, HKLM\Software\NEO98\CheckBoxesFollowFontSize:
    // "1" (or a DWORD 1) makes the cell of check boxes and radio buttons
    // 13 x dpi / 96 instead of the 13 pixels of USER (docs/FONT-SIZE.md).
    U neo = Registry::Missing;
    const Registry::Value *follow = nullptr;
    bool scaled = false;
    if (!registry.Open(registry.roots[2], "Software\\NEO98", false, neo, created) &&
        !registry.Query(neo, "CheckBoxesFollowFontSize", follow) && follow && follow->data && follow->size)
        scaled = (follow->type == 1 && follow->data[0] == '1') ||
                 ((follow->type == 3 || follow->type == 4) && follow->data[0] == 1);
    const int dpi = logical_x > 0 ? logical_x : 96;
    User::SetCheckCell(scaled ? (13 * dpi + 48) / 96 : 13);
}

// A raster face as a GDI font, enlarged by a whole factor when the mapper
// chose that (GdiFonts::Match).
unsigned Application::CreateRasterFont(const GdiFonts::Face &face, unsigned scale)
{
    if (scale < 2)
        return Gdi::CreateFontView(face.font.data, face.font.length);
    U length = 0;
    unsigned char *image = GdiFonts::ScaledFnt(face, scale, length);
    if (!image)
        return Gdi::CreateFontView(face.font.data, face.font.length);
    const U font = Gdi::CreateFontMemory(image, length);
    free(image);
    return font;
}

// A font of NONCLIENTMETRICS for USER's own drawing (User::MetricFont): the
// physical font the mapper gives a LOGFONT16, without a logical font object
// of a process. A face the installation does not have is MS Sans Serif, as
// for CreateFontIndirect (src/x86_font_create.cpp).
unsigned Application::CreateMetricFont(const unsigned char *logfont)
{
    auto word = [&](U at) { return U(logfont[at] | logfont[at + 1] << 8); };
    const int height = short(word(0));
    const U weight = word(8);
    const bool italic = logfont[10] != 0, underline = logfont[11] != 0, strikeout = logfont[12] != 0;
    char name[33] = {};
    memcpy(name, logfont + 18, 32);
    U scale = 1;
    const auto *face = GdiFonts::Match(name, height, weight, italic, logfont[13], &scale);
    if (!face)
        face = GdiFonts::Match("MS Sans Serif", height ? height : 13, 400, false, 1, &scale);
    if (!face)
        return 0;
    if (face->truetype)
    {
        const auto *font = GdiTrueType::Realize(face->truetype, height, 0, weight, italic, underline, strikeout);
        return font ? Gdi::CreateFontOutline(font->view) : 0;
    }
    const U plain = Application::CreateRasterFont(*face, scale);
    const bool bold = weight >= 550 && (face->font.data[0x53] | face->font.data[0x54] << 8) < 700;
    if (!plain || !(italic || underline || strikeout || bold))
        return plain;
    const U variant = Gdi::CreateFontVariant(plain, italic, underline, strikeout, bold);
    Gdi::DeleteObject(plain);
    return variant;
}

// Win98 GDI loads the three system fonts (fonts.fon, fixedfon.fon,
// oemfonts.fon) by the names HKEY_CURRENT_CONFIG\Display\Settings holds for
// them, or SYSTEM.INI [boot] when the key has none, or VGASYS.FON, VGAFIX.FON
// and VGAOEM.FON; then every font of the registry keys
// HKEY_CURRENT_CONFIG\Display\Fonts and
// HKLM\Software\Microsoft\Windows\CurrentVersion\Fonts and of WIN.INI [fonts].
// Display Properties writes the system fonts and Display\Fonts for the font
// size it sets: the 96-dpi set (VGASYS, SSERIFE...) or the 120-dpi set
// (8514SYS, SSERIFF...), whichever number is nearer.
// Standalone comparison runners keep built-in SSERIFE.FON for
// runs without the Win98 tree. Release hosts require the installed files.
void Application::LoadInstalledFonts()
{
    LoadDisplayDpi();
    Win98Frame::SetFontProvider([](int height)->Gdi::HFONT{
        if(!GdiFonts::Installed("MARLETT.TTF"))Install("MARLETT.TTF");
        const auto *face=GdiFonts::Match("Marlett",height,400,false,2);
        if(!face||!face->truetype)return 0;
        const auto *font=GdiTrueType::Realize(face->truetype,height,0,400,false,false,false);
        return font?Gdi::CreateFontOutline(font->view):0;
    });
    if (GdiFonts::Count())
        return;
    unsigned char *ini = nullptr;
    U size = 0;
    char boot[3][64] = {};
    if (ReadGuestFile("C:\\WINDOWS\\SYSTEM.INI", ini, size))
    {
        Section(ini, size, "boot", [&](const char *key, const char *value) {
            static const char *const keys[3] = {"fonts.fon", "fixedfon.fon", "oemfonts.fon"};
            for (U i = 0; i < 3; ++i)
                if (strlen(key) == strlen(keys[i]) && Same(key, keys[i], U(strlen(key))))
                    snprintf(boot[i], sizeof boot[i], "%s", value);
        });
        free(ini);
    }
    auto &registry = Registry::System();
    {
        static const char *const names[3] = {"fonts.fon", "fixedfon.fon", "oemfonts.fon"};
        static const char *const fallback[3] = {"vgasys.fon", "vgafix.fon", "vgaoem.fon"};
        U key = Registry::Missing;
        bool created = false;
        const bool open = !registry.Open(registry.roots[5], "Display\\Settings", false, key, created);
        for (U i = 0; i < 3; ++i)
        {
            const Registry::Value *value = nullptr;
            if (open && !registry.Query(key, names[i], value) && value && value->type == 1 && value->data &&
                value->size && value->size < sizeof boot[i])
            {
                memset(boot[i], 0, sizeof boot[i]);
                memcpy(boot[i], value->data, value->size);
            }
            if (!boot[i][0])
                snprintf(boot[i], sizeof boot[i], "%s", fallback[i]);
        }
    }
    for (U i = 0; i < 3; ++i)
        if (Install(boot[i]) || GdiFonts::Installed(Leaf(boot[i])))
            GdiFonts::SetRole(GdiFonts::Role(i), Leaf(boot[i]));
    // USER.EXE names HKEY_CURRENT_CONFIG\Display\Fonts, the raster fonts of
    // the display resolution (SSERIFE.FON and its siblings); KRNL386.EXE names
    // HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Fonts, the rest.
    const struct { U root; const char *path; } keys[2] = {
        {registry.roots[5], "Display\\Fonts"},
        {registry.roots[2], "Software\\Microsoft\\Windows\\CurrentVersion\\Fonts"}};
    for (const auto &entry : keys)
    {
        U key = Registry::Missing;
        bool created = false;
        if (registry.Open(entry.root, entry.path, false, key, created))
            continue;
        const Registry::Value *value = nullptr;
        for (U i = 0; !registry.EnumValue(key, i, value); ++i)
            if (value && value->data && value->size && value->size < 260)
            {
                char name[260] = {};
                memcpy(name, value->data, value->size);
                Install(name);
            }
    }
    if (ReadGuestFile("C:\\WINDOWS\\WIN.INI", ini, size))
    {
        Section(ini, size, "fonts", [](const char *, const char *value) { Install(value); });
        Section(ini, size, "FontSubstitutes",
                [](const char *from, const char *to) { GdiFonts::AddSubstitute(from, to); });
        // GDI reads the switches of [TrueType] with GetProfileInt.
        Win98GdiParameters::Boot(Win98GdiParameters::System(), [&](const char *wanted) {
            int number = 0;
            Section(ini, size, "TrueType", [&](const char *key, const char *value) {
                if (strlen(key) == strlen(wanted) && Same(key, wanted, U(strlen(key))))
                    number = atoi(value);
            });
            return number;
        });
        free(ini);
    }
#ifdef NEO98_RUNTIME_CONTENT
    // Also support an installation whose optional registry font list is
    // empty: MS Sans Serif of the set Display Properties would install for
    // the font size, the one whose number is nearer.
    if (!GdiFonts::Match("MS Sans Serif", 0, 400, false, 0))
        Install(Gdi::Dpi().logical_y > 108 ? "SSERIFF.FON" : "SSERIFE.FON");
#else
    if (!GdiFonts::RoleFace(GdiFonts::SystemRole))
    {
        // No Win98 tree: the former built-in catalogue.
        GdiFonts::Reset();
        GdiFonts::InstallFnt("FIXEDSYS", font_bytes, font_size, true);
        GdiFonts::SetRole(GdiFonts::FixedRole, "FIXEDSYS");
        for (U i = 0; i < DibText::FontCount(); ++i)
        {
            const auto font = DibText::OriginalFont(i);
            GdiFonts::InstallFnt("SSERIFE.FON", font.data, font.length, false);
        }
    }
#endif
    if (const auto *system_face = GdiFonts::RoleFace(GdiFonts::SystemRole))
        Gdi::SetDefaultFont(system_face->font.data, system_face->font.length);
}

// The stock fonts. SYSTEM_FONT and DEVICE_DEFAULT_FONT are the System font,
// SYSTEM_FIXED_FONT is Fixedsys, both straight from the files of SYSTEM.INI.
// OEM_FIXED_FONT, ANSI_FIXED_FONT and ANSI_VAR_FONT are the logical fonts GDI.EXE
// keeps: {12, 8, OEM_CHARSET, fixed, "Terminal"},
// {12, 9, fixed, "Courier"} and {12, 9, variable, "MS Sans Serif"}, realized by
// the mapper; their heights are pixels and do not follow the font size of the
// display. DEFAULT_GUI_FONT is made from GDI.EXE's string resources, 8 points
// of MS Sans Serif: lfHeight = -(8 * dpLogPixelsY + 36) / 72, -11 at 96 dots
// per inch and -13 at 120. Without the Win98 tree it shares ANSI_VAR_FONT.
bool Application::InstallStockFonts()
{
    LoadInstalledFonts();
    struct Stock { const GdiFonts::Face *face; int height, width; unsigned charset, pitch; const char *name; };
    const auto *system_face = GdiFonts::RoleFace(GdiFonts::SystemRole);
    const auto *fixed_face = GdiFonts::RoleFace(GdiFonts::FixedRole);
    const auto *ansi_var = GdiFonts::Match("MS Sans Serif", 12, 400, false, 0);
    if (!system_face)
        system_face = ansi_var;
    const auto *oem = GdiFonts::RoleFace(GdiFonts::OemRole);
    const auto *ansi_fixed = GdiFonts::Match("Courier", 12, 400, false, 0);
    const int gui_height = -((8 * Gdi::Dpi().logical_y + 36) / 72);
    const auto *gui = GdiFonts::RoleFace(GdiFonts::SystemRole)
                          ? GdiFonts::Match("MS Sans Serif", gui_height, 400, false, 0) : nullptr;
    Stock stocks[6] = {{fixed_face, 0, 0, 0, 0, nullptr},
                       {system_face, 0, 0, 0, 0, nullptr},
                       {oem ? oem : fixed_face, 12, 8, 255, 1, "Terminal"},
                       {ansi_fixed ? ansi_fixed : fixed_face, 12, 9, 0, 1, "Courier"},
                       {ansi_var ? ansi_var : system_face, 12, 9, 0, 2, "MS Sans Serif"},
                       {gui, gui_height, 0, 0, 0, "MS Sans Serif"}};
    U handles[6] = {}, slots = 0;
    for (U i = 0; i < 6; ++i)
    {
        // The GUI font is a logical font of its own, whatever face it gets.
        if (i == 5 && !gui)
        {
            handles[5] = handles[4];
            continue;
        }
        // A run without the Win98 tree has fewer faces than roles; a face that
        // already stands for an earlier stock object keeps its handle, so the
        // application keeps the logical-font slots it had before.
        U earlier = 0;
        while (earlier < i && stocks[earlier].face != stocks[i].face)
            ++earlier;
        if (earlier < i && i != 5)
        {
            handles[i] = handles[earlier];
            continue;
        }
        if (!stocks[i].face || !(handles[i] = Gdi::CreateFontView(stocks[i].face->font.data,
                                                                   stocks[i].face->font.length)))
        {
            Fail("Original bitmap font load failed");
            return false;
        }
        Gdi::TEXTMETRICA metrics{};
        Gdi::FontMetrics(stocks[i].face->font, &metrics);
        auto *free_slot = fonts.Free();
        if (!free_slot)
        {
            Fail("Original bitmap font load failed");
            return false;
        }
        auto &slot = *free_slot;
        ++slots;
        slot.handle = handles[i];
        memset(slot.logfont, 0, sizeof slot.logfont);
        const int height = stocks[i].name ? stocks[i].height : metrics.tmHeight;
        const int width = stocks[i].name ? stocks[i].width : 0;
        const int weight = i == 5 ? 400 : stocks[i].name ? 0 : metrics.tmWeight;
        memcpy(slot.logfont, &height, 4);
        memcpy(slot.logfont + 4, &width, 4);
        memcpy(slot.logfont + 16, &weight, 4);
        slot.logfont[23] = stocks[i].name ? stocks[i].charset : metrics.tmCharSet;
        slot.logfont[25] = stocks[i].name && i != 5 ? 2 : 0;
        slot.logfont[26] = stocks[i].name && i != 5 ? 2 : 0;
        slot.logfont[27] = stocks[i].name ? stocks[i].pitch : 0;
        char face[32] = {};
        stocks[i].face->font.FaceName(face);
        snprintf(reinterpret_cast<char *>(slot.logfont) + 28, 32, "%s", stocks[i].name ? stocks[i].name : face);
        slot.requested_charset = slot.logfont[23];
    }
    fixed = handles[0];
    system = handles[1];
    stock[10] = handles[2];
    stock[11] = handles[3];
    stock[12] = handles[4];
    stock[17] = handles[5];
    stock[13] = stock[14] = system;
    stock[16] = fixed;
    return true;
}
} // namespace X86
