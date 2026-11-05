// SPDX-License-Identifier: GPL-2.0-or-later
// GLIDEPAG.DLL, the page "Glide" of the display properties. A card vendor added its page to DESK.CPL as a shell
// extension: the handler under Controls Folder\Display\shellex\
// PropertySheetHandlers names a CLSID, its InprocServer32 a DLL, and the
// shell asks that DLL's DllGetClassObject for an object with IShellExtInit
// and IShellPropSheetExt. This is that DLL, next to the driver it sets:
// the class factory and the extension are guest memory whose method tables
// are gates of this module, the page is a dialog template built here, and
// the page procedure writes NEO98_SCALE and NEO98_ANTIALIAS into the key
// the driver reads (GlideRegistryString). tools/win98_glide_driver.py
// registers the handler the way the driver's setup did.
namespace
{
// {5B0D7C3E-9A41-4F26-8E13-3DF6A2C98B51}
const unsigned char GlidePageClass[16] = {0x3e, 0x7c, 0x0d, 0x5b, 0x41, 0x9a, 0x26, 0x4f,
                                          0x8e, 0x13, 0x3d, 0xf6, 0xa2, 0xc9, 0x8b, 0x51};
// IUnknown, IClassFactory, IShellExtInit, IShellPropSheetExt: xxxxxxxx-0000-0000-C000-000000000046.
constexpr unsigned IidUnknown = 0x00000000, IidClassFactory = 0x00000001, IidShellExtInit = 0x000214e8,
                   IidShellPropSheetExt = 0x000214e9;
constexpr unsigned SOk = 0, SFalse = 1, ENoInterface = 0x80004002u, ENotImpl = 0x80004001u,
                   ClassENoAggregation = 0x80040110u, ClassENotAvailable = 0x80040111u, EOutOfMemory = 0x8007000eu;

// The module's entries: name, stack words. The method tables list them in
// the order of the interfaces.
struct PageEntry
{
    const char *name;
    unsigned words;
};
const PageEntry PageEntries[] = {
    {"DllGetClassObject", 3}, {"DllCanUnloadNow", 0},
    // IClassFactory
    {"FactoryQueryInterface", 3}, {"FactoryAddRef", 1}, {"FactoryRelease", 1}, {"FactoryCreateInstance", 4},
    {"FactoryLockServer", 2},
    // IShellExtInit, at offset 0 of the extension
    {"InitQueryInterface", 3}, {"InitAddRef", 1}, {"InitRelease", 1}, {"InitInitialize", 4},
    // IShellPropSheetExt, at offset 4
    {"SheetQueryInterface", 3}, {"SheetAddRef", 1}, {"SheetRelease", 1}, {"SheetAddPages", 3},
    {"SheetReplacePage", 4},
    // the DLGPROC of the page
    {"GlidePageProc", 4},
};
enum : unsigned
{
    PageGetClassObject, PageCanUnloadNow, FactoryQuery, FactoryAddRef, FactoryRelease, FactoryCreate, FactoryLock,
    InitQuery, InitAddRef, InitRelease, InitInitialize, SheetQuery, SheetAddRef, SheetRelease, SheetAddPages,
    SheetReplacePage, PageProc, PageEntryCount
};
static_assert(sizeof PageEntries / sizeof PageEntries[0] == PageEntryCount, "entry table");
constexpr unsigned PageEntryBase = 0x1000;

// The controls of the page.
constexpr unsigned IdScale = 101, IdSmooth = 102;
constexpr unsigned WmInitDialog = 0x0110, WmCommand = 0x0111, WmNotify = 0x004e;
constexpr unsigned CbAddString = 0x143, CbGetCurSel = 0x147, CbSetCurSel = 0x14e;
constexpr unsigned BmGetCheck = 0xf0, BmSetCheck = 0xf1;
constexpr unsigned PsmChanged = 0x468, PsnApply = 0xffffff36u;
const char *const ScaleChoices[] = {"Automatic, as large as the window allows",
                                    "1 x, the program's own resolution",
                                    "2 x", "3 x", "4 x", "5 x", "6 x", "7 x", "8 x"};

// A DLGTEMPLATE with DS_SETFONT and its items, in the 32-bit form.
struct TemplateWriter
{
    std::vector<unsigned char> bytes;
    void Word(unsigned v) { bytes.push_back(v & 0xff); bytes.push_back((v >> 8) & 0xff); }
    void Long(unsigned v) { Word(v & 0xffff); Word(v >> 16); }
    void Text(const char *s) { for (; *s; ++s) Word((unsigned char)*s); Word(0); }
    void Align() { while (bytes.size() & 3) bytes.push_back(0); }
    void Item(unsigned style, int x, int y, int cx, int cy, unsigned id, unsigned atom, const char *text)
    {
        Align();
        Long(style | 0x50000000u); // WS_CHILD | WS_VISIBLE
        Long(0);
        Word(x); Word(y); Word(cx); Word(cy); Word(id);
        Word(0xffff); Word(atom);
        Text(text);
        Word(0);
    }
};
std::vector<unsigned char> GlidePageTemplate()
{
    TemplateWriter t;
    // WS_CHILD | WS_CAPTION | WS_DISABLED | DS_SETFONT, the style of the
    // pages of DESK.CPL; the caption is the tab's text.
    t.Long(0x48c00040u);
    t.Long(0);
    t.Word(8); // items
    t.Word(0); t.Word(0); t.Word(246); t.Word(212);
    t.Word(0); t.Word(0);
    t.Text("Glide");
    t.Word(8);
    t.Text("MS Sans Serif");
    constexpr unsigned Button = 0x80, Static = 0x82, Combo = 0x85;
    t.Item(0x7, 7, 7, 232, 84, 0xffff, Button, "Resolution of 3D pictures");                 // BS_GROUPBOX
    t.Item(0x0, 16, 21, 214, 26, 0xffff, Static,
           "Programs for a 3dfx card draw at a multiple of their own resolution. "
           "The picture gets sharper; what they read back stays as it was.");
    t.Item(0x0, 16, 54, 44, 8, 0xffff, Static, "&Multiple:");
    t.Item(0x00210003u, 62, 52, 168, 120, IdScale, Combo, "");                               // CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP
    t.Item(0x0, 16, 72, 214, 12, 0xffff, Static, "Automatic fills the window with whole multiples.");
    t.Item(0x7, 7, 98, 232, 48, 0xffff, Button, "Edges");
    t.Item(0x00010003u, 16, 112, 214, 10, IdSmooth, Button, "&Smooth edges with four samples per pixel");   // BS_AUTOCHECKBOX | WS_TABSTOP
    t.Item(0x0, 16, 127, 214, 16, 0xffff, Static, "The settings take effect when a program opens its 3D window.");
    return t.bytes;
}
} // namespace

const char *Application::GlidePageModule() { return "GLIDEPAG.DLL"; }

unsigned Application::GlidePageEntry(const char *name)
{
    for (unsigned i = 0; i < PageEntryCount; ++i)
        if (!strcmp(PageEntries[i].name, name)) return PageEntryBase + i;
    return ~0u;
}

unsigned Application::GlidePageArgumentCount(const char *name)
{
    for (const auto &e : PageEntries)
        if (!strcmp(e.name, name)) return e.words;
    return ~0u;
}

// One method table per interface in guest memory, filled with the gates of
// this module, made once per process.
unsigned Application::GlidePageTables()
{
    if (glide->page_tables) return glide->page_tables;
    const unsigned tables = Allocate(4 * PageEntryCount);
    if (!tables) return 0;
    for (unsigned i = 0; i < PageEntryCount; ++i)
    {
        unsigned address = 0;
        if (!NativeProcedure(GlidePageModule(), PageEntries[i].name, 0, &address, false)) return 0;
        Write32(tables + 4 * i, address);
    }
    glide->page_tables = tables;
    return tables;
}

bool Application::GlidePageStore(int scale, bool smooth)
{
    const unsigned text = Allocate(600);
    if (!text) return false;
    PutString(text, "System\\CurrentControlSet\\Services\\3Dfx\\Device0\\glide");
    const unsigned create[] = {0x80000002u, text, 0, 0, 0, 0xf003f, 0, text + 400, text + 404};
    bool stored = false;
    if (!Api("RegCreateKeyExA", create))
    {
        const unsigned key = Read32(text + 400);
        char number[16];
        snprintf(number, sizeof number, "%d", scale);
        PutString(text + 300, "NEO98_SCALE");
        PutString(text + 340, number);
        const unsigned scale_value[] = {key, text + 300, 0, 1, text + 340, unsigned(strlen(number) + 1)};
        stored = !Api("RegSetValueExA", scale_value);
        PutString(text + 300, "NEO98_ANTIALIAS");
        PutString(text + 340, smooth ? "1" : "0");
        const unsigned smooth_value[] = {key, text + 300, 0, 1, text + 340, 2};
        stored = !Api("RegSetValueExA", smooth_value) && stored;
        const unsigned close[] = {key};
        Api("RegCloseKey", close);
    }
    Free(text);
    return stored;
}

unsigned Application::GlidePageApi(unsigned entry, const unsigned *a)
{
    const unsigned tables = GlidePageTables();
    if (!tables) return EOutOfMemory;
    // The settings the page shows: the value of the driver's key, 0 when it
    // is not there.
    auto setting = [&](const char *value) {
        char text[32];
        return GlideRegistryString(value, text, sizeof text) ? atoi(text) : 0;
    };
    const unsigned factory_table = tables + 4 * FactoryQuery, init_table = tables + 4 * InitQuery,
                   sheet_table = tables + 4 * SheetQuery;
    // IID_* differ only in the first long; the rest is C000-000000000046.
    auto interface_is = [&](unsigned iid, unsigned first) {
        unsigned char id[16];
        static const unsigned char tail[12] = {0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
        return Read(iid, id, 16) && Read32(iid) == first && !memcmp(id + 4, tail, 12);
    };
    // The extension: +0 IShellExtInit, +4 IShellPropSheetExt, +8 references.
    auto query = [&](unsigned object, unsigned iid, unsigned out) -> unsigned {
        if (!out) return 0x80004003u; // E_POINTER
        unsigned result = 0;
        if (interface_is(iid, IidUnknown) || interface_is(iid, IidShellExtInit)) result = object;
        else if (interface_is(iid, IidShellPropSheetExt)) result = object + 4;
        Write32(out, result);
        if (!result) return ENoInterface;
        Write32(object + 8, Read32(object + 8) + 1);
        return SOk;
    };
    auto release = [&](unsigned object) -> unsigned {
        const unsigned count = Read32(object + 8) - 1;
        Write32(object + 8, count);
        if (!count) Free(object);
        return count;
    };
    switch (entry - PageEntryBase)
    {
    case PageGetClassObject:
    {
        // (rclsid, riid, ppv)
        if (a[2]) Write32(a[2], 0);
        unsigned char id[16];
        if (!Read(a[0], id, 16) || memcmp(id, GlidePageClass, 16)) return ClassENotAvailable;
        if (!interface_is(a[1], IidClassFactory) && !interface_is(a[1], IidUnknown)) return ENoInterface;
        if (!glide->page_factory)
        {
            glide->page_factory = Allocate(4);
            if (!glide->page_factory) return EOutOfMemory;
            Write32(glide->page_factory, factory_table);
        }
        Write32(a[2], glide->page_factory);
        return SOk;
    }
    case PageCanUnloadNow:
        // The module is native and stays; the shell may keep asking.
        return SFalse;
    case FactoryQuery:
        if (!a[2]) return 0x80004003u;
        Write32(a[2], 0);
        if (!interface_is(a[1], IidClassFactory) && !interface_is(a[1], IidUnknown)) return ENoInterface;
        Write32(a[2], a[0]);
        return SOk;
    case FactoryAddRef:
    case FactoryRelease:
        // The one factory of the process lives as long as the process.
        return 1;
    case FactoryCreate:
    {
        // (this, outer, riid, ppv)
        if (!a[3]) return 0x80004003u;
        Write32(a[3], 0);
        if (a[1]) return ClassENoAggregation;
        const unsigned object = Allocate(12);
        if (!object) return EOutOfMemory;
        Write32(object, init_table);
        Write32(object + 4, sheet_table);
        Write32(object + 8, 1);
        const unsigned result = query(object, a[2], a[3]);
        release(object);
        return result;
    }
    case FactoryLock:
        return SOk;
    case InitQuery: return query(a[0], a[1], a[2]);
    case SheetQuery: return query(a[0] - 4, a[1], a[2]);
    case InitAddRef:
    case SheetAddRef:
    {
        const unsigned object = a[0] - (entry - PageEntryBase == SheetAddRef ? 4 : 0);
        Write32(object + 8, Read32(object + 8) + 1);
        return Read32(object + 8);
    }
    case InitRelease: return release(a[0]);
    case SheetRelease: return release(a[0] - 4);
    case InitInitialize:
        // The display has no folder or data object to look at.
        return SOk;
    case SheetReplacePage:
        return ENotImpl;
    case SheetAddPages:
    {
        // (this, lpfnAddPage, lParam): a PROPSHEETPAGEA with PSP_DLGINDIRECT.
        if (!glide->page_template)
        {
            const auto bytes = GlidePageTemplate();
            glide->page_template = Allocate(unsigned(bytes.size()));
            if (!glide->page_template) return EOutOfMemory;
            Store(glide->page_template, bytes.data(), bytes.size());
        }
        unsigned procedure = 0;
        if (!NativeProcedure(GlidePageModule(), PageEntries[PageProc].name, 0, &procedure, false)) return EOutOfMemory;
        const unsigned text = Allocate(64);
        if (!text) return EOutOfMemory;
        PutString(text, GlidePageModule());
        const unsigned module_name[] = {text};
        const unsigned instance = ModuleApi("GetModuleHandleA", module_name);
        PutString(text, "COMCTL32.DLL");
        const unsigned comctl = ModuleApi("GetModuleHandleA", module_name);
        PutString(text, "CreatePropertySheetPageA");
        const unsigned lookup[] = {comctl, text};
        const unsigned create = comctl ? ModuleApi("GetProcAddress", lookup) : 0;
        Free(text);
        if (!create)
        {
            Fail("GLIDEPAG.DLL: the property sheet asked for pages without COMCTL32 loaded");
            return ENotImpl;
        }
        const unsigned page = Allocate(40);
        if (!page) return EOutOfMemory;
        const unsigned fields[10] = {40, 0x1 /* PSP_DLGINDIRECT */, instance, glide->page_template, 0, 0, procedure, 0, 0, 0};
        Store(page, fields, sizeof fields);
        const unsigned create_args[] = {page};
        const unsigned handle = Call(create, create_args, 1);
        Free(page);
        if (!handle) return EOutOfMemory;
        const unsigned add[] = {handle, a[2]};
        if (!Call(a[1], add, 2))
        {
            // The sheet refused the page; it owns nothing of it yet.
            const unsigned destroy_name = Allocate(32);
            if (destroy_name)
            {
                PutString(destroy_name, "DestroyPropertySheetPage");
                const unsigned find[] = {comctl, destroy_name};
                const unsigned destroy = ModuleApi("GetProcAddress", find);
                Free(destroy_name);
                const unsigned destroy_args[] = {handle};
                if (destroy) Call(destroy, destroy_args, 1);
            }
        }
        return SOk;
    }
    case PageProc:
    {
        // (hwnd, message, wParam, lParam)
        const unsigned window = a[0], message = a[1], wp = a[2], lp = a[3];
        auto item = [&](unsigned id) {
            const unsigned args[] = {window, id};
            return Api("GetDlgItem", args);
        };
        auto send = [&](unsigned to, unsigned msg, unsigned w, unsigned l) {
            const unsigned args[] = {to, msg, w, l};
            return Api("SendMessageA", args);
        };
        if (message == WmInitDialog)
        {
            const unsigned combo = item(IdScale);
            const unsigned text = Allocate(64);
            if (!text) return 1;
            for (const char *choice : ScaleChoices)
            {
                PutString(text, choice);
                send(combo, CbAddString, 0, text);
            }
            Free(text);
            const int scale = setting("NEO98_SCALE");
            send(combo, CbSetCurSel, unsigned(scale < 0 || scale > 8 ? 0 : scale), 0);
            send(item(IdSmooth), BmSetCheck, setting("NEO98_ANTIALIAS") > 0 ? 1 : 0, 0);
            return 1;
        }
        if (message == WmCommand)
        {
            const unsigned id = wp & 0xffff, code = wp >> 16;
            if ((id == IdScale && code == 1 /* CBN_SELCHANGE */) || (id == IdSmooth && code == 0 /* BN_CLICKED */))
            {
                const unsigned parent_args[] = {window};
                send(Api("GetParent", parent_args), PsmChanged, window, 0);
            }
            return 0;
        }
        if (message == WmNotify && lp && Read32(lp + 8) == PsnApply)
        {
            const int choice = int(send(item(IdScale), CbGetCurSel, 0, 0));
            const bool smooth = send(item(IdSmooth), BmGetCheck, 0, 0) == 1;
            const bool stored = GlidePageStore(choice < 0 ? 0 : choice, smooth);
            // DWL_MSGRESULT: PSNRET_NOERROR, or PSNRET_INVALID_NOCHANGEPAGE
            // when the key could not be written.
            const unsigned result[] = {window, 0, stored ? 0u : 2u};
            Api("SetWindowLongA", result);
            return 1;
        }
        return 0;
    }
    default:
        Fail("GLIDEPAG.DLL: unknown entry");
        return ENotImpl;
    }
}
