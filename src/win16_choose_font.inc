// SPDX-License-Identifier: GPL-2.0-or-later
// ChooseFont of the 16-bit COMMDLG (ordinal 15), built natively the way
// COMMDLG.DLL of Win98 builds it. Included in src/win16_commdlg.cpp
// after src/win16_choose_color.inc, whose resource helpers it uses.
//
// The 16-bit COMMDLG shows the Windows 3.1 font dialog. ChooseFont checks
// the structure, selects the caller's template or its own dialog, and runs
// DialogBoxIndirectParam with FormatCharDlgProc. The procedure handles:
//
// WM_INITDIALOG: the four style names (strings 504h..507h), the
// colour list, the default LOGFONT, the face
// list, styles, sizes and scripts of the face, the style
// and the size of the caller's LOGFONT, the hook last
// WM_COMMAND: OK (size check, LOGFONT,
// CF_FORCEFONTEXIST), Cancel, Help, the three lists with their
// private notifications 5F4h and 5F5h, effects, colour and script
// WM_PAINT: the sample in the font the controls describe
// WM_DRAWITEM of the lists; the face list shows
// the TrueType and printer marks of bitmap 190h
// WM_CHOOSEFONT_GETLOGFONT, WM_CHOOSEFONT_SETLOGFONT,
// WM_CHOOSEFONT_SETFLAGS
//
// The native dialog speaks the 32-bit messages of its controls (CB_* at
// 140h..154h instead of 400h..414h); the hook is the caller's 16-bit
// procedure and sees every message first, WM_INITDIALOG with the far pointer
// of its own CHOOSEFONT. Where the original keeps a LOGFONT in its local heap
// as the item data of a style, the native frame keeps it in a slot and the
// item data names the slot; the high word is the font type.
namespace
{
// CHOOSEFONT16 (lStructSize 2Eh).
constexpr unsigned CfOwner = 0x04, CfDc = 0x06, CfLogFont = 0x08, CfPointSize = 0x0c, CfFlags = 0x0e, CfColors = 0x12,
                   CfHook = 0x1a, CfTemplate = 0x1e, CfInstance = 0x22, CfStyle = 0x24, CfFontType = 0x28,
                   CfSizeMin = 0x2a, CfSizeMax = 0x2c;
constexpr unsigned CfScreenFonts = 0x1, CfPrinterFonts = 0x2, CfShowHelp = 0x4, CfEnableHook = 0x8,
                   CfEnableTemplate = 0x10, CfEnableTemplateHandle = 0x20, CfInitToLogFont = 0x40, CfUseStyle = 0x80,
                   CfEffects = 0x100, CfApply = 0x200, CfAnsiOnly = 0x400, CfNoOemFonts = 0x800,
                   CfNoSimulations = 0x1000, CfLimitSize = 0x2000, CfFixedPitchOnly = 0x4000, CfWysiwyg = 0x8000,
                   CfForceFontExist = 0x10000, CfScalableOnly = 0x20000, CfTrueTypeOnly = 0x40000,
                   CfNoFaceSel = 0x80000, CfNoStyleSel = 0x100000, CfNoSizeSel = 0x200000, CfSelectScript = 0x400000,
                   CfNoScriptSel = 0x800000,
                   // Set by ChooseFont itself. The first
                   // for a hook of a program for Windows 3.0 or older (no
                   // WM_HELP, a white sample), the second for a template of
                   // a 4.0 module or COMMDLG's own (script list, DrawEdge).
                   CfOldApplication = 0x10000000, CfNewTemplate = 0x20000000;
// Control identifiers of the template (font.dlg).
constexpr unsigned FontFace = 0x470, FontStyle = 0x471, FontSize = 0x472, FontColor = 0x473, FontScript = 0x474,
                   FontSample = 0x444, FontKind = 0x445, FontStrikeout = 0x410, FontUnderline = 0x411,
                   FontApply = 0x402, FontHelp = 0x40e, FontColorLabel = 0x443, FontEffects = 0x430;
// The font type bits of the item data: GDI's RASTER (1), DEVICE (2) and
// TRUETYPE (4), the list the face came from, the style and the simulation.
constexpr unsigned TypeScreen = 0x2000, TypePrinter = 0x4000, TypeSimulated = 0x8000, TypeBold = 0x100,
                   TypeItalic = 0x200, TypeRegular = 0x400;
// 32-bit combo box messages.
constexpr unsigned CbLimitText = 0x141, CbSetEditSel = 0x142, CbAddString = 0x143, CbDeleteString = 0x144,
                   CbGetCount = 0x146, CbGetCurSel = 0x147, CbGetLbText = 0x148, CbInsertString = 0x14a,
                   CbResetContent = 0x14b, CbSetCurSel = 0x14e, CbGetItemData = 0x150, CbSetItemData = 0x151,
                   CbSetItemHeight = 0x153, CbGetItemHeight = 0x154;
// Private notifications used by the Windows 98 font dialog.
constexpr unsigned NotifySelectAll = 0x5f4, NotifyTyped = 0x5f5;
// data 17Ch: the sixteen colours of the colour list, names 410h..41Fh.
const unsigned FontColors[16] = {0x000000, 0x000080, 0x008000, 0x008080, 0x800000, 0x800080, 0x808000, 0x808080,
                                 0xc0c0c0, 0x0000ff, 0x00ff00, 0x00ffff, 0xff0000, 0xff00ff, 0xffff00, 0xffffff};
// the help identifiers of HELP_WM_HELP and HELP_CONTEXTMENU.
const unsigned FontHelpTable[] = {0x440, 0x6f24, 0x470, 0x6f24, 0x441, 0x6f20, 0x471, 0x6f20, 0x442, 0x6f21, 0x472,
                                  0x6f21, 0x402, 0x6f1f, 0x430, 0x6f23, 0x410, 0x6f23, 0x411, 0x6f23, 0x443, 0x6f69,
                                  0x473, 0x6f69, 0x431, 0x6f22, 0x444, 0x6f22, 0x445, 0x6f22, 0x446, 0x6f77, 0x474,
                                  0x6f77, 0, 0};
// A LOGFONT in the terms of both sides.
struct FontLog
{
    int height = 0, width = 0, escapement = 0, orientation = 0, weight = 0;
    unsigned char italic = 0, underline = 0, strikeout = 0, charset = 0, out = 0, clip = 0, quality = 0, pitch = 0;
    char face[32] = {};
};
// The 32-bit record of the shared font provider: LOGFONTA at 0, the full
// name at 60, the style at 124, the script at 156, NEWTEXTMETRICA at 188
// (ntmFlags at 244).
struct FontRecord
{
    unsigned char bytes[264];
    unsigned Dword(unsigned at) const
    {
        return unsigned(bytes[at]) | unsigned(bytes[at + 1]) << 8 | unsigned(bytes[at + 2]) << 16 |
               unsigned(bytes[at + 3]) << 24;
    }
    FontLog Log() const
    {
        FontLog l;
        l.height = int(Dword(0));
        l.width = int(Dword(4));
        l.escapement = int(Dword(8));
        l.orientation = int(Dword(12));
        l.weight = int(Dword(16));
        l.italic = bytes[20];
        l.underline = bytes[21];
        l.strikeout = bytes[22];
        l.charset = bytes[23];
        l.out = bytes[24];
        l.clip = bytes[25];
        l.quality = bytes[26];
        l.pitch = bytes[27];
        memcpy(l.face, bytes + 28, 31);
        return l;
    }
    const char *Text(unsigned at, unsigned size, char *out) const
    {
        memcpy(out, bytes + at, size);
        out[size - 1] = 0;
        return out;
    }
    int MetricHeight() const { return int(Dword(188)); }
    int MetricInternal() const { return int(Dword(188 + 12)); }
    unsigned MetricFlags() const { return Dword(188 + 56); }
};
constexpr unsigned FontStyleSlots = 96;
struct Win16FontFrame
{
    Application *app;
    Win16FontFrame *next;
    unsigned hwnd = 0, native_template = 0;
    bool initializing = true;
    unsigned cf = 0, cf_far = 0, hook = 0;
    bool ready = false; // SetProp; before it only the hook runs
    bool ended = false; // RemoveProp; after it only the hook runs
    Box sample;         // frame+6: the sample rectangle in dialog coordinates
    unsigned charset = 0, fonttype = 0; // frame+0Eh, frame+10h
    unsigned scratch = 0;               // guest bytes for texts, rectangles, records
    unsigned message_help = 0;
    FontLog styles[FontStyleSlots];
    bool used[FontStyleSlots] = {};
    static unsigned bitmap; // DS:1BCh, the marks of bitmap 190h
    static char names[4][32]; // DS:588h, DA2h, B66h, 974h
    static Win16FontFrame *head;
    Win16FontFrame(Application *a) : app(a), next(head) { head = this; }
    ~Win16FontFrame()
    {
        auto **at = &head;
        while (*at && *at != this)
            at = &(*at)->next;
        if (*at)
            *at = next;
    }
};
unsigned Win16FontFrame::bitmap = 0;
char Win16FontFrame::names[4][32] = {};
Win16FontFrame *Win16FontFrame::head = nullptr;
Win16FontFrame *Win16FontFrameOf(Application *app, unsigned hwnd)
{
    for (auto *f = Win16FontFrame::head; f; f = f->next)
        if (f->app == app && f->hwnd == hwnd)
            return f;
    // The dialog's first message binds the frame ChooseFont made for it.
    for (auto *f = Win16FontFrame::head; f; f = f->next)
        if (f->app == app && !f->hwnd)
        {
            f->hwnd = hwnd;
            return f;
        }
    return nullptr;
}
// The state, and their callbacks share through lParam.
struct Win16FontEnumState
{
    enum Kind { Families, Scripts, Styles } kind = Families;
    Win16FontFrame *frame = nullptr;
    unsigned combo = 0, style = 0, size = 0, script = 0; // +2, +4, +6, +8
    unsigned result_charset = 0, saved_charset = 0;       // +0Ah, +0Ch
    unsigned dc = 0, flags = 0;                           // +0Eh, +10h
    unsigned face_type = 0;                               // +14h
    bool sizes = false, printer = false;                  // +16h, +18h
};
Win16FontEnumState *active_font_enum = nullptr;
} // namespace

struct Application::Win16FontDialog
{
    Application &app;
    Win16FontFrame &f;
    bool recorded = false;
    template <class... T> unsigned Call(const char *name, T... values)
    {
        const unsigned a[] = {unsigned(values)..., 0u};
        return app.Api(name, a);
    }
    unsigned Item(unsigned id) { return User::GetDlgItem(f.hwnd, id); }
    unsigned Flags() { return app.Read32(f.cf + CfFlags); }
    void SetFlags(unsigned value) { app.Write32(f.cf + CfFlags, value); }
    unsigned Send(unsigned hwnd, unsigned message, unsigned wp = 0, unsigned lp = 0)
    {
        return Call("SendMessageA", hwnd, message, wp, lp);
    }
    unsigned LogFontAt() { return app.Win16FarPointer(f.cf + CfLogFont); }
    // Guest scratch: +000h and +100h two texts of 256 bytes, +200h the paint
    // structure and rectangles, +300h the help table, +3A0h a LOGFONTA,
    // +3F0h four words.
    unsigned TextA() { return f.scratch; }
    unsigned TextB() { return f.scratch + 0x100; }
    unsigned RectAt() { return f.scratch + 0x280; }
    unsigned LogAt() { return f.scratch + 0x3a0; }
    unsigned WordAt() { return f.scratch + 0x3f0; }
    void Put(unsigned at, const char *text)
    {
        char copy[256] = {};
        strncpy(copy, text, sizeof copy - 1);
        app.Store(at, copy, unsigned(strlen(copy)) + 1);
    }
    void Get(unsigned at, char *out, unsigned capacity)
    {
        const char *s = app.ReadString(at);
        unsigned i = 0;
        for (; s && s[i] && i + 1 < capacity; ++i)
            out[i] = s[i];
        out[i] = 0;
    }
    bool LoadString(unsigned id, char *out, unsigned capacity)
    {
        auto &images = app.CommDlgResources();
        out[0] = 0;
        return images.file && NeString(images.file, images.length, id, out, capacity);
    }
    unsigned Hook(unsigned message, unsigned wp, unsigned lp)
    {
        if (!f.hook || !User::IsWindow(f.hwnd))
            return 0;
        const unsigned arguments[] = {f.hwnd, message, wp, lp};
        return app.Call(f.hook, arguments, 4) & 0xffff;
    }
    // A LOGFONT in the 16-bit caller's layout (50 bytes).
    static void Narrow(const FontLog &l, unsigned char *b)
    {
        auto word = [&](unsigned at, int v) {
            b[at] = static_cast<unsigned char>(v);
            b[at + 1] = static_cast<unsigned char>(unsigned(v) >> 8);
        };
        memset(b, 0, 50);
        word(0, l.height);
        word(2, l.width);
        word(4, l.escapement);
        word(6, l.orientation);
        word(8, l.weight);
        b[10] = l.italic;
        b[11] = l.underline;
        b[12] = l.strikeout;
        b[13] = l.charset;
        b[14] = l.out;
        b[15] = l.clip;
        b[16] = l.quality;
        b[17] = l.pitch;
        memcpy(b + 18, l.face, 31);
    }
    static FontLog Widen16(const unsigned char *b)
    {
        auto word = [&](unsigned at) { return int(short(b[at] | b[at + 1] << 8)); };
        FontLog l;
        l.height = word(0);
        l.width = word(2);
        l.escapement = word(4);
        l.orientation = word(6);
        l.weight = word(8);
        l.italic = b[10];
        l.underline = b[11];
        l.strikeout = b[12];
        l.charset = b[13];
        l.out = b[14];
        l.clip = b[15];
        l.quality = b[16];
        l.pitch = b[17];
        memcpy(l.face, b + 18, 31);
        return l;
    }
    FontLog ReadLog16(unsigned at)
    {
        unsigned char b[50] = {};
        if (at)
            app.Read(at, b, sizeof b);
        return Widen16(b);
    }
    void WriteLog16(unsigned at, const FontLog &l)
    {
        unsigned char b[50];
        Narrow(l, b);
        if (at)
            app.Store(at, b, sizeof b);
    }
    // The same LOGFONT as LOGFONTA in the scratch, for CreateFontIndirectA.
    unsigned WideLog(const FontLog &l)
    {
        unsigned char b[60] = {};
        const int values[5] = {l.height, l.width, l.escapement, l.orientation, l.weight};
        memcpy(b, values, sizeof values);
        b[20] = l.italic;
        b[21] = l.underline;
        b[22] = l.strikeout;
        b[23] = l.charset;
        b[24] = l.out;
        b[25] = l.clip;
        b[26] = l.quality;
        b[27] = l.pitch;
        memcpy(b + 28, l.face, 31);
        app.Store(LogAt(), b, sizeof b);
        return LogAt();
    }

    // a control that does not apply is disabled and hidden.
    void Remove(unsigned id)
    {
        const unsigned item = Item(id);
        User::EnableWindow(item, false);
        User::ShowWindow(item, 0);
    }
    // the face and size lists take the selection height of the
    // style list.
    void Heights()
    {
        const unsigned height = Send(Item(FontStyle), CbGetItemHeight, unsigned(-1), 0);
        Send(Item(FontFace), CbSetItemHeight, unsigned(-1), height);
        Send(Item(FontSize), CbSetItemHeight, unsigned(-1), height);
    }
    // the entry whose text equals `text` without regard to case.
    int FindExact(unsigned combo, const char *text)
    {
        const int count = int(Send(combo, CbGetCount));
        if (count <= 0)
            return -1;
        Put(TextB(), text);
        for (int i = 0; i < count; ++i)
        {
            Send(combo, CbGetLbText, unsigned(i), TextA());
            if (!Call("lstrcmpiA", TextA(), TextB()))
                return i;
        }
        return -1;
    }
    // select that entry.
    int SelectExact(unsigned combo, const char *text)
    {
        const int index = FindExact(combo, text);
        if (index >= 0)
            Send(combo, CbSetCurSel, unsigned(index));
        return index;
    }
    // the edit field of a simple combo box shows the selection.
    int ShowSelection(unsigned combo)
    {
        const int selection = int(Send(combo, CbGetCurSel));
        if (selection >= 0)
        {
            Send(combo, CbGetLbText, unsigned(selection), TextA());
            Call("SetWindowTextA", combo, TextA());
        }
        return selection;
    }
    // the text of the edit field, and the data of the entry that
    // carries it.
    int TypedEntry(unsigned combo, char *text, unsigned capacity, unsigned &data)
    {
        Call("GetWindowTextA", combo, TextA(), capacity);
        Get(TextA(), text, capacity);
        const int index = FindExact(combo, text);
        if (index >= 0)
            data = Send(combo, CbGetItemData, unsigned(index));
        return index;
    }
    // the LOGFONT of 10 points in the screen's character set.
    FontLog DefaultLog()
    {
        const unsigned dc = Call("GetDC", 0);
        FontLog l;
        l.charset = static_cast<unsigned char>(Call("GetTextCharset", dc));
        l.weight = 400;
        l.height = -MulDiv16(10, int(Call("GetDeviceCaps", dc, 0x5a)), 72);
        Call("ReleaseDC", 0, dc);
        return l;
    }
    // the points of a height on a device, as text.
    int Points(unsigned dc, int height, char *text)
    {
        const int points = MulDiv16(height < 0 ? -height : height, 72, int(Call("GetDeviceCaps", dc, 0x5a)));
        snprintf(text, 16, "%d", points);
        return points;
    }
    // one size into the ordered size list, inside CF_LIMITSIZE.
    void AddSize(unsigned combo, int points)
    {
        if ((Flags() & CfLimitSize) &&
            (int(short(app.Win16Word(f.cf + CfSizeMax))) < points || int(short(app.Win16Word(f.cf + CfSizeMin))) > points))
            return;
        char text[16];
        snprintf(text, sizeof text, "%d", points);
        int found = -1, index = 0;
        const int count = int(Send(combo, CbGetCount));
        for (; index < count; ++index)
        {
            found = int(short(Send(combo, CbGetItemData, unsigned(index))));
            if (found >= points)
                break;
        }
        if (found == points)
            return;
        Put(TextA(), text);
        const int at = int(Send(combo, CbInsertString, unsigned(index), TextA()));
        if (at >= 0)
            Send(combo, CbSetItemData, unsigned(at), unsigned(points));
    }
    // the sizes of a scalable face.
    void StandardSizes(unsigned combo)
    {
        static const int sizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72};
        for (int size : sizes)
            AddSize(combo, size);
    }
    const FontLog *StyleOf(unsigned data)
    {
        const unsigned slot = (data & 0xffff) - 1;
        return slot < FontStyleSlots && f.used[slot] ? &f.styles[slot] : nullptr;
    }
    // the place of a style, by weight and then upright before italic.
    int InsertStyle(unsigned combo, const char *name, const FontLog &l)
    {
        int index = 0;
        const int count = int(Send(combo, CbGetCount));
        for (; index < count; ++index)
        {
            const FontLog *other = StyleOf(Send(combo, CbGetItemData, unsigned(index)));
            const int weight = other ? other->weight : 0;
            if (l.weight < weight)
                break;
            if (l.weight == weight)
            {
                if (l.italic && other && !other->italic)
                    ++index;
                break;
            }
        }
        Put(TextA(), name);
        return int(Send(combo, CbInsertString, unsigned(index), TextA()));
    }
    // a style once, with a copy of its LOGFONT.
    bool AddStyle(unsigned combo, const char *name, unsigned type, const FontLog &l)
    {
        if (FindExact(combo, name) >= 0)
            return false;
        const int index = InsertStyle(combo, name, l);
        if (index < 0)
            return false;
        unsigned slot = 0;
        while (slot < FontStyleSlots && f.used[slot])
            ++slot;
        if (slot == FontStyleSlots)
        {
            Send(combo, CbDeleteString, unsigned(index));
            return false;
        }
        f.used[slot] = true;
        f.styles[slot] = l;
        Send(combo, CbSetItemData, unsigned(index), (type & 0xffff) << 16 | (slot + 1));
        return true;
    }
    // a script once, with its character set.
    int AddScript(unsigned combo, const char *name, unsigned charset)
    {
        if (!User::IsWindow(combo) || !User::IsEnabled(combo) || FindExact(combo, name) >= 0)
            return -1;
        Put(TextA(), name);
        const int index = int(Send(combo, CbAddString, 0, TextA()));
        if (index >= 0)
            Send(combo, CbSetItemData, unsigned(index), charset);
        return index;
    }
    // the style list gives up its LOGFONTs.
    void FreeStyles(unsigned combo)
    {
        const int count = int(Send(combo, CbGetCount));
        for (int i = 0; i < count; ++i)
        {
            const unsigned slot = (Send(combo, CbGetItemData, unsigned(i)) & 0xffff) - 1;
            if (slot < FontStyleSlots)
                f.used[slot] = false;
        }
        Send(combo, CbResetContent);
    }
    // bold, italic and bold italic imitated from what the face has.
    void Simulations(unsigned combo)
    {
        const FontLog *regular = nullptr, *bold = nullptr, *italic = nullptr;
        bool bold_italic = false, have_bold = false, have_italic = false;
        unsigned type = 0;
        const int count = int(Send(combo, CbGetCount));
        for (int i = 0; i < count; ++i)
        {
            const unsigned data = Send(combo, CbGetItemData, unsigned(i));
            type = data >> 16;
            const FontLog *l = StyleOf(data);
            if ((type & TypeBold) && (type & TypeItalic))
                bold_italic = true;
            else if (type & TypeBold)
            {
                have_bold = true;
                bold = l;
            }
            else if (type & TypeItalic)
            {
                have_italic = true;
                italic = l;
            }
            else
                regular = l;
        }
        type |= TypeSimulated;
        if (!have_bold && regular)
        {
            FontLog l = *regular;
            l.weight = 700;
            AddStyle(combo, Win16FontFrame::names[1], type | TypeBold, l);
        }
        if (!have_italic && regular)
        {
            FontLog l = *regular;
            l.italic = 1;
            AddStyle(combo, Win16FontFrame::names[2], type | TypeItalic, l);
        }
        if (!bold_italic && (have_bold || have_italic || regular))
        {
            const FontLog *from = italic ? italic : bold ? bold : regular;
            if (from)
            {
                FontLog l = *from;
                l.italic = 1;
                l.weight = 700;
                AddStyle(combo, Win16FontFrame::names[3], type | TypeBold | TypeItalic, l);
            }
        }
    }
    unsigned Enumerate(Win16FontEnumState &state, unsigned dc, const FontLog &l)
    {
        state.dc = dc;
        unsigned char b[60] = {};
        b[23] = l.charset;
        memcpy(b + 28, l.face, 31);
        app.Store(LogAt(), b, sizeof b);
        Win16FontEnumState *previous = active_font_enum;
        active_font_enum = &state;
        const unsigned answer = Call("EnumFontFamiliesExA", dc, LogAt(), Application::Win16FontEnumToken, 0, 0);
        active_font_enum = previous;
        return answer;
    }
    // the faces of the screen and, with CF_PRINTERFONTS, of the
    // printer; faces of the wrong kind go again.
    bool Families(unsigned dc, unsigned flags, unsigned charset)
    {
        Win16FontEnumState state;
        state.kind = Win16FontEnumState::Families;
        state.frame = &f;
        state.combo = Item(FontFace);
        state.flags = flags;
        FontLog l;
        l.charset = static_cast<unsigned char>(flags & CfSelectScript ? charset : 1);
        const unsigned screen = Call("GetDC", 0);
        Enumerate(state, screen, l);
        Call("ReleaseDC", 0, screen);
        if (flags & CfPrinterFonts)
        {
            state.printer = true;
            Enumerate(state, dc, l);
        }
        auto drop = [&](auto keep) {
            for (int i = int(Send(state.combo, CbGetCount)) - 1; i >= 0; --i)
                if (!keep(Send(state.combo, CbGetItemData, unsigned(i)) & 0x6000))
                    Send(state.combo, CbDeleteString, unsigned(i));
        };
        if (!(flags & CfScreenFonts))
            drop([](unsigned kind) { return kind != TypeScreen; });
        if (flags & CfWysiwyg)
            drop([](unsigned kind) { return kind == (TypeScreen | TypePrinter); });
        if (int(Send(state.combo, CbGetCount)) > 0)
            return true;
        char caption[48], text[128];
        LoadString(0x500, caption, sizeof caption);
        LoadString(0x501, text, sizeof text);
        Put(TextA(), text);
        Put(TextB(), caption);
        Win16ControllerPhase phase(app, HostWin16Font, f.hwnd, Win16ControllerPhase::FontEmpty,
                                   0, 0, 0, recorded);
        phase.Warning(f.hwnd, TextA(), TextB(), 0x40);
        return false;
    }
    // which of two kinds of the same face wins.
    static int Rank(unsigned mine, unsigned other)
    {
        return int((mine & 0x9fff) ^ 1) - int((other & 0x9fff) ^ 1);
    }
    // The callback of the face list.
    unsigned Family(Win16FontEnumState &s, const FontRecord &r, unsigned type)
    {
        const unsigned flags = s.flags;
        const unsigned charset = r.bytes[23];
        if ((flags & CfTrueTypeOnly) && !(type & 4))
            return 1;
        if ((flags & CfScalableOnly) && (type & 1))
            return 1;
        if ((flags & CfAnsiOnly) && (charset == 0xff || charset == 2))
            return 1;
        if (charset == 0x4d)
            return 1;
        if ((flags & CfFixedPitchOnly) && (r.bytes[27] & 2))
            return 1;
        if ((flags & CfNoOemFonts) && charset == 0xff)
            return 1;
        const unsigned mine = type | (s.printer ? TypePrinter : TypeScreen);
        char face[32];
        r.Text(28, sizeof face, face);
        const int index = FindExact(s.combo, face);
        if (index < 0)
        {
            Put(TextA(), face);
            const int at = int(Send(s.combo, CbAddString, 0, TextA()));
            if (at < 0)
                return 0;
            Send(s.combo, CbSetItemData, unsigned(at), mine);
            return 1;
        }
        const unsigned other = Send(s.combo, CbGetItemData, unsigned(index)) & 0xffff;
        unsigned kept = other, merged = mine;
        bool replace = false;
        if (!(flags & CfScreenFonts) && (flags & CfPrinterFonts) && (mine & TypePrinter) && (other & TypeScreen))
            replace = true, kept = 0;
        else if (Rank(mine, other) > 0)
            replace = true;
        else
            merged = other;
        if (replace)
        {
            Put(TextA(), face);
            Send(s.combo, CbInsertString, unsigned(index), TextA());
            Send(s.combo, CbDeleteString, unsigned(index + 1));
        }
        Send(s.combo, CbSetItemData, unsigned(index), ((kept | mine) & 0x6000) | merged);
        return 1;
    }
    // The callback of the script list.
    unsigned Script(Win16FontEnumState &s, const FontRecord &r)
    {
        const unsigned charset = r.bytes[23];
        if ((s.flags & CfAnsiOnly) && (charset == 0xff || charset == 2))
            return 1;
        if (charset == 0x4d)
            return 1;
        if (s.script && !(s.flags & CfNoScriptSel))
        {
            char name[32];
            const int index = AddScript(s.script, r.Text(156, sizeof name, name), charset);
            if (charset == s.saved_charset)
            {
                s.result_charset = charset;
                if (index >= 0)
                    Send(s.script, CbSetCurSel, unsigned(index));
            }
            return Flags() & CfSelectScript ? 0 : 1;
        }
        s.result_charset = s.saved_charset = charset;
        return 0;
    }
    // The callback of the style and size lists.
    unsigned Style(Win16FontEnumState &s, const FontRecord &r, unsigned type)
    {
        if ((s.face_type & 7) != type || r.bytes[23] == 0x4d)
            return 1;
        if (!(type & 1))
        {
            if (s.sizes && !Send(s.size, CbGetCount))
                StandardSizes(s.size);
        }
        else
        {
            char text[16];
            const int points = Points(s.dc, r.MetricHeight() - r.MetricInternal(), text);
            if (FindExact(s.size, text) < 0)
                AddSize(s.size, points);
        }
        type |= s.face_type & 0x6000;
        const FontLog l = r.Log();
        if (type & 4)
        {
            const unsigned flags = r.MetricFlags();
            if (!(flags & 0x21))
                type |= TypeRegular;
            if (flags & 1)
                type |= TypeItalic;
            if (flags & 0x20)
                type |= TypeBold;
            char name[32];
            AddStyle(s.style, r.Text(124, sizeof name, name), type, l);
        }
        else if (l.weight >= 700 && l.italic)
            AddStyle(s.style, Win16FontFrame::names[3], type | TypeBold | TypeItalic, l);
        else if (l.weight >= 700)
            AddStyle(s.style, Win16FontFrame::names[1], type | TypeBold, l);
        else if (l.italic)
            AddStyle(s.style, Win16FontFrame::names[2], type | TypeItalic, l);
        else
            AddStyle(s.style, Win16FontFrame::names[0], type | TypeRegular, l);
        return 1;
    }
    // styles, sizes and scripts of the face in the face list.
    void FaceChanged(bool)
    {
        const unsigned flags = Flags();
        Win16FontEnumState s;
        s.frame = &f;
        s.combo = Item(FontFace);
        s.style = Item(FontStyle);
        s.size = Item(FontSize);
        s.script = flags & CfNewTemplate ? Item(FontScript) : 0;
        s.flags = flags;
        FreeStyles(s.style);
        if (!User::IsWindow(s.script) || !User::IsEnabled(s.script))
            s.script = 0;
        const int selection = int(Send(s.combo, CbGetCurSel));
        if (selection < 0)
        {
            // No face: the four styles of the default LOGFONT and the sizes of
            // a scalable face.
            FontLog l = DefaultLog();
            AddStyle(s.style, Win16FontFrame::names[0], TypeRegular, l);
            l.weight = 700;
            AddStyle(s.style, Win16FontFrame::names[1], TypeBold, l);
            l.weight = 400;
            l.italic = 1;
            AddStyle(s.style, Win16FontFrame::names[2], TypeItalic, l);
            l.weight = 700;
            AddStyle(s.style, Win16FontFrame::names[3], TypeBold | TypeItalic, l);
            StandardSizes(s.size);
            return;
        }
        s.face_type = Send(s.combo, CbGetItemData, unsigned(selection)) & 0xffff;
        s.sizes = true;
        Send(s.size, CbResetContent);
        Send(s.size, 0xb, 0);  // WM_SETREDRAW
        Send(s.style, 0xb, 0);
        FontLog l;
        Call("GetDlgItemTextA", f.hwnd, FontFace, TextA(), 32);
        Get(TextA(), l.face, sizeof l.face);
        if (s.script)
            Send(s.script, CbResetContent);
        s.result_charset = 0x100;
        s.saved_charset = f.charset;
        l.charset = static_cast<unsigned char>(flags & CfSelectScript ? f.charset : 1);
        s.kind = Win16FontEnumState::Scripts;
        if (flags & CfScreenFonts)
        {
            const unsigned screen = Call("GetDC", 0);
            s.printer = false;
            Enumerate(s, screen, l);
            Call("ReleaseDC", 0, screen);
        }
        const unsigned printer = app.Win16Word(f.cf + CfDc);
        if (flags & CfPrinterFonts)
        {
            s.printer = true;
            Enumerate(s, printer, l);
        }
        f.charset = s.saved_charset;
        if (s.result_charset == 0x100)
        {
            Send(s.script, CbSetCurSel, 0);
            s.result_charset = Send(s.script, CbGetItemData, 0) & 0xff;
        }
        f.charset = s.result_charset;
        l.charset = static_cast<unsigned char>(s.result_charset);
        s.kind = Win16FontEnumState::Styles;
        if (flags & CfScreenFonts)
        {
            const unsigned screen = Call("GetDC", 0);
            s.printer = false;
            Enumerate(s, screen, l);
            Call("ReleaseDC", 0, screen);
        }
        if (flags & CfPrinterFonts)
        {
            // the printer's sizes come in MM_TEXT.
            const unsigned mode = Call("GetMapMode", printer);
            unsigned window = 0, viewport = 0;
            if (mode == 7 || mode == 8)
            {
                Call("GetViewportExtEx", printer, WordAt());
                viewport = WordAt();
                Call("GetWindowExtEx", printer, WordAt() + 8);
                window = WordAt() + 8;
            }
            if (mode != 1)
                Call("SetMapMode", printer, 1);
            s.printer = true;
            Enumerate(s, printer, l);
            if (mode != 1)
            {
                Call("SetMapMode", printer, mode);
                if (window)
                {
                    Call("SetWindowExtEx", printer, app.Read32(window), app.Read32(window + 4), 0);
                    Call("SetViewportExtEx", printer, app.Read32(viewport), app.Read32(viewport + 4), 0);
                }
            }
        }
        if (!(flags & CfNoSimulations))
            Simulations(s.style);
        Send(s.style, 0xb, 1);
        if (s.sizes)
            Send(s.size, 0xb, 1);
        ScriptSample(s.script);
        if (flags & CfNoScriptSel)
            f.charset = 1;
    }
    // the style nearest the weight, upright or italic as asked.
    void NearestStyle(unsigned combo, const FontLog &l)
    {
        int best = 1000, index = 0;
        const int count = int(Send(combo, CbGetCount));
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = 0; i < count; ++i)
            {
                const FontLog *other = StyleOf(Send(combo, CbGetItemData, unsigned(i)));
                if (!other)
                    continue;
                if (!pass && (other->italic != 0) != (l.italic != 0))
                    continue;
                const int distance = l.weight > other->weight ? l.weight - other->weight : other->weight - l.weight;
                if (distance < best)
                {
                    best = distance;
                    index = i;
                }
            }
            if (index)
                break;
        }
        Send(combo, CbSetCurSel, unsigned(index));
    }
    // the style of the caller's LOGFONT, or lpszStyle.
    void SelectStyle(bool from_caller)
    {
        const unsigned flags = Flags();
        if (flags & CfNoStyleSel)
            return;
        const unsigned combo = Item(FontStyle), log = LogFontAt();
        if (from_caller && (flags & CfUseStyle))
        {
            char style[64] = {};
            const unsigned text = app.Win16FarPointer(f.cf + CfStyle);
            if (text)
                Get(text, style, sizeof style);
            FontLog l = ReadLog16(log);
            const int index = SelectExact(combo, style);
            if (index >= 0)
            {
                const FontLog *other = StyleOf(Send(combo, CbGetItemData, unsigned(index)));
                l.weight = other ? other->weight : 400;
                l.italic = other ? other->italic : 0;
            }
            else
            {
                l.weight = 400;
                l.italic = 0;
            }
            WriteLog16(log, l);
        }
        else
            NearestStyle(combo, ReadLog16(log));
        ShowSelection(combo);
    }
    // the size in points. Bit 0 checks it and says what is wrong,
    // bit 1 takes 10 for an empty field.
    bool PointSize(unsigned how, int &size)
    {
        size = 0;
        int value = 0;
        bool okay = false;
        if (Call("GetDlgItemTextA", f.hwnd, FontSize, TextA(), 90))
        {
            value = int(Call("GetDlgItemInt", f.hwnd, FontSize, WordAt(), 1));
            okay = app.Read32(WordAt()) != 0;
            if (!okay)
                value = 0;
        }
        else if (how & 2)
        {
            value = 10;
            okay = true;
        }
        else
            return false;
        if (how & 1)
        {
            char text[128] = {};
            const int minimum = int(short(app.Win16Word(f.cf + CfSizeMin))),
                      maximum = int(short(app.Win16Word(f.cf + CfSizeMax)));
            if (Flags() & CfLimitSize)
            {
                if (!okay || value > maximum || value < minimum)
                {
                    okay = false;
                    // String 44Bh with FormatMessage's inserts %1!d! and %2!d!.
                    char format[96];
                    LoadString(0x44b, format, sizeof format);
                    const int values[2] = {minimum, maximum};
                    unsigned at = 0;
                    for (const char *p = format; *p && at + 12 < sizeof text; ++p)
                    {
                        if (p[0] == '%' && (p[1] == '1' || p[1] == '2'))
                        {
                            at += unsigned(snprintf(text + at, sizeof text - at, "%d", values[p[1] - '1']));
                            p += 1;
                            if (p[1] == '!')
                            {
                                const char *end = strchr(p + 2, '!');
                                if (end)
                                    p = end;
                            }
                        }
                        else
                            text[at++] = *p;
                    }
                    text[at] = 0;
                }
            }
            else if (!okay)
                LoadString(0x44a, text, sizeof text);
            if (!okay)
            {
                Call("GetWindowTextA", f.hwnd, TextB(), 90);
                Put(TextA(), text);
                Win16ControllerPhase phase(app, HostWin16Font, f.hwnd, Win16ControllerPhase::FontField,
                                           0, FontSize, 0, recorded);
                phase.Warning(f.hwnd, TextA(), TextB(), 0x40);
                return false;
            }
        }
        size = value;
        return true;
    }
    // the sample text of the script (strings 600h + character set).
    bool ScriptSample(unsigned script)
    {
        unsigned charset = f.charset;
        if (script && User::IsWindow(script) && User::IsEnabled(script))
        {
            const int selection = int(Send(script, CbGetCurSel));
            if (selection >= 0)
            {
                charset = Send(script, CbGetItemData, unsigned(selection)) & 0xffff;
                f.charset = charset;
            }
        }
        char text[32];
        if (!LoadString(0x600 + charset, text, sizeof text))
            return false;
        Put(TextA(), text);
        Call("SetDlgItemTextA", f.hwnd, FontSample, TextA());
        return true;
    }
    // the LOGFONT the controls describe. With `update` the flags
    // CF_NOFACESEL, CF_NOSTYLESEL and CF_NOSIZESEL follow the fields, and
    // the line under the sample names the kind of the font.
    bool BuildLog(FontLog &l, bool update)
    {
        bool okay = true;
        l = DefaultLog();
        Call("GetDlgItemTextA", f.hwnd, FontFace, TextA(), 32);
        Get(TextA(), l.face, sizeof l.face);
        auto flag = [&](unsigned bit, bool on) {
            if (update)
                SetFlags(on ? Flags() | bit : Flags() & ~bit);
        };
        if (FindExact(Item(FontFace), l.face) >= 0)
            flag(CfNoFaceSel, false);
        else
        {
            okay = false;
            flag(CfNoFaceSel, true);
        }
        char style[32];
        unsigned data = 0, type = 0;
        if (TypedEntry(Item(FontStyle), style, sizeof style, data) >= 0)
        {
            if (const FontLog *chosen = StyleOf(data))
                l = *chosen;
            l.width = 0;
            flag(CfNoStyleSel, false);
            type = data >> 16;
        }
        else
        {
            okay = false;
            flag(CfNoStyleSel, true);
        }
        int size = 0;
        PointSize(0, size);
        const unsigned dc = Call("GetDC", 0);
        if (size)
        {
            l.height = -MulDiv16(size, int(Call("GetDeviceCaps", dc, 0x5a)), 72);
            flag(CfNoSizeSel, false);
        }
        else
        {
            l.height = -MulDiv16(10, int(Call("GetDeviceCaps", dc, 0x5a)), 72);
            okay = false;
            flag(CfNoSizeSel, true);
        }
        Call("ReleaseDC", 0, dc);
        l.strikeout = static_cast<unsigned char>(Call("IsDlgButtonChecked", f.hwnd, FontStrikeout));
        l.underline = static_cast<unsigned char>(Call("IsDlgButtonChecked", f.hwnd, FontUnderline));
        l.charset = static_cast<unsigned char>(f.charset);
        if (f.fonttype != type && (Flags() & CfPrinterFonts))
        {
            unsigned id = 0;
            if (type & TypeSimulated)
                id = 0x44c;
            else if (type & 4)
                id = 0x44d;
            else if ((type & 0x6000) == TypeScreen)
                id = 0x44f;
            else if ((type & 0x4002) == 0x4002)
                id = 0x44e;
            char text[128] = {};
            if (id)
                LoadString(id, text, sizeof text);
            Put(TextA(), text);
            Call("SetDlgItemTextA", f.hwnd, FontKind, TextA());
        }
        f.fonttype = type;
        return okay;
    }
    // bitmap 190h with its blue as COLOR_WINDOW and its magenta as
    // COLOR_HIGHLIGHT.
    unsigned Marks()
    {
        auto &images = app.CommDlgResources();
        unsigned size = 0;
        const unsigned char *b = images.file ? NeResource(images.file, images.length, 2, 0x190, size) : nullptr;
        if (!b || size < 40)
            return 0;
        auto quad = [&](unsigned index) {
            const unsigned colour = Call("GetSysColor", index);
            return (colour & 0xff) << 16 | (colour & 0xff00) | (colour >> 16 & 0xff);
        };
        unsigned char *copy = new unsigned char[size];
        memcpy(copy, b, size);
        const unsigned window = quad(5), highlight = quad(0xd);
        for (unsigned i = 0; i < 16 && 40 + i * 4 + 4 <= size; ++i)
        {
            unsigned entry = 0;
            memcpy(&entry, copy + 40 + i * 4, 4);
            if (entry == 0xff)
                memcpy(copy + 40 + i * 4, &window, 4);
            else if (entry == 0xff00ff)
                memcpy(copy + 40 + i * 4, &highlight, 4);
        }
        const unsigned bits = 40 + (4u << copy[14]);
        unsigned bitmap = 0;
        const unsigned at = bits <= size ? app.Allocate(size) : 0;
        if (at && app.Store(at, copy, size))
        {
            const unsigned dc = Call("GetDC", 0);
            bitmap = Call("CreateDIBitmap", dc, at, 4, at + bits, at, 0);
            Call("ReleaseDC", 0, dc);
        }
        if (at)
            app.Free(at);
        delete[] copy;
        return bitmap;
    }
    // WM_DRAWITEM (DRAWITEMSTRUCT: CtlID 4, itemID 8, itemState 16, hwndItem
    // 20, hDC 24, rcItem 28, itemData 44).
    struct Draw
    {
        unsigned id = 0, item = 0, state = 0, control = 0, dc = 0, data = 0;
        Box r;
    };
    Draw ReadDraw(unsigned lp)
    {
        unsigned v[12] = {};
        app.Read(lp, v, sizeof v);
        Draw d;
        d.id = v[1];
        d.item = v[2];
        d.state = v[4];
        d.control = v[5];
        d.dc = v[6];
        d.r = Box{int(v[7]), int(v[8]), int(v[9]), int(v[10])};
        d.data = v[11];
        return d;
    }
    unsigned Rect(const Box &b, unsigned slot = 0)
    {
        const int v[4] = {b.left, b.top, b.right, b.bottom};
        app.Store(RectAt() + slot * 16, v, 16);
        return RectAt() + slot * 16;
    }
    void Colours(const Draw &d, unsigned &background, unsigned &text)
    {
        const bool selected = d.state & 1;
        background = Call("SetBkColor", d.dc, Call("GetSysColor", selected ? 0xdu : 5u));
        text = Call("SetTextColor", d.dc, Call("GetSysColor", selected ? 0xeu : 8u));
    }
    // the size and script lists.
    void DrawText(const Draw &d)
    {
        unsigned background, text;
        Colours(d, background, text);
        Send(d.control, CbGetLbText, d.item, TextA());
        const unsigned length = Call("lstrlenA", TextA());
        Call("ExtTextOutA", d.dc, unsigned(d.r.left + int(Call("GetSystemMetrics", 5))), unsigned(d.r.top), 2,
             Rect(d.r), TextA(), length, 0);
        Call("SetTextColor", d.dc, text);
        Call("SetBkColor", d.dc, background);
    }
    // the face list, with the TrueType or printer mark.
    void DrawFace(const Draw &d)
    {
        unsigned background, text;
        Colours(d, background, text);
        Send(d.control, CbGetLbText, d.item, TextA());
        const unsigned length = Call("lstrlenA", TextA());
        Call("ExtTextOutA", d.dc, unsigned(d.r.left + 20), unsigned(d.r.top), 2, Rect(d.r), TextA(), length, 0);
        const unsigned memory = Call("CreateCompatibleDC", d.dc);
        if (memory)
        {
            if (Win16FontFrame::bitmap)
            {
                const unsigned old = Call("SelectObject", memory, Win16FontFrame::bitmap);
                int x = -1;
                if (d.data & 4)
                    x = 0;
                else if ((d.data & 0x4002) == 0x4002)
                    x = 20;
                if (x >= 0)
                {
                    const int height = d.r.bottom - d.r.top - 12;
                    Call("BitBlt", d.dc, unsigned(d.r.left), unsigned(d.r.top + height / 2), 20, 12, memory,
                         unsigned(x), (d.state & 1) ? 12u : 0u, 0xcc0020u);
                }
                Call("SelectObject", memory, old);
            }
            Call("DeleteDC", memory);
        }
        Call("SetTextColor", d.dc, text);
        Call("SetBkColor", d.dc, background);
    }
    // the colour list, a swatch and the name.
    void DrawColour(const Draw &d)
    {
        unsigned background, text;
        Colours(d, background, text);
        Call("ExtTextOutA", d.dc, unsigned(d.r.left), unsigned(d.r.top), 2, Rect(d.r), 0, 0, 0);
        const int border_x = int(Call("GetSystemMetrics", 5)), border_y = int(Call("GetSystemMetrics", 6));
        const int top = d.r.top + border_y, bottom = d.r.bottom - border_y, left = d.r.left + border_x;
        const int right = left + 2 * (bottom - top);
        const unsigned brush = Call("CreateSolidBrush", d.data);
        if (brush)
        {
            const unsigned old = Call("SelectObject", d.dc, brush);
            Call("Rectangle", d.dc, unsigned(left), unsigned(top), unsigned(right), unsigned(bottom));
            Call("DeleteObject", Call("SelectObject", d.dc, old));
            Send(d.control, CbGetLbText, d.item, TextA());
            Call("TextOutA", d.dc, unsigned(2 * border_x + right), unsigned(d.r.top), TextA(),
                 Call("lstrlenA", TextA()));
            Call("SetTextColor", d.dc, text);
            Call("SetBkColor", d.dc, background);
        }
    }
    // the sample.
    void Paint(unsigned dc)
    {
        FontLog l;
        const bool okay = BuildLog(l, false);
        const unsigned font = Call("CreateFontIndirectA", WideLog(l));
        if (!font)
            return;
        const unsigned old_font = Call("SelectObject", dc, font);
        const unsigned flags = Flags();
        const unsigned background = Call("SetBkColor", dc, Call("GetSysColor", flags & CfOldApplication ? 5u : 0xfu));
        unsigned colour = Call("GetSysColor", 8);
        if (flags & CfEffects)
        {
            const int selection = int(Send(Item(FontColor), CbGetCurSel));
            if (selection != -1)
                colour = Send(Item(FontColor), CbGetItemData, unsigned(selection));
        }
        const unsigned text_colour = Call("SetTextColor", dc, colour);
        char text[64] = {};
        if (okay)
        {
            Call("GetDlgItemTextA", f.hwnd, FontSample, TextA(), 50);
            Get(TextA(), text, sizeof text);
        }
        Put(TextA(), text);
        const unsigned metrics = TextB();
        Call("GetTextMetricsA", dc, metrics);
        const int ascent = int(app.Read32(metrics + 4)), internal = int(app.Read32(metrics + 12));
        const unsigned length = unsigned(strlen(text));
        const unsigned extent = WordAt();
        app.Write32(extent, 0);
        Call("GetTextExtentPoint32A", dc, TextA(), length, extent);
        const int width = int(app.Read32(extent));
        const int cap = ascent - internal;
        Box r = f.sample;
        if (flags & CfNewTemplate)
        {
            Call("DrawEdge", dc, Rect(r), 0xa, 0x200f);
            int v[4] = {};
            app.Read(RectAt(), v, sizeof v);
            r = Box{v[0], v[1], v[2], v[3]};
        }
        else
            Send(f.hwnd, 0x138, dc, 0); // WM_CTLCOLORSTATIC
        const int x = (r.right - r.left > width && width > 0) ? r.left + (r.right - r.left - width) / 2 : r.left;
        int y = r.bottom + (r.top - r.bottom + cap) / 2;
        if (y > r.bottom)
            y = r.bottom;
        Call("ExtTextOutA", dc, unsigned(x), unsigned(y - ascent), 6, Rect(r), TextA(), length, 0);
        Call("SetBkColor", dc, background);
        Call("SetTextColor", dc, text_colour);
        if (old_font)
            Call("DeleteObject", Call("SelectObject", dc, old_font));
    }
    void Invalidate() { Call("InvalidateRect", f.hwnd, Rect(f.sample, 1), 0); }
    // The end of the dialog.
    void End(unsigned result)
    {
        FreeStyles(Item(FontStyle));
        app.EndDialog(f.hwnd, result);
        f.ended = true;
    }
    // WM_INITDIALOG.
    unsigned Initialize(unsigned wp, bool recorded)
    {
        this->recorded = recorded;
        auto error = [&](unsigned code) {
            app.Win16CommonDialogError(code);
            app.EndDialog(f.hwnd, 0);
            f.ended = true;
            return 0u;
        };
        for (unsigned i = 0; i < 4; ++i)
            if (!LoadString(0x504 + i, Win16FontFrame::names[i], sizeof Win16FontFrame::names[i]))
                return error(5); // CDERR_LOADSTRFAILURE
        unsigned flags = Flags();
        if ((flags & CfLimitSize) &&
            int(short(app.Win16Word(f.cf + CfSizeMin))) > int(short(app.Win16Word(f.cf + CfSizeMax))))
            return error(0x2002); // CFERR_MAXLESSTHANMIN
        f.ready = true;
        if (!Win16FontFrame::bitmap)
            Win16FontFrame::bitmap = Marks();
        if (!(flags & CfApply))
            Remove(FontApply);
        if (!(flags & CfEffects))
        {
            Remove(FontColorLabel);
            Remove(FontColor);
        }
        else
        {
            // then the colour of rgbColors or black.
            const unsigned colours = Item(FontColor);
            for (unsigned i = 0; i < 16; ++i)
            {
                char name[16];
                LoadString(0x410 + i, name, sizeof name);
                Put(TextA(), name);
                const int index = int(Send(colours, CbAddString, 0, TextA()));
                if (index >= 0)
                    Send(colours, CbSetItemData, unsigned(index), FontColors[i]);
            }
            const unsigned wanted = app.Read32(f.cf + CfColors);
            unsigned index = 15;
            for (; index > 0; --index)
                if (Send(colours, CbGetItemData, index) == wanted)
                    break;
            Send(colours, CbSetCurSel, index);
        }
        {
            const unsigned at = RectAt();
            Call("GetWindowRect", Item(FontSample), at);
            Call("MapWindowPoints", 0, f.hwnd, at, 2);
            int v[4] = {};
            app.Read(at, v, sizeof v);
            f.sample = Box{v[0], v[1], v[2], v[3]};
        }
        Heights();
        const unsigned log = LogFontAt();
        if (!(flags & CfInitToLogFont))
            WriteLog16(log, DefaultLog());
        FontLog l = ReadLog16(log);
        if (!(flags & CfEffects))
        {
            Remove(FontEffects);
            Remove(FontStrikeout);
            Remove(FontUnderline);
        }
        else
        {
            Call("CheckDlgButton", f.hwnd, FontStrikeout, l.strikeout);
            Call("CheckDlgButton", f.hwnd, FontUnderline, l.underline);
        }
        if (!Families(app.Win16Word(f.cf + CfDc), flags, l.charset))
        {
            app.Win16CommonDialogError(0x2001); // CFERR_NOFONTS
            app.EndDialog(f.hwnd, 0);
            f.ended = true;
            return 0;
        }
        flags = Flags();
        if (!(flags & CfNoFaceSel) && l.face[0])
        {
            SelectExact(Item(FontFace), l.face);
            ShowSelection(Item(FontFace));
        }
        const unsigned screen = Call("GetDC", 0);
        unsigned charset;
        if (flags & CfNoScriptSel)
        {
            if (const unsigned script = Item(FontScript))
            {
                char none[32];
                LoadString(0x508, none, sizeof none);
                AddScript(script, none, 1);
                User::EnableWindow(script, false);
            }
            charset = 1;
        }
        else if (flags & (CfSelectScript | CfInitToLogFont))
            charset = l.charset;
        else
            charset = Call("GetTextCharset", screen);
        f.charset = charset & 0xffff;
        FaceChanged(true);
        if (!(Flags() & CfNoStyleSel))
            SelectStyle(true);
        l = ReadLog16(log);
        if (!(Flags() & CfNoSizeSel) && l.height)
        {
            char text[16];
            Points(screen, l.height, text);
            SelectExact(Item(FontSize), text);
            Put(TextA(), text);
            Call("SetDlgItemTextA", f.hwnd, FontSize, TextA());
        }
        Call("ReleaseDC", 0, screen);
        if (!(Flags() & CfShowHelp))
        {
            const unsigned help = Item(FontHelp);
            User::ShowWindow(help, 0);
            User::EnableWindow(help, false);
        }
        Send(Item(FontFace), CbLimitText, 31);
        Send(Item(FontStyle), CbLimitText, 31);
        Send(Item(FontSize), CbLimitText, 4);
        f.initializing = false;
        if (Flags() & CfEnableHook)
        {
            if (recorded) app.GiveTail(TailRecord);
            return Hook(0x110, wp, f.cf_far);
        }
        Call("SetCursor", Call("LoadCursorA", 0, 0x7f00));
        return 1;
    }
    // WM_COMMAND.
    unsigned Command(unsigned id, unsigned code, unsigned control)
    {
        switch (id)
        {
        case 1: // IDOK
        {
            int size = 0;
            if (!PointSize(3, size))
            {
                Call("PostMessageA", f.hwnd, 0x28, Item(FontSize), 1); // WM_NEXTDLGCTL
                return 1;
            }
            app.Win16Word(f.cf + CfPointSize, unsigned(size * 10));
            FontLog l;
            BuildLog(l, true);
            WriteLog16(LogFontAt(), l);
            const unsigned flags = Flags();
            if (flags & CfForceFontExist)
            {
                unsigned field = flags & CfNoFaceSel ? FontFace : flags & CfNoStyleSel ? FontStyle : 0;
                if (field)
                {
                    char text[160], caption[160];
                    LoadString(field == FontFace ? 0x502 : 0x503, text, sizeof text);
                    Call("GetWindowTextA", f.hwnd, TextB(), 160);
                    Get(TextB(), caption, sizeof caption);
                    Put(TextA(), text);
                    Put(TextB(), caption);
                    Win16ControllerPhase phase(app, HostWin16Font, f.hwnd, Win16ControllerPhase::FontField,
                                               0, field, 0, recorded);
                    phase.Warning(f.hwnd, TextA(), TextB(), 0x40);
                    Call("PostMessageA", f.hwnd, 0x28, Item(field), 1);
                    return 1;
                }
            }
            if (flags & CfEffects)
            {
                const unsigned colours = Item(FontColor);
                app.Write32(f.cf + CfColors, Send(colours, CbGetItemData, Send(colours, CbGetCurSel)));
            }
            char style[32];
            unsigned data = 0;
            TypedEntry(Item(FontStyle), style, sizeof style, data);
            app.Win16Word(f.cf + CfFontType, data >> 16);
            if (flags & CfUseStyle)
                if (const unsigned text = app.Win16FarPointer(f.cf + CfStyle))
                    app.Store(text, style, unsigned(strlen(style)) + 1);
            End(1);
            return 1;
        }
        case 2: // IDCANCEL
            End(0);
            return 1;
        case 3:
            FreeStyles(Item(FontStyle));
            app.EndDialog(f.hwnd, control & 0xffff);
            f.ended = true;
            return 1;
        case FontHelp:
            if (f.message_help && app.Win16Word(f.cf + CfOwner))
            {
                Win16ControllerPhase help(app, HostWin16Font, f.hwnd, Win16ControllerPhase::OwnerHelp,
                                           0, 1, 0, recorded);
                help.Help(app.Win16Word(f.cf + CfOwner), f.message_help, f.hwnd, f.cf_far);
            }
            return 1;
        case FontStrikeout:
        case FontUnderline:
            Invalidate();
            return 1;
        case FontFace:
            switch (code)
            {
            case 1: // CBN_SELCHANGE
                ShowSelection(control);
                break;
            case 6: // CBN_EDITUPDATE
                Call("PostMessageA", f.hwnd, 0x111, NotifyTyped << 16 | id, control);
                return 1;
            case NotifySelectAll:
                ShowSelection(control);
                Send(control, CbSetEditSel, 0, 0xffff0000u);
                return 1;
            case NotifyTyped:
            {
                char text[32];
                Call("GetWindowTextA", control, TextA(), 32);
                Get(TextA(), text, sizeof text);
                const int index = FindExact(control, text);
                if (index < 0)
                    return 1;
                Send(control, CbSetCurSel, unsigned(index));
                Send(control, CbSetEditSel, 0, 0xffffffffu);
                break;
            }
            default:
                return 1;
            }
            {
                // the size field survives the new face.
                char size[16];
                Call("GetDlgItemTextA", f.hwnd, FontSize, TextB(), 10);
                Get(TextB(), size, sizeof size);
                FaceChanged(false);
                SelectStyle(false);
                const int index = FindExact(Item(FontSize), size);
                if (index < 0)
                {
                    Put(TextA(), size);
                    Call("SetDlgItemTextA", f.hwnd, FontSize, TextA());
                }
                else
                    Send(Item(FontSize), CbSetCurSel, unsigned(index));
            }
            Invalidate();
            return 1;
        case FontStyle:
        case FontSize:
            switch (code)
            {
            case 1:
            {
                const int selection = ShowSelection(control);
                if (selection >= 0 && id == FontStyle)
                {
                    if (const FontLog *chosen = StyleOf(Send(control, CbGetItemData, unsigned(selection))))
                    {
                        FontLog l = ReadLog16(LogFontAt());
                        l.weight = chosen->weight;
                        l.italic = chosen->italic;
                        WriteLog16(LogFontAt(), l);
                    }
                }
                Invalidate();
                return 1;
            }
            case 4: // CBN_KILLFOCUS
                Invalidate();
                return 1;
            case 6:
                Call("PostMessageA", f.hwnd, 0x111, NotifyTyped << 16 | id, control);
                return 1;
            case NotifySelectAll:
                ShowSelection(control);
                Send(control, CbSetEditSel, 0, 0xffff0000u);
                return 1;
            case NotifyTyped:
                if (id == FontStyle)
                {
                    char text[32];
                    Call("GetWindowTextA", control, TextA(), 32);
                    Get(TextA(), text, sizeof text);
                    const int index = FindExact(control, text);
                    if (index < 0)
                        return 1;
                    Send(control, CbSetCurSel, unsigned(index));
                    Send(control, CbSetEditSel, 0, 0xffffffffu);
                    Invalidate();
                }
                return 1;
            default:
                return 1;
            }
        case FontColor:
            if (code == 1)
                Invalidate();
            return 1;
        case FontScript:
            if (code != 1)
                return 1;
            if (!(Flags() & CfNewTemplate))
                return 0;
            if (ScriptSample(control))
                Invalidate();
            return 1;
        default:
            return 0;
        }
    }
};

// ChooseFont (COMMDLG.15).
bool Application::Win16ChooseFont(const Win16Frame &frame, unsigned &result)
{
    const bool gate = TakeFresh();
    result = 0;
    const unsigned cf = frame.Pointer(0);
    // the wait cursor until the dialog sets the arrow.
    {
        const unsigned a[] = {0, 0x7f02};
        const unsigned cursor = Api("LoadCursorA", a);
        Api("SetCursor", &cursor);
    }
    Win16CommonDialogError(0);
    if (!cf || Read32(cf) != ChooseFont16)
    {
        Win16CommonDialogError(ErrorStructSize);
        return true;
    }
    unsigned flags = Read32(cf + CfFlags);
    if ((flags & CfEnableHook) && !Read32(cf + CfHook))
    {
        Win16CommonDialogError(0xb); // CDERR_NOHOOK
        return true;
    }
    // The template: the caller's by name or by handle, or COMMDLG's own 607h.
    // A template of a module for Windows 4.0 and COMMDLG's own get the
    // script list.
    const unsigned module = win16.state && win16.state->module_count ? win16.state->modules[0].handle : 0;
    unsigned char *converted = nullptr;
    unsigned length = 0, native = 0;
    auto newer = [&](unsigned instance) {
        auto *owner = Win16ModuleOf(instance);
        return owner && owner->image.ExpectedVersion() > 0x30a;
    };
    if (flags & CfEnableTemplate)
    {
        native = Win16NativeTemplate(Win16Word(cf + CfInstance), Read32(cf + CfTemplate), 0);
        if (!native)
        {
            Win16CommonDialogError(6); // CDERR_FINDRESFAILURE
            return true;
        }
        if (newer(Win16Word(cf + CfInstance)))
            flags |= CfNewTemplate;
    }
    else if (flags & CfEnableTemplateHandle)
    {
        // the handle goes to LockResource unchecked; a block that
        // does not lock ends ChooseFont with FALSE and no error.
        native = Win16NativeTemplate(0, 0, Win16Word(cf + CfInstance));
        if (!native)
            return true;
        // GetExpWinVer of a global handle asks the module that owns it.
        if (newer(win16.state && win16.state->module_count ? win16.state->modules[0].instance : 0))
            flags |= CfNewTemplate;
    }
    else
    {
        flags |= CfNewTemplate;
        auto &images = CommDlgResources();
        unsigned size = 0;
        const unsigned char *bytes = images.file ? NeResource(images.file, images.length, 5, 0x607, size) : nullptr;
        converted = bytes ? new unsigned char[4 * size + 64] : nullptr;
        if (!converted || !ConvertDialogTemplate(bytes, size, converted, 4 * size + 64, length))
        {
            delete[] converted;
            Win16CommonDialogError(6);
            return true;
        }
    }
    Write32(cf + CfFlags, flags);
    // the instance of the dialog is COMMDLG's, except for a
    // hook of a program for Windows 3.0 or older, which gets the dialog of
    // its own instance (the owner's, else the template's) and no WM_HELP;
    // L1WMAP and FULLTEXT keep COMMDLG's.
    unsigned instance = Win16SystemModule("COMMDLG");
    if (flags & CfEnableHook)
    {
        unsigned candidate = 0;
        const unsigned owner = Win16Word(cf + CfOwner);
        if (owner)
        {
            unsigned word = 0;
            User::WindowLong(owner, -6, word, false);
            candidate = word & 0xffff;
        }
        else if ((flags & CfEnableTemplate) && Win16Word(cf + CfInstance))
            candidate = Win16Word(cf + CfInstance);
        if (candidate)
            if (auto *owner_module = Win16ModuleOf(candidate))
                if (owner_module->image.ExpectedVersion() <= 0x30a)
                {
                    flags |= CfOldApplication;
                    Write32(cf + CfFlags, flags);
                    if (strcmp(owner_module->name, "L1WMAP") && strcmp(owner_module->name, "FULLTEXT"))
                        instance = candidate;
                }
    }
    auto *held = new Win16FontFrame(this);
    struct Holder { Win16FontFrame *frame; ~Holder() { delete frame; } } release{held};
    auto &f = *held;
    f.cf = cf;
    f.cf_far = frame.Word(1) << 16 | frame.Word(0);
    f.hook = flags & CfEnableHook ? Win16Mark(Read32(cf + CfHook)) : 0;
    f.scratch = Allocate(1024);
    const unsigned help = String("commdlg_help");
    f.message_help = Api("RegisterWindowMessageA", &help);
    Free(help);
    const unsigned owner = Win16Word(cf + CfOwner);
    unsigned hwnd = 0;
    const bool kept = gate && PushHostFrame(HostWin16Font, 0, 1);
    f.native_template = native;
    if (native)
    {
        const unsigned locked = Api("GlobalLock", &native);
        const unsigned size = Api("GlobalSize", &native);
        auto *copy = locked && size ? new unsigned char[size] : nullptr;
        if (copy && Read(locked, copy, size))
        {
            if (kept) GiveTail(TailRecord);
            hwnd = CreateDialogTemplate(copy, size, owner, Win16FontDialogToken, cf, true,
                                        instance ? instance : module);
        }
        delete[] copy;
        Api("GlobalUnlock", &native);
        Api("GlobalFree", &native);
        f.native_template = 0;
    }
    else
    {
        if (kept) GiveTail(TailRecord);
        hwnd = CreateDialogTemplate(converted, length, owner, Win16FontDialogToken, cf, true, instance);
        delete[] converted;
    }
    f.hwnd = hwnd;
    if (kept)
    {
        auto &record = threads[current_thread].host_frames[threads[current_thread].host_frame_count - 1];
        record.a = hwnd; record.b = 0;
    }
    const unsigned answer = hwnd && state != Failed ? RunModal(hwnd, kept) : 0xffffffffu;
    if (kept) PopHostFrame();
    release.frame = nullptr;
    result = FinishWin16FontDialog(hwnd, answer);
    return true;
}
unsigned Application::FinishWin16FontDialog(unsigned hwnd, unsigned result)
{
    auto *frame = Win16FontFrameOf(this, hwnd);
    if (!frame) return 0;
    if (frame->native_template)
    {
        Api("GlobalUnlock", &frame->native_template);
        Api("GlobalFree", &frame->native_template);
        frame->native_template = 0;
    }
    if (result == 0xffffffffu && state != Failed)
        Win16CommonDialogError(0xffff); // CDERR_DIALOGFAILURE
    if (frame->scratch) Free(frame->scratch);
    delete frame;
    return result == 1;
}
void Application::DiscardSavedWin16FontDialogs()
{
    for (auto *f = Win16FontFrame::head, *next = f; f; f = next)
    {
        next = f->next;
        if (f->app != this) continue;
        FinishWin16FontDialog(f->hwnd, 0);
    }
}

// FormatCharDlgProc.
unsigned Application::Win16FontDialogMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    const unsigned tail = TakeTail();
    auto *frame = Win16FontFrameOf(this, hwnd);
    if (!frame)
        return 0;
    Win16FontDialog e{*this, *frame};
    auto &f = *frame;
    bool resumed_init = false;
    auto &thread = threads[current_thread];
    if (message == 0x110 && thread.host_resume && thread.host_resume_at < thread.host_frame_count)
    {
        const auto &record = thread.host_frames[thread.host_resume_at];
        if (record.kind == HostWin16Font && record.a == hwnd && record.b == (0x80000000u | message))
        {
            resumed_init = true;
            ++thread.host_resume_at;
        }
    }
    Win16ControllerPhase phase(*this, HostWin16Font, hwnd, Win16ControllerPhase::Message,
                               message, wp, lp, message != 0x110 && tail == TailRecord && !f.initializing);
    if (phase.AfterHook()) return phase.Rest();
    // A resumed Init owns its separate final hook record above.
    if (!resumed_init && (phase.resumed || (f.hook && (message != 0x110 || f.ready) &&
        (!f.ready || f.ended || (e.Flags() & CfEnableHook)))))
    {
        const unsigned arguments[] = {hwnd, message, wp, lp};
        if (const unsigned answer = phase.Callback(f.hook, arguments, 4)) return answer;
    }
    phase.Continue();
    e.recorded = phase.kept;
    const bool live = f.ready && !f.ended;
    switch (message)
    {
    case 0xf: // WM_PAINT
    {
        if (!live)
            return 0;
        const unsigned paint = f.scratch + 0x200;
        const unsigned a[] = {hwnd, paint};
        if (const unsigned dc = Api("BeginPaint", a))
        {
            e.Paint(dc);
            Api("EndPaint", a);
        }
        return 1;
    }
    case 0x15: // WM_SYSCOLORCHANGE
        if (Win16FontFrame::bitmap)
            e.Call("DeleteObject", Win16FontFrame::bitmap);
        Win16FontFrame::bitmap = e.Marks();
        return 1;
    case 0x2b: // WM_DRAWITEM
    {
        const auto d = e.ReadDraw(lp);
        if (d.item == ~0u)
            return 1;
        if (d.id == FontColor)
            e.DrawColour(d);
        else if (d.id == FontFace)
            e.DrawFace(d);
        else
            e.DrawText(d);
        return 1;
    }
    case 0x2c: // WM_MEASUREITEM: CtlID 4, itemID 8, itemHeight 16
    {
        const unsigned dc = e.Call("GetDC", hwnd);
        unsigned font = e.Send(hwnd, 0x31); // WM_GETFONT
        if (font)
            font = e.Call("SelectObject", dc, font);
        const unsigned metrics = e.TextB();
        e.Call("GetTextMetricsA", dc, metrics);
        const int height = int(Read32(metrics));
        if (font)
            e.Call("SelectObject", dc, font);
        e.Call("ReleaseDC", hwnd, dc);
        const unsigned item = Read32(lp + 8);
        Write32(lp + 16, item != ~0u ? unsigned(height < 12 ? 12 : height) : unsigned(height + 1));
        return 1;
    }
    case 0x53: // WM_HELP
        if (live && !(e.Flags() & CfOldApplication))
        {
            const unsigned table = f.scratch + 0x300;
            Store(table, FontHelpTable, sizeof FontHelpTable);
            e.Call("WinHelpA", Read32(lp + 12), 0, 0xc, table);
        }
        return 1;
    case 0x7b: // WM_CONTEXTMENU
    {
        const unsigned table = f.scratch + 0x300;
        Store(table, FontHelpTable, sizeof FontHelpTable);
        e.Call("WinHelpA", wp, 0, 0xa, table);
        return 1;
    }
    case 0x110: // WM_INITDIALOG
    {
        const bool resumed = resumed_init;
        const bool pushed = resumed || (tail == TailRecord &&
            PushHostFrame(HostWin16Font, hwnd, 0x80000000u | message, wp, lp));
        struct Record { Application &app; bool pushed; ~Record() { if(pushed) app.PopHostFrame(); } } record{*this, pushed};
        if (!resumed) f.initializing = true;
        const unsigned answer = resumed ? ResumeInner() & 0xffff : e.Initialize(wp, pushed);
        return f.ended ? 0 : answer;
    }
    case 0x111: // WM_COMMAND
        if (!live)
            return 0;
        return e.Command(wp & 0xffff, wp >> 16, lp);
    case 0x401: // WM_CHOOSEFONT_GETLOGFONT, : lParam is a far LOGFONT
    {
        if (!live)
            return 0;
        FontLog l;
        const unsigned okay = e.BuildLog(l, true);
        const unsigned at = (lp >> 16) ? Win16Linear(lp >> 16, lp & 0xffff) : 0;
        if (at)
            e.WriteLog16(at, l);
        return okay;
    }
    case 0x465: // WM_CHOOSEFONT_SETLOGFONT: serves only a 32-bit hook
        return 0;
    case 0x466: // WM_CHOOSEFONT_SETFLAGS,
    {
        if (!live)
            return 0;
        const unsigned old = e.Flags();
        e.SetFlags(lp);
        e.Call("SetWindowLongA", hwnd, 0, old);
        return 1;
    }
    default:
        return 0;
    }
}

// The callbacks, and of the running enumeration.
unsigned Application::Win16FontEnumCallback(unsigned record, unsigned type)
{
    Win16FontEnumState *s = active_font_enum;
    if (!s || !s->frame)
        return 0;
    FontRecord r;
    if (!Read(record, r.bytes, sizeof r.bytes))
        return 0;
    Win16FontDialog e{*this, *s->frame};
    switch (s->kind)
    {
    case Win16FontEnumState::Families:
        return e.Family(*s, r, type & 0xffff);
    case Win16FontEnumState::Scripts:
        return e.Script(*s, r);
    case Win16FontEnumState::Styles:
        return e.Style(*s, r, type & 0xffff);
    }
    return 0;
}

// ENUMLOGFONTEX.elfScript of GDI: string 1000 plus the character set of
// GDI.EXE ("Western", "Symbol", "Central European",...), string 1256
// ("Unknown") for a set without its own.
void Application::GdiScriptName(unsigned charset, char *out, unsigned capacity)
{
    static unsigned char *gdi = nullptr;
    static unsigned gdi_length = 0;
    static bool read = false;
    if (!capacity)
        return;
    out[0] = 0;
    if (!read)
    {
        read = true;
        char full[260];
        if (files.FullPath("C:\\WINDOWS\\SYSTEM\\GDI.EXE", full) || Win16ReadFile(full, gdi, gdi_length))
            gdi = nullptr, gdi_length = 0;
    }
    if (gdi && !NeString(gdi, gdi_length, 1000 + (charset & 0xff), out, capacity))
        NeString(gdi, gdi_length, 1256, out, capacity);
}
