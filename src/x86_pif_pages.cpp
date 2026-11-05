// SPDX-License-Identifier: GPL-2.0-or-later
// DOS program property pages: Program, Font, Memory, Screen, Misc, and Advanced
// Program Settings. Host the user's PIFMGR dialogs, marshal their arguments, and
// connect the shell's property-page callbacks.
#include "x86_app.h"
#include "win98_ne_image.h"
#include "win98_pifmgr.h"
#include "dos_vm_truetype.h"
#include "user.h"
#include <stdio.h>
#include <string.h>

namespace X86
{
namespace
{
constexpr unsigned PageToken = 0x71000041, AdvancedToken = 0x71000042;
constexpr unsigned WmDestroy = 0x0002, WmPaint = 0x000f, WmDrawItem = 0x002b, WmMeasureItem = 0x002c,
                   WmNotify = 0x004e, WmInitDialog = 0x0110, WmCommand = 0x0111;
constexpr unsigned PsmChanged = 0x0468;
constexpr unsigned PsnSetActive = 0xffffff38u, PsnKillActive = 0xffffff37u, PsnApply = 0xffffff36u;
constexpr unsigned CbAddString = 0x143, CbGetCurSel = 0x147, CbSetCurSel = 0x14e, CbSetEditSel = 0x142,
                   CbResetContent = 0x14b, CbGetItemData = 0x150, CbSetItemData = 0x151;
constexpr unsigned LbAddString = 0x180, LbInsertString = 0x181, LbResetContent = 0x184, LbSetCurSel = 0x186,
                   LbGetCurSel = 0x188, LbGetText = 0x189, LbGetItemData = 0x199, LbSetItemData = 0x19a,
                   LbGetCount = 0x18b;
constexpr unsigned EmLimitText = 0xc5, StmSetIcon = 0x170;
constexpr unsigned HkmSetHotKey = 0x401, HkmGetHotKey = 0x402, HkmSetRules = 0x403;
constexpr unsigned TbmGetPos = 0x400, TbmSetPos = 0x405, TbmSetRange = 0x406;

// The pages in the order of the table at : template and procedure.
enum Kind : unsigned { PageProgram, PageFont, PageMemory, PageScreen, PageMisc, OriginalKinds };
constexpr unsigned PageTemplates[OriginalKinds] = {101, 106, 103, 105, 108};
constexpr unsigned AdvancedTemplate = 102;

// A row of the check box tables: the control, the bit of the flag word,
// 0x80 for a box that is checked while the bit is clear.
struct CheckRow
{
    unsigned short id;
    unsigned char bit;
};
//  flPrg;  flPrg and flPrgInit of the Advanced dialog.
constexpr CheckRow ProgramChecks[] = {{4015, 0}};
constexpr CheckRow AdvancedPrgChecks[] = {{3008, 0x80 | 10}};
constexpr CheckRow AdvancedInitChecks[] = {{3007, 2}, {4009, 3}, {6008, 0x80 | 8}};
//  flMemInit.
constexpr CheckRow MemoryChecks[] = {{1028, 0x80 | 0}, {1019, 4}};
//  flVid,  flWin,  flWinInit.
constexpr CheckRow ScreenVidChecks[] = {{1043, 0x80 | 8}, {1044, 8}, {1052, 0}, {1053, 0x80 | 7}};
constexpr CheckRow ScreenWinChecks[] = {{1049, 1}};
constexpr CheckRow ScreenWinInitChecks[] = {{1050, 0x80 | 0}};
//  flTsk,  flKbd,  flMse.
constexpr CheckRow MiscTskChecks[] = {{1031, 0x80 | 5}, {1033, 0x80 | 1}, {1040, 0x80 | 4}};
constexpr CheckRow MiscKbdChecks[] = {{1070, 0x80 | 6}, {1071, 0x80 | 5}, {1072, 0x80 | 11}, {1073, 0x80 | 10},
                                   {1074, 0x80 | 9},  {1075, 0x80 | 7}, {1076, 0x80 | 8},  {1081, 0}};
constexpr CheckRow MiscMseChecks[] = {{1078, 0x80 | 0}, {1079, 1}};

// A row of the numeric combo tables (ten bytes each): the offset of the
// word in the group, 1 for "Auto" as zero, 2 for "Auto" and "None" over a
// minimum/maximum pair, the control, the range and the error string.
struct NumberRow
{
    unsigned char offset, flags;
    unsigned short id;
    int minimum;
    unsigned maximum;
    unsigned short error;
};
//  over PROPMEM,  over PROPENV.
constexpr NumberRow MemoryNumbers[] = {{4, 2, 1016, 0, 640, 3361}, {8, 2, 1022, 0, 65535, 3362}, {0xc, 2, 1026, 0, 65535, 3363}};
constexpr NumberRow EnvironmentNumbers[] = {{0x54, 1, 1014, 0, 32768, 3364}, {0x56, 1, 1088, 0, 65535, 3365}};
// The values of the Initial size list of the Screen page, ds:0x592.
constexpr unsigned ScreenLines[4] = {0, 25, 43, 50};

unsigned Word(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
void PutWord(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
} // namespace

struct Application::PifPagesState
{
    // PIFMGR.DLL of the tree, for its templates, strings and the bitmap.
    unsigned char *module = nullptr;
    unsigned module_size = 0;
    Win98Ne::Image image;
    bool tried = false;
    // One property handle and its pages; the pages share it the way the
    // pages of share the handle in lParam.
    struct Sheet
    {
        bool used = false;
        unsigned props = 0, pages = 0;
        bool realmode = false; // the record bit 0x400 that keeps
        char path[260] = {};
        unsigned notify = 0;   // the MS-DOS window that opened the sheet
    };
    struct Page
    {
        bool used = false;
        unsigned sheet = 0, kind = 0;
        unsigned block = 0, handle16 = 0, hwnd = 0;
        // Program: the icon of the page and the file and index it came from.
        unsigned icon = 0, icon_index = 0;
        char icon_file[80] = {};
        // Font: the FNT and WIN groups the page edits.
        unsigned char fnt[78] = {}, win[38] = {};
        bool memory_changed = false;
    };
    Sheet sheets[8];
    Page pages[64];
    // The Advanced dialog runs modal over one Program page.
    unsigned advanced_page = ~0u, advanced_hwnd = 0, advanced_saved = 0;
    // The WOAFont went into GDI's font table, as PIFMGR's start does it
    // (AddFontResource), so the font preview finds its sizes.
    bool woa_added = false;
    ~PifPagesState() { delete[] module; }
};

void Application::FreePifPages()
{
    if (pif_pages)
        for (auto &p : pif_pages->pages)
            if (p.used && p.block) Free(p.block);
    delete pif_pages;
    pif_pages = nullptr;
}

// The dialog helpers the page procedures share, over the native USER.
struct Application::PifUi
{
    Application &app;
    unsigned dlg;
    unsigned Item(unsigned id) { return User::GetDlgItem(dlg, id); }
    unsigned Send(unsigned id, unsigned message, unsigned wp = 0, unsigned lp = 0)
    {
        return app.Send(Item(id), message, wp, lp);
    }
    unsigned SendText(unsigned id, unsigned message, unsigned wp, const char *text)
    {
        const unsigned s = app.String(text);
        const unsigned r = s ? app.Send(Item(id), message, wp, s) : ~0u;
        if (s) app.Free(s);
        return r;
    }
    void SetText(unsigned id, const char *text)
    {
        const unsigned s = app.String(text);
        if (!s) return;
        const unsigned a[] = {dlg, id, s};
        app.Api("SetDlgItemTextA", a);
        app.Free(s);
    }
    void Text(unsigned id, char *out, unsigned capacity)
    {
        out[0] = 0;
        const unsigned buffer = app.Allocate(capacity + 1);
        if (!buffer) return;
        const unsigned a[] = {dlg, id, buffer, capacity};
        app.Api("GetDlgItemTextA", a);
        snprintf(out, capacity, "%s", app.ReadString(buffer));
        app.Free(buffer);
    }
    void SetCheck(unsigned id, bool on)
    {
        const unsigned a[] = {dlg, id, on ? 1u : 0u};
        app.Api("CheckDlgButton", a);
    }
    bool Checked(unsigned id)
    {
        const unsigned a[] = {dlg, id};
        return app.Api("IsDlgButtonChecked", a) != 0;
    }
    void Enable(unsigned id, bool on)
    {
        const unsigned a[] = {Item(id), on ? 1u : 0u};
        if (a[0]) app.Api("EnableWindow", a);
    }
    void Show(unsigned id, bool on)
    {
        if (const unsigned h = Item(id)) app.ShowWindow(h, on ? 5 : 0);
    }
    void Changed()
    {
        app.Send(User::Parent(dlg), PsmChanged, dlg, 0);
    }
    // the boxes from the bits of a flag word.
    template <unsigned N> void SetChecks(const CheckRow (&table)[N], unsigned flags)
    {
        for (const auto &c : table)
        {
            const bool set = (flags >> (c.bit & 0x3f)) & 1;
            SetCheck(c.id, set == !(c.bit & 0x80));
        }
    }
    // the bits of a flag word from the boxes.
    template <unsigned N> unsigned GetChecks(const CheckRow (&table)[N], unsigned flags)
    {
        for (const auto &c : table)
        {
            const unsigned bit = 1u << (c.bit & 0x3f);
            flags &= ~bit;
            if (Checked(c.id) == !(c.bit & 0x80)) flags |= bit;
        }
        return flags;
    }
};

// PIFMGR.DLL of the tree. Without it there are no pages, the way Win98 has
// none when LoadLibrary16 of the module fails.
bool Application::PifPagesModule()
{
    if (!pif_pages) pif_pages = new PifPagesState;
    auto &s = *pif_pages;
    if (s.module || s.tried) return s.module != nullptr;
    s.tried = true;
    unsigned handle = 0, status = 0, size = 0, done = 0;
    if (files.Open("C:\\WINDOWS\\SYSTEM\\PIFMGR.DLL", 1, 3, 3, 0x80, handle, status)) return false;
    bool ok = !files.Size(handle, size) && size >= 64 && size <= (1u << 20);
    if (ok)
    {
        s.module = new unsigned char[size];
        ok = s.module && !files.Read(handle, s.module, size, done) && done == size;
    }
    files.Close(handle);
    unsigned error = 0;
    if (ok) ok = s.image.Parse(s.module, size, error);
    if (!ok)
    {
        delete[] s.module;
        s.module = nullptr;
        return false;
    }
    s.module_size = size;
    return true;
}

// A string of the module's string tables, RT_STRING block id / 16 + 1.
bool Application::PifPagesString(unsigned id, char *out, unsigned capacity)
{
    out[0] = 0;
    if (!PifPagesModule()) return false;
    auto &s = *pif_pages;
    unsigned offset = 0, bytes = 0;
    if (!s.image.ResourceAt(0x8006, 0x8000 | ((id >> 4) + 1), nullptr, offset, bytes)) return false;
    const unsigned char *p = s.module + offset, *end = p + bytes;
    if (end > s.module + s.module_size) end = s.module + s.module_size;
    for (unsigned i = 0; i < 16 && p < end; ++i)
    {
        const unsigned n = *p++;
        if (i == (id & 15))
        {
            // "Auto" (3330) starts with a byte 01h, the mark the module's
            // own combo code looks for; it is not part of the text shown.
            const unsigned skip = n && p[0] == 1 ? 1 : 0;
            unsigned k = 0;
            for (; skip + k < n && k + 1 < capacity && p + skip + k < end; ++k) out[k] = (char)p[skip + k];
            out[k] = 0;
            return n != 0;
        }
        p += n;
    }
    return false;
}

// A dialog of the module as a 32-bit template. A page gets the style the
// property sheet gives every page (COMCTL32 : WS_CHILD, WS_TABSTOP,
// DS_CONTROL, DS_3DLOOK over the font and local edit bits of the template).
// `caption` receives the template's title, the tab of the page.
unsigned char *Application::PifPagesTemplate(unsigned id, bool page, unsigned &length, char *caption,
                                             unsigned caption_capacity, unsigned &cx, unsigned &cy)
{
    length = 0;
    if (caption && caption_capacity) caption[0] = 0;
    if (!PifPagesModule()) return nullptr;
    auto &s = *pif_pages;
    unsigned offset = 0, bytes = 0;
    if (!s.image.ResourceAt(0x8005, 0x8000 | id, nullptr, offset, bytes) || bytes < 13) return nullptr;
    const unsigned char *t = s.module + offset;
    cx = Word(t + 9);
    cy = Word(t + 11);
    // Menu, class and caption follow the header as strings; FFh and a word
    // is an ordinal.
    unsigned at = 13;
    for (unsigned field = 0; field < 3 && at < bytes; ++field)
    {
        if (t[at] == 0xff) { at += 3; continue; }
        const unsigned start = at;
        while (at < bytes && t[at]) ++at;
        if (field == 2 && caption)
            snprintf(caption, caption_capacity, "%.*s", (int)(at - start), reinterpret_cast<const char *>(t + start));
        ++at;
    }
    unsigned char *out = new unsigned char[4 * bytes + 64];
    if (!out) return nullptr;
    if (!ConvertDialogTemplate(t, bytes, out, 4 * bytes + 64, length))
    {
        delete[] out;
        return nullptr;
    }
    if (page)
    {
        const unsigned style = out[0] | out[1] << 8 | out[2] << 16 | (unsigned)out[3] << 24;
        const unsigned patched = (style & 0x02000068u) | 0x40010404u;
        for (unsigned i = 0; i < 4; ++i) out[i] = (unsigned char)(patched >> (8 * i));
    }
    return out;
}

// The pages of one file, the work of PIFPROPGETPAGES : no pages for
// a Windows program (SHGetFileInfo SHGFI_EXETYPE) or a file the
// property services refuse; otherwise one handle for the file and a page
// record for each page of the table.
unsigned Application::PifPagesCreate(const char *path, unsigned notify, unsigned *handles16, unsigned capacity,
                                     bool segmented)
{
    if (!PifPagesModule()) return 0;
    auto &s = *pif_pages;
    {
        unsigned handle = 0, status = 0, done = 0, position = 0;
        unsigned char header[0x40 + 4] = {};
        if (!files.Open(path, 1, 3, 3, 0x80, handle, status))
        {
            files.Read(handle, header, 0x40, done);
            unsigned size = 0;
            files.Size(handle, size);
            const unsigned lfanew = header[0x3c] | header[0x3d] << 8 | header[0x3e] << 16 | (unsigned)header[0x3f] << 24;
            if (done == 0x40 && header[0] == 'M' && header[1] == 'Z' && (header[0x18] | header[0x19] << 8) >= 0x40 &&
                lfanew + 2 <= size && !files.Seek(handle, lfanew, 0, position))
            {
                unsigned char magic[2] = {};
                unsigned got = 0;
                files.Read(handle, magic, 2, got);
                if (got == 2 && ((magic[0] == 'P' && magic[1] == 'E') || (magic[0] == 'N' && magic[1] == 'E')))
                {
                    files.Close(handle);
                    return 0;
                }
            }
            files.Close(handle);
        }
    }
    unsigned sheet = 0;
    while (sheet < 8 && s.sheets[sheet].used) ++sheet;
    if (sheet == 8) return 0;
    const unsigned props = PifMgrOpenHandle(path);
    if (!props) return 0;
    auto &sh = s.sheets[sheet];
    sh = PifPagesState::Sheet();
    sh.used = true;
    sh.props = props;
    sh.notify = notify;
    snprintf(sh.path, sizeof sh.path, "%s", path);
    unsigned char prg[Win98PifMgr::PrgBytes];
    if (PifMgrGroup(props, Win98PifMgr::GroupPrg, prg, sizeof prg))
        sh.realmode = (Word(prg + Win98PifMgr::PrgFlagsInit) & 8) != 0;
    unsigned count = 0;
    for (unsigned kind = 0; kind < PifPageKinds() && count < capacity; ++kind)
    {
        unsigned index = 0;
        while (index < 64 && s.pages[index].used) ++index;
        if (index == 64) break;
        auto &p = s.pages[index];
        p = PifPagesState::Page();
        // The PROPSHEETPAGE of the 16-bit module (0x26 bytes): COMCTL32's
        // CreateProxyPage reads dwFlags at +4 through MapSLFix and keeps the
        // handle; everything else of the page is answered here.
        p.block = Allocate(0x40);
        if (!p.block) break;
        unsigned char psp[0x40] = {};
        psp[0] = 0x26;
        psp[0x0a] = (unsigned char)PifPageTemplateId(kind);
        psp[0x0b] = (unsigned char)(PifPageTemplateId(kind) >> 8);
        psp[0x1a] = (unsigned char)props;
        psp[0x1b] = (unsigned char)(props >> 8);
        Store(p.block, psp, sizeof psp);
        p.handle16 = segmented ? SegmentMap(p.block) : p.block;
        if (!p.handle16) { Free(p.block); p.block = 0; break; }
        p.used = true;
        p.sheet = sheet;
        p.kind = kind;
        ++sh.pages;
        handles16[count++] = p.handle16;
    }
    if (!count)
    {
        PifMgrCloseHandle(props);
        sh.used = false;
    }
    if (log)
    {
        char text[400];
        snprintf(text, sizeof text, "PIFPAGES file=%s pages=%u props=%04x", path, count, props);
        log(text);
    }
    return count;
}

unsigned Application::PifPageKinds() const { return OriginalKinds + PifOptionPageCount(); }
unsigned Application::PifPageKind(unsigned index) const
{
    return pif_pages && index < 64 && pif_pages->pages[index].used ? pif_pages->pages[index].kind : ~0u;
}
unsigned Application::PifPageProps(unsigned index) const
{
    if (!pif_pages || index >= 64 || !pif_pages->pages[index].used) return 0;
    return pif_pages->sheets[pif_pages->pages[index].sheet].props;
}
unsigned Application::PifPageTemplateId(unsigned kind) const
{
    return kind < OriginalKinds ? PageTemplates[kind] : 0x7000 + kind;
}

// The page record behind a page handle, a dialog, or the lParam of
// WM_INITDIALOG: the block itself (the proxy path) or a PROPSHEETPAGE whose
// lParam is the block (the native sheet of the MS-DOS window).
unsigned Application::PifPageFind(unsigned handle16, unsigned hwnd, unsigned parameter)
{
    if (!pif_pages) return ~0u;
    auto &s = *pif_pages;
    for (unsigned i = 0; i < 64; ++i)
    {
        const auto &p = s.pages[i];
        if (!p.used) continue;
        if ((handle16 && p.handle16 == handle16) || (hwnd && p.hwnd == hwnd)) return i;
    }
    if (parameter)
        for (unsigned i = 0; i < 64; ++i)
        {
            const auto &p = s.pages[i];
            if (p.used && !p.hwnd && (p.block == parameter || (parameter > 0x10000 && Read32(parameter + 0x1c) == p.block)))
                return i;
        }
    return ~0u;
}

void Application::PifPageRelease(unsigned index)
{
    auto &s = *pif_pages;
    auto &p = s.pages[index];
    if (!p.used) return;
    if (p.icon)
    {
        unsigned a[] = {p.icon};
        Api("DestroyIcon", a);
    }
    if (p.handle16 != p.block) selectors.Unmap(p.handle16);
    if (p.block) Free(p.block);
    auto &sh = s.sheets[p.sheet];
    p = PifPagesState::Page();
    // the release callback of every page: the last page closes the
    // property handle.
    if (sh.used && sh.pages && !--sh.pages)
    {
        PifMgrCloseHandle(sh.props);
        sh.used = false;
    }
}

// The dialog of a page below the sheet, COMMCTRL.
unsigned Application::PifPageDialog(unsigned index, unsigned parent)
{
    auto &p = pif_pages->pages[index];
    unsigned length = 0, cx = 0, cy = 0;
    unsigned char *bytes = p.kind < OriginalKinds ? PifPagesTemplate(PageTemplates[p.kind], true, length, nullptr, 0, cx, cy)
                                                  : PifOptionTemplate(p.kind - OriginalKinds, length, nullptr, 0, cx, cy);
    if (!bytes) return 0;
    const unsigned hwnd = CreateDialogTemplate(bytes, length, parent, PageToken, p.block, false);
    delete[] bytes;
    return hwnd;
}

// The four thunks. `words` are the 16-bit words the 32-bit stub pushed, in
// push order; `frame` is the stub's EBP.
unsigned Application::PifPagesThunk(unsigned which, const unsigned *words, unsigned frame)
{
    const auto flat = [&](unsigned selector, unsigned offset) {
        return selector ? selectors.Linear((selector << 16) | (offset & 0xffff)) : (offset & 0xffff);
    };
    switch (which)
    {
    case 0:
    {
        // SHELL.DLL: the procedure, the DROPFILES of the file and
        // the array of the collector (a count, then up to ten handles).
        const unsigned drop = flat(words[2], words[3]), array = flat(words[4], words[5]);
        if (!drop || !array) return 0;
        char path[260] = {};
        const unsigned files_at = drop + Read32(drop), wide = Read32(drop + 16);
        for (unsigned i = 0; i + 1 < sizeof path; ++i)
        {
            unsigned short c = 0;
            if (wide) Read(files_at + 2 * i, &c, 2);
            else { unsigned char b = 0; Read(files_at + i, &b, 1); c = b; }
            if (!c) break;
            path[i] = c < 0x100 ? (char)c : '?';
        }
        unsigned handles[10] = {};
        const unsigned count = path[0] ? PifPagesCreate(path, 0, handles, 10, true) : 0;
        for (unsigned i = 0; i < count; ++i) Write32(array + 4 + 4 * i, handles[i]);
        Write32(array, count);
        return 1;
    }
    case 1:
    {
        // COMMCTRL  (hpage, pszCaption, cbCaption, ppt, phIcon).
        const unsigned index = PifPageFind((words[0] << 16) | words[1], 0, 0);
        if (index == ~0u) return 0;
        const auto &p = pif_pages->pages[index];
        char caption[128] = {};
        unsigned length = 0, cx = 0, cy = 0;
        unsigned char *bytes = p.kind < OriginalKinds
                                   ? PifPagesTemplate(PageTemplates[p.kind], true, length, caption, sizeof caption, cx, cy)
                                   : PifOptionTemplate(p.kind - OriginalKinds, length, caption, sizeof caption, cx, cy);
        delete[] bytes;
        const unsigned out = flat(words[2], words[3]), capacity = words[4];
        if (out && capacity)
        {
            const unsigned n = (unsigned)strlen(caption) < capacity - 1 ? (unsigned)strlen(caption) : capacity - 1;
            Store(out, caption, n);
            const unsigned char zero = 0;
            Store(out + n, &zero, 1);
        }
        // The stub hands the POINT16 as a flat stack pointer and sets bit 2
        // of its pointer mask for it, which FT_Thunk would
        // have turned into a 16:16 pointer to the copied stack.
        const unsigned mask = Read32(frame - 0x14), point = (words[5] << 16) | words[6];
        const unsigned target = (mask & 4) ? point : flat(words[5], words[6]);
        if (target)
        {
            const unsigned short size[2] = {(unsigned short)cx, (unsigned short)cy};
            Store(target, size, 4);
        }
        if (const unsigned icon = flat(words[7], words[8]))
        {
            const unsigned short none = 0;
            Store(icon, &none, 2);
        }
        return 1;
    }
    case 2:
    {
        // COMMCTRL  (hpage, hwndParent): the dialog, its HWND.
        const unsigned index = PifPageFind((words[0] << 16) | words[1], 0, 0);
        if (index == ~0u) return 0;
        return PifPageDialog(index, words[2]);
    }
    case 3:
    {
        // DestroyPropertySheetPage of COMMCTRL (hpage).
        const unsigned index = PifPageFind((words[0] << 16) | words[1], 0, 0);
        if (index == ~0u) return 0;
        PifPageRelease(index);
        return 1;
    }
    default: return 0;
    }
}

// The sheet of the MS-DOS window: its Properties button (start page 0) and
// its Font button (start page 1), EDITPROPERTIES over COMMCTRL's
// PropertySheet. The pages are the same records, as 32-bit pages of the
// native sheet; the title is the program's.
unsigned Application::PifPropertySheet(const char *path, unsigned owner, unsigned start_page, const char *title)
{
    const bool recorded = TakeFresh();
    // Guest scratch owns the module, header, page handles and templates.
    // No pointer to a host array survives presentation.
    const unsigned resources = Allocate(140);
    if (!resources) return 0;
    const unsigned zero[35] = {};
    Store(resources, zero, sizeof zero);
    const bool kept = recorded && PushHostFrame(HostPifSheet, resources);
    const unsigned frame_at = kept ? threads[current_thread].host_frame_count - 1 : 0;
    struct Release
    {
        Application &app; unsigned resources, frame_at; bool kept;
        ~Release()
        {
            if (kept) app.threads[app.current_thread].host_frames[frame_at].d = 2;
            app.FinishPifPropertySheet(resources);
            if (kept) app.PopHostFrame();
        }
    } release{*this, resources, frame_at, kept};
    // The sheet and its controls are COMCTL32's; the MS-DOS process has no
    // module of its own that would have loaded it, so it is loaded here, the
    // way SERIALUI's sheet does it (src/x86_serialui.inc).
    const unsigned library = String("COMCTL32.DLL");
    if (!library) return 0;
    const unsigned comctl = ModuleApi("LoadLibraryA", &library);
    Free(library);
    if (!comctl) return 0;
    Write32(resources + 4, comctl);
    unsigned handles[16] = {};
    const unsigned count = PifPagesCreate(path, owner, handles, 16, false);
    Write32(resources, count);
    Store(resources + 12, handles, sizeof handles);
    if (!count) return 0;
    const unsigned memory = Allocate(64 + 40 * count + 512);
    Write32(resources + 8, memory);
    if (!memory) return 0;
    unsigned caption = memory + 64 + 40 * count;
    Store(caption, title, (unsigned)strlen(title) + 1);
    unsigned char *templates[16] = {};
    // The native sheet keeps only pointers to the templates, so they live in
    // guest memory for the life of the sheet.
    unsigned template_memory[16] = {};
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned index = PifPageFind(handles[i], 0, 0);
        const auto &p = pif_pages->pages[index];
        unsigned length = 0, cx = 0, cy = 0;
        templates[i] = p.kind < OriginalKinds ? PifPagesTemplate(PageTemplates[p.kind], false, length, nullptr, 0, cx, cy)
                                              : PifOptionTemplate(p.kind - OriginalKinds, length, nullptr, 0, cx, cy);
        template_memory[i] = templates[i] ? Allocate(length + 16) : 0;
        Write32(resources + 76 + 4 * i, template_memory[i]);
        if (template_memory[i]) Store(template_memory[i], templates[i], length);
        delete[] templates[i];
        // PROPSHEETPAGEA: PSP_DLGINDIRECT, the native page procedure, the
        // block as lParam.
        const unsigned page[10] = {40, 1, image.Base(), template_memory[i], 0, 0, PageToken, p.block, 0, 0};
        Store(memory + 64 + 40 * i, page, sizeof page);
    }
    // PSH_PROPSHEETPAGE | PSH_PROPTITLE, as builds it.
    // A host request runs in the session process. Its DOS notification
    // target can belong to another process; the new sheet has no cross-process
    // modal owner (PropertySheetA refuses one), while the pages still notify it.
    const unsigned sheet_owner = owner && User::WindowProcess(owner) != process_id ? 0 : owner;
    const unsigned header[10] = {40, 9, sheet_owner, image.Base(), 0, caption, count, start_page < count ? start_page : 0,
                                 memory + 64, 0};
    Store(memory, header, sizeof header);
    if (kept) { threads[current_thread].host_frames[frame_at].d = 1; GiveTail(TailFresh); }
    return PropertySheetApi("PropertySheetA", &memory);
}
bool Application::DosSettingsStateOpen()
{
    for (const auto *process : session_processes)
        if (process && process->dos_vm && process->pif_pages)
            for (const auto &sheet : process->pif_pages->sheets)
                if (sheet.used) return true;
    return false;
}
void Application::FinishPifPropertySheet(unsigned resources)
{
    if (!resources) return;
    const unsigned count = Read32(resources), module = Read32(resources + 4), memory = Read32(resources + 8);
    // The native sheet frees its pages; a page it never created is ours.
    if (count <= 16)
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned index = PifPageFind(Read32(resources + 12 + 4 * i), 0, 0);
            if (index != ~0u) PifPageRelease(index);
            if (const unsigned templ = Read32(resources + 76 + 4 * i)) Free(templ);
        }
    if (memory) Free(memory);
    Free(resources);
    if (module) ModuleApi("FreeLibrary", &module);
}


// The real-mode layout uses a child enumerator: on
// a page of a program set to MS-DOS mode, the controls below 4000 hide
// (below 3000) or go grey (3000 to 3999) and the note 5001 shows; normally
// the note hides.
void Application::PifPageRealMode(unsigned hwnd, bool realmode)
{
    for (unsigned child = User::GetWindow(hwnd, 5); child; child = User::GetWindow(child, 2))
    {
        const unsigned id = Api("GetDlgCtrlID", &child) & 0xffff;
        bool on = true;
        if (!realmode && id >= 5000) on = false;
        if (realmode && id < 4000) on = false;
        if (id >= 3000 && (id < 4000 || id >= 6000))
        {
            const unsigned a[] = {child, on ? 1u : 0u};
            Api("EnableWindow", a);
        }
        else ShowWindow(child, on ? 5 : 0);
    }
}

// A message box of the pages, : the module string, formatted with
// up to two numbers, the program as the caption, an exclamation mark.
void Application::PifPageMessageBox(unsigned hwnd, unsigned sheet, unsigned string, unsigned a, unsigned b)
{
    char format[400], text[480];
    if (!PifPagesString(string, format, sizeof format)) return;
    snprintf(text, sizeof text, format, a, b);
    const char *caption = sheet < 8 && pif_pages->sheets[sheet].used ? pif_pages->sheets[sheet].path : "";
    const unsigned t = String(text), c = String(caption);
    const unsigned args[] = {hwnd, t, c, 0x30};
    if (t && c) MessageBoxApi("MessageBoxA", args);
    if (t) Free(t);
    if (c) Free(c);
}

namespace
{
// the values of a numeric combo, a sixteenth of the maximum apart.
void FillNumbers(Application::PifUi &ui, unsigned id, unsigned maximum)
{
    int top = maximum == 0x8000 ? 0x1000 : (int)(short)maximum < 0 ? 0x4000 : (int)maximum;
    const int step = (top + 15) >> 4;
    char text[16];
    for (int v = step; v <= top && step; v += step)
    {
        snprintf(text, sizeof text, "%d", v);
        ui.SendText(id, CbAddString, 0, text);
    }
}
unsigned GetNumber(Application::PifUi &ui, unsigned id, bool &translated)
{
    char text[32];
    ui.Text(id, text, sizeof text);
    translated = false;
    unsigned v = 0;
    const char *p = text;
    while (*p == ' ') ++p;
    if (*p < '0' || *p > '9') return 0;
    for (; *p >= '0' && *p <= '9'; ++p) v = v * 10 + (unsigned)(*p - '0');
    while (*p == ' ') ++p;
    translated = !*p;
    return v;
}
bool SameText(const char *a, const char *b)
{
    for (; *a || *b; ++a, ++b)
    {
        unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
    }
    return true;
}
} // namespace

// the numeric combos from a group.
void Application::PifPageSetNumbers(unsigned hwnd, const void *rows, unsigned count, const unsigned char *group)
{
    PifUi ui{*this, hwnd};
    char automatic[32], none[32];
    PifPagesString(3330, automatic, sizeof automatic);
    PifPagesString(3328, none, sizeof none);
    const auto *table = static_cast<const NumberRow *>(rows);
    for (unsigned i = 0; i < count; ++i)
    {
        const auto &n = table[i];
        ui.Send(n.id, CbResetContent);
        const unsigned value = Word(group + n.offset);
        if (n.flags & 1)
        {
            ui.SendText(n.id, CbAddString, 0, automatic);
            FillNumbers(ui, n.id, n.maximum);
            if (!value) { ui.SetText(n.id, automatic); continue; }
            char text[16];
            snprintf(text, sizeof text, "%u", value);
            ui.SetText(n.id, text);
            continue;
        }
        ui.SendText(n.id, CbAddString, 0, automatic);
        ui.SendText(n.id, CbAddString, 0, none);
        FillNumbers(ui, n.id, n.maximum);
        unsigned shown = Word(group + n.offset + 2);
        if (!value && !shown) { ui.SetText(n.id, none); continue; }
        if (value == 0xffff || shown == 0xffff) shown = n.maximum;
        if (shown == n.maximum) { ui.SetText(n.id, automatic); continue; }
        char text[16];
        snprintf(text, sizeof text, "%u", shown);
        ui.SetText(n.id, text);
    }
}

// the group from the numeric combos.
void Application::PifPageGetNumbers(unsigned hwnd, const void *rows, unsigned count, unsigned char *group)
{
    PifUi ui{*this, hwnd};
    char automatic[32], none[32], text[32];
    PifPagesString(3330, automatic, sizeof automatic);
    PifPagesString(3328, none, sizeof none);
    const auto *table = static_cast<const NumberRow *>(rows);
    for (unsigned i = 0; i < count; ++i)
    {
        const auto &n = table[i];
        bool translated = false;
        unsigned v = GetNumber(ui, n.id, translated);
        if (!translated) v = Word(group + n.offset);
        ui.Text(n.id, text, sizeof text);
        if ((n.flags & 1) && SameText(text, automatic)) v = 0;
        if (n.flags & 2)
        {
            unsigned top = v;
            if (SameText(text, automatic)) { v = 0; top = n.maximum; }
            else if (SameText(text, none)) { v = 0; top = 0; }
            PutWord(group + n.offset + 2, top);
        }
        PutWord(group + n.offset, v);
    }
}

// a number outside its range, or a word that is neither "Auto"
// nor "None", keeps the page open with the error string.
bool Application::PifPageCheckNumbers(unsigned hwnd, unsigned sheet, const void *rows, unsigned count)
{
    PifUi ui{*this, hwnd};
    char automatic[32], none[32], text[32];
    PifPagesString(3330, automatic, sizeof automatic);
    PifPagesString(3328, none, sizeof none);
    const auto *table = static_cast<const NumberRow *>(rows);
    for (unsigned i = 0; i < count; ++i)
    {
        const auto &n = table[i];
        bool translated = false;
        const unsigned v = GetNumber(ui, n.id, translated);
        if (!translated)
        {
            ui.Text(n.id, text, sizeof text);
            if (SameText(text, automatic) || SameText(text, none)) continue;
        }
        else if ((int)v >= n.minimum && v <= n.maximum) continue;
        PifPageMessageBox(hwnd, sheet, n.error, (unsigned)n.minimum, n.maximum);
        ui.Send(n.id, CbSetEditSel, 0, 0xffff0000u);
        SetFocus(ui.Item(n.id));
        return false;
    }
    return true;
}

namespace
{
constexpr unsigned GroupPrg = 1, GroupTsk = 2, GroupVid = 3, GroupMem = 4, GroupKbd = 5, GroupMse = 6, GroupFnt = 8,
                   GroupWin = 9, GroupEnv = 10;
constexpr unsigned ErrorQuery = 3334, ErrorUpdate = 3335;
}

// The raster cells of the WOAFont of SYSTEM.INI (DosVmOpenWoaFont), its FNT
// resources 1 to 31 in order: cell width at +0x56, height at +0x58. `path`
// receives the guest path of the file.
unsigned Application::PifWoaCells(unsigned (*cells)[2], unsigned capacity, char *path, unsigned path_capacity)
{
    unsigned handle = 0, size = 0, done = 0, count = 0;
    if (!DosVmOpenWoaFont(handle, path, path_capacity)) return 0;
    unsigned char *font = nullptr;
    if (!files.Size(handle, size) && size > 64 && size < (1u << 20))
    {
        font = new unsigned char[size];
        if (font && (files.Read(handle, font, size, done) || done != size)) { delete[] font; font = nullptr; }
    }
    files.Close(handle);
    Win98Ne::Image image;
    unsigned error = 0;
    if (font && image.Parse(font, size, error))
        for (unsigned id = 1; id < 32 && count < capacity; ++id)
        {
            unsigned offset = 0, bytes = 0;
            if (!image.ResourceAt(0x8008, 0x8000 | id, nullptr, offset, bytes) || bytes < 0x5a || offset + 0x5a > size) continue;
            cells[count][0] = Word(font + offset + 0x56);
            cells[count][1] = Word(font + offset + 0x58);
            ++count;
        }
    delete[] font;
    return count;
}

// The size list of the Font page, : "Auto", then the raster sizes of
// the Terminal face of the WOAFont of SYSTEM.INI (DosVmOpenWoaFont) when bitmap
// fonts are allowed, then the TrueType sizes when those are. The item data
// is the cell, width in the low byte and height in the next, 0x8000 for a
// TrueType size and 0xffff for Auto; the text is "%2d x %2d" (
// writes it with tab stops for the owner drawn list).
void Application::PifFontList(unsigned hwnd, const unsigned char *fnt)
{
    PifUi ui{*this, hwnd};
    ui.Send(1055, LbResetContent);
    const unsigned flags = Word(fnt);
    auto add = [&](unsigned w, unsigned h, bool tt) {
        char text[32];
        snprintf(text, sizeof text, "%2u x %2u", w, h);
        const unsigned at = ui.SendText(1055, LbAddString, 0, text);
        if (at < 0x8000) ui.Send(1055, LbSetItemData, at, w | h << 8 | (tt ? 0x8000u : 0));
    };
    if (flags & 4)
    {
        // The raster sizes of the font file of the tree.
        unsigned cells[32][2];
        const unsigned count = PifWoaCells(cells, 32);
        for (unsigned i = 0; i < count; ++i) add(cells[i][0], cells[i][1], false);
    }
    if (flags & 8)
        // The TrueType list (ds:0x550 after filled it).
        for (unsigned h : DosVmTrueType::Heights) add(DosVmTrueType::WidthFor(h), h, true);
    char automatic[32];
    PifPagesString(3330, automatic, sizeof automatic);
    const unsigned at = ui.SendText(1055, LbInsertString, 0, automatic);
    ui.Send(1055, LbSetItemData, at, 0xffff);
    // select the size of the group, or Auto.
    unsigned selected = 0;
    if (!(flags & 0x10))
    {
        const unsigned want = Word(fnt + 8) | Word(fnt + 0xa) << 8 | ((flags & 0x800) ? 0x8000u : 0);
        const unsigned count = ui.Send(1055, LbGetCount);
        for (unsigned i = 0; i < count && i < 256; ++i)
            if (ui.Send(1055, LbGetItemData, i) == want) { selected = i; break; }
    }
    ui.Send(1055, LbSetCurSel, selected);
}

// Tell the MS-DOS window which group changed, the PostMessage of
// to the window that opened the sheet (hwnd and message in the record).
void Application::PostNotify(unsigned window, unsigned group)
{
    if (!window || !User::IsWindow(window)) return;
    const unsigned a[] = {window, 0x0400 + 0x0292, group, 0};
    Api("PostMessageA", a);
}

unsigned Application::PifPageMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    const bool recorded = TakeTail() == TailRecord;
    if (!pif_pages) return 0;
    auto &s = *pif_pages;
    const unsigned index = PifPageFind(0, hwnd, message == WmInitDialog ? lp : 0);
    if (index == ~0u) return 0;
    auto &p = s.pages[index];
    auto &sh = s.sheets[p.sheet];
    if (message == WmInitDialog) p.hwnd = hwnd;
    if (p.kind >= OriginalKinds) return PifOptionMessage(index, hwnd, message, wp, lp);
    PifUi ui{*this, hwnd};
    const unsigned props = sh.props;
    auto set_result = [&](unsigned value) {
        unsigned v = value;
        User::WindowLong(hwnd, 0, v, true);
    };
    if (message == WmDestroy)
    {
        p.hwnd = 0;
        return 0;
    }
    if (message == WmNotify && lp)
    {
        const unsigned code = Read32(lp + 8);
        if (code == PsnSetActive)
        {
            PifPageRealMode(hwnd, sh.realmode);
            // the Memory page hides its EMS note again.
            if (p.kind == PageMemory && !sh.realmode)
            {
                ui.Show(1084, false);
                ui.Show(1085, false);
            }
            set_result(0);
            return 1;
        }
        if (code == PsnKillActive)
        {
            // the Memory page checks its numbers before it lets go.
            bool bad = false;
            if (p.kind == PageMemory)
                bad = !PifPageCheckNumbers(hwnd, p.sheet, MemoryNumbers, 3) ||
                      !PifPageCheckNumbers(hwnd, p.sheet, EnvironmentNumbers, 2);
            set_result(bad ? 1 : 0);
            return 1;
        }
        if (code != PsnApply) return 0;
        bool ok = true;
        switch (p.kind)
        {
        case PageProgram:
        {

            unsigned char prg[Win98PifMgr::PrgBytes], env[0x58];
            ok = PifMgrGroup(props, GroupPrg, prg, sizeof prg) && PifMgrGroup(props, GroupEnv, env, sizeof env);
            if (!ok) break;
            snprintf(reinterpret_cast<char *>(prg + Win98PifMgr::PrgIconFile), 80, "%s", p.icon_file);
            PutWord(prg + Win98PifMgr::PrgIconIndex, p.icon_index);
            ui.Text(4001, reinterpret_cast<char *>(prg + Win98PifMgr::PrgTitle), 0x1e);
            ui.Text(4002, reinterpret_cast<char *>(prg + Win98PifMgr::PrgCmdLine), 0x80);
            ui.Text(4005, reinterpret_cast<char *>(prg + Win98PifMgr::PrgWorkDir), 0x40);
            PutWord(prg + Win98PifMgr::PrgHotKey, ui.Send(3003, HkmGetHotKey) & 0xffff);
            ui.Text(4007, reinterpret_cast<char *>(env + 4), 0x50);
            unsigned init = Word(prg + Win98PifMgr::PrgFlagsInit) & ~3u;
            const unsigned run = ui.Send(3005, CbGetCurSel);
            if (run == 1) init |= 1;
            if (run == 2) init |= 2;
            PutWord(prg + Win98PifMgr::PrgFlagsInit, init);
            PutWord(prg, ui.GetChecks(ProgramChecks, Word(prg)));
            ok = PifMgrSetGroup(props, GroupPrg, prg, sizeof prg) && PifMgrSetGroup(props, GroupEnv, env, sizeof env);
            PostNotify(sh.notify, GroupPrg);
            break;
        }
        case PageFont:
            ok = PifMgrSetGroup(props, GroupFnt, p.fnt, sizeof p.fnt) != 0;
            PostNotify(sh.notify, GroupFnt);
            break;
        case PageMemory:
        {

            unsigned char mem[16], env[0x58];
            ok = PifMgrGroup(props, GroupMem, mem, sizeof mem) && PifMgrGroup(props, GroupEnv, env, sizeof env);
            if (!ok) break;
            PutWord(mem + 2, ui.GetChecks(MemoryChecks, Word(mem + 2)));
            PifPageGetNumbers(hwnd, MemoryNumbers, 3, mem);
            PifPageGetNumbers(hwnd, EnvironmentNumbers, 2, env);
            ok = PifMgrSetGroup(props, GroupMem, mem, sizeof mem) && PifMgrSetGroup(props, GroupEnv, env, sizeof env);
            if (ok && sh.notify && p.memory_changed)
            {
                p.memory_changed = false;
                PifPageMessageBox(hwnd, p.sheet, 3366, 0, 0);
            }
            break;
        }
        case PageScreen:
        {

            unsigned char vid[12], win[38];
            ok = PifMgrGroup(props, GroupVid, vid, sizeof vid) && PifMgrGroup(props, GroupWin, win, sizeof win);
            if (!ok) break;
            PutWord(vid, ui.GetChecks(ScreenVidChecks, Word(vid)));
            PutWord(win, ui.GetChecks(ScreenWinChecks, Word(win)));
            PutWord(win + 2, ui.GetChecks(ScreenWinInitChecks, Word(win + 2)));
            const unsigned line = ui.Send(1047, CbGetCurSel);
            if (line < 4) PutWord(vid + 0xa, ScreenLines[line]);
            ok = PifMgrSetGroup(props, GroupVid, vid, sizeof vid) && PifMgrSetGroup(props, GroupWin, win, sizeof win);
            PostNotify(sh.notify, GroupWin);
            break;
        }
        case PageMisc:
        {

            unsigned char tsk[14], kbd[18], mse[4];
            ok = PifMgrGroup(props, GroupTsk, tsk, sizeof tsk) && PifMgrGroup(props, GroupKbd, kbd, sizeof kbd) &&
                 PifMgrGroup(props, GroupMse, mse, sizeof mse);
            if (!ok) break;
            PutWord(tsk, ui.GetChecks(MiscTskChecks, Word(tsk)));
            PutWord(tsk + 0xc, (ui.Send(1037, TbmGetPos) & 0xffff) * 5);
            PutWord(kbd, ui.GetChecks(MiscKbdChecks, Word(kbd)));
            PutWord(mse, ui.GetChecks(MiscMseChecks, Word(mse)));
            ok = PifMgrSetGroup(props, GroupTsk, tsk, sizeof tsk) && PifMgrSetGroup(props, GroupKbd, kbd, sizeof kbd) &&
                 PifMgrSetGroup(props, GroupMse, mse, sizeof mse);
            PostNotify(sh.notify, GroupTsk);
            break;
        }
        default: break;
        }
        if (ok) ok = PifMgrFlushHandle(props);
        if (!ok) PifPageMessageBox(hwnd, p.sheet, ErrorUpdate, 0, 0);
        if (log)
        {
            char text[200];
            snprintf(text, sizeof text, "PIFPAGES apply page=%u ok=%u file=%s", p.kind, ok ? 1u : 0u, sh.path);
            log(text);
        }
        set_result(ok ? 0 : 1); // PSNRET_INVALID: a failed write must keep the sheet open.
        return 1;
    }
    if (message == WmInitDialog)
    {
        switch (p.kind)
        {
        case PageProgram:
        {

            unsigned char prg[Win98PifMgr::PrgBytes], env[0x58];
            if (!PifMgrGroup(props, GroupPrg, prg, sizeof prg) || !PifMgrGroup(props, GroupEnv, env, sizeof env))
            {
                PifPageMessageBox(hwnd, p.sheet, ErrorQuery, 0, 0);
                break;
            }
            snprintf(p.icon_file, sizeof p.icon_file, "%s", reinterpret_cast<const char *>(prg + Win98PifMgr::PrgIconFile));
            p.icon_index = Word(prg + Win98PifMgr::PrgIconIndex);
            PifProgramIcon(index);
            ui.Send(4001, EmLimitText, 0x1d);
            ui.SetText(4001, reinterpret_cast<const char *>(prg + Win98PifMgr::PrgTitle));
            ui.Send(4002, EmLimitText, 0x7f);
            ui.SetText(4002, reinterpret_cast<const char *>(prg + Win98PifMgr::PrgCmdLine));
            ui.Send(4005, EmLimitText, 0x3f);
            ui.SetText(4005, reinterpret_cast<const char *>(prg + Win98PifMgr::PrgWorkDir));
            ui.Send(3003, HkmSetRules, 1, 6);
            ui.Send(3003, HkmSetHotKey, Word(prg + Win98PifMgr::PrgHotKey));
            ui.Send(4007, EmLimitText, 0x4f);
            ui.SetText(4007, reinterpret_cast<const char *>(env + 4));
            for (unsigned i = 0; i < 3; ++i)
            {
                char text[64];
                PifPagesString(3488 + i, text, sizeof text);
                ui.SendText(3005, CbAddString, 0, text);
            }
            const unsigned init = Word(prg + Win98PifMgr::PrgFlagsInit);
            ui.Send(3005, CbSetCurSel, (init & 2) ? 2 : (init & 1) ? 1 : 0);
            ui.SetChecks(ProgramChecks, Word(prg));
            PifProgramEnable(index);
            break;
        }
        case PageFont:
        {

            if (!PifMgrGroup(props, GroupFnt, p.fnt, sizeof p.fnt) || !PifMgrGroup(props, GroupWin, p.win, sizeof p.win))
            {
                PifPageMessageBox(hwnd, p.sheet, ErrorQuery, 0, 0);
                break;
            }
            ui.SetCheck(0x420 + ((Word(p.fnt) & 0xc) >> 2), true);
            PifFontList(hwnd, p.fnt);
            break;
        }
        case PageMemory:
        {
            // the EMS note and its Details button stay hidden,
            // the machine always offers expanded memory.
            unsigned char mem[16], env[0x58];
            if (!PifMgrGroup(props, GroupMem, mem, sizeof mem) || !PifMgrGroup(props, GroupEnv, env, sizeof env))
            {
                PifPageMessageBox(hwnd, p.sheet, ErrorQuery, 0, 0);
                break;
            }
            ui.SetChecks(MemoryChecks, Word(mem + 2));
            PifPageSetNumbers(hwnd, MemoryNumbers, 3, mem);
            PifPageSetNumbers(hwnd, EnvironmentNumbers, 2, env);
            ui.Show(1084, false);
            ui.Show(1085, false);
            break;
        }
        case PageScreen:
        {

            unsigned char vid[12], win[38];
            if (!PifMgrGroup(props, GroupVid, vid, sizeof vid) || !PifMgrGroup(props, GroupWin, win, sizeof win))
            {
                PifPageMessageBox(hwnd, p.sheet, ErrorQuery, 0, 0);
                break;
            }
            ui.SetChecks(ScreenVidChecks, Word(vid));
            ui.SetChecks(ScreenWinChecks, Word(win));
            ui.SetChecks(ScreenWinInitChecks, Word(win + 2));
            for (unsigned i = 0; i < 4; ++i)
            {
                char text[64];
                PifPagesString(3472 + i, text, sizeof text);
                ui.SendText(1047, CbAddString, 0, text);
                if (Word(vid + 0xa) == ScreenLines[i]) ui.Send(1047, CbSetCurSel, i);
            }
            break;
        }
        case PageMisc:
        {

            unsigned char tsk[14], kbd[18], mse[4];
            if (!PifMgrGroup(props, GroupTsk, tsk, sizeof tsk) || !PifMgrGroup(props, GroupKbd, kbd, sizeof kbd) ||
                !PifMgrGroup(props, GroupMse, mse, sizeof mse))
            {
                PifPageMessageBox(hwnd, p.sheet, ErrorQuery, 0, 0);
                break;
            }
            ui.Send(1037, TbmSetRange, 0, 20u << 16);
            ui.Send(1037, TbmSetPos, 1, Word(tsk + 0xc) / 5);
            ui.SetChecks(MiscTskChecks, Word(tsk));
            ui.SetChecks(MiscKbdChecks, Word(kbd));
            ui.SetChecks(MiscMseChecks, Word(mse));
            break;
        }
        default: break;
        }
        PifPageRealMode(hwnd, sh.realmode);
        if (p.kind == PageMemory && !sh.realmode)
        {
            ui.Show(1084, false);
            ui.Show(1085, false);
        }
        return 1;
    }
    if (message == WmCommand)
    {
        const unsigned id = wp & 0xffff, code = wp >> 16;
        switch (p.kind)
        {
        case PageProgram:
            if ((id == 4001 || id == 4002 || id == 4005 || id == 4007 || id == 3003) && code == 0x300) ui.Changed();
            else if (id == 3005 && code == 1) ui.Changed();
            else if (id == 4015 && code == 0) ui.Changed();
            else if (id == 4008 && code == 0)
            {
                const bool kept = recorded && PushHostFrame(HostPifAdvancedMessage, index, hwnd);
                if (kept) GiveTail(TailFresh);
                PifAdvancedDialog(index);
                if (kept) PopHostFrame();
                FinishPifAdvancedMessage(index, hwnd);
            }
            else if (id == 4013 && code == 0) PifChangeIcon(index);
            break;
        case PageFont:
            if (id == 1055 && code == 1)
            {
                // the cell of the selection into the group.
                const unsigned at = ui.Send(1055, LbGetCurSel);
                const unsigned data = at < 0x8000 ? ui.Send(1055, LbGetItemData, at) : 0xffff;
                unsigned flags = Word(p.fnt);
                if (data == 0xffff) flags |= 0x10;
                else
                {
                    flags &= ~0x810u;
                    if (data & 0x8000) flags |= 0x800;
                    PutWord(p.fnt + 8, data & 0xff);
                    PutWord(p.fnt + 0xa, (data >> 8) & 0x7f);
                }
                PutWord(p.fnt, flags);
                User::InvalidateRect(hwnd, nullptr, true);
                ui.Changed();
            }
            else if (id >= 1057 && id <= 1059 && code == 0)
            {
                PutWord(p.fnt, (Word(p.fnt) & ~0xcu) | ((id - 0x420) << 2));
                PifFontList(hwnd, p.fnt);
                User::InvalidateRect(hwnd, nullptr, true);
                ui.Changed();
            }
            break;
        case PageMemory:
            if ((id == 1014 || id == 1016 || id == 1022 || id == 1026 || id == 1088) && (code == 1 || code == 5))
            {
                p.memory_changed = true;
                ui.Changed();
            }
            else if ((id == 1019 || id == 1028) && code == 0)
            {
                ui.Changed();
                if (id != 1019) p.memory_changed = true;
            }
            break;
        case PageScreen:
            if (id >= 1043 && id <= 1053 && id != 1047 && code == 0) ui.Changed();
            else if (id == 1047 && code == 1) ui.Changed();
            break;
        case PageMisc:
            if (code == 0 && ((id >= 1031 && id <= 1040) || (id >= 1070 && id <= 1081))) ui.Changed();
            break;
        default: break;
        }
        return 1;
    }
    if (message == 0x0114 || message == 0x0115 || message == 0x0276)
    {
        // WM_HSCROLL of the idle trackbar: a change.
        if (p.kind == PageMisc) ui.Changed();
        return 0;
    }
    if (p.kind == PageFont && message == WmMeasureItem && lp)
    {
        Write32(lp + 0x10, 16);
        return 1;
    }
    if (p.kind == PageFont && message == WmDrawItem && lp) return PifFontDrawItem(lp);
    if (p.kind == PageFont && message == WmPaint) return PifFontPreview(index, hwnd);
    return 0;
}

//  Set the icon with STM_SETICON: the icon of the Program page.
void Application::PifProgramIcon(unsigned index)
{
    auto &p = pif_pages->pages[index];
    unsigned large = 0, small = 0;
    ShellExtractIconPair(p.icon_file[0] ? p.icon_file : "C:\\WINDOWS\\SYSTEM\\PIFMGR.DLL", p.icon_index,
                         (16u << 16) | 32u, (16u << 16) | 32u, 0, large, small);
    if (small)
    {
        unsigned a[] = {small};
        Api("DestroyIcon", a);
    }
    if (!large) return;
    PifUi ui{*this, p.hwnd};
    const unsigned old = ui.Send(3001, StmSetIcon, large);
    if (old)
    {
        unsigned a[] = {old};
        Api("DestroyIcon", a);
    }
    p.icon = large;
}

// Working and Batch file go grey for a program in MS-DOS mode
// without its own configuration.
void Application::PifProgramEnable(unsigned index)
{
    auto &p = pif_pages->pages[index];
    auto &sh = pif_pages->sheets[p.sheet];
    bool on = true;
    if (sh.realmode)
    {
        auto *record = PifMgrRecord(sh.props);
        on = record && (Win98PifMgr::GetNamed(*record, "CONFIG  SYS 4.0", nullptr, 0) ||
                        Win98PifMgr::GetNamed(*record, "AUTOEXECBAT 4.0", nullptr, 0));
    }
    PifUi ui{*this, p.hwnd};
    for (unsigned id = 4004; id <= 4007; ++id) ui.Enable(id, on);
}

// Change Icon, : SHELL.166 PickIconDlg is the 32-bit shell's
// PickIconDlg (SHELL32 ordinal 62) behind the 16-bit export; the icon is
// taken when the file carries one (SHELL.34 ExtractIcon above 1).
void Application::PifChangeIcon(unsigned index)
{
    auto &p = pif_pages->pages[index];
    const unsigned library = String("SHELL32.DLL");
    if (!library) return;
    const unsigned module = ModuleApi("LoadLibraryA", &library);
    Free(library);
    if (!module) return;
    const unsigned lookup[] = {module, 62};
    const unsigned procedure = ModuleApi("GetProcAddress", lookup);
    const unsigned buffer = Allocate(0x200);
    if (procedure && buffer)
    {
        Store(buffer, p.icon_file, sizeof p.icon_file);
        Write32(buffer + 0x100, p.icon_index);
        const unsigned arguments[] = {p.hwnd, buffer, 0x50, buffer + 0x100};
        if (Call(procedure, arguments, 4))
        {
            char file[80];
            snprintf(file, sizeof file, "%s", ReadString(buffer));
            const unsigned chosen = Read32(buffer + 0x100);
            unsigned large = 0, small = 0;
            ShellExtractIconPair(file, chosen, (16u << 16) | 32u, (16u << 16) | 32u, 0, large, small);
            if (small) { unsigned a[] = {small}; Api("DestroyIcon", a); }
            if (!large) PifPageMessageBox(p.hwnd, p.sheet, 3333, 0, 0);
            else
            {
                unsigned a[] = {large};
                Api("DestroyIcon", a);
                snprintf(p.icon_file, sizeof p.icon_file, "%s", file);
                p.icon_index = chosen;
                PifProgramIcon(index);
                PifUi{*this, p.hwnd}.Changed();
            }
        }
    }
    if (buffer) Free(buffer);
    ModuleApi("FreeLibrary", &module);
}

// The Advanced Program Settings, RT_DIALOG 102 run modal over the page.
void Application::PifAdvancedDialog(unsigned index)
{
    const bool recorded = TakeFresh();
    const bool kept = recorded && PushHostFrame(HostPifAdvanced, 0);
    const unsigned frame_at = kept ? threads[current_thread].host_frame_count - 1 : 0;
    struct Record { Application &app; bool kept; ~Record() { if (kept) app.PopHostFrame(); } } record{*this, kept};
    auto &s = *pif_pages;
    auto &p = s.pages[index];
    unsigned length = 0, cx = 0, cy = 0;
    unsigned char *bytes = PifPagesTemplate(AdvancedTemplate, false, length, nullptr, 0, cx, cy);
    if (!bytes) return;
    unsigned owner = p.hwnd;
    while (owner && (User::Style(owner) & 0x40000000u)) owner = User::Parent(owner);
    s.advanced_page = index;
    s.advanced_hwnd = 0;
    const unsigned hwnd = CreateDialogTemplate(bytes, length, owner, AdvancedToken, p.block, true);
    delete[] bytes;
    if (kept) { auto &frame = threads[current_thread].host_frames[frame_at]; frame.a = hwnd; frame.d = 1; GiveTail(TailRecord); }
    if (hwnd) RunModal(hwnd, kept);
    FinishPifAdvancedDialog();
}
void Application::FinishPifAdvancedDialog()
{
    if (pif_pages) { pif_pages->advanced_page = ~0u; pif_pages->advanced_hwnd = 0; }
}
void Application::FinishPifAdvancedMessage(unsigned index, unsigned hwnd)
{
    if (!pif_pages || index >= 64 || !pif_pages->pages[index].used || !User::IsWindow(hwnd)) return;
    PifProgramEnable(index);
    const unsigned sheet = pif_pages->pages[index].sheet;
    if (sheet < 8) PifPageRealMode(hwnd, pif_pages->sheets[sheet].realmode);
}

namespace
{
// Enable or disable the controls.
constexpr unsigned short AdvancedControls[10] = {3007, 3008, 6008, 6009, 6010, 6002, 6003, 6004, 6005, 6007};
} // namespace

unsigned Application::PifAdvancedMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    (void)lp;
    if (!pif_pages || pif_pages->advanced_page == ~0u) return 0;
    auto &s = *pif_pages;
    auto &p = s.pages[s.advanced_page];
    auto &sh = s.sheets[p.sheet];
    PifUi ui{*this, hwnd};
    auto *record = PifMgrRecord(sh.props);
    auto enable = [&]() {
        const bool specify = sh.realmode && ui.Checked(6010);
        for (unsigned id : AdvancedControls)
        {
            bool on = specify;
            if (id == 3007 || id == 3008) on = !sh.realmode;
            else if (id >= 6008 && id <= 6010) on = sh.realmode;
            ui.Enable(id, on);
        }
    };
    auto empty = [&]() {
        char text[8];
        ui.Text(6003, text, sizeof text);
        if (text[0]) return false;
        ui.Text(6005, text, sizeof text);
        return !text[0];
    };
    // The two sections into their edit fields.
    auto fill = [&]() {
        const char *names[2] = {"CONFIG  SYS 4.0", "AUTOEXECBAT 4.0"};
        const unsigned edits[2] = {6003, 6005};
        for (unsigned i = 0; i < 2; ++i)
        {
            char text[0x1001] = {};
            if (record)
            {
                const unsigned n = Win98PifMgr::GetNamed(*record, names[i], reinterpret_cast<unsigned char *>(text), 0x1000);
                text[n < 0x1000 ? n : 0x1000] = 0;
            }
            ui.Send(edits[i], EmLimitText, 0x1000);
            ui.SetText(edits[i], text);
        }
    };
    //  without the end of the dialog: the check boxes and the two
    // sections into the property handle.
    auto save = [&](unsigned char *prg) {
        if (!PifMgrGroup(sh.props, GroupPrg, prg, Win98PifMgr::PrgBytes)) return false;
        PutWord(prg, ui.GetChecks(AdvancedPrgChecks, Word(prg)));
        PutWord(prg + Win98PifMgr::PrgFlagsInit, ui.GetChecks(AdvancedInitChecks, Word(prg + Win98PifMgr::PrgFlagsInit)));
        const char *names[2] = {"CONFIG  SYS 4.0", "AUTOEXECBAT 4.0"};
        const unsigned edits[2] = {6003, 6005};
        for (unsigned i = 0; i < 2 && record; ++i)
        {
            char text[0x1001] = {};
            if (!ui.Checked(6009)) ui.Text(edits[i], text, sizeof text);
            const unsigned n = (unsigned)strlen(text);
            if (ui.Checked(6009) || n)
            {
                // "Use current" writes an empty section of one byte.
                const unsigned char none = 0;
                PifMgrSetNamed(sh.props, names[i], n ? reinterpret_cast<unsigned char *>(text) : &none, n ? n : 1);
            }
        }
        return PifMgrSetGroup(sh.props, GroupPrg, prg, Win98PifMgr::PrgBytes) != 0;
    };
    // the MS-DOS configuration wizard of APPWIZ.CPL. PIFMGR keeps
    // the program group, saves the dialog, calls AppWizard(hwnd, hProps,
    // action) through LoadLibraryEx32W, GetProcAddress32W and CallProc32W,
    // then fills the dialog again from the sections the wizard
    // wrote and puts the program group back. Action 2 is the Configuration
    // button, action 0 the choice of a new configuration with both fields
    // empty. The wizard is original 32-bit code and reads and writes the
    // sections through SHELL32's PifMgr ordinals, the native PIFMGR here.
    auto wizard = [&](unsigned action) {
        unsigned char kept[Win98PifMgr::PrgBytes], prg[Win98PifMgr::PrgBytes];
        if (!PifMgrGroup(sh.props, GroupPrg, kept, sizeof kept) || !save(prg)) return;
        const unsigned name = String("APPWIZ.CPL");
        const unsigned module = name ? ModuleApi("LoadLibraryA", &name) : 0;
        if (name) Free(name);
        unsigned result = 1;
        if (module)
        {
            const unsigned symbol = String("AppWizard");
            const unsigned lookup[] = {module, symbol};
            const unsigned procedure = symbol ? ModuleApi("GetProcAddress", lookup) : 0;
            if (symbol) Free(symbol);
            if (procedure)
            {
                const unsigned arguments[] = {hwnd, sh.props, action};
                result = Call(procedure, arguments, 3);
            }
            ModuleApi("FreeLibrary", &module);
        }
        if (log)
        {
            char text[120];
            snprintf(text, sizeof text, "PIFPAGES configuration wizard action=%u module=%x result=%u", action, module, result);
            log(text);
        }
        fill();
        PifMgrSetGroup(sh.props, GroupPrg, kept, sizeof kept);
        const bool use_current = empty();
        ui.Send(6009, 0xf1, use_current ? 1 : 0);
        ui.Send(6010, 0xf1, use_current ? 0 : 1);
        enable();
    };
    if (message == WmInitDialog)
    {

        s.advanced_hwnd = hwnd;
        unsigned char prg[Win98PifMgr::PrgBytes];
        if (!PifMgrGroup(sh.props, GroupPrg, prg, sizeof prg))
        {
            PifPageMessageBox(hwnd, p.sheet, ErrorQuery, 0, 0);
            return 1;
        }
        s.advanced_saved = Word(prg + Win98PifMgr::PrgFlagsInit);
        sh.realmode = (s.advanced_saved & 8) != 0;
        ui.SetChecks(AdvancedPrgChecks, Word(prg));
        ui.SetChecks(AdvancedInitChecks, Word(prg + Win98PifMgr::PrgFlagsInit));
        ui.SetText(4012, reinterpret_cast<const char *>(prg + Win98PifMgr::PrgPifFile));
        fill();
        const bool use_current = empty();
        ui.Send(6009, 0xf1, use_current ? 1 : 0);
        ui.Send(6010, 0xf1, use_current ? 0 : 1);
        enable();
        return 1;
    }
    if (message != WmCommand) return 0;
    const unsigned id = wp & 0xffff, code = wp >> 16;
    if (id == 1 || id == 4010)
    {

        unsigned char prg[Win98PifMgr::PrgBytes];
        bool ok = save(prg);
        if (ok)
        {
            ok = PifMgrFlushHandle(sh.props);
            if (ok && sh.notify && ((s.advanced_saved ^ Word(prg + Win98PifMgr::PrgFlagsInit)) & 0x10c))
                PifPageMessageBox(hwnd, p.sheet, 3367, 0, 0);
            sh.realmode = (Word(prg + Win98PifMgr::PrgFlagsInit) & 8) != 0;
        }
        if (!ok) PifPageMessageBox(hwnd, p.sheet, ErrorUpdate, 0, 0);
        EndDialog(hwnd, 0);
        return 1;
    }
    if (id == 2 || id == 4014)
    {
        sh.realmode = (s.advanced_saved & 8) != 0;
        EndDialog(hwnd, 0);
        return 1;
    }
    if (id == 4009 && code == 0)
    {
        sh.realmode = ui.Checked(4009);
        enable();
        return 1;
    }
    if (id == 6007 && code == 0)
    {
        wizard(2);
        return 1;
    }
    if ((id == 6009 || id == 6010) && code == 0)
    {
        enable();
        // a new configuration with nothing in it yet starts the
        // wizard, which fills in the lines of the options chosen there.
        if (id == 6010 && ui.Checked(6010) && empty()) wizard(0);
        return 1;
    }
    return 0;
}

// The owner drawn size list: a TrueType mark from bitmap 100 of the module
// (two 10x12 marks side by side, 4 bits) in front of TrueType sizes, and
// the text.
unsigned Application::PifFontDrawItem(unsigned lp, bool compact)
{
    const unsigned item = Read32(lp + 8), state = Read32(lp + 0x10), dc = Read32(lp + 0x18);
    int rect[4];
    Read(lp + 0x1c, rect, 16);
    const unsigned data = Read32(lp + 0x2c);
    if (item == 0xffffffffu) return 1;
    const bool selected = (state & 1) != 0;
    const unsigned brush_args[] = {selected ? 13u : 5u};
    const unsigned brush = Api("GetSysColorBrush", brush_args);
    const unsigned rect_memory = Allocate(64);
    if (!rect_memory) return 1;
    Store(rect_memory, rect, 16);
    const unsigned fill[] = {dc, rect_memory, brush};
    Api("FillRect", fill);
    const unsigned color_args[] = {selected ? 14u : 8u};
    const unsigned color[] = {dc, Api("GetSysColor", color_args)};
    Api("SetTextColor", color);
    const unsigned mode[] = {dc, 1};
    Api("SetBkMode", mode);
    char text[40];
    if (data == 0xffff) PifPagesString(3330, text, sizeof text);
    else snprintf(text, sizeof text, "%2u x %2u", data & 0xff, (data >> 8) & 0x7f);
    if ((data & 0x8000) && data != 0xffff && PifPagesModule())
    {
        auto &s = *pif_pages;
        unsigned offset = 0, bytes = 0;
        if (s.image.ResourceAt(0x8002, 0x8000 | 100, nullptr, offset, bytes) && bytes >= 40 + 64)
        {
            const unsigned bits = Allocate(bytes + 16);
            if (bits)
            {
                Store(bits, s.module + offset, bytes);
                // Magenta (FF00FFh) is the transparent colour of the mark:
                // it takes the colour of the row, selected or not.
                const unsigned background_args[] = {selected ? 13u : 5u};
                const unsigned background = Api("GetSysColor", background_args);
                for (unsigned entry = 0; entry < 16; ++entry)
                {
                    unsigned char quad[4] = {};
                    Read(bits + 40 + entry * 4, quad, 4);
                    if (quad[0] == 0xff && quad[1] == 0 && quad[2] == 0xff)
                    {
                        const unsigned char colour[4] = {(unsigned char)(background >> 16), (unsigned char)(background >> 8),
                                                         (unsigned char)background, 0};
                        Store(bits + 40 + entry * 4, colour, 4);
                    }
                }
                // The first mark of the strip, bottom-up rows.
                const int w = 10, h = 12;
                const unsigned blit[] = {dc, (unsigned)(rect[0] + 2), (unsigned)(rect[1] + (rect[3] - rect[1] - h) / 2),
                                         (unsigned)w, (unsigned)h, 0, 0, 0, (unsigned)Word(s.module + offset + 8),
                                         bits + 40 + 64, bits, 0};
                Api("SetDIBitsToDevice", blit);
                Free(bits);
            }
        }
    }
    const unsigned t = String(text);
    if (t)
    {
        // The list of the Font page keeps a column for the mark before every
        // size; the size box of the toolbar indents only a TrueType size.
        const int indent = compact ? ((data & 0x8000) && data != 0xffff ? 14 : 2) : 16;
        const unsigned out[] = {dc, (unsigned)(rect[0] + indent), (unsigned)(rect[1] + 1), t, (unsigned)strlen(text)};
        Api("TextOutA", out);
        Free(t);
    }
    Free(rect_memory);
    return 1;
}

// The cell the previews show for the FNT group: its size, or for Auto
// (FNT 0x10) the raster cell the MS-DOS window takes (DosVmAutoFor; the WIN
// group's bit 2 is the toolbar). TrueType for FNT 0x800.
bool Application::PifFontCell(const unsigned char *fnt, const unsigned char *win, unsigned &width, unsigned &height,
                              bool &truetype)
{
    const unsigned flags = Word(fnt);
    truetype = false;
    if (!(flags & 0x10) && Word(fnt + 8) && Word(fnt + 0xa))
    {
        width = Word(fnt + 8);
        height = Word(fnt + 0xa);
        truetype = (flags & 0x800) != 0;
        return true;
    }
    unsigned cells[32][2];
    const unsigned count = PifWoaCells(cells, 32);
    return count && DosVmAutoFor(cells, count, (Word(win) & 2) != 0, width, height);
}

// The two previews below the size list, WOAWinPreview and WOAFontPreview.
// PIFMGR creates them as child windows : each at the left edge of
// its label (1060 "Window preview:", 1061 "Font preview:"), one and a half
// lines of DEFAULT_GUI_FONT (tmHeight + tmExternalLeading) below
// the label's top, as wide as the label, both as high as the window
// preview's width scaled to the proportions of the screen, with
// WS_EX_CLIENTEDGE. Here the page paints them in those places. The font
// preview fills its client area black (the class brush), selects
// the font of the cell (: CreateFont with the cell's height and
// width, OEM_CHARSET, OUT_RASTER_PRECIS and the raster face of the FNT group
// at +0xc for a raster size, OUT_TT_PRECIS and the TrueType face at +0x2c
// for a TrueType size, FIXED_PITCH), light grey on black, and draws string
// 3332 with DrawText into the client rectangle less two pixels. Before, the
// sample was always OEM_FIXED_FONT stepped by the cell height, so small
// sizes drew the lines into each other.
unsigned Application::PifFontPreview(unsigned index, unsigned hwnd)
{
    auto &p = pif_pages->pages[index];
    const unsigned paint = Allocate(512);
    if (!paint) return 0;
    const unsigned begin[] = {hwnd, paint};
    const unsigned dc = Api("BeginPaint", begin);
    if (dc)
    {
        // The line of the dialog font.
        int line = 13;
        {
            const unsigned gui_args[] = {17};
            const unsigned select[] = {dc, Api("GetStockObject", gui_args)};
            const unsigned old = Api("SelectObject", select);
            const unsigned metrics[] = {dc, paint + 64};
            if (Api("GetTextMetricsA", metrics)) line = (int)Read32(paint + 64) + (int)Read32(paint + 64 + 16);
            const unsigned restore[] = {dc, old};
            Api("SelectObject", restore);
        }
        const unsigned cx_args[] = {0}, cy_args[] = {1};
        const int screen_w = (int)Api("GetSystemMetrics", cx_args), screen_h = (int)Api("GetSystemMetrics", cy_args);
        int areas[2][4] = {};
        int preview_h = 0;
        for (unsigned i = 0; i < 2; ++i)
        {
            Gdi::RECT r = {};
            const unsigned label = User::GetDlgItem(hwnd, 1060 + i);
            if (!label || !User::GetWindowRect(label, &r)) continue;
            Gdi::POINT a = {r.left, r.top}, b = {r.right, r.bottom};
            User::ScreenToClient(hwnd, &a);
            User::ScreenToClient(hwnd, &b);
            if (!i && screen_w > 0) preview_h = (b.x - a.x) * screen_h / screen_w;
            const int top = a.y + 3 * line / 2;
            areas[i][0] = a.x;
            areas[i][1] = top;
            areas[i][2] = b.x;
            areas[i][3] = top + preview_h;
        }
        unsigned cw = 8, ch = 12;
        bool truetype = false;
        if (!PifFontCell(p.fnt, p.win, cw, ch, truetype)) cw = 8, ch = 12, truetype = false;
        // WS_EX_CLIENTEDGE: the sunken edge, the client area inside it.
        for (auto &a : areas)
        {
            if (a[2] <= a[0] + 4 || a[3] <= a[1] + 4) continue;
            Store(paint + 64, a, 16);
            const unsigned edge[] = {dc, paint + 64, 0xa /* EDGE_SUNKEN */, 0xf /* BF_RECT */};
            Api("DrawEdge", edge);
            a[0] += 2, a[1] += 2, a[2] -= 2, a[3] -= 2;
        }
        // The window preview: the desktop, and a window of 80x25 cells.
        if (areas[0][2] > areas[0][0] && areas[0][3] > areas[0][1])
        {
            int *a = areas[0];
            Store(paint + 64, a, 16);
            const unsigned desk_args[] = {1};
            const unsigned desk[] = {dc, paint + 64, Api("GetSysColorBrush", desk_args)};
            Api("FillRect", desk);
            const int aw = a[2] - a[0], ah = a[3] - a[1];
            int ww = (int)(80 * cw + 8) * aw / (screen_w > 0 ? screen_w : 800),
                wh = (int)(25 * ch + 27) * ah / (screen_h > 0 ? screen_h : 600);
            if (ww > aw - 4) ww = aw - 4;
            if (wh > ah - 4) wh = ah - 4;
            int w[4] = {a[0] + 2, a[1] + 2, a[0] + 2 + ww, a[1] + 2 + wh};
            Store(paint + 64, w, 16);
            const unsigned frame_args[] = {3};
            const unsigned frame[] = {dc, paint + 64, Api("GetSysColorBrush", frame_args)};
            Api("FillRect", frame);
            int c[4] = {w[0] + 2, w[1] + 5, w[2] - 2, w[3] - 2};
            Store(paint + 64, c, 16);
            const unsigned black_args[] = {4};
            const unsigned black[] = {dc, paint + 64, Api("GetStockObject", black_args)};
            Api("FillRect", black);
        }
        // The font preview.
        if (areas[1][2] > areas[1][0] && areas[1][3] > areas[1][1])
        {
            int *a = areas[1];
            Store(paint + 64, a, 16);
            const unsigned black_args[] = {4};
            const unsigned black[] = {dc, paint + 64, Api("GetStockObject", black_args)};
            Api("FillRect", black);
            auto &s = *pif_pages;
            if (!s.woa_added)
            {
                // PIFMGR's start puts the WOAFont into the font table.
                s.woa_added = true;
                unsigned cells[1][2];
                char path[192] = {};
                PifWoaCells(cells, 1, path, sizeof path);
                if (path[0]) AddFontResourceFile(path);
            }
            char face[33] = {};
            memcpy(face, p.fnt + (truetype ? 0x2c : 0xc), 32);
            if (!face[0]) snprintf(face, sizeof face, "%s", truetype ? "Courier New" : "Terminal");
            const unsigned face_text = String(face);
            const unsigned create[] = {ch, cw, 0, 0, 0, 0, 0, 0, 255 /* OEM_CHARSET */,
                                       truetype ? 4u /* OUT_TT_PRECIS */ : 6u /* OUT_RASTER_PRECIS */, 0, 0,
                                       1 /* FIXED_PITCH */, face_text};
            const unsigned font = face_text ? Api("CreateFontA", create) : 0;
            if (face_text) Free(face_text);
            const unsigned select[] = {dc, font};
            const unsigned old = font ? Api("SelectObject", select) : 0;
            const unsigned color[] = {dc, 0xc0c0c0};
            Api("SetTextColor", color);
            const unsigned background[] = {dc, 0};
            Api("SetBkColor", background);
            char sample[300];
            PifPagesString(3332, sample, sizeof sample);
            const int text[4] = {a[0] + 2, a[1] + 2, a[2] - 2, a[3] - 2};
            Store(paint + 64, text, 16);
            if (const unsigned t = String(sample))
            {
                const unsigned draw[] = {dc, t, 0xffffffffu, paint + 64, 0};
                Api("DrawTextA", draw);
                Free(t);
            }
            if (old)
            {
                const unsigned restore[] = {dc, old};
                Api("SelectObject", restore);
            }
            if (font)
            {
                const unsigned remove[] = {font};
                Api("DeleteObject", remove);
            }
            if (log)
            {
                Gdi::RECT client = {};
                User::ClientScreenRect(hwnd, &client);
                const Gdi::POINT origin = {client.left + a[0], client.top + a[1]};
                const unsigned data = (Word(p.fnt) & 0x10) || !Word(p.fnt + 8) ? 0xffff
                                    : (Word(p.fnt + 8) | Word(p.fnt + 0xa) << 8 | ((Word(p.fnt) & 0x800) ? 0x8000 : 0));
                char line_text[200];
                snprintf(line_text, sizeof line_text, "DOSVM fontpreview data=0x%04x cell=%u,%u box=%d,%d,%d,%d font=\"%s\"%s",
                         data, cw, ch, origin.x, origin.y, origin.x + a[2] - a[0], origin.y + a[3] - a[1], face,
                         font ? "" : "(none)");
                log(line_text);
            }
        }
        const unsigned end[] = {hwnd, paint};
        Api("EndPaint", end);
    }
    Free(paint);
    return 1;
}
} // namespace X86
