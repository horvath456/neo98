// SPDX-License-Identifier: GPL-2.0-or-later
// Extra DOS property pages for Sound, Graphics, and Hardware. Store
// nondefault values as name=value lines in the PIF MACHINE OPTIONS section
// (src/win98_pifmgr.h). The DOS process forwards them to the VM through
// src/x86_dos_vm.cpp and tools/host_dos_vm.cpp.
#include "x86_app.h"
#include "win98_pifmgr.h"
#include "user.h"
#include <stdio.h>
#include <string.h>

namespace X86
{
namespace
{
constexpr unsigned WmInitDialog = 0x0110, WmCommand = 0x0111, WmNotify = 0x004e, WmHScroll = 0x0114;
constexpr unsigned PsmChanged = 0x0468, PsnApply = 0xffffff36u, PsnSetActive = 0xffffff38u, PsnKillActive = 0xffffff37u;
constexpr unsigned CbAddString = 0x143, CbGetCurSel = 0x147, CbSetCurSel = 0x14e, CbGetItemData = 0x150, CbSetItemData = 0x151,
                   CbGetLbText = 0x148, CbResetContent = 0x14b;
constexpr unsigned CbnSelChange = 1, BnClicked = 0;
constexpr unsigned TbmGetPos = 0x400, TbmSetPos = 0x405, TbmSetRange = 0x406;

struct Value
{
    const char *value, *label;
};
// A control of a page and the option it sets. A combo lists `values` (the
// first is shown for an option the PIF does not name); a check box is on
// for values[1] and off for values[0]; a slider walks the values.
enum Kind : unsigned char { KindCombo, KindCheck, KindSlider, KindMidi };
struct Option
{
    const char *key, *label;
    Kind kind;
    const Value *values;
    unsigned count;
    const char *fallback; // what the machine uses when the PIF names nothing
};
#define VALUES(name) name, sizeof name / sizeof name[0]

const Value SbTypes[] = {{"sb16", "Sound Blaster 16"}, {"sbpro2", "Sound Blaster Pro 2"}, {"sbpro1", "Sound Blaster Pro"},
                         {"sb2", "Sound Blaster 2.0"}, {"sb1", "Sound Blaster 1.0"}, {"gb", "Game Blaster"}, {"none", "None"}};
const Value SbSettings[] = {{"A220 I7 D1 H5", "Port 220, IRQ 7, DMA 1, high DMA 5"}, {"A220 I5 D1 H5", "Port 220, IRQ 5, DMA 1, high DMA 5"},
                            {"A240 I7 D1 H5", "Port 240, IRQ 7, DMA 1, high DMA 5"}, {"A240 I7 D3 H7", "Port 240, IRQ 7, DMA 3, high DMA 7"},
                            {"A240 I2 D3 H7", "Port 240, IRQ 2, DMA 3, high DMA 7"}, {"A240 I5 D3 H5", "Port 240, IRQ 5, DMA 3, high DMA 5"},
                            {"A240 I5 D1 H5", "Port 240, IRQ 5, DMA 1, high DMA 5"}, {"A240 I10 D3 H7", "Port 240, IRQ 10, DMA 3, high DMA 7"},
                            {"A280 I10 D0 H6", "Port 280, IRQ 10, DMA 0, high DMA 6"}, {"A280 I5 D1 H5", "Port 280, IRQ 5, DMA 1, high DMA 5"}};
const Value FmModes[] = {{"auto", "Automatic (by card)"}, {"opl2", "OPL2 (AdLib)"}, {"dualopl2", "Dual OPL2"}, {"opl3", "OPL3"},
                         {"opl3gold", "OPL3 (AdLib Gold)"}, {"cms", "Creative Music System"}, {"none", "None"}};
const Value FmQuality[] = {{"default", ""}, {"nuked", ""}};
const Value OffOn[] = {{"false", ""}, {"true", ""}};
const Value Tandy[] = {{"auto", ""}, {"on", ""}};
const Value Volumes[] = {{"0.05", "5%"},   {"0.1", "10%"},  {"0.15", "15%"}, {"0.2", "20%"},   {"0.25", "25%"}, {"0.3", "30%"},
                         {"0.35", "35%"},  {"0.4", "40%"},  {"0.45", "45%"}, {"0.5", "50%"},   {"0.55", "55%"}, {"0.6", "60%"},
                         {"0.65", "65%"},  {"0.7", "70%"},  {"0.75", "75%"}, {"0.8", "80%"},   {"0.85", "85%"}, {"0.9", "90%"},
                         {"0.95", "95%"},  {"1.0", "100%"}, {"1.1", "110%"}, {"1.2", "120%"},  {"1.3", "130%"}, {"1.4", "140%"},
                         {"1.5", "150%"},  {"1.6", "160%"}, {"1.7", "170%"}, {"1.8", "180%"},  {"1.9", "190%"}, {"2.0", "200%"},
                         {"2.25", "225%"}, {"2.5", "250%"}, {"2.75", "275%"}, {"3.0", "300%"}, {"3.25", "325%"}, {"3.5", "350%"},
                         {"3.75", "375%"}, {"4.0", "400%"}, {"4.25", "425%"}, {"4.5", "450%"}, {"4.75", "475%"}, {"5.0", "500%"}};
const Value Machines[] = {{"svga", "SVGA (Super VGA)"}, {"vga", "VGA"}, {"ega", "EGA"}, {"cga", "CGA"},
                          {"tandy", "Tandy"}, {"hercules", "Hercules"}, {"pcjr", "PCjr"}};
const Value SvgaChips[] = {{"svga_s3", "S3 Trio64"}, {"vesa_nolfb", "S3 Trio64, no linear frame buffer"}, {"vesa_oldvbe", "S3 Trio64, VESA 1.3"},
                           {"svga_et3000", "Tseng Labs ET3000"}, {"svga_et4000", "Tseng Labs ET4000"}, {"svga_paradise", "Paradise PVGA1A"}};
const Value SvgaMemory[] = {{"2", "2 MB"}, {"0", "512 KB"}, {"1", "1 MB"}, {"3", "3 MB"}, {"4", "4 MB"}, {"8", "8 MB"}};
const Value CgaModels[] = {{"early_auto", "Early model, composite automatic"}, {"early_on", "Early model, composite on"},
                           {"early_off", "Early model, composite off"}, {"late_auto", "Late model, composite automatic"},
                           {"late_on", "Late model, composite on"}, {"late_off", "Late model, composite off"}};
const Value HerculesColours[] = {{"white", "White"}, {"amber", "Amber"}, {"green", "Green"}};
const Value Voodoo[] = {{"off", "None"}, {"4mb", "3dfx Voodoo, 4 MB"}, {"8mb", "3dfx Voodoo, 8 MB"}, {"12mb", "3dfx Voodoo, 12 MB, two texture units"}};
const Value CpuTypes[] = {{"auto", "Automatic"}, {"386", "386"}, {"386_slow", "386 (slow, with privilege checks)"},
                          {"386_prefetch", "386 (with prefetch queue)"}, {"486_slow", "486 (slow)"}, {"pentium_slow", "Pentium (slow)"},
                          {"pentium_mmx", "Pentium MMX (slow)"}, {"pentium_pro", "Pentium Pro (slow, reports CMOV)"},
                          {"pentium_ii", "Pentium II (slow, reports CMOV and MMX)"}};
// The processor core of DOSBox: the dynamic recompiler translates x86 code
// into ARM64 code, the interpreter runs it instruction by instruction;
// Automatic takes the interpreter in real mode and the recompiler in
// protected mode and at maximum speed.
const Value CpuCores[] = {{"auto", "Automatic"}, {"dynamic", "Recompiler (fast)"}, {"normal", "Interpreter (exact)"}};
const Value Speeds[] = {{"auto", "Automatic"}, {"max", "As fast as possible"}, {"315", "8088, 4.77 MHz"}, {"1320", "286, 6 MHz"},
                        {"2750", "286, 12 MHz"}, {"4720", "386, 20 MHz"}, {"7800", "386DX, 33 MHz"}, {"13400", "486DX, 33 MHz"},
                        {"26800", "486DX2, 66 MHz"}, {"77000", "Pentium, 100 MHz"}, {"200000", "Pentium II, 300 MHz"},
                        {"500000", "Pentium III, 600 MHz"}, {"1000000", "Athlon, 1.2 GHz"}};
const Value SpeedLimits[] = {{"none", "None"}, {"315", "8088, 4.77 MHz"}, {"1320", "286, 6 MHz"}, {"2750", "286, 12 MHz"},
                             {"4720", "386, 20 MHz"}, {"7800", "386DX, 33 MHz"}, {"13400", "486DX, 33 MHz"}, {"26800", "486DX2, 66 MHz"},
                             {"77000", "Pentium, 100 MHz"}, {"200000", "Pentium II, 300 MHz"}, {"500000", "Pentium III, 600 MHz"},
                             {"1000000", "Athlon, 1.2 GHz"}};
const Value Scales[] = {{"1.0", "100%"}, {"0.2", "20%"}, {"0.3", "30%"}, {"0.4", "40%"}, {"0.5", "50%"}, {"0.6", "60%"},
                        {"0.7", "70%"},  {"0.8", "80%"}, {"0.9", "90%"}, {"1.1", "110%"}, {"1.2", "120%"}, {"1.3", "130%"},
                        {"1.4", "140%"}, {"1.5", "150%"}, {"1.6", "160%"}, {"1.7", "170%"}, {"1.8", "180%"}, {"1.9", "190%"}, {"2.0", "200%"}};
const Value HostLimits[] = {{"", "Automatic"}, {"0.2", "20%"}, {"0.3", "30%"}, {"0.4", "40%"}, {"0.5", "50%"},
                            {"0.6", "60%"}, {"0.7", "70%"}, {"0.8", "80%"}, {"0.9", "90%"}};
const Value Layouts[] = {{"", "Same as Windows"}, {"us", "United States"}, {"uk", "United Kingdom"}, {"gr", "German"},
                         {"sg", "Swiss German"}, {"sf", "Swiss French"}, {"fr", "French"}, {"be", "Belgian"}, {"it", "Italian"},
                         {"sp", "Spanish"}, {"po", "Portuguese"}, {"br", "Brazilian"}, {"nl", "Dutch"}, {"dk", "Danish"},
                         {"no", "Norwegian"}, {"sv", "Swedish"}, {"su", "Finnish"}, {"is161", "Icelandic"}, {"pl", "Polish"},
                         {"cz243", "Czech"}, {"sk", "Slovak"}, {"hu", "Hungarian"}, {"hr", "Croatian"}, {"si", "Slovenian"},
                         {"ru", "Russian"}, {"gk", "Greek"}, {"tr", "Turkish"}};
const Value MouseSpeeds[] = {{"1.0", "100%"}, {"0.25", "25%"}, {"0.5", "50%"}, {"0.75", "75%"}, {"1.25", "125%"}, {"1.5", "150%"},
                             {"2.0", "200%"}, {"2.5", "250%"}, {"3.0", "300%"}, {"4.0", "400%"}, {"5.0", "500%"}};
// MIDI: the fixed choices; files of C:\WINDOWS\SYSTEM follow (MidiFiles).
const Value MidiChoices[] = {{"frontend", "General MIDI synthesizer"}, {"disabled", "None"}};

// The pages. Each control is a row of a group: a label and its control, or
// a check box across the group; the layout is in PIFMGR's dialog units.
struct Row
{
    unsigned short id;
    Option option;
    bool beside = false; // a check box in the right half of the line before
};
struct Group
{
    const char *title;
    const Row *rows;
    unsigned count;
};
struct Page
{
    const char *title;
    const Group *groups;
    unsigned count;
};
const Row SoundBlaster[] = {
    {101, {"sblaster_type", "&Card:", KindCombo, VALUES(SbTypes), "sb16"}},
    {102, {"sblaster_conf", "&Settings:", KindCombo, VALUES(SbSettings), "A220 I5 D1 H5"}},
    {103, {"sblaster_adlib_mode", "&FM music:", KindCombo, VALUES(FmModes), "auto"}},
    {104, {"sblaster_adlib_emu", "&High quality FM synthesis (slower)", KindCheck, VALUES(FmQuality), "default"}},
};
const Row OtherSound[] = {
    {111, {"midi", "&MIDI:", KindMidi, VALUES(MidiChoices), "frontend"}},
    {112, {"gus", "&Gravis UltraSound", KindCheck, VALUES(OffOn), "false"}},
    {113, {"tandysound", "&Tandy sound always on", KindCheck, VALUES(Tandy), "auto"}, true},
    {114, {"swapstereo", "S&wap left and right channels", KindCheck, VALUES(OffOn), "false"}},
};
const Row Volume[] = {
    {121, {"volume_sb", "Sound Blaster", KindSlider, VALUES(Volumes), "1.0"}},
    {122, {"volume_adlib", "FM music", KindSlider, VALUES(Volumes), "1.0"}},
    {123, {"volume_midi", "MIDI", KindSlider, VALUES(Volumes), "1.0"}},
    {124, {"volume_speaker", "PC speaker", KindSlider, VALUES(Volumes), "1.0"}},
    {125, {"volume_cdrom", "CD audio", KindSlider, VALUES(Volumes), "1.0"}},
    {126, {"volume_other", "Other", KindSlider, VALUES(Volumes), "1.0"}},
};
const Group SoundGroups[] = {{"Sound Blaster", VALUES(SoundBlaster)}, {"Other devices", VALUES(OtherSound)}, {"Volume", VALUES(Volume)}};
const Row Adapter[] = {
    {201, {"machine", "&Adapter:", KindCombo, VALUES(Machines), "svga"}},
    {202, {"svga", "&Chip:", KindCombo, VALUES(SvgaChips), "svga_s3"}},
    {203, {"svgamem", "&Memory:", KindCombo, VALUES(SvgaMemory), "2"}},
    {204, {"cga", "C&GA model:", KindCombo, VALUES(CgaModels), "early_auto"}},
    {205, {"hercules", "&Hercules:", KindCombo, VALUES(HerculesColours), "white"}},
};
const Row Accelerator[] = {
    {211, {"voodoo", "&3D card:", KindCombo, VALUES(Voodoo), "8mb"}},
};
const Group GraphicsGroups[] = {{"Display adapter", VALUES(Adapter)}, {"3D accelerator", VALUES(Accelerator)}};
const Row Processor[] = {
    {301, {"cpu_type", "&Type:", KindCombo, VALUES(CpuTypes), "auto"}},
    {302, {"cycles", "&Speed:", KindCombo, VALUES(Speeds), "auto"}},
    {303, {"cycles_max", "&Limit:", KindCombo, VALUES(SpeedLimits), "none"}},
    {304, {"cycles_scale", "&Adjust:", KindCombo, VALUES(Scales), "1.0"}},
    {305, {"cycle_limit", "&Host load:", KindCombo, VALUES(HostLimits), ""}},
    {306, {"cpu_core", "&Emulation:", KindCombo, VALUES(CpuCores), "auto"}},
};
const Row Input[] = {
    {311, {"keyboard_layout", "&Keyboard:", KindCombo, VALUES(Layouts), ""}},
    {312, {"mouse_speed_factor", "&Mouse speed:", KindCombo, VALUES(MouseSpeeds), "1.0"}},
    {313, {"joystick_timed", "Timed &joystick intervals", KindCheck, VALUES(OffOn), "true"}},
};
const Group HardwareGroups[] = {{"Processor", VALUES(Processor)}, {"Input", VALUES(Input)}};
const Page Pages[] = {{"Sound", VALUES(SoundGroups)}, {"Graphics", VALUES(GraphicsGroups)}, {"Hardware", VALUES(HardwareGroups)}};
constexpr unsigned PageCount = sizeof Pages / sizeof Pages[0];

// The layout: a group box per group, 12 units of title, then 14 units for a
// labelled drop-down and 12 for a check box (two check boxes may share a
// line); the volume sliders sit three to a row of 22 units with their label
// above. Every page has to fit into the height of PIFMGR's own pages, 200
// units (template 108); the Sound page ran 8 units past it.
constexpr unsigned Margin = 7, LabelX = 14, LabelWidth = 56, FieldX = 72, ComboHeight = 14, CheckHeight = 12,
                   SliderHeight = 22, SliderTrack = 14, GroupTop = 12;
unsigned RowHeight(const Row &r) { return r.beside ? 0 : r.option.kind == KindCheck ? CheckHeight : ComboHeight; }

// A 16-bit dialog template, the form of PIFMGR's own.
struct Template
{
    unsigned char bytes[4096];
    unsigned length = 0, items = 0, count_at = 0;
    void Byte(unsigned v) { if (length < sizeof bytes) bytes[length++] = (unsigned char)v; }
    void Word(unsigned v) { Byte(v); Byte(v >> 8); }
    void Dword(unsigned v) { Word(v & 0xffff); Word(v >> 16); }
    void Text(const char *s) { while (*s) Byte((unsigned char)*s++); Byte(0); }
    void Item(unsigned x, unsigned y, unsigned cx, unsigned cy, unsigned id, unsigned style, unsigned klass, const char *text)
    {
        Word(x); Word(y); Word(cx); Word(cy); Word(id); Dword(style);
        Byte(klass);
        Text(text);
        Byte(0);
        ++items;
    }
    void Item(unsigned x, unsigned y, unsigned cx, unsigned cy, unsigned id, unsigned style, const char *klass, const char *text)
    {
        Word(x); Word(y); Word(cx); Word(cy); Word(id); Dword(style);
        Text(klass);
        Text(text);
        Byte(0);
        ++items;
    }
};
constexpr unsigned Visible = 0x50000000, TabStop = 0x00010000;

unsigned GroupHeight(const Group &g)
{
    if (g.count && g.rows[0].option.kind == KindSlider) return GroupTop + ((g.count + 2) / 3) * SliderHeight + 2;
    unsigned height = GroupTop + 3;
    for (unsigned r = 0; r < g.count; ++r) height += RowHeight(g.rows[r]);
    return height;
}

void Build(Template &t, const Page &page, unsigned cx, unsigned cy)
{
    t.Dword(0x40010444); // WS_CHILD | WS_TABSTOP | DS_CONTROL | DS_SETFONT | DS_3DLOOK, as the pages are patched
    t.count_at = t.length;
    t.Byte(0);
    t.Word(0); t.Word(0); t.Word(cx); t.Word(cy);
    t.Byte(0); t.Byte(0); // no menu, the dialog class
    t.Text(page.title);
    t.Word(8);
    t.Text("MS Sans Serif");
    unsigned y = Margin - 1;
    const unsigned width = cx - 2 * Margin;
    for (unsigned g = 0; g < page.count; ++g)
    {
        const Group &group = page.groups[g];
        const unsigned height = GroupHeight(group);
        t.Item(Margin, y, width, height, 0xffff, Visible | 7, 0x80, group.title);
        unsigned row_y = y + GroupTop;
        for (unsigned r = 0; r < group.count; ++r)
        {
            const Row &row = group.rows[r];
            const Option &o = row.option;
            if (o.kind == KindSlider)
            {
                const unsigned column = r % 3, line = r / 3, column_width = (width - 14) / 3;
                const unsigned x = LabelX + column * column_width, top = y + GroupTop + line * SliderHeight;
                t.Item(x, top, column_width - 4, 8, 0xffff, Visible, 0x82, o.label);
                t.Item(x - 3, top + 8, column_width - 2, SliderTrack, row.id, Visible | TabStop | 0x1, "msctls_trackbar", "");
                continue;
            }
            if (o.kind == KindCheck && row.beside)
                t.Item(LabelX + (width - 14) / 2, row_y - CheckHeight + 1, (width - 14) / 2, 10, row.id,
                       Visible | TabStop | 3, 0x80, o.label);
            else if (o.kind == KindCheck)
            {
                const bool shared = r + 1 < group.count && group.rows[r + 1].beside;
                t.Item(LabelX, row_y + 1, shared ? (width - 14) / 2 - 2 : width - 14, 10, row.id, Visible | TabStop | 3, 0x80,
                       o.label);
            }
            else
            {
                t.Item(LabelX, row_y + 2, LabelWidth, 8, 0xffff, Visible, 0x82, o.label);
                t.Item(FieldX, row_y, cx - Margin - 7 - FieldX, 120, row.id, Visible | TabStop | 0x00200003, 0x85, "");
            }
            row_y += RowHeight(row);
        }
        y += height + 3;
    }
    t.bytes[t.count_at] = (unsigned char)t.items;
}

const Row *FindRow(const Page &page, unsigned id)
{
    for (unsigned g = 0; g < page.count; ++g)
        for (unsigned r = 0; r < page.groups[g].count; ++r)
            if (page.groups[g].rows[r].id == id) return &page.groups[g].rows[r];
    return nullptr;
}

// The section as "name=value" lines; Get answers the value of `key`.
bool Get(const char *section, const char *key, char *out, unsigned capacity)
{
    const unsigned n = (unsigned)strlen(key);
    for (const char *p = section; *p;)
    {
        const char *end = p;
        while (*end && *end != '\r' && *end != '\n') ++end;
        if ((unsigned)(end - p) > n && !strncmp(p, key, n) && p[n] == '=')
        {
            snprintf(out, capacity, "%.*s", (int)(end - p - n - 1), p + n + 1);
            return true;
        }
        p = end;
        while (*p == '\r' || *p == '\n') ++p;
    }
    return false;
}
// Put replaces or appends the line of `key`, or drops it for a null value.
void Put(char *section, unsigned capacity, const char *key, const char *value)
{
    char out[2048];
    unsigned used = 0;
    const unsigned n = (unsigned)strlen(key);
    for (const char *p = section; *p;)
    {
        const char *end = p;
        while (*end && *end != '\r' && *end != '\n') ++end;
        const bool mine = (unsigned)(end - p) > n && !strncmp(p, key, n) && p[n] == '=';
        if (!mine && end > p)
            used += snprintf(out + used, used < sizeof out ? sizeof out - used : 0, "%.*s\r\n", (int)(end - p), p);
        p = end;
        while (*p == '\r' || *p == '\n') ++p;
    }
    if (value) used += snprintf(out + used, used < sizeof out ? sizeof out - used : 0, "%s=%s\r\n", key, value);
    snprintf(section, capacity, "%s", used < sizeof out ? out : "");
}
} // namespace

unsigned Application::PifOptionPageCount() const { return PageCount; }

unsigned char *Application::PifOptionTemplate(unsigned page, unsigned &length, char *caption, unsigned capacity, unsigned &cx,
                                              unsigned &cy)
{
    length = cx = cy = 0;
    if (caption && capacity) caption[0] = 0;
    if (page >= PageCount) return nullptr;
    // The size of the pages before: PIFMGR's Misc page (template 108).
    unsigned misc_length = 0;
    delete[] PifPagesTemplate(108, false, misc_length, nullptr, 0, cx, cy);
    if (!cx || !cy) { cx = 227; cy = 215; }
    if (caption && capacity) snprintf(caption, capacity, "%s", Pages[page].title);
    auto *t = new Template;
    if (!t) return nullptr;
    Build(*t, Pages[page], cx, cy);
    unsigned char *out = new unsigned char[4 * t->length + 64];
    if (out && !ConvertDialogTemplate(t->bytes, t->length, out, 4 * t->length + 64, length))
    {
        delete[] out;
        out = nullptr;
    }
    delete t;
    return out;
}

// The soundfonts and ROM sets of C:\WINDOWS\SYSTEM for the MIDI list, the
// files the machine's own scan would pick: *.SF2, *_CONTROL.ROM (MT-32,
// the PCM ROM beside it) and *ROM1.BIN (SC-55).
void Application::PifOptionMidiFiles(unsigned hwnd, unsigned id)
{
    static const struct { const char *pattern, *kind; } kinds[] = {
        {"C:\\WINDOWS\\SYSTEM\\*.SF2", "SoundFont"}, {"C:\\WINDOWS\\SYSTEM\\*_CONTROL.ROM", "Roland MT-32"},
        {"C:\\WINDOWS\\SYSTEM\\*ROM1.BIN", "Roland SC-55"}};
    const unsigned data = Allocate(320), pattern = Allocate(64);
    if (!data || !pattern) { if (data) Free(data); if (pattern) Free(pattern); return; }
    const unsigned combo = User::GetDlgItem(hwnd, id);
    for (const auto &k : kinds)
    {
        Store(pattern, k.pattern, (unsigned)strlen(k.pattern) + 1);
        const unsigned find[] = {pattern, data};
        const unsigned handle = Api("FindFirstFileA", find);
        if (handle == 0xffffffffu) continue;
        for (unsigned more = 1; more;)
        {
            char line[300];
            snprintf(line, sizeof line, "%s: %s", k.kind, ReadString(data + 44));
            const unsigned s = String(line);
            if (s)
            {
                const unsigned item = Send(combo, CbAddString, 0, s);
                Send(combo, CbSetItemData, item, 0x1000);
                Free(s);
            }
            const unsigned next[] = {handle, data};
            more = Api("FindNextFileA", next);
        }
        const unsigned close[] = {handle};
        Api("FindClose", close);
    }
    Free(data);
    Free(pattern);
}

// The machine options of a sheet's file, the section as a C string.
void Application::PifOptionSection(unsigned props, char *out, unsigned capacity)
{
    out[0] = 0;
    auto *record = PifMgrRecord(props);
    if (!record) return;
    const unsigned n = Win98PifMgr::GetNamed(*record, Win98PifMgr::OptionSection, reinterpret_cast<unsigned char *>(out), capacity - 1);
    out[n < capacity ? n : capacity - 1] = 0;
}

// Which rows make sense for the adapter and the speed: the chip and memory
// only on SVGA, the CGA model on CGA, the colours on Hercules; the limit and
// the host load for automatic and maximum speed, the adjustment for a fixed one.
void Application::PifOptionEnable(unsigned hwnd, unsigned page)
{
    auto selected = [&](unsigned id, const Row *row) -> const char * {
        const unsigned combo = User::GetDlgItem(hwnd, id);
        if (!combo || !row) return "";
        const unsigned item = Send(combo, CbGetCurSel, 0, 0);
        const unsigned index = Send(combo, CbGetItemData, item, 0);
        return index < row->option.count ? row->option.values[index].value : "";
    };
    auto enable = [&](unsigned id, bool on) {
        const unsigned a[] = {User::GetDlgItem(hwnd, id), on ? 1u : 0u};
        if (a[0]) Api("EnableWindow", a);
    };
    const Page &p = Pages[page];
    if (const Row *row = FindRow(p, 201))
    {
        const char *machine = selected(201, row);
        enable(202, !strcmp(machine, "svga"));
        enable(203, !strcmp(machine, "svga"));
        enable(204, !strcmp(machine, "cga"));
        enable(205, !strcmp(machine, "hercules"));
    }
    if (const Row *row = FindRow(p, 302))
    {
        const char *speed = selected(302, row);
        const bool free_running = !strcmp(speed, "auto") || !strcmp(speed, "max");
        enable(303, free_running);
        enable(305, free_running);
        enable(304, !free_running);
    }
}

unsigned Application::PifOptionMessage(unsigned index, unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    const unsigned kind = PifPageKind(index), props = PifPageProps(index);
    if (kind == ~0u) return 0;
    const unsigned page = kind - PifPageKinds() + PageCount;
    if (page >= PageCount) return 0;
    const Page &p = Pages[page];
    auto set_result = [&](unsigned value) { User::WindowLong(hwnd, 0, value, true); };
    auto changed = [&] { Send(User::Parent(hwnd), PsmChanged, hwnd, 0); };
    if (message == WmInitDialog)
    {
        char section[2048];
        PifOptionSection(props, section, sizeof section);
        for (unsigned g = 0; g < p.count; ++g)
            for (unsigned r = 0; r < p.groups[g].count; ++r)
            {
                const Row &row = p.groups[g].rows[r];
                const Option &o = row.option;
                char value[260];
                if (!Get(section, o.key, value, sizeof value)) snprintf(value, sizeof value, "%s", o.fallback);
                const unsigned control = User::GetDlgItem(hwnd, row.id);
                if (!control) continue;
                unsigned chosen = 0;
                for (unsigned i = 0; i < o.count; ++i)
                    if (!strcmp(o.values[i].value, value)) chosen = i;
                if (o.kind == KindCheck)
                {
                    const unsigned a[] = {hwnd, row.id, chosen == 1 ? 1u : 0u};
                    Api("CheckDlgButton", a);
                }
                else if (o.kind == KindSlider)
                {
                    Send(control, TbmSetRange, 0, (o.count - 1) << 16);
                    Send(control, TbmSetPos, 1, chosen);
                }
                else
                {
                    for (unsigned i = 0; i < o.count; ++i)
                    {
                        const unsigned s = String(o.values[i].label);
                        if (!s) continue;
                        const unsigned item = Send(control, CbAddString, 0, s);
                        Send(control, CbSetItemData, item, i);
                        Free(s);
                    }
                    unsigned selection = chosen;
                    if (o.kind == KindMidi)
                    {
                        PifOptionMidiFiles(hwnd, row.id);
                        // A file the PIF names is chosen by its name.
                        const char *leaf = strrchr(value, '\\');
                        if (leaf)
                        {
                            const unsigned count = Send(control, 0x146, 0, 0); // CB_GETCOUNT
                            const unsigned text = Allocate(300);
                            for (unsigned i = o.count; text && i < count; ++i)
                            {
                                Send(control, CbGetLbText, i, text);
                                const char *shown = ReadString(text);
                                const char *colon = strstr(shown, ": ");
                                if (colon && !strcmp(colon + 2, leaf + 1)) selection = i;
                            }
                            if (text) Free(text);
                        }
                    }
                    Send(control, CbSetCurSel, selection, 0);
                }
            }
        PifOptionEnable(hwnd, page);
        return 1;
    }
    if (message == WmCommand)
    {
        const unsigned id = wp & 0xffff, code = wp >> 16;
        const Row *row = FindRow(p, id);
        if (!row) return 0;
        if ((row->option.kind == KindCheck && code == BnClicked) || (row->option.kind != KindCheck && code == CbnSelChange))
        {
            PifOptionEnable(hwnd, page);
            changed();
        }
        return 1;
    }
    if (message == WmHScroll)
    {
        changed();
        return 1;
    }
    if (message == WmNotify && lp)
    {
        const unsigned code = Read32(lp + 8);
        if (code == PsnSetActive || code == PsnKillActive)
        {
            set_result(0);
            return 1;
        }
        if (code != PsnApply) return 0;
        char section[2048];
        PifOptionSection(props, section, sizeof section);
        for (unsigned g = 0; g < p.count; ++g)
            for (unsigned r = 0; r < p.groups[g].count; ++r)
            {
                const Row &row = p.groups[g].rows[r];
                const Option &o = row.option;
                const unsigned control = User::GetDlgItem(hwnd, row.id);
                if (!control) continue;
                char value[260];
                value[0] = 0;
                if (o.kind == KindCheck)
                {
                    const unsigned a[] = {hwnd, row.id};
                    snprintf(value, sizeof value, "%s", o.values[Api("IsDlgButtonChecked", a) ? 1 : 0].value);
                }
                else if (o.kind == KindSlider)
                {
                    const unsigned position = Send(control, TbmGetPos, 0, 0);
                    snprintf(value, sizeof value, "%s", o.values[position < o.count ? position : o.count - 1].value);
                }
                else
                {
                    const unsigned item = Send(control, CbGetCurSel, 0, 0);
                    const unsigned data = Send(control, CbGetItemData, item, 0);
                    if (data < o.count) snprintf(value, sizeof value, "%s", o.values[data].value);
                    else if (data == 0x1000)
                    {
                        // A soundfont or ROM set: its DOS path.
                        const unsigned text = Allocate(300);
                        if (text)
                        {
                            Send(control, CbGetLbText, item, text);
                            const char *colon = strstr(ReadString(text), ": ");
                            if (colon) snprintf(value, sizeof value, "C:\\WINDOWS\\SYSTEM\\%s", colon + 2);
                            Free(text);
                        }
                    }
                }
                Put(section, sizeof section, o.key, strcmp(value, o.fallback) ? value : nullptr);
            }
        const unsigned n = (unsigned)strlen(section);
        const unsigned char none = 0;
        // An empty section goes away; SetNamed removes a section of no bytes.
        const bool ok = PifMgrSetNamed(props, Win98PifMgr::OptionSection,
                                       n ? reinterpret_cast<const unsigned char *>(section) : &none, n ? n + 1 : 0) != 0 || !n;
        const bool flushed = ok && PifMgrFlushHandle(props);
        if (log)
        {
            char text[160];
            snprintf(text, sizeof text, "PIFOPTIONS apply page=%s ok=%u bytes=%u", p.title, flushed ? 1u : 0u, n);
            log(text);
        }
        if (!flushed) PifPageMessageBox(hwnd, ~0u, 3335, 0, 0);
        set_result(flushed ? 0 : 1);
        return 1;
    }
    return 0;
}
} // namespace X86
