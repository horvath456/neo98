// SPDX-License-Identifier: GPL-2.0-or-later
// PrintDlg of the 16-bit COMMDLG (ordinal 20), built natively the way
// COMMDLG.DLL of Win98 builds it. Included in src/win16_commdlg.cpp
// after src/win16_choose_font.inc, whose helpers it shares.
//
// The 16-bit COMMDLG has its own print dialogs in the style of Windows 3.1,
// all of segment 7: PrintDlg, the dialog "Print" (602h) with
// PrintDlgProc and "Print Setup" (603h) with PrnSetupDlgProc.
// They talk to the printer driver itself: its DEVNAMES come from WIN.INI
// ([windows] device=, [devices]), the driver is loaded as <driver>.DRV and
// asked through its exports ExtDeviceMode, DeviceCapabilities and
// AdvancedSetupDialog. The driver runs as the 16-bit library this port
// hosts for printing (src/x86_print_driver.cpp); every pointer reaches it as
// a 16:16 alias. Rebuilt here, with the addresses of the original:
//
// PrintDlg: templates, hooks, PD_RETURNDEFAULT
// the DEVNAMES of the default printer, the DEVNAMES a call
// needs, the check of the caller's DEVNAMES
// ExtDeviceMode into a DEVMODE block, PD_RETURNDC/IC
// the Print dialog's WM_INITDIALOG, the printer line,
// collate and copies (SETCOPYCOUNT), the print qualities
// OK with the checks of copies and pages (strings 450h..459h)
// the Setup dialog's WM_INITDIALOG, the printers of
// [devices], paper sizes, paper sources,
// orientation, another printer and Options...
// OK and Cancel of Setup with the working DEVMODE
//
// The hooks are the caller's 16-bit procedures and see every message first;
// WM_INITDIALOG carries the far pointer of the caller's PRINTDLG.
namespace
{
// PRINTDLG16 (lStructSize 34h).
constexpr unsigned PdOwner = 0x04, PdDevMode = 0x06, PdDevNames = 0x08, PdDc = 0x0a, PdFlags = 0x0c,
                   PdFromPage = 0x10, PdToPage = 0x12, PdMinPage = 0x14, PdMaxPage = 0x16, PdCopies = 0x18,
                   PdInstance = 0x1a, PdPrintHook = 0x20, PdSetupHook = 0x24, PdPrintTemplate = 0x28,
                   PdSetupTemplate = 0x2c, PdPrintTemplateHandle = 0x30, PdSetupTemplateHandle = 0x32;
constexpr unsigned PdSelection = 0x1, PdPageNums = 0x2, PdNoSelection = 0x4, PdNoPageNums = 0x8,
                   PdCollate = 0x10, PdPrintToFile = 0x20, PdPrintSetup = 0x40, PdNoWarning = 0x80,
                   PdReturnDc = 0x100, PdReturnIc = 0x200, PdReturnDefault = 0x400, PdShowHelp = 0x800,
                   PdEnablePrintHook = 0x1000, PdEnableSetupHook = 0x2000, PdEnablePrintTemplate = 0x4000,
                   PdEnableSetupTemplate = 0x8000, PdEnablePrintTemplateHandle = 0x10000,
                   PdEnableSetupTemplateHandle = 0x20000, PdUseDevModeCopies = 0x40000,
                   PdDisablePrintToFile = 0x80000, PdHidePrintToFile = 0x100000, PdNoNetworkButton = 0x200000,
                   PdOldApplication = 0x10000000;
// DEVNAMES: wDriverOffset, wDeviceOffset, wOutputOffset, wDefault. 4000h
// marks a block COMMDLG made during the call, 8000h one it checked.
constexpr unsigned DnDefault = 0x1, DnMade = 0x4000;
// DEVMODE.
constexpr unsigned DmSpecVersion = 0x20, DmDriverVersion = 0x22, DmOrientation = 0x2c, DmPaperSize = 0x2e,
                   DmCopies = 0x36, DmDefaultSource = 0x38, DmPrintQuality = 0x3a, DmYResolution = 0x40;
// Control identifiers of the templates (prnsetup.dlg).
constexpr unsigned PrPrinter = 0x440, PrAll = 0x420, PrSelection = 0x421, PrPages = 0x422, PrFrom = 0x480,
                   PrTo = 0x481, PrCopies = 0x482, PrQuality = 0x470, PrQualityLabel = 0x443, PrToFile = 0x410,
                   PrCollate = 0x411, PrSetup = 0x400, PrHelp = 0x40e, PrRange = 0x430, PrFromLabel = 0x441,
                   PrToLabel = 0x442;
constexpr unsigned SuDefault = 0x422, SuSpecific = 0x423, SuPrinters = 0x470, SuOrientation = 0x430,
                   SuPortrait = 0x420, SuLandscape = 0x421, SuIcon = 0x43c, SuPaper = 0x431, SuSizeLabel = 0x441,
                   SuSize = 0x471, SuSourceLabel = 0x442, SuSource = 0x472, SuOptions = 0x400, SuNetwork = 0x40d;
// and segment 9: the help identifiers of Setup and Print.
const unsigned SetupHelpTable[] = {0x432, 0x6f84, 0x440, 0x6f25, 0x422, 0x6f25, 0x423, 0x6f25, 0x470, 0x6f25,
                                   0x430, 0x6f84, 0x43c, 0x6f6b, 0x420, 0x6f6b, 0x421, 0x6f6b, 0x431, 0x6f84,
                                   0x441, 0x6f6a, 0x442, 0x6f6a, 0x471, 0x6f6a, 0x472, 0x6f6a, 0x400, 0x6f26,
                                   0x40d, 0x6f94, 0x40d, 0x6f94, 0, 0};
const unsigned PrintHelpTable[] = {0x445, 0x6f38, 0x440, 0x6f38, 0x430, 0x6f3e, 0x420, 0x6f3e, 0x421, 0x6f3e,
                                   0x422, 0x6f3e, 0x441, 0x6f3e, 0x480, 0x6f3e, 0x442, 0x6f3e, 0x481, 0x6f3e,
                                   0x470, 0x6f3d, 0x443, 0x6f3d, 0x444, 0x6f3a, 0x482, 0x6f3a, 0x410, 0x6f3c,
                                   0x411, 0x6f3b, 0x400, 0x6f39, 0, 0};
// Drivers COMMDLG treats as older than their DEVMODE says
// in the dialog state.
const char *const OldDrivers[] = {"CITOH", "IBMGRX", "TI850", "TOSHIBA", "THINKJET", "IBMCOLOR", "CANON10E", nullptr};
const char *const HpDrivers[] = {"HPPCL", "HPPCL5A", "SD_LJET", nullptr};
constexpr unsigned Win16PrintDialogToken = 0x71000049, Win16SetupDialogToken = 0x7100004a,
                   Win16DigitsToken = 0x7100004b, Win16PrinterListToken = 0x7100004c;
struct Win16PrintFrame
{
    Application *app;
    Win16PrintFrame *next;
    bool setup = false; // PrnSetupDlgProc or PrintDlgProc
    unsigned hwnd = 0, native_template = 0;
    bool initializing = true;
    unsigned pd = 0, pd_far = 0;
    unsigned mode = 0;  // frame+4: Setup's working DEVMODE
    bool ready = false, ended = false;
    unsigned scratch = 0;
    unsigned message_help = 0;
    static unsigned init_hook;        // DS:214h
    static unsigned digits_procedure;  // DS:584h
    static unsigned list_procedure;    // DS:580h
    static unsigned item_height;       // DS:210h
    static bool driver_failed;         // DS:212h
    static unsigned portrait, landscape; // DS:1F8h, DS:1FAh
    static Win16PrintFrame *head;
    Win16PrintFrame(Application *a, bool is_setup) : app(a), next(head), setup(is_setup) { head = this; }
    ~Win16PrintFrame()
    {
        auto **at = &head;
        while (*at && *at != this)
            at = &(*at)->next;
        if (*at)
            *at = next;
    }
};
unsigned Win16PrintFrame::init_hook = 0, Win16PrintFrame::digits_procedure = 0, Win16PrintFrame::list_procedure = 0,
         Win16PrintFrame::item_height = 0, Win16PrintFrame::portrait = 0, Win16PrintFrame::landscape = 0;
bool Win16PrintFrame::driver_failed = false;
Win16PrintFrame *Win16PrintFrame::head = nullptr;
Win16PrintFrame *Win16PrintFrameOf(Application *app, unsigned hwnd, bool setup)
{
    for (auto *f = Win16PrintFrame::head; f; f = f->next)
        if (f->app == app && f->hwnd == hwnd)
            return f;
    for (auto *f = Win16PrintFrame::head; f; f = f->next)
        if (f->app == app && !f->hwnd && f->setup == setup)
        {
            f->hwnd = hwnd;
            return f;
        }
    return nullptr;
}
} // namespace

struct Application::Win16PrintDialogs
{
    Application &app;
    Win16PrintFrame *f; // null outside a dialog
    unsigned pd;
    unsigned scratch = 0;
    bool recorded = false;
    Win16PrintDialogs(Application &a, Win16PrintFrame *frame, unsigned structure)
        : app(a), f(frame), pd(structure), scratch(frame ? frame->scratch : 0) {}
    template <class... T> unsigned Call(const char *name, T... values)
    {
        const unsigned a[] = {unsigned(values)..., 0u};
        return app.Api(name, a);
    }
    unsigned Flags() { return app.Read32(pd + PdFlags); }
    void SetFlags(unsigned value) { app.Write32(pd + PdFlags, value); }
    unsigned Word(unsigned offset) { return app.Win16Word(pd + offset); }
    void Word(unsigned offset, unsigned value) { app.Win16Word(pd + offset, value & 0xffff); }
    void Error(unsigned value) { app.Win16CommonDialogError(value); }
    unsigned ErrorValue() { return app.Api("CommDlgExtendedError", nullptr); }
    unsigned Item(unsigned hwnd, unsigned id) { return User::GetDlgItem(hwnd, id); }
    unsigned Send(unsigned hwnd, unsigned message, unsigned wp = 0, unsigned lp = 0)
    {
        return Call("SendMessageA", hwnd, message, wp, lp);
    }
    unsigned SendItem(unsigned hwnd, unsigned id, unsigned message, unsigned wp = 0, unsigned lp = 0)
    {
        return Send(Item(hwnd, id), message, wp, lp);
    }
    void Enable(unsigned hwnd, unsigned id, bool on) { User::EnableWindow(Item(hwnd, id), on); }
    // Guest scratch of 2 KiB: +000h and +200h two texts, +400h a third,
    // +600h words, +700h the help table.
    unsigned Scratch()
    {
        if (!scratch)
            scratch = app.Allocate(0x800);
        return scratch;
    }
    unsigned TextA() { return Scratch(); }
    unsigned TextB() { return Scratch() + 0x200; }
    unsigned TextC() { return Scratch() + 0x400; }
    unsigned WordAt() { return Scratch() + 0x600; }
    void Put(unsigned at, const char *text)
    {
        const unsigned length = unsigned(strlen(text));
        app.Store(at, text, (length < 0x1ff ? length : 0x1ff) + 1);
    }
    void Get(unsigned at, char *out, unsigned capacity)
    {
        const char *s = at ? app.ReadString(at) : "";
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
    unsigned Profile(const char *section, const char *key, char *out, unsigned capacity)
    {
        const unsigned s = app.String(section), k = key ? app.String(key) : 0, empty = app.String("");
        const unsigned at = TextC();
        unsigned length = Call("GetProfileStringA", s, k, empty, at, capacity < 0x200 ? capacity : 0x200u);
        app.Free(s);
        if (k)
            app.Free(k);
        app.Free(empty);
        if (length >= capacity)
            length = capacity - 1;
        app.Read(at, out, length + 1);
        out[length] = 0;
        return length;
    }
    // 16-bit global blocks of the caller's heap.
    unsigned BlockMemory(unsigned handle)
    {
        auto *block = handle ? app.Win16GlobalBlock(handle) : nullptr;
        return block ? block->memory : 0;
    }
    unsigned BlockSize(unsigned handle)
    {
        auto *block = handle ? app.Win16GlobalBlock(handle) : nullptr;
        return block ? block->size : 0;
    }
    unsigned Allocate(unsigned bytes, bool zero = true)
    {
        return app.Win16GlobalAlloc(zero ? 0x42u : 0x2u, bytes);
    }
    void FreeBlock(unsigned handle)
    {
        if (handle)
            app.Win16GlobalFree(handle);
    }
    // DEVNAMES fields and strings.
    unsigned NameWord(unsigned names, unsigned field) { return app.Win16Word(names + field); }
    const char *NameText(unsigned names, unsigned field)
    {
        return app.ReadString(names + NameWord(names, field));
    }
    // lstrcmpi of two names; both are copied first because ReadString may
    // hand out one buffer for both.
    static bool Equal(const char *a, const char *b)
    {
        char x[0x104], y[0x104];
        snprintf(x, sizeof x, "%s", a ? a : "");
        snprintf(y, sizeof y, "%s", b ? b : "");
        for (unsigned i = 0;; ++i)
        {
            char c = x[i], d = y[i];
            if (c >= 'a' && c <= 'z')
                c = char(c - 32);
            if (d >= 'a' && d <= 'z')
                d = char(d - 32);
            if (c != d)
                return false;
            if (!c)
                return true;
        }
    }
    void Copy(char *out, unsigned capacity, const char *text)
    {
        snprintf(out, capacity, "%s", text ? text : "");
    }
    // DEVNAMES of the default printer, "device,driver,port".
    unsigned DefaultNames()
    {
        char line[0x84];
        if (!Profile("windows", "device", line, sizeof line))
        {
            Error(0x1008); // PDERR_NODEFAULTPRN
            return 0;
        }
        const unsigned length = unsigned(strlen(line));
        const unsigned handle = Allocate(length + 0x29);
        if (!handle)
        {
            Error(9);
            return 0;
        }
        const unsigned names = BlockMemory(handle);
        char *comma = strchr(line, ',');
        char *second = comma ? strchr(comma + 1, ',') : nullptr;
        if (!second)
        {
            Error(0x1002); // PDERR_PARSEFAILURE
            FreeBlock(handle);
            return 0;
        }
        *comma = 0;
        *second = 0;
        app.Store(names + 8, line, length + 1);
        app.Win16Word(names + 2, 8);
        app.Win16Word(names + 0, unsigned(comma + 1 - line) + 8);
        app.Win16Word(names + 4, unsigned(second + 1 - line) + 8);
        app.Win16Word(names + 6, DnMade | DnDefault);
        return handle;
    }
    // the driver, "<driver>.DRV" out of the system directory.
    PrintDriver *Driver(const char *name)
    {
        char bare[16];
        Copy(bare, sizeof bare, name);
        if (char *dot = strchr(bare, '.'))
            *dot = 0;
        unsigned error = 0;
        return bare[0] ? app.PrintLoadDriver(bare, error) : nullptr;
    }
    // A far call of the driver with Pascal pushes; every pointer argument is
    // given flat and goes as a 16:16 alias.
    struct Far
    {
        Application &app;
        unsigned mapped[8] = {};
        unsigned count = 0;
        unsigned Map(unsigned flat)
        {
            if (!flat)
                return 0;
            const unsigned far = app.PrintMapFlat(flat);
            if (far && count < 8)
                mapped[count++] = far;
            return far;
        }
        ~Far()
        {
            for (unsigned i = 0; i < count; ++i)
                app.PrintUnmapFar(mapped[i]);
        }
    };
    // DeviceCapabilities(lpDevice, lpPort, wIndex, lpOutput, lpDevMode).
    int Capabilities(PrintDriver *driver, unsigned device, unsigned port, unsigned index, unsigned output,
                     unsigned mode, bool &ok)
    {
        ok = false;
        if (!driver || !driver->capabilities)
            return 0;
        Far far{app};
        const unsigned d = far.Map(device), p = far.Map(port), o = far.Map(output), m = far.Map(mode);
        const unsigned push[9] = {d >> 16, d & 0xffff, p >> 16, p & 0xffff, index & 0xffff, o >> 16, o & 0xffff,
                                  m >> 16, m & 0xffff};
        const unsigned result = app.PrintCall(*driver, Print::DdiDeviceCapabilities, push, 9, ok);
        return int(result);
    }
    // ExtDeviceMode(hWnd, hDriver, lpdmOut, lpDeviceName, lpPort, lpdmIn,
    // lpProfile, wMode).
    int ExtDeviceMode(PrintDriver *driver, unsigned hwnd, unsigned out, unsigned device, unsigned port,
                      unsigned in, unsigned mode)
    {
        bool ok = false;
        if (!driver || !driver->extended_mode)
            return -1;
        Far far{app};
        const unsigned o = far.Map(out), d = far.Map(device), p = far.Map(port), i = far.Map(in);
        const unsigned push[13] = {hwnd & 0xffff, driver->instance & 0xffff, o >> 16, o & 0xffff, d >> 16,
                                   d & 0xffff, p >> 16, p & 0xffff, i >> 16, i & 0xffff, 0, 0, mode & 0xffff};
        const unsigned result = app.PrintCall(*driver, Print::DdiExtDeviceMode, push, 13, ok);
        return ok ? int(short(result & 0xffff)) : -1;
    }
    // AdvancedSetupDialog(hWnd, hDriver, lpdmIn, lpdmOut).
    int AdvancedSetup(PrintDriver *driver, unsigned hwnd, unsigned in, unsigned out)
    {
        bool ok = false;
        const unsigned entry = driver ? app.Win16HostedProcedure(driver->module, Print::DdiAdvancedSetupDialog) : 0;
        if (!entry)
            return -1;
        Far far{app};
        const unsigned i = far.Map(in), o = far.Map(out);
        const unsigned push[6] = {hwnd & 0xffff, driver->instance & 0xffff, i >> 16, i & 0xffff, o >> 16,
                                  o & 0xffff};
        const unsigned result = app.Win16HostedCall(entry, push, 6, driver->instance, nullptr, &ok);
        return ok ? int(short(result & 0xffff)) : -1;
    }
    bool HasAdvancedSetup(PrintDriver *driver)
    {
        return driver && app.Win16HostedProcedure(driver->module, Print::DdiAdvancedSetupDialog);
    }
    // a DEVMODE block from ExtDeviceMode. `prompt` asks the driver
    // (DM_COPY|DM_MODIFY with the caller's block as input), otherwise the
    // driver's current settings go into `mode` (reallocated) or a new block.
    // Answers FFFFh when the driver has no ExtDeviceMode, 0 when it has only
    // DeviceMode or fails.
    unsigned DeviceMode(unsigned owner, PrintDriver *driver, unsigned mode, unsigned names, bool modify)
    {
        if (!driver || !driver->extended_mode)
            return driver ? 0 : 0xffff;
        const unsigned device = names + NameWord(names, 2), port = names + NameWord(names, 4);
        const int bytes = ExtDeviceMode(driver, owner, 0, device, port, 0, 0);
        if (bytes <= 0)
            return 0;
        if (modify)
        {
            const unsigned in = BlockMemory(mode);
            if (!mode || !in)
                return 0;
            const unsigned handle = Allocate(unsigned(bytes), false);
            if (handle)
                ExtDeviceMode(driver, owner, BlockMemory(handle), device, port, in, 0xa);
            return handle;
        }
        unsigned handle = mode ? app.Win16GlobalReAlloc(mode, unsigned(bytes), 2) : Allocate(unsigned(bytes), false);
        if (!handle)
            return 0xffff;
        if (ExtDeviceMode(driver, owner, BlockMemory(handle), device, port, 0, 2) <= 0)
        {
            FreeBlock(handle);
            return 0;
        }
        return handle;
    }
    // the caller's DEVNAMES names a printer of [devices] with that
    // driver and port, and with DN_DEFAULTPRN the default printer.
    bool CheckNames(unsigned handle)
    {
        const unsigned names = BlockMemory(handle);
        if (!names)
        {
            Error(0xa);
            return false;
        }
        app.Win16Word(names + 6, NameWord(names, 6) & ~DnMade);
        Error(0x100b); // PDERR_PRINTERNOTFOUND
        char line[0x200];
        bool okay = false;
        if (Profile("devices", NameText(names, 2), line, sizeof line))
        {
            char *comma = strchr(line, ',');
            if (comma)
            {
                *comma = 0;
                {
                    char driver[64];
                    Copy(driver, sizeof driver, NameText(names, 0));
                    if (Equal(driver, line))
                    {
                        for (char *port = comma + 1; *port;)
                        {
                            char *end = strchr(port, ',');
                            if (end)
                                *end = 0;
                            if (Equal(NameText(names, 4), port))
                            {
                                Error(0);
                                okay = true;
                                break;
                            }
                            if (!end)
                                break;
                            port = end + 1;
                        }
                    }
                }
            }
        }
        if (ErrorValue() || !(NameWord(names, 6) & DnDefault))
            return okay;
        const unsigned fresh = DefaultNames();
        const unsigned other = BlockMemory(fresh);
        if (!fresh || !other)
        {
            if (fresh)
                FreeBlock(fresh);
            else
                Error(0xa);
            return false;
        }
        if (!Equal(NameText(names, 2), NameText(other, 2)) || !Equal(NameText(names, 0), NameText(other, 0)) ||
            !Equal(NameText(names, 4), NameText(other, 4)))
        {
            Error(0x100c); // PDERR_DEFAULTDIFFERENT
            okay = false;
        }
        FreeBlock(fresh);
        return okay;
    }
    // the DEVNAMES the call needs; 8000h, C000h for one made from
    // the default printer, 0 on failure.
    unsigned NeedNames()
    {
        const unsigned mode_handle = Word(PdDevMode);
        if (mode_handle)
        {
            const unsigned mode = BlockMemory(mode_handle);
            if (!mode)
            {
                Error(0xa);
                return 0;
            }
            if (Word(PdDevNames))
                return 0x8000;
            char device[40];
            Get(mode, device, 33);
            char line[0x200];
            if (!Profile("devices", device, line, sizeof line))
            {
                Error(0x1007); // PDERR_NODEVICES
                return 0;
            }
            char *comma = strchr(line, ',');
            if (!comma)
            {
                Error(0x1002);
                return 0;
            }
            *comma = 0;
            char *port = comma + 1;
            if (char *end = strchr(port, ','))
                *end = 0;
            const unsigned dl = unsigned(strlen(device)), rl = unsigned(strlen(line)), pl = unsigned(strlen(port));
            const unsigned handle = Allocate(dl + rl + pl + 0x2b, false);
            Word(PdDevNames, handle);
            const unsigned names = BlockMemory(handle);
            if (!handle || !names)
            {
                Error(9);
                return 0;
            }
            app.Win16Word(names + 2, 8);
            app.Store(names + 8, device, dl + 1);
            app.Win16Word(names + 0, dl + 9);
            app.Store(names + dl + 9, line, rl + 1);
            app.Win16Word(names + 4, dl + rl + 10);
            app.Store(names + dl + rl + 10, port, pl + 1);
            app.Win16Word(names + 6, DnMade);
            return 0x8000;
        }
        if (Word(PdDevNames))
            return 0x8000;
        const unsigned handle = DefaultNames();
        Word(PdDevNames, handle);
        return handle ? 0xc000 : 0;
    }
    // PD_RETURNDC or PD_RETURNIC; PD_PRINTTOFILE prints to FILE:.
    void ResultDc(unsigned names, unsigned mode)
    {
        const unsigned flags = Flags();
        if (!(flags & (PdReturnDc | PdReturnIc)))
            return;
        const unsigned port = flags & PdPrintToFile ? app.String("FILE:") : 0;
        const unsigned a[] = {names + NameWord(names, 0), names + NameWord(names, 2),
                              port ? port : names + NameWord(names, 4), mode};
        const unsigned dc = app.Api(flags & PdReturnDc ? "CreateDCA" : "CreateICA", a);
        if (port)
            app.Free(port);
        Word(PdDc, dc);
    }
    // "device on port", or "device on \\server\share (port)" for a
    // network port (strings 447h and 442h). No redirector belongs to this
    // port, so WNetGetConnection answers WN_NOT_SUPPORTED as USER.512 does.
    void Describe(const char *device, const char *port, char *out, unsigned capacity)
    {
        char format[64];
        LoadString(0x447, format, sizeof format);
        const char *args[3] = {device, "", port};
        unsigned at = 0;
        for (const char *p = format; *p && at + 1 < capacity; ++p)
        {
            if (p[0] == '%' && p[1] >= '1' && p[1] <= '3')
            {
                at += unsigned(snprintf(out + at, capacity - at, "%s", args[p[1] - '1']));
                if (at >= capacity)
                    at = capacity - 1;
                ++p;
            }
            else
                out[at++] = *p;
        }
        out[at] = 0;
    }
    // the printer line, "Default Printer " for the default one.
    bool PrinterLine(unsigned names, char *out, unsigned capacity)
    {
        Describe(NameText(names, 2), NameText(names, 4), out, capacity);
        if (NameWord(names, 6) & DnDefault)
        {
            char prefix[0x200];
            if (!LoadString(0x441, prefix, sizeof prefix))
            {
                Error(5);
                return false;
            }
            char line[0x200];
            snprintf(line, sizeof line, "%s%s)", prefix, out);
            Copy(out, capacity, line);
        }
        return true;
    }
    // the print qualities of the driver (DC_ENUMRESOLUTIONS) or
    // High, Medium, Low and Draft (strings 430h..433h, DMRES_*).
    void Qualities(unsigned hwnd, bool with_driver, int quality)
    {
        const unsigned combo = Item(hwnd, PrQuality);
        Send(combo, CbResetContent);
        const unsigned names = with_driver ? BlockMemory(Word(PdDevNames)) : 0;
        const unsigned mode = with_driver ? BlockMemory(Word(PdDevMode)) : 0;
        PrintDriver *driver = names && mode ? Driver(NameText(names, 0)) : nullptr;
        if (driver && app.Win16Word(mode + DmSpecVersion) >= 0x30a && driver->capabilities)
        {
            bool ok = false;
            const unsigned device = names + NameWord(names, 2), port = names + NameWord(names, 4);
            const int count = Capabilities(driver, device, port, 0xd, 0, 0, ok);
            if (ok && count != 0 && count != -1 && count > 0)
            {
                const unsigned buffer = app.Allocate(unsigned(count) * 8);
                int selection = 0;
                if (buffer)
                {
                    const int got = Capabilities(driver, device, port, 0xd, buffer, 0, ok);
                    for (int i = got - 1; i >= 0; --i)
                    {
                        const int x = int(app.Read32(buffer + unsigned(i) * 8)),
                                  y = int(app.Read32(buffer + unsigned(i) * 8 + 4));
                        char text[48];
                        if (x != y)
                            snprintf(text, sizeof text, "%d dpi x %d dpi", x, y);
                        else
                            snprintf(text, sizeof text, "%d dpi", y);
                        Put(TextA(), text);
                        Send(combo, CbInsertString, 0, TextA());
                        Send(combo, CbSetItemData, 0, (unsigned(x) & 0xffff) | unsigned(y) << 16);
                        const int wanted_y = int(short(app.Win16Word(mode + DmYResolution)));
                        if (x == quality && (!wanted_y || wanted_y == y))
                            selection = i;
                    }
                    app.Free(buffer);
                }
                Send(combo, CbSetCurSel, unsigned(selection));
                return;
            }
        }
        int value = -1;
        for (unsigned id = 0x433; id >= 0x430; --id, --value)
        {
            char text[40];
            if (!LoadString(id, text, sizeof text))
                break;
            Put(TextA(), text);
            Send(combo, CbInsertString, 0, TextA());
            Send(combo, CbSetItemData, 0, unsigned(value) & 0xffff);
        }
        if (quality >= 0 || quality < -4)
            quality = -4;
        Send(combo, CbSetCurSel, unsigned(quality + 4));
    }
    void FieldFocus(unsigned hwnd, unsigned id, Win16ControllerPhase &phase,
                    unsigned stage = 0, unsigned item = 0)
    {
        if (stage) phase.Rest();
        else
        {
            item = Item(hwnd, id);
            if (phase.kept) { phase.Data()[1] = item; phase.Data()[3] = 1; app.GiveTail(TailRecord); }
            Send(hwnd, 0x28, item, 1);
        }
        if (stage <= 1)
        {
            if (phase.kept) { phase.Data()[3] = 2; app.GiveTail(TailRecord); }
            Send(item, 0xb1, 0, unsigned(-1)); // EM_SETSEL 0,-1
        }
    }
    // a message about a field, then the focus on it with all selected.
    void FieldError(unsigned hwnd, unsigned id, unsigned text_id)
    {
        Win16ControllerPhase phase(app, HostWin16Print, hwnd, Win16ControllerPhase::PrintField,
                                   0, id, 0, recorded);
        char text[0x100], caption[0x100];
        if (text_id && LoadString(text_id, text, sizeof text))
        {
            Call("GetWindowTextA", hwnd, TextB(), 0x100);
            Get(TextB(), caption, sizeof caption);
            Put(TextA(), text);
            Put(TextB(), caption);
            phase.Warning(hwnd, TextA(), TextB(), 0x30);
        }
        FieldFocus(hwnd, id, phase);
    }
    // the printer line, qualities, copies and collate of the Print
    // dialog, from an information context of the printer.
    bool PrinterInfo(unsigned hwnd)
    {
        const unsigned names = BlockMemory(Word(PdDevNames));
        if (!names)
        {
            Error(0xa);
            return false;
        }
        unsigned mode = 0;
        if (Word(PdDevMode))
        {
            mode = BlockMemory(Word(PdDevMode));
            if (!mode)
            {
                Error(0xa);
                return false;
            }
            Enable(hwnd, PrQualityLabel, true);
            Enable(hwnd, PrQuality, true);
            if (app.Win16Word(mode + DmSpecVersion) <= 0x300)
                Qualities(hwnd, false, int(short(app.Win16Word(mode + DmPrintQuality))));
            else
                Qualities(hwnd, true, int(short(app.Win16Word(mode + DmPrintQuality))));
            if (int(short(app.Win16Word(mode + DmCopies))) > 1)
                Call("SetDlgItemInt", hwnd, PrCopies, app.Win16Word(mode + DmCopies), 0);
        }
        else
        {
            Enable(hwnd, PrQualityLabel, false);
            Enable(hwnd, PrQuality, false);
        }
        const unsigned a[] = {names + NameWord(names, 0), names + NameWord(names, 2), names + NameWord(names, 4),
                              mode};
        const unsigned ic = app.Api("CreateICA", a);
        Word(PdDc, ic);
        if (!ic)
        {
            Error(0x100a); // PDERR_CREATEICFAILURE
            return false;
        }
        // QUERYESCSUPPORT for SETCOPYCOUNT (11h).
        app.Write32(WordAt(), 0x11);
        if (Call("Escape", ic, 8, 2, WordAt(), 0))
        {
            Enable(hwnd, PrCollate, true);
            if (Flags() & PdCollate)
                Call("CheckDlgButton", hwnd, PrCollate, 1);
        }
        else
        {
            Enable(hwnd, PrCollate, false);
            Call("CheckDlgButton", hwnd, PrCollate, 1);
            if (Flags() & PdUseDevModeCopies)
            {
                Call("SetDlgItemInt", hwnd, PrCopies, 1, 0);
                Enable(hwnd, PrCopies, false);
            }
        }
        Call("DeleteDC", ic);
        Word(PdDc, 0);
        char line[0x200];
        if (!PrinterLine(names, line, sizeof line))
            return false;
        Put(TextA(), line);
        Call("SetDlgItemTextA", hwnd, PrPrinter, TextA());
        return true;
    }
    // --- Print Setup -------------------------------------------------------
    // the orientation buttons and the picture.
    void Orientation(unsigned hwnd, bool portrait)
    {
        Call("CheckRadioButton", hwnd, SuPortrait, SuLandscape, portrait ? SuPortrait : SuLandscape);
        SendItem(hwnd, SuIcon, 0x170, portrait ? Win16PrintFrame::portrait : Win16PrintFrame::landscape);
    }
    // the orientation and paper groups on or off.
    void Groups(unsigned hwnd, bool on)
    {
        if (!on)
        {
            Call("CheckDlgButton", hwnd, SuPortrait, 0);
            Call("CheckDlgButton", hwnd, SuLandscape, 0);
            SendItem(hwnd, SuSize, CbResetContent);
            SendItem(hwnd, SuSource, CbResetContent);
        }
const unsigned ids1[] = {SuOrientation, SuPortrait, SuLandscape};
        for (unsigned id : ids1)
            Enable(hwnd, id, on);
        User::ShowWindow(Item(hwnd, SuIcon), on ? 8 : 0);
const unsigned ids2[] = {SuPaper, SuSizeLabel, SuSize, SuSourceLabel, SuSource};
        for (unsigned id : ids2)
            Enable(hwnd, id, on);
    }
    static bool Listed(const char *const *list, const char *driver)
    {
        for (unsigned i = 0; list[i]; ++i)
            if (Equal(list[i], driver))
                return true;
        return false;
    }
    static unsigned HpIndex(const char *driver)
    {
        for (unsigned i = 0; HpDrivers[i]; ++i)
            if (Equal(HpDrivers[i], driver))
                return i + 1;
        return 0;
    }
    // the paper sizes (DC_PAPERS, DC_PAPERNAMES, strings 480h + n).
    bool Papers(unsigned hwnd, PrintDriver *driver, unsigned mode, unsigned names)
    {
        const unsigned combo = Item(hwnd, SuSize);
        Send(combo, CbResetContent);
        bool okay = false;
        const unsigned spec = app.Win16Word(mode + DmSpecVersion);
        const char *driver_name = NameText(names, 0);
        bool ask = true;
        if (spec < 0x30a)
        {
            if (app.Win16Word(mode + DmDriverVersion) < 0x3850 && HpIndex(driver_name) == 2)
                ask = false;
            else if (spec < 0x300 || Listed(OldDrivers, driver_name))
                ask = false;
        }
        if (ask && driver && driver->capabilities)
        {
            bool ok = false;
            const unsigned device = names + NameWord(names, 2), port = names + NameWord(names, 4);
            const int count = Capabilities(driver, device, port, 2, 0, mode, ok);
            if (ok && count != 0 && count != -1)
            {
                int named = 0;
                if (spec >= 0x30a)
                {
                    named = Capabilities(driver, device, port, 0x10, 0, mode, ok);
                    if (named == -1 || (named & 0xffff) == 0xffff)
                        named = 0;
                }
                const unsigned paper_names = named > 0 ? app.Allocate(unsigned(named) * 64) : 0;
                if (paper_names)
                    Capabilities(driver, device, port, 0x10, paper_names, mode, ok);
                const unsigned papers = app.Allocate(unsigned(count) * 2);
                if (papers)
                {
                    Capabilities(driver, device, port, 2, papers, mode, ok);
                    unsigned index = 0;
                    for (int i = 0; i < count; ++i)
                    {
                        const unsigned paper = app.Win16Word(papers + unsigned(i) * 2);
                        char text[64];
                        if (paper <= 0x100)
                        {
                            if (!LoadString(0x480 + paper, text, 0x3f))
                                continue;
                        }
                        else if (paper_names)
                            Get(paper_names + unsigned(i) * 64, text, sizeof text);
                        else
                            continue;
                        Put(TextA(), text);
                        Send(combo, CbInsertString, unsigned(-1), TextA());
                        if (app.Win16Word(mode + DmPaperSize) == paper)
                            Send(combo, CbSetCurSel, index);
                        Send(combo, CbSetItemData, index++, paper);
                    }
                    app.Free(papers);
                }
                if (paper_names)
                    app.Free(paper_names);
                okay = true;
            }
        }
        Enable(hwnd, SuSize, okay);
        Enable(hwnd, SuSizeLabel, okay);
        return okay;
    }
    // the paper sources (DC_BINS, DC_BINNAMES, or the escapes
    // GETSETPAPERBINS and ENUMPAPERBINS).
    bool Sources(unsigned hwnd, PrintDriver *driver, unsigned mode, unsigned names)
    {
        const unsigned combo = Item(hwnd, SuSource);
        Send(combo, CbResetContent);
        const unsigned spec = app.Win16Word(mode + DmSpecVersion);
        const char *driver_name = NameText(names, 0);
        if (spec < 0x30a)
        {
            const unsigned hp = HpIndex(driver_name);
            if (hp && (hp != 2 || app.Win16Word(mode + DmDriverVersion) < 0x3850))
            {
                Enable(hwnd, SuSource, false);
                Enable(hwnd, SuSourceLabel, false);
                return false;
            }
        }
        const unsigned device = names + NameWord(names, 2), port = names + NameWord(names, 4);
        bool okay = false;
        unsigned count = 0, buffer = 0;
        if (spec >= 0x30a && driver && driver->capabilities)
        {
            bool ok = false;
            const int n = Capabilities(driver, device, port, 0xc, 0, 0, ok);
            if (ok && n != 0 && n != -1 && n > 0)
            {
                buffer = app.Allocate(unsigned(n) * 26);
                if (buffer)
                {
                    Capabilities(driver, device, port, 6, buffer, mode, ok);
                    if (Capabilities(driver, device, port, 0xc, buffer + unsigned(n) * 2, mode, ok))
                    {
                        okay = true;
                        count = unsigned(n);
                    }
                    else
                    {
                        app.Free(buffer);
                        buffer = 0;
                    }
                }
            }
        }
        if (!okay)
        {
            const unsigned a[] = {names + NameWord(names, 0), device, port, mode};
            const unsigned ic = app.Api("CreateICA", a);
            if (ic)
            {
                app.Write32(WordAt(), 0x1f);
                const bool enumerate = Call("Escape", ic, 8, 2, WordAt(), 0);
                app.Write32(WordAt(), 0x1d);
                if (enumerate && Call("Escape", ic, 8, 2, WordAt(), 0))
                {
                    Call("Escape", ic, 0x1d, 0, 0, WordAt() + 8);
                    count = app.Win16Word(WordAt() + 10);
                    if (count)
                    {
                        okay = true;
                        buffer = app.Allocate(count * 26);
                        app.Win16Word(WordAt(), count);
                        if (buffer)
                            Call("Escape", ic, 0x1f, 2, WordAt(), buffer);
                        else
                            okay = false;
                    }
                }
                Call("DeleteDC", ic);
            }
        }
        Enable(hwnd, SuSource, okay);
        Enable(hwnd, SuSourceLabel, okay);
        if (!okay)
            return false;
        unsigned name = buffer + count * 2, bin = buffer;
        for (unsigned i = 0; i < count; ++i)
        {
            char text[32];
            Get(name, text, 25);
            if (!text[0])
                continue;
            name += 24;
            Put(TextA(), text);
            Send(combo, CbInsertString, unsigned(-1), TextA());
            const unsigned value = app.Win16Word(bin);
            bin += 2;
            Send(combo, CbSetItemData, i, value);
            if (app.Win16Word(mode + DmDefaultSource) == value)
                Send(combo, CbSetCurSel, i);
        }
        if (int(Send(combo, CbGetCurSel)) == -1)
            Send(combo, CbSetCurSel, 0);
        app.Free(buffer);
        return true;
    }
    // DC_ORIENTATION decides whether the orientation group is on.
    bool OrientationSupport(unsigned hwnd, PrintDriver *driver, unsigned mode, unsigned names)
    {
        bool support = true;
        if (app.Win16Word(mode + DmSpecVersion) >= 0x30a && driver && driver->capabilities)
        {
            bool ok = false;
            if (!Capabilities(driver, names + NameWord(names, 2), names + NameWord(names, 4), 0x11, 0, 0, ok))
                support = false;
        }
        const bool enabled = User::IsEnabled(Item(hwnd, SuPortrait));
        if (enabled == support)
            return false;
        if (!support)
        {
            Call("CheckDlgButton", hwnd, SuPortrait, 0);
            Call("CheckDlgButton", hwnd, SuLandscape, 0);
        }
const unsigned ids3[] = {SuOrientation, SuPortrait, SuLandscape};
        for (unsigned id : ids3)
            Enable(hwnd, id, support);
        User::ShowWindow(Item(hwnd, SuIcon), support ? 8 : 0);
        return support;
    }
    // one printer of the list, with its own DEVNAMES as item data.
    int AddPrinter(unsigned hwnd, const char *device, const char *driver, const char *port)
    {
        const unsigned dl = unsigned(strlen(device)) + 1, rl = unsigned(strlen(driver)) + 1,
                       pl = unsigned(strlen(port)) + 1;
        const unsigned handle = Allocate(dl + rl + pl + 0x28, false);
        const unsigned names = BlockMemory(handle);
        if (!handle || !names)
            return -1;
        app.Win16Word(names + 6, DnMade);
        app.Win16Word(names + 2, 8);
        app.Win16Word(names + 0, dl + 8);
        app.Win16Word(names + 4, rl + dl + 8);
        app.Store(names + 8, device, dl);
        app.Store(names + dl + 8, driver, rl);
        app.Store(names + rl + dl + 8, port, pl);
        char line[0x100];
        Describe(device, port, line, sizeof line);
        Put(TextA(), line);
        const int index = int(SendItem(hwnd, SuPrinters, CbAddString, 0, TextA()));
        SendItem(hwnd, SuPrinters, CbSetItemData, unsigned(index), handle);
        return index;
    }
    // every printer of [devices], one entry per port, and the
    // default printer in the line above ("(currently %s)", string 446h).
    unsigned Printers(unsigned hwnd)
    {
        char keys[0x200];
        const unsigned listed = Profile("devices", nullptr, keys, sizeof keys);
        unsigned last = 0;
        for (unsigned at = 0; at < listed && keys[at];)
        {
            const char *device = keys + at;
            const unsigned length = unsigned(strlen(device)) + 1;
            char line[0x200];
            last = Profile("devices", device, line, sizeof line);
            if (last)
            {
                char *p = line;
                while (*p && *p != '.' && *p != ',')
                    ++p;
                const bool dot = *p == '.';
                *p++ = 0;
                if (dot)
                    while (*p && *p != ',')
                        ++p;
                char *port = p;
                while (unsigned(port - line) < last)
                {
                    char *end = port;
                    while (*end && *end != ',')
                        ++end;
                    *end = 0;
                    const unsigned port_length = unsigned(strlen(port)) + 1;
                    if (strcmp(device, "Rendering Subsystem"))
                        AddPrinter(hwnd, device, line, port);
                    port += port_length;
                }
            }
            at += length;
        }
        char line[0x200];
        const unsigned found = Profile("windows", "device", line, sizeof line);
        if (found)
        {
            char format[0x50];
            if (!LoadString(0x446, format, sizeof format))
            {
                Error(5);
                return last;
            }
            char *comma = strchr(line, ',');
            char *port = comma ? strchr(comma + 1, ',') : nullptr;
            if (comma)
                *comma = 0;
            if (port)
                *port++ = 0;
            char described[0x200], text[0x200];
            Describe(line, port ? port : "", described, sizeof described);
            // "(currently %s)"
            char *mark = strstr(format, "%s");
            if (mark)
            {
                *mark = 0;
                snprintf(text, sizeof text, "%s%s%s", format, described, mark + 2);
            }
            else
                Copy(text, sizeof text, format);
            Put(TextA(), text);
            Call("SetDlgItemTextA", hwnd, PrPrinter, TextA());
            Error(0);
        }
        else
            Error(0x1008);
        if (!(last | listed))
            Error(0x1007);
        return listed;
    }
    // the entry of the list with this driver, device and port.
    int FindPrinter(unsigned hwnd, unsigned names)
    {
        const unsigned combo = Item(hwnd, SuPrinters);
        const int count = int(Send(combo, CbGetCount));
        int found = -1;
        for (int i = 0; i < count; ++i)
        {
            const unsigned other = BlockMemory(Send(combo, CbGetItemData, unsigned(i)) & 0xffff);
            if (other && Equal(NameText(names, 2), NameText(other, 2)) && Equal(NameText(names, 0), NameText(other, 0)) &&
                Equal(NameText(names, 4), NameText(other, 4)))
            {
                found = i;
                break;
            }
        }
        return found;
    }
    // the list gives up its DEVNAMES; with `keep` the chosen one (or
    // a new one of the default printer) becomes the caller's.
    void DropPrinters(unsigned hwnd, bool keep)
    {
        if (keep)
        {
            FreeBlock(Word(PdDevNames));
            Word(PdDevNames, 0);
        }
        int chosen;
        if (Call("IsDlgButtonChecked", hwnd, SuDefault))
        {
            if (keep)
                Word(PdDevNames, DefaultNames());
            chosen = -1;
        }
        else
            chosen = int(SendItem(hwnd, SuPrinters, CbGetCurSel));
        const int count = int(SendItem(hwnd, SuPrinters, CbGetCount));
        for (int i = count - 1; i >= 0; --i)
        {
            const unsigned data = SendItem(hwnd, SuPrinters, CbGetItemData, unsigned(i)) & 0xffff;
            if (keep && chosen == i)
                Word(PdDevNames, data);
            else
                FreeBlock(data);
        }
    }
    // orientation, paper size and source into a DEVMODE.
    void ReadSetup(unsigned hwnd, unsigned mode)
    {
        app.Win16Word(mode + DmOrientation, Call("IsDlgButtonChecked", hwnd, SuLandscape) ? 2 : 1);
        int selection = int(SendItem(hwnd, SuSize, CbGetCurSel));
        if (selection != -1)
            app.Win16Word(mode + DmPaperSize, SendItem(hwnd, SuSize, CbGetItemData, unsigned(selection)));
        selection = int(SendItem(hwnd, SuSource, CbGetCurSel));
        if (selection != -1)
            app.Win16Word(mode + DmDefaultSource, SendItem(hwnd, SuSource, CbGetItemData, unsigned(selection)));
    }
    // the controls into the caller's DEVMODE, and a DC for
    // PD_PRINTSETUP with PD_RETURNDC or PD_RETURNIC.
    void ApplySetup(unsigned hwnd)
    {
        const unsigned mode = BlockMemory(Word(PdDevMode));
        if (!Word(PdDevMode) || !mode)
            return;
        ReadSetup(hwnd, mode);
        if ((Flags() & PdPrintSetup) && (Flags() & (PdReturnDc | PdReturnIc)))
            if (const unsigned names = BlockMemory(Word(PdDevNames)))
                ResultDc(names, mode);
    }
    // another printer, the radio buttons, or Options... (400h).
    bool PrinterChanged(unsigned hwnd, unsigned why)
    {
        const bool default_printer = Call("IsDlgButtonChecked", hwnd, SuDefault);
        Win16PrintFrame::driver_failed = false;
        unsigned handle;
        if (default_printer)
            handle = DefaultNames();
        else
        {
            const int selection = int(SendItem(hwnd, SuPrinters, CbGetCurSel));
            if (selection < 0)
                return false;
            handle = SendItem(hwnd, SuPrinters, CbGetItemData, unsigned(selection)) & 0xffff;
        }
        const unsigned names = BlockMemory(handle);
        if (!handle || !names)
        {
            if (default_printer && handle)
                FreeBlock(handle);
            return false;
        }
        bool okay = true;
        PrintDriver *driver = Driver(NameText(names, 0));
        bool advanced = false, extended = false;
        if (!driver)
        {
            Win16PrintFrame::driver_failed = true;
            Groups(hwnd, false);
            Enable(hwnd, SuOptions, false);
        }
        else
        {
            if (why != SuOptions && f->mode)
            {
                FreeBlock(f->mode);
                f->mode = 0;
            }
            if (!f->mode)
            {
                f->mode = DeviceMode(Word(PdOwner), driver, 0, names, false);
                if (f->mode == 0xffff)
                    f->mode = 0;
            }
            if (f->mode)
            {
                const unsigned mode = BlockMemory(f->mode);
                advanced = HasAdvancedSetup(driver);
                extended = driver->extended_mode != 0;
                int result = 0;
                if (why == SuOptions)
                {
                    ReadSetup(hwnd, mode);
                    if (advanced)
                        result = AdvancedSetup(driver, hwnd, mode, mode);
                    else if (extended)
                        result = ExtDeviceMode(driver, hwnd, mode, names + NameWord(names, 2),
                                               names + NameWord(names, 4), mode, 0xe);
                }
                if (why == SuOptions && result != 1)
                    okay = false;
                else
                {
                    const bool papers = Papers(hwnd, driver, mode, names);
                    const bool sources = Sources(hwnd, driver, mode, names);
                    Enable(hwnd, SuPaper, papers || sources);
                    if (OrientationSupport(hwnd, driver, mode, names) || User::IsEnabled(Item(hwnd, SuPortrait)))
                        Orientation(hwnd, app.Win16Word(mode + DmOrientation) != 2);
                }
            }
            else
                Groups(hwnd, false);
            Enable(hwnd, SuOptions, advanced || extended);
        }
        if (default_printer)
            FreeBlock(handle);
        return okay;
    }
    // WM_MEASUREITEM, the height of the dialog font once.
    void MeasureItem(unsigned hwnd, unsigned lp)
    {
        if (!Win16PrintFrame::item_height)
        {
            const unsigned dc = Call("GetDC", hwnd);
            unsigned font = Send(hwnd, 0x31);
            if (!font)
                font = Call("GetStockObject", 0xd);
            const unsigned old = Call("SelectObject", dc, font);
            Call("GetTextMetricsA", dc, TextB());
            Call("SelectObject", dc, old);
            Call("ReleaseDC", hwnd, dc);
            Win16PrintFrame::item_height = app.Read32(TextB());
        }
        app.Write32(lp + 16, Win16PrintFrame::item_height);
    }
    unsigned Hook(unsigned hwnd, unsigned hook_offset, unsigned message, unsigned wp, unsigned lp, bool recorded = false)
    {
        const unsigned far_hook = app.Read32(pd + hook_offset);
        const unsigned mark = far_hook ? app.Win16Mark(far_hook) : 0;
        if (!mark || !User::IsWindow(hwnd))
            return 0;
        const unsigned arguments[] = {hwnd, message, wp, lp};
        if (recorded) app.GiveTail(TailRecord);
        return app.Call(mark, arguments, 4) & 0xffff;
    }
    void Help(unsigned item, unsigned command, const unsigned *table, unsigned bytes)
    {
        const unsigned at = Scratch() + 0x700;
        app.Store(at, table, bytes);
        Call("WinHelpA", item, 0, command, at);
    }
    void Subclass(unsigned item, unsigned token, unsigned &previous)
    {
        const unsigned old = User::Procedure(item);
        if (old != token)
            previous = old;
        User::SetProcedure(item, token);
    }
    // WM_INITDIALOG of Print Setup.
    unsigned InitFailed(unsigned hwnd)
    {
        f->ended = true;
        app.EndDialog(hwnd, 0);
        if (!ErrorValue()) Error(0x1006); // PDERR_INITFAILURE
        return 0;
    }
    unsigned SetupInitialize(unsigned hwnd, unsigned wp, bool recorded)
    {
        this->recorded = recorded;
        auto fail = [&] { return InitFailed(hwnd); };
        const unsigned combo = Item(hwnd, SuPrinters);
        f->ready = true;
        if (Flags() & PdEnableSetupHook)
        {
            if (!app.Read32(pd + PdSetupHook))
            {
                Error(0xb);
                return fail();
            }
        }
        else
            app.Write32(pd + PdSetupHook, 0);
        f->mode = 0;
        bool skip_driver = false;
        if (!Word(PdDevMode) && Word(PdDevNames))
        {
            if (!BlockMemory(Word(PdDevNames)))
            {
                Error(0xa);
                return fail();
            }
            if (!(Flags() & PdPrintSetup))
            {
                Enable(hwnd, SuOptions, false);
                skip_driver = true;
            }
        }
        if (!skip_driver)
        {
            if (Word(PdDevNames) && !CheckNames(Word(PdDevNames)))
                return fail();
            const unsigned made = NeedNames();
            if (!made)
            {
                char text[0x100], caption[0x40];
                if (ErrorValue() == 0x1008 && !(Flags() & PdNoWarning) && LoadString(0x45a, text, sizeof text))
                {
                    LoadString(0x448, caption, sizeof caption);
                    Put(TextA(), text);
                    Put(TextB(), caption);
                    Win16ControllerPhase phase(app, HostWin16Print, hwnd, Win16ControllerPhase::PrintNoPrinter,
                                               0, 0, 0, recorded);
                    phase.Warning(hwnd, TextA(), TextB(), 0x30);
                }
                return fail();
            }
            const unsigned names = BlockMemory(Word(PdDevNames));
            if (!names)
            {
                Error(0xa);
                return fail();
            }
            app.Win16Word(names + 6, NameWord(names, 6) | made);
            PrintDriver *driver = Driver(NameText(names, 0));
            if (!driver)
            {
                Error(0x1004); // PDERR_LOADDRVFAILURE
                return fail();
            }
            if (!Word(PdDevMode))
            {
                f->mode = DeviceMode(hwnd, driver, 0, names, false);
                if (f->mode == 0xffff)
                {
                    f->mode = 0;
                    Error(0x1005); // PDERR_GETDEVMODEFAIL
                    return fail();
                }
                if (!f->mode)
                    Enable(hwnd, SuOptions, false);
            }
            else
            {
                f->mode = DeviceMode(hwnd, driver, Word(PdDevMode), names, true);
                if (!f->mode || f->mode == 0xffff)
                {
                    f->mode = 0;
                    Error(0x1005);
                    return fail();
                }
            }
            if (f->mode)
            {
                const unsigned mode = BlockMemory(f->mode);
                if (!mode)
                {
                    Error(0xa);
                    return fail();
                }
                const bool papers = Papers(hwnd, driver, mode, names);
                const bool sources = Sources(hwnd, driver, mode, names);
                Enable(hwnd, SuPaper, papers || sources);
                Word(PdDc, 0);
                if (OrientationSupport(hwnd, driver, mode, names) || User::IsEnabled(Item(hwnd, SuPortrait)))
                    Orientation(hwnd, app.Win16Word(mode + DmOrientation) != 2);
                Enable(hwnd, SuOptions, HasAdvancedSetup(driver) || driver->extended_mode);
            }
        }
        if (!f->mode)
            Groups(hwnd, false);
        if (!Printers(hwnd))
            return fail();
        if (ErrorValue() == 0x1008)
            Enable(hwnd, SuDefault, false);
        const unsigned names = BlockMemory(Word(PdDevNames));
        if (!names)
        {
            Error(0xa);
            return fail();
        }
        const int index = FindPrinter(hwnd, names);
        if (index == -1 && !(NameWord(names, 6) & DnDefault))
        {
            DropPrinters(hwnd, false);
            Error(0x100b);
            return fail();
        }
        const bool is_default = NameWord(names, 6) & DnDefault;
        Call("CheckRadioButton", hwnd, SuDefault, SuSpecific, is_default ? SuDefault : SuSpecific);
        if (!is_default)
        {
            const unsigned style = Call("GetWindowLongA", combo, unsigned(-16));
            Call("SetWindowLongA", combo, unsigned(-16), style | 0x10000);
        }
        Send(combo, CbSetCurSel, index != -1 ? unsigned(index) : 0);
        Send(combo, 0x155, 1); // CB_SETEXTENDEDUI
        SendItem(hwnd, SuSize, 0x155, 1);
        SendItem(hwnd, SuSource, 0x155, 1);
        Subclass(combo, Win16PrinterListToken, Win16PrintFrame::list_procedure);
        if (!(Flags() & PdShowHelp))
        {
            const unsigned help = Item(hwnd, PrHelp);
            User::EnableWindow(help, false);
            Call("MoveWindow", help, unsigned(-8000), unsigned(-8000), 20, 20, 0);
            User::ShowWindow(help, 0);
        }
        // PD_NONETWORKBUTTON or not, the "Network..." button of needs
        // WNetGetCaps(WNNC_DIALOG), which answers zero here as USER.513 does.
        f->initializing = false;
        if (Flags() & PdEnableSetupHook)
            return Hook(hwnd, PdSetupHook, 0x110, wp, f->pd_far, recorded);
        return 1;
    }
    // OK, Cancel and 3 of Print Setup.
    unsigned SetupClose(unsigned hwnd, unsigned id, unsigned lp)
    {
        Hourglass(true);
        const bool keep = id == 1;
        DropPrinters(hwnd, keep);
        const unsigned names = BlockMemory(Word(PdDevNames));
        unsigned result = keep;
        bool free_names = false;
        if (!names)
        {
            Error(0xa);
            result = 0;
        }
        else
        {
            bool applied = false;
            if (keep)
            {
                PrintDriver *driver = Driver(NameText(names, 0));
                if (!driver)
                {
                    Hourglass(false);
                    DriverMissing(hwnd, names, Win16ControllerPhase::PrintDriverClose, id, lp);
                    if (Win16PrintFrame::driver_failed) return 1;
                    return FinishMissingSetupClose(hwnd, id, lp, names);
                }
                else
                {
                    app.Win16Word(names + 6, NameWord(names, 6) & ~DnMade);
                    Word(PdDc, 0);
                    FreeBlock(Word(PdDevMode));
                    Word(PdDevMode, f->mode);
                    f->mode = 0;
                    ApplySetup(hwnd);
                    applied = true;
                }
            }
            if (!applied)
            {
                FreeBlock(f->mode);
                f->mode = 0;
                free_names = (NameWord(names, 6) & DnMade) && (Flags() & PdPrintSetup);
            }
        }
        return FinishSetupClose(hwnd, id, lp, result, free_names);
    }
    unsigned FinishMissingSetupClose(unsigned hwnd, unsigned id, unsigned lp, unsigned names)
    {
        FreeBlock(f->mode);
        f->mode = 0;
        return FinishSetupClose(hwnd, id, lp, 0, (NameWord(names, 6) & DnMade) && (Flags() & PdPrintSetup));
    }
    unsigned FinishSetupClose(unsigned hwnd, unsigned id, unsigned lp, unsigned result, bool free_names)
    {
        if (free_names)
        {
            FreeBlock(Word(PdDevNames));
            Word(PdDevNames, 0);
        }
        f->ended = true;
        if (Flags() & PdEnableSetupHook)
            Win16PrintFrame::init_hook = app.Read32(pd + PdSetupHook);
        Hourglass(false);
        app.EndDialog(hwnd, id == 3 ? lp & 0xffff : result);
        return 1;
    }
    // string 45Bh, "The %1 driver file %2 not found...."
    void DriverMissing(unsigned hwnd, unsigned names, unsigned operation, unsigned id = 0, unsigned lp = 0)
    {
        char format[0x100], text[0x200], caption[0x40];
        if (!LoadString(0x45b, format, sizeof format))
            return;
        LoadString(0x448, caption, sizeof caption);
        const char *args[2] = {NameText(names, 2), NameText(names, 0)};
        char device[64], driver[64];
        Copy(device, sizeof device, args[0]);
        Copy(driver, sizeof driver, args[1]);
        const char *values[2] = {device, driver};
        unsigned at = 0;
        for (const char *p = format; *p && at + 1 < sizeof text; ++p)
        {
            if (p[0] == '%' && (p[1] == '1' || p[1] == '2'))
            {
                at += unsigned(snprintf(text + at, sizeof text - at, "%s", values[p[1] - '1']));
                if (at >= sizeof text)
                    at = sizeof text - 1;
                ++p;
            }
            else
                text[at++] = *p;
        }
        text[at] = 0;
        Put(TextA(), text);
        Put(TextB(), caption);
        Win16ControllerPhase phase(app, HostWin16Print, hwnd, operation, 0, id, lp, recorded, names);
        phase.Warning(hwnd, TextA(), TextB(), 0x30);
    }
    // the wait cursor.
    void Hourglass(bool on) { Call("SetCursor", Call("LoadCursorA", 0, on ? 0x7f02u : 0x7f00u)); }
    // WM_COMMAND of Print Setup.
    unsigned SetupCommand(unsigned hwnd, unsigned id, unsigned code, unsigned control)
    {
        switch (id)
        {
        case 1:
            if (SendItem(hwnd, SuPrinters, 0x157)) // CB_GETDROPPEDSTATE
                PrinterChanged(hwnd, SuPrinters);
            if (Win16PrintFrame::driver_failed)
            {
                const unsigned handle =
                    SendItem(hwnd, SuPrinters, CbGetItemData, SendItem(hwnd, SuPrinters, CbGetCurSel)) & 0xffff;
                if (const unsigned names = BlockMemory(handle))
                    DriverMissing(hwnd, names, Win16ControllerPhase::PrintDriverCommand);
                return 1;
            }
            return SetupClose(hwnd, id, control);
        case 2:
        case 3:
            return SetupClose(hwnd, id, control);
        case 4:
        {
            // the printer of the caller's structure again.
            if (!NeedNames() || !Word(PdDevNames))
                return 0;
            const unsigned names = BlockMemory(Word(PdDevNames));
            if (!names)
                return 0;
            Call("CheckRadioButton", hwnd, SuDefault, SuSpecific,
                 NameWord(names, 6) & DnDefault ? SuDefault : SuSpecific);
            const int index = FindPrinter(hwnd, names);
            PrintDriver *driver = Driver(NameText(names, 0));
            if (!driver)
                return 0;
            SendItem(hwnd, SuPrinters, CbSetCurSel, unsigned(index));
            if (const unsigned mode = BlockMemory(Word(PdDevMode)))
            {
                Groups(hwnd, true);
                const bool papers = Papers(hwnd, driver, mode, names);
                const bool sources = Sources(hwnd, driver, mode, names);
                Enable(hwnd, SuPaper, papers || sources);
                if (OrientationSupport(hwnd, driver, mode, names))
                    Orientation(hwnd, app.Win16Word(mode + DmOrientation) != 2);
                FreeBlock(f->mode);
                f->mode = DeviceMode(hwnd, driver, Word(PdDevMode), names, true);
            }
            else
                Groups(hwnd, false);
            return 1;
        }
        case SuOptions:
            if (PrinterChanged(hwnd, SuOptions))
                Send(hwnd, 0x28, Item(hwnd, 1), 1);
            return 1;
        case SuNetwork:
            // WNetConnectDialog; no network provider here (USER.513).
            return 1;
        case PrHelp:
            if (f->message_help && Word(PdOwner))
            {
                Win16ControllerPhase help(app, HostWin16Print, hwnd, Win16ControllerPhase::OwnerHelp,
                                           0, 1, 0, recorded);
                help.Help(Word(PdOwner), f->message_help, hwnd, f->pd_far);
            }
            return 1;
        case SuPortrait:
        case SuLandscape:
            Orientation(hwnd, id != SuLandscape);
            return 1;
        case SuDefault:
        case SuSpecific:
        {
            const bool checked = Call("IsDlgButtonChecked", hwnd, id);
            const unsigned combo = Item(hwnd, SuPrinters);
            unsigned style = Call("GetWindowLongA", combo, unsigned(-16));
            if (id == SuDefault)
                style &= ~0x10000u;
            else
            {
                style |= 0x10000;
                Send(hwnd, 0x28, combo, 1);
            }
            Call("SetWindowLongA", combo, unsigned(-16), style);
            if (!checked)
            {
                Call("CheckRadioButton", hwnd, SuDefault, SuSpecific, id);
                PrinterChanged(hwnd, id);
            }
            return 1;
        }
        case SuPrinters:
            if (code == 1 && SendItem(hwnd, SuPrinters, 0x157))
                return 1;
            if (code == 1 || code == 8) // CBN_SELCHANGE, CBN_CLOSEUP
            {
                Hourglass(true);
                Call("CheckRadioButton", hwnd, SuDefault, SuSpecific, SuSpecific);
                PrinterChanged(hwnd, SuPrinters);
                Hourglass(false);
            }
            else if (code == 3) // CBN_SETFOCUS
                Call("CheckRadioButton", hwnd, SuDefault, SuSpecific, SuSpecific);
            return 1;
        default:
            return 0;
        }
    }
    // --- Print -------------------------------------------------------------
    // WM_INITDIALOG of Print.
    unsigned PrintInitialize(unsigned hwnd, unsigned wp, bool recorded)
    {
        this->recorded = recorded;
        auto fail = [&] { return InitFailed(hwnd); };
        f->ready = true;
        if (Flags() & PdEnablePrintHook)
        {
            if (!app.Read32(pd + PdPrintHook))
            {
                Error(0xb);
                return fail();
            }
        }
        else
            app.Write32(pd + PdPrintHook, 0);
        if (Flags() & PdEnableSetupHook)
        {
            if (!app.Read32(pd + PdSetupHook))
            {
                Error(0xb);
                return fail();
            }
        }
        else
            app.Write32(pd + PdSetupHook, 0);
        SendItem(hwnd, PrQuality, 0x155, 1);
        const unsigned made = NeedNames();
        if (!made)
        {
            char text[0x100], caption[0x40];
            if (ErrorValue() == 0x1008 && !(Flags() & PdNoWarning) && LoadString(0x45a, text, sizeof text))
            {
                LoadString(0x448, caption, sizeof caption);
                Put(TextA(), text);
                Put(TextB(), caption);
                Win16ControllerPhase phase(app, HostWin16Print, hwnd, Win16ControllerPhase::PrintNoPrinter,
                                           0, 0, 0, recorded);
                phase.Warning(hwnd, TextA(), TextB(), 0x30);
            }
            return fail();
        }
        const unsigned names = BlockMemory(Word(PdDevNames));
        if (!names)
        {
            Error(0xa);
            return fail();
        }
        app.Win16Word(names + 6, NameWord(names, 6) | made);
        PrintDriver *driver = Driver(NameText(names, 0));
        if (!driver)
        {
            Error(0x1004);
            return fail();
        }
        if (!Word(PdDevMode))
        {
            const unsigned mode = DeviceMode(hwnd, driver, 0, names, false);
            if (mode == 0xffff)
            {
                Error(0x1005);
                Word(PdDevMode, 0);
                return fail();
            }
            Word(PdDevMode, mode);
        }
        SendItem(hwnd, PrCopies, 0xc5, 5); // EM_LIMITTEXT
        if (!Word(PdCopies))
            Word(PdCopies, 1);
        Call("SetDlgItemInt", hwnd, PrCopies, Word(PdCopies), 0);
        if (!PrinterInfo(hwnd))
            return fail();
        const unsigned flags = Flags();
        if (!(flags & PdShowHelp))
        {
            Enable(hwnd, PrHelp, false);
            User::ShowWindow(Item(hwnd, PrHelp), 0);
        }
        if (flags & PdHidePrintToFile)
        {
            Enable(hwnd, PrToFile, false);
            User::ShowWindow(Item(hwnd, PrToFile), 0);
        }
        else if (flags & PdDisablePrintToFile)
            Enable(hwnd, PrToFile, false);
        if (flags & PdPrintToFile)
            Call("CheckDlgButton", hwnd, PrToFile, 1);
        if (flags & PdNoPageNums)
        {
const unsigned ids4[] = {PrPages, PrFromLabel, PrToLabel, PrFrom, PrTo};
            for (unsigned id : ids4)
                Enable(hwnd, id, false);
            SetFlags(Flags() & ~PdPageNums);
        }
        else
        {
            if (Word(PdFromPage) != 0xffff)
                Call("SetDlgItemInt", hwnd, PrFrom, Word(PdFromPage), 0);
            if (Word(PdToPage) != 0xffff)
                Call("SetDlgItemInt", hwnd, PrTo, Word(PdToPage), 0);
        }
        if (Flags() & PdNoSelection)
        {
            Enable(hwnd, PrSelection, false);
            SetFlags(Flags() & ~PdSelection);
        }
        const unsigned range = Flags() & PdPageNums ? PrPages : Flags() & PdSelection ? PrSelection : PrAll;
        Call("CheckRadioButton", hwnd, PrAll, PrPages, range);
        Subclass(Item(hwnd, PrFrom), Win16DigitsToken, Win16PrintFrame::digits_procedure);
        Subclass(Item(hwnd, PrTo), Win16DigitsToken, Win16PrintFrame::digits_procedure);
        Subclass(Item(hwnd, PrCopies), Win16DigitsToken, Win16PrintFrame::digits_procedure);
        f->initializing = false;
        if (Flags() & PdEnablePrintHook)
            return Hook(hwnd, PdPrintHook, 0x110, wp, f->pd_far, recorded);
        return 1;
    }
    // OK of Print.
    unsigned PrintOk(unsigned hwnd)
    {
        SetFlags(Flags() & ~(PdSelection | PdPageNums | PdCollate | PdPrintToFile));
        if (Call("IsDlgButtonChecked", hwnd, PrToFile))
            SetFlags(Flags() | PdPrintToFile);
        if (Call("IsDlgButtonChecked", hwnd, PrCollate))
            SetFlags(Flags() | PdCollate);
        const unsigned ok = WordAt();
        auto number = [&](unsigned id, bool &translated) {
            const unsigned value = Call("GetDlgItemInt", hwnd, id, ok, 0);
            translated = app.Read32(ok) != 0;
            return value & 0xffff;
        };
        auto typed = [&](unsigned id) {
            Call("GetDlgItemTextA", hwnd, id, TextA(), 4);
            return app.Read32(TextA()) & 0xff;
        };
        bool translated = false;
        Word(PdCopies, number(PrCopies, translated));
        if (!translated || !Word(PdCopies))
        {
            const unsigned text_id = typed(PrCopies) ? (Word(PdCopies) < 1 ? 0x459u : 0x458u) : 0x457u;
            FieldError(hwnd, PrCopies, text_id);
            return 0;
        }
        if (Call("IsDlgButtonChecked", hwnd, PrSelection))
            SetFlags(Flags() | PdSelection);
        else if (Call("IsDlgButtonChecked", hwnd, PrPages))
        {
            SetFlags(Flags() | PdPageNums);
            bool from_ok = false, to_ok = false;
            Word(PdFromPage, number(PrFrom, from_ok));
            if (!from_ok && typed(PrFrom))
            {
                FieldError(hwnd, PrFrom, 0x454);
                return 0;
            }
            Word(PdToPage, number(PrTo, to_ok));
            if (!to_ok && typed(PrTo))
            {
                FieldError(hwnd, PrTo, 0x455);
                return 0;
            }
            if (!from_ok && !to_ok)
            {
                FieldError(hwnd, PrFrom, 0x456);
                return 0;
            }
            if (!from_ok)
                Word(PdFromPage, Word(PdMinPage));
            else if (!to_ok)
                Word(PdToPage, Word(PdMaxPage));
            if (Word(PdFromPage) < Word(PdMinPage))
                return FieldError(hwnd, PrFrom, 0x450), 0;
            if (Word(PdFromPage) > Word(PdMaxPage))
                return FieldError(hwnd, PrFrom, 0x451), 0;
            if (Word(PdToPage) < Word(PdMinPage))
                return FieldError(hwnd, PrTo, 0x452), 0;
            if (Word(PdToPage) > Word(PdMaxPage))
                return FieldError(hwnd, PrTo, 0x453), 0;
        }
        Hourglass(true);
        const unsigned names = BlockMemory(Word(PdDevNames));
        unsigned mode = 0;
        if (Word(PdDevMode))
        {
            mode = BlockMemory(Word(PdDevMode));
            if (mode)
            {
                if (Flags() & PdUseDevModeCopies)
                {
                    app.Win16Word(mode + DmCopies, Word(PdCopies));
                    Word(PdCopies, 1);
                }
                else
                    app.Win16Word(mode + DmCopies, 1);
                const int selection = int(SendItem(hwnd, PrQuality, CbGetCurSel));
                if (selection != -1)
                {
                    const unsigned data = SendItem(hwnd, PrQuality, CbGetItemData, unsigned(selection));
                    app.Win16Word(mode + DmPrintQuality, data & 0xffff);
                    app.Win16Word(mode + DmYResolution, data >> 16);
                }
            }
        }
        if (names)
        {
            ResultDc(names, mode);
            app.Win16Word(names + 6, NameWord(names, 6) & ~DnMade);
        }
        return 1;
    }
    // the end of Print.
    void PrintEnd(unsigned hwnd, unsigned id, unsigned result, unsigned lp)
    {
        f->ended = true;
        if (Flags() & PdEnablePrintHook)
            Win16PrintFrame::init_hook = app.Read32(pd + PdPrintHook);
        Hourglass(false);
        app.EndDialog(hwnd, id == 3 ? lp & 0xffff : result);
    }
    // WM_COMMAND of Print.
    void PrintCommand(unsigned hwnd, unsigned id, unsigned code, unsigned control, bool recorded);
    void ContinueSetup(unsigned hwnd, bool recorded, unsigned answer, bool resumed);
    // The template of one of the two dialogs: the caller's by handle or by name
    // (with hInstance), or COMMDLG's own 602h/603h.
    static unsigned Template(Application &app, unsigned pd, bool setup, unsigned char *&converted,
                                       unsigned &length, unsigned &native, unsigned &error)
    {
        const unsigned flags = app.Read32(pd + PdFlags);
        const unsigned handle_flag = setup ? PdEnableSetupTemplateHandle : PdEnablePrintTemplateHandle,
                       name_flag = setup ? PdEnableSetupTemplate : PdEnablePrintTemplate;
        converted = nullptr;
        length = native = error = 0;
        if (flags & handle_flag)
        {
            native = app.Win16NativeTemplate(0, 0, app.Win16Word(pd + (setup ? PdSetupTemplateHandle : PdPrintTemplateHandle)));
            if (!native)
                error = 8; // CDERR_LOCKRESFAILURE
            return native;
        }
        if (flags & name_flag)
        {
            const unsigned name = app.Read32(pd + (setup ? PdSetupTemplate : PdPrintTemplate));
            if (!name)
            {
                error = 3;
                return 0;
            }
            if (!app.Win16Word(pd + PdInstance))
            {
                error = 4;
                return 0;
            }
            native = app.Win16NativeTemplate(app.Win16Word(pd + PdInstance), name, 0);
            if (!native)
                error = 7;
            return native;
        }
        auto &images = app.CommDlgResources();
        unsigned size = 0;
        const unsigned char *bytes = images.file ? NeResource(images.file, images.length, 5, setup ? 0x603 : 0x602, size) : nullptr;
        converted = bytes ? new unsigned char[4 * size + 64] : nullptr;
        if (!converted || !app.ConvertDialogTemplate(bytes, size, converted, 4 * size + 64, length))
        {
            delete[] converted;
            converted = nullptr;
            error = 7;
            return 0;
        }
        return 1;
    }

    // Build and run one of the dialogs; the answer of DialogBoxIndirectParam.
    static unsigned Run(Application &app, unsigned pd, unsigned pd_far, bool setup, unsigned owner,
                                        unsigned instance, unsigned token, bool recorded = false)
    {
        unsigned char *converted = nullptr;
        unsigned length = 0, native = 0, error = 0;
        if (!Template(app, pd, setup, converted, length, native, error))
        {
            app.Win16CommonDialogError(error);
            return 0;
        }
        auto *held = new Win16PrintFrame(&app, setup);
        struct Holder { Win16PrintFrame *frame; ~Holder() { delete frame; } } release{held};
        auto &f = *held;
        f.pd = pd;
        f.pd_far = pd_far;
        f.scratch = app.Allocate(0x800);
        const unsigned help = app.String("commdlg_help");
        f.message_help = app.Api("RegisterWindowMessageA", &help);
        app.Free(help);
        unsigned hwnd = 0;
        const bool kept = recorded && app.PushHostFrame(HostWin16Print, 0, setup, 0, 2);
        f.native_template = native;
        if (native)
        {
            const unsigned locked = app.Api("GlobalLock", &native);
            const unsigned size = app.Api("GlobalSize", &native);
            auto *copy = locked && size ? new unsigned char[size] : nullptr;
            if (copy && app.Read(locked, copy, size))
            {
                if (kept) app.GiveTail(TailRecord);
                hwnd = app.CreateDialogTemplate(copy, size, owner, token, pd, true, instance);
            }
            delete[] copy;
            app.Api("GlobalUnlock", &native);
            app.Api("GlobalFree", &native);
            f.native_template = 0;
        }
        else
        {
            if (kept) app.GiveTail(TailRecord);
            hwnd = app.CreateDialogTemplate(converted, length, owner, token, pd, true, instance);
            delete[] converted;
        }
        f.hwnd = hwnd;
        if (kept)
        {
            auto &record = app.threads[app.current_thread].host_frames[app.threads[app.current_thread].host_frame_count - 1];
            record.a = hwnd; record.d = 0;
        }
        const unsigned answer = hwnd ? app.RunModal(hwnd, kept) : 0xffffffffu;
        if (kept) app.PopHostFrame();
        release.frame = nullptr;
        return app.FinishWin16PrintDialog(hwnd, setup, answer);

    }
};

void Application::Win16PrintDialogs::PrintCommand(unsigned hwnd, unsigned id, unsigned code, unsigned control, bool recorded)
{
    recorded = recorded && app.TakeTail() == TailRecord;
    switch (id)
    {
    case 1:
        if (PrintOk(hwnd))
            PrintEnd(hwnd, id, 1, control);
        return;
    case 2:
    {
        // a DEVNAMES COMMDLG made during this call goes again.
        const unsigned names = BlockMemory(Word(PdDevNames));
        if (names && (NameWord(names, 6) & DnMade))
        {
            FreeBlock(Word(PdDevNames));
            Word(PdDevNames, 0);
        }
        PrintEnd(hwnd, id, 0, control);
        return;
    }
    case 3:
        PrintEnd(hwnd, id, 0, control);
        return;
    case PrSetup:
    {
        const bool kept = recorded && app.PushHostFrame(HostWin16Print, hwnd, pd, f->pd_far, 1);
        ContinueSetup(hwnd, kept, 0, false);
        if (kept) app.PopHostFrame();
        return;
    }
    case PrHelp:
        if (f->message_help && Word(PdOwner))
        {
            Win16ControllerPhase help(app, HostWin16Print, hwnd, Win16ControllerPhase::OwnerHelp,
                                       0, 0, 0, recorded);
            help.Help(Word(PdOwner), f->message_help, hwnd, f->pd_far);
        }
        return;
    case PrAll:
    case PrSelection:
    case PrPages:
        Call("CheckRadioButton", hwnd, PrAll, PrPages, id);
        if (id == PrPages)
            Send(hwnd, 0x28, Item(hwnd, PrFrom), 1);
        return;
    case PrFrom:
    case PrTo:
        if (code == 0x300) // EN_CHANGE
            Call("CheckRadioButton", hwnd, PrAll, PrPages, PrPages);
        return;
    default:
        return;
    }
}

// finish an already running Print Setup, or start another one if
// the default printer changed under it (PDERR_DEFAULTDIFFERENT, 1009h).
void Application::Win16PrintDialogs::ContinueSetup(unsigned hwnd, bool recorded, unsigned answer, bool resumed)
{
    const unsigned instance = app.Win16SystemModule("COMMDLG");
    do
    {
        if (!resumed)
        {
            Error(0);
            answer = Run(app, pd, f->pd_far, true, hwnd, instance, Win16SetupDialogToken, recorded);
        }
        resumed = false;
        const unsigned error = ErrorValue();
        if (error == 0x100c || error == 0x1009)
        {
            FreeBlock(Word(PdDevNames));
            Word(PdDevNames, 0);
            Error(0x100c);
        }
    } while (ErrorValue() == 0x100c);
    Win16PrintFrame::init_hook = 0;
    if (answer && answer != 0xffffffffu) PrinterInfo(hwnd);
    Send(hwnd, 0x28, Item(hwnd, 1), 1);
}
unsigned Application::FinishWin16PrintDialog(unsigned hwnd, bool setup, unsigned answer)
{
    auto *frame = Win16PrintFrameOf(this, hwnd, setup);
    if (!frame) return answer;
    if (frame->native_template)
    {
        Api("GlobalUnlock", &frame->native_template);
        Api("GlobalFree", &frame->native_template);
        frame->native_template = 0;
    }
    if (frame->mode) Win16GlobalFree(frame->mode);
    if (frame->scratch) Free(frame->scratch);
    delete frame;
    return answer;
}
unsigned Application::FinishWin16PrintApi(unsigned pd, unsigned scratch, unsigned answer)
{
    Win16PrintFrame::init_hook = 0;
    if (answer == 0xffffffffu && state != Failed) Win16CommonDialogError(0xffff);
    Win16PrintDialogs e(*this, nullptr, pd);
    e.scratch = scratch;
    e.Hourglass(false);
    if (e.scratch) Free(e.scratch);
    return answer == 1;
}
unsigned Application::FinishWin16PrintSetup(unsigned hwnd, unsigned answer)
{
    auto *frame = Win16PrintFrameOf(this, hwnd, false);
    if (frame)
    {
        Win16PrintDialogs e(*this, frame, frame->pd);
        e.ContinueSetup(hwnd, true, answer, true);
    }
    PopHostFrame();
    return 0;
}
void Application::DiscardSavedWin16PrintDialogs()
{
    for (auto *f = Win16PrintFrame::head, *next = f; f; f = next)
    {
        next = f->next;
        if (f->app != this) continue;
        FinishWin16PrintDialog(f->hwnd, f->setup, 0);
    }
}
bool Application::Win16ChoiceDispatchResumable(unsigned hwnd, unsigned procedure, unsigned message, unsigned wp) const
{
    if (message == 0x110)
        return procedure == Win16ColorDialogToken || procedure == Win16FontDialogToken ||
               procedure == Win16PrintDialogToken || procedure == Win16SetupDialogToken;
    auto *app = const_cast<Application *>(this);
    if (procedure == Win16ColorDialogToken)
        for (auto *f = Win16ColorFrame::head; f; f = f->next)
            if (f->app == this && f->hwnd == hwnd && !f->initializing && f->ready &&
                ((f->hook && (app->Read32(f->cc + CcFlags) & CcEnableHook)) ||
                 (message == 0x111 && (wp & 65535) == 1 && app->Read32(f->cc + CcHook)) ||
                 (message == 0x111 && (wp & 65535) == PushHelp && f->message_help && app->Win16Word(f->cc + CcOwner)))) return true;
    if (procedure == Win16FontDialogToken)
        for (auto *f = Win16FontFrame::head; f; f = f->next)
            if (f->app == this && f->hwnd == hwnd && !f->initializing &&
                ((f->hook && (!f->ready || f->ended || (app->Read32(f->cf + CfFlags) & CfEnableHook))) ||
                 (f->ready && !f->ended && message == 0x111 &&
                  ((wp & 65535) == 1 || ((wp & 65535) == FontHelp && f->message_help && app->Win16Word(f->cf + CfOwner)))))) return true;
    if (procedure == Win16PrintDialogToken || procedure == Win16SetupDialogToken)
        for (auto *f = Win16PrintFrame::head; f; f = f->next)
            if (f->app == this && f->hwnd == hwnd && !f->initializing &&
                ((f->ready && !f->ended && app->Read32(f->pd + (f->setup ? PdSetupHook : PdPrintHook))) ||
                 ((!f->ready || f->ended) && Win16PrintFrame::init_hook) ||
                 (f->ready && !f->ended && message == 0x111 &&
                  ((wp & 65535) == 1 || ((wp & 65535) == PrHelp &&
                   f->message_help && app->Win16Word(f->pd + PdOwner)))))) return true;
    return procedure == Win16PrintDialogToken && message == 0x111 && (wp & 0xffff) == PrSetup;
}

// PrintDlg (COMMDLG.20).
bool Application::Win16PrintDialog(const Win16Frame &frame, unsigned &result)
{
    const bool gate = TakeFresh();
    result = 0;
    const unsigned pd = frame.Pointer(0);
    Win16PrintDialogs e(*this, nullptr, pd);
    e.Hourglass(true);
    if (!pd || Read32(pd) != PrintDialog16)
    {
        Win16CommonDialogError(ErrorStructSize);
        e.Hourglass(false);
        if (e.scratch)
            Free(e.scratch);
        return true;
    }
    // the pictures of the orientation (icons 210h and 211h of
    // COMMDLG, the first member of each group).
    const unsigned commdlg = Win16SystemModule("COMMDLG");
    auto icon = [&](unsigned id) {
        auto &images = CommDlgResources();
        unsigned size = 0, bytes = 0;
        const unsigned char *group = images.file ? NeResource(images.file, images.length, 14, id, size) : nullptr;
        if (!group || size < 20)
            return 0u;
        const unsigned member = unsigned(group[18] | group[19] << 8);
        const unsigned char *image = NeResource(images.file, images.length, 3, member, bytes);
        const unsigned block = image && bytes ? Allocate(bytes) : 0;
        if (!block)
            return 0u;
        Store(block, image, bytes);
        const unsigned a[] = {block, bytes, 1, 0x00030000, 0, 0, 0};
        const unsigned handle = Api("CreateIconFromResourceEx", a);
        Free(block);
        return handle;
    };
    if (!Win16PrintFrame::portrait)
        Win16PrintFrame::portrait = icon(0x210);
    if (!Win16PrintFrame::landscape)
        Win16PrintFrame::landscape = icon(0x211);
    auto done = [&] {
        e.Hourglass(false);
        if (e.scratch)
            Free(e.scratch);
        return state != Failed;
    };
    if (!Win16PrintFrame::portrait || !Win16PrintFrame::landscape)
    {
        Win16CommonDialogError(0x1001); // CDERR_INITIALIZATION
        return done();
    }
    if (e.Word(PdDevNames) && !e.CheckNames(e.Word(PdDevNames)))
        return done();
    Win16CommonDialogError(0);
    unsigned flags = e.Flags();
    if (flags & PdReturnDefault)
    {
        // the default printer without a dialog.
        if (e.Word(PdDevNames) || e.Word(PdDevMode))
        {
            Win16CommonDialogError(0x1003); // PDERR_RETDEFFAILURE
            return done();
        }
        const unsigned handle = e.DefaultNames();
        e.Word(PdDevNames, handle);
        if (!handle)
            return done();
        const unsigned names = e.BlockMemory(handle);
        if (!names)
        {
            Win16CommonDialogError(0xa);
            return done();
        }
        PrintDriver *driver = e.Driver(e.NameText(names, 0));
        if (!driver)
        {
            Win16CommonDialogError(0x1004);
            e.FreeBlock(handle);
            e.Word(PdDevNames, 0);
            return done();
        }
        unsigned mode = e.DeviceMode(e.Word(PdOwner), driver, e.Word(PdDevMode), names, false);
        if (mode == 0xffff)
            mode = 0;
        e.Word(PdDevMode, mode);
        e.ResultDc(names, mode ? e.BlockMemory(mode) : 0);
        result = 1;
        return done();
    }
    const bool setup = flags & PdPrintSetup;
    // The instance of the dialog: COMMDLG's, or with a hook the owner's (or
    // the template's hInstance).
    unsigned instance = commdlg;
    if (flags & (setup ? PdEnableSetupHook : PdEnablePrintHook))
    {
        Win16PrintFrame::init_hook = Read32(pd + (setup ? PdSetupHook : PdPrintHook));
        unsigned candidate = 0;
        if (e.Word(PdOwner))
        {
            unsigned word = 0;
            User::WindowLong(e.Word(PdOwner), -6, word, false);
            candidate = word & 0xffff;
        }
        else if ((flags & (setup ? PdEnableSetupTemplate : PdEnablePrintTemplate)) && e.Word(PdInstance))
            candidate = e.Word(PdInstance);
        if (candidate)
            instance = candidate;
    }
    if (instance != commdlg)
        if (auto *module = Win16ModuleOf(instance))
            if (module->image.ExpectedVersion() <= 0x30a)
                e.SetFlags(flags |= PdOldApplication);
    const bool kept = gate && PushHostFrame(HostWin16PrintApi, pd, e.scratch);
    const unsigned answer = Win16PrintDialogs::Run(*this, pd, frame.Word(1) << 16 | frame.Word(0), setup,
                                                e.Word(PdOwner), instance,
                                                setup ? Win16SetupDialogToken : Win16PrintDialogToken, kept);
    if (kept) PopHostFrame();
    result = FinishWin16PrintApi(pd, e.scratch, answer);
    return state != Failed;
}

// PrintDlgProc and PrnSetupDlgProc.
unsigned Application::Win16PrintDialogMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp, bool setup)
{
    const unsigned tail = TakeTail();
    auto *frame = Win16PrintFrameOf(this, hwnd, setup);
    if (!frame)
        return 0;
    Win16PrintDialogs e(*this, frame, frame->pd);
    auto &f = *frame;
    const bool original_live = f.ready && !f.ended;
    bool resumed_init = false;
    auto &thread = threads[current_thread];
    if (message == 0x110 && thread.host_resume && thread.host_resume_at < thread.host_frame_count)
    {
        const auto &record = thread.host_frames[thread.host_resume_at];
        if (record.kind == HostWin16Print && record.a == hwnd && record.b == (0x80000000u | message))
        {
            resumed_init = true;
            ++thread.host_resume_at;
        }
    }
    Win16ControllerPhase phase(*this, HostWin16Print, hwnd, Win16ControllerPhase::Message,
                               message, wp, lp, message != 0x110 && tail == TailRecord && !f.initializing,
                               original_live);
    const bool live = phase.Flags(original_live) != 0;
    if (phase.AfterHook()) return phase.Rest();
    const unsigned hook_offset = setup ? PdSetupHook : PdPrintHook;
    if (!resumed_init && (phase.resumed || live || (Win16PrintFrame::init_hook && message != 0x110)))
    {
        const unsigned far_hook = live ? Read32(f.pd + hook_offset) : Win16PrintFrame::init_hook;
        const unsigned mark = far_hook ? Win16Mark(far_hook) : 0;
        const unsigned arguments[] = {hwnd, message, wp, lp};
        if (const unsigned answer = phase.Callback(mark, arguments, 4)) return answer;
    }
    phase.Continue();
    e.recorded = phase.kept;
    switch (message)
    {
    case 0x2c: // WM_MEASUREITEM
        if (setup)
            e.MeasureItem(hwnd, lp);
        return setup ? 1 : 0;
    case 0x53: // WM_HELP
        if (!live || (e.Flags() & PdOldApplication))
            return setup ? 1 : 0;
        if (setup)
            e.Help(Read32(lp + 12), 0xc, SetupHelpTable, sizeof SetupHelpTable);
        else
            e.Help(Read32(lp + 12), 0xc, PrintHelpTable, sizeof PrintHelpTable);
        return setup ? 1 : 0;
    case 0x7b: // WM_CONTEXTMENU
        e.Help(wp, 0xa, setup ? SetupHelpTable : PrintHelpTable,
               setup ? sizeof SetupHelpTable : sizeof PrintHelpTable);
        return setup ? 1 : 0;
    case 0x110: // WM_INITDIALOG
    {
        Win16PrintFrame::init_hook = 0;
        const bool pushed = resumed_init || (tail == TailRecord &&
            PushHostFrame(HostWin16Print, hwnd, 0x80000000u | message, wp, lp));
        struct Record { Application &app; bool pushed; ~Record() { if(pushed) app.PopHostFrame(); } } record{*this, pushed};
        if (!resumed_init) f.initializing = true;
        const unsigned answer = resumed_init ? ResumeInner() & 0xffff :
            setup ? e.SetupInitialize(hwnd, wp, pushed) : e.PrintInitialize(hwnd, wp, pushed);
        e.Hourglass(false);
        return f.ended ? 0 : answer;
    }
    case 0x111: // WM_COMMAND
        if (!live)
            return 0;
        if (setup)
            return e.SetupCommand(hwnd, wp & 0xffff, wp >> 16, lp) ? 1 : 0;
        if (phase.kept || tail == TailRecord) GiveTail(TailRecord);
        e.PrintCommand(hwnd, wp & 0xffff, wp >> 16, lp, phase.kept || tail == TailRecord);
        return 0;
    default:
        return 0;
    }
}

// The subclass of From, To and Copies: only digits and backspace.
// The subclass of the printer list: Up and Left on a closed list go
// to "Default printer".
unsigned Application::Win16PrintFieldMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp, bool list)
{
    if (!list && message == 0x102)
    {
        const unsigned key = wp & 0xffff;
        if (key != 8 && (key < '0' || key > '9'))
        {
            const unsigned zero = 0;
            Api("MessageBeep", &zero);
            return 0;
        }
    }
    if (list && message == 0x100 && ((wp & 0xffff) == 0x26 || (wp & 0xffff) == 0x25))
    {
        const unsigned a[] = {hwnd, 0x157, 0, 0};
        if (!Api("SendMessageA", a))
        {
            const unsigned parent = User::Parent(hwnd);
            const unsigned next[] = {parent, 0x28, User::GetDlgItem(parent, SuDefault), 1};
            Api("SendMessageA", next);
            const unsigned radio[] = {parent, SuDefault, SuSpecific, SuDefault};
            Api("CheckRadioButton", radio);
            return 0;
        }
    }
    const unsigned previous = list ? Win16PrintFrame::list_procedure : Win16PrintFrame::digits_procedure;
    if (previous && previous != (list ? Win16PrinterListToken : Win16DigitsToken))
    {
        const unsigned a[] = {previous, hwnd, message, wp, lp};
        return Api("CallWindowProcA", a);
    }
    return list ? ComboBoxMessage(hwnd, message, wp, lp) : Edit(hwnd, message, wp, lp);
}
