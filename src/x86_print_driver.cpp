// SPDX-License-Identifier: GPL-2.0-or-later
// The printer half of GDI.EXE: a 16-bit printer driver loaded into the
// process that makes the printer DC, and its device driver interface (DDI).
// CreateDC for a driver that is not the display goes through GDI:
// the driver name "WINSPOOL" is replaced by the driver of the printer
// (registry Printers\<device>\"Printer Driver" and
// Environments\Windows 4.0\Drivers\<model>\"Driver"), Windows 98 loads
// <driver>.DRV with LoadLibrary and fills the driver block with the DDI
// entries by ordinal, Windows 98 calls Enable twice ( for GDIINFO,
// for the PDEVICE in a block of dpDEVICEsize bytes), and hands the
// driver the DC with Control(SETABORTPROC). Every later operation on the DC
// is a far call into the driver with the PDEVICE as its first argument.
// The driver is the original 16-bit code of the Win98 SE CD (PSCRIPT.DRV);
// the far calls go through src/x86_win16_hosted.cpp.
#include "x86_print.h"
#include "gdi.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
using namespace Print;
namespace
{
bool SameText(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
        if (!x) return true;
    }
}
void Copy(char *out, const char *in, unsigned capacity)
{
    unsigned n = 0;
    if (in)
        for (; in[n] && n + 1 < capacity; ++n)
            out[n] = in[n];
    out[n] = 0;
}
} // namespace

Application::PrintBlock Application::PrintAllocate(unsigned bytes)
{
    PrintBlock block;
    if (!Win16HostedReady())
        return block;
    // GMEM_FIXED | GMEM_ZEROINIT; a fixed block's selector is the handle.
    block.handle = Win16GlobalAlloc(0x40, bytes);
    if (Win16Block *b = block.handle ? Win16GlobalBlock(block.handle) : nullptr)
    {
        block.selector = b->selector;
        block.memory = b->memory;
        block.size = bytes;
    }
    else
        block = {};
    return block;
}
void Application::PrintRelease(PrintBlock &block)
{
    if (block.handle)
        Win16GlobalFree(block.handle);
    block = {};
}

// A value of the printer's registry keys under
// HKLM\System\CurrentControlSet\Control\Print.
bool Application::PrintRegistryString(const char *path, const char *value, char *out, unsigned capacity)
{
    out[0] = 0;
    const unsigned text = Allocate(600);
    if (!text)
        return false;
    char full[300];
    snprintf(full, sizeof full, "System\\CurrentControlSet\\Control\\Print\\%s", path);
    PutString(text, full);
    PutString(text + 300, value);
    const unsigned open[] = {0x80000002u, text, 0, 0x20019, text + 400};
    bool found = false;
    if (!Api("RegOpenKeyExA", open))
    {
        const unsigned key = Read32(text + 400);
        Write32(text + 404, 256);
        const unsigned query[] = {key, text + 300, 0, 0, text + 408, text + 404};
        if (!Api("RegQueryValueExA", query))
        {
            Copy(out, ReadString(text + 408), capacity);
            found = true;
        }
        const unsigned close[] = {key};
        Api("RegCloseKey", close);
    }
    Free(text);
    return found;
}

Application::PrintDriver *Application::PrintLoadDriver(const char *driver_name, unsigned &error)
{
    if (!print_host)
    {
        print_host = new PrintHost;
        if (!print_host)
        {
            error = 8;
            return nullptr;
        }
    }
    char bare[16];
    Copy(bare, driver_name, sizeof bare);
    for (unsigned i = 0; bare[i]; ++i)
        if (bare[i] == '.')
            bare[i] = 0;
    for (auto &driver : print_host->drivers)
        if (driver.module && SameText(driver.name, bare))
        {
            ++driver.references;
            return &driver;
        }
    PrintDriver *slot = nullptr;
    for (auto &driver : print_host->drivers)
        if (!driver.module)
        {
            slot = &driver;
            break;
        }
    if (!slot)
    {
        error = 8;
        return nullptr;
    }
    // LoadLibrary of KRNL386 with the name GDI built, "<driver>.drv": the
    // Windows and the system directory.
    char path[260];
    unsigned module = 0;
    const char *roots[] = {"C:\\WINDOWS\\SYSTEM", "C:\\WINDOWS"};
    for (const char *root : roots)
    {
        snprintf(path, sizeof path, "%s\\%s.DRV", root, bare);
        module = Win16HostedLibrary(path, error);
        if (module || (error != 2 && error != 3))
            break;
    }
    if (!module)
    {
        if (log)
        {
            char text[320];
            snprintf(text, sizeof text, "PRINT driver %s.DRV not loaded, error %u", bare, error);
            log(text);
        }
        return nullptr;
    }
    *slot = {};
    slot->module = module;
    slot->instance = Win16HostedInstance(module);
    slot->references = 1;
    Copy(slot->name, bare, sizeof slot->name);
    for (unsigned ordinal = 1; ordinal <= DdiEntries; ++ordinal)
        slot->entry[ordinal] = Win16HostedProcedure(module, ordinal);
    slot->extended_mode = Win16HostedProcedure(module, DdiExtDeviceMode);
    slot->capabilities = Win16HostedProcedure(module, DdiDeviceCapabilities);
    return slot;
}

// A far call of one DDI entry. `push` in the order of the Pascal prototype;
// a far pointer or a DWORD is two words, the high one first.
unsigned Application::PrintCall(PrintDriver &driver, unsigned ordinal, const unsigned *push, unsigned count,
                                bool &ok)
{
    ok = false;
    const unsigned entry = ordinal <= DdiEntries ? driver.entry[ordinal]
                           : ordinal == DdiExtDeviceMode ? driver.extended_mode
                           : ordinal == DdiDeviceCapabilities ? driver.capabilities
                                                              : 0;
    if (!entry)
        return 0;
    const unsigned result = Win16HostedCall(entry, push, count, driver.instance, nullptr, &ok);
    return result;
}

Application::PrintDc *Application::PrintFind(unsigned hdc)
{
    if (!print_host || !hdc)
        return nullptr;
    for (auto *dc : print_host->dcs)
        if (dc && dc->hdc == hdc)
            return dc;
    return nullptr;
}
bool Application::PrinterDC(unsigned hdc) { return PrintFind(hdc) != nullptr; }

// GDI32: GetDeviceCaps 110 to 115 through escapes 12 to 14.
int Application::PrintEscapePair(void *context, unsigned number, unsigned (&pair)[2])
{
    auto *dc = static_cast<Application::PrintDc *>(context);
    return dc ? dc->owner->PrintControlPair(*dc, number, pair) : 0;
}

// GDI, CreateDC and CreateIC for a device that is not the display.
unsigned Application::PrinterCreateDC(unsigned driver_text, unsigned device_text, unsigned output_text,
                                      unsigned mode, bool information)
{
    char driver_name[64] = {}, device[64] = {}, port[128] = {};
    Copy(driver_name, driver_text ? ReadString(driver_text) : "", sizeof driver_name);
    Copy(device, device_text ? ReadString(device_text) : "", sizeof device);
    Copy(port, output_text ? ReadString(output_text) : "", sizeof port);
    // COMDLG32 hands out "winspool" as the driver of every printer. GDI32 has already replaced it with the driver of
    // the [devices] line of WIN.INI for the device, the line the "Add
    // Printer" wizard writes, so GDI.EXE sees the driver file name here.
    // no driver name, or a LoadLibrary result below 32, ends in the
    // common exit, which frees the DC and returns null. Neither GDI nor
    // the loader of KRNL386 calls SetLastError on this path (KERNEL.147 has no
    // caller in LoadModule), so the thread's error stays what it was.
    if (!driver_name[0])
        return 0;
    unsigned error = 0;
    PrintDriver *driver = PrintLoadDriver(driver_name, error);
    if (!driver)
        return 0;
    auto *dc = new PrintDc;
    dc->owner = this;
    dc->driver = driver;
    dc->information = information;
    Copy(dc->device, device, sizeof dc->device);
    Copy(dc->port, port, sizeof dc->port);
    auto fail = [&](unsigned code) {
        PrintRelease(dc->gdiinfo);
        PrintRelease(dc->pdevice);
        PrintRelease(dc->strings);
        PrintRelease(dc->devmode);
        PrintRelease(dc->scratch);
        if (driver->references)
            --driver->references;
        delete dc;
        // Windows 98 frees the DC and returns null without SetLastError.
        (void)code;
        return 0u;
    };
    dc->strings = PrintAllocate(0x200);
    dc->gdiinfo = PrintAllocate(0x100);
    dc->scratch = PrintAllocate(0x400);
    if (!dc->strings.selector || !dc->gdiinfo.selector || !dc->scratch.selector)
        return fail(8);
    Store(dc->strings.memory, device, strlen(device) + 1);
    Store(dc->strings.memory + 0x40, port, strlen(port) + 1);
    // The DEVMODE the program handed in goes to the driver as lpData.
    unsigned data = 0;
    if (mode)
    {
        unsigned short sizes[2] = {};
        Read(mode + 36, sizes, sizeof sizes);
        const unsigned bytes = sizes[0] + sizes[1];
        if (bytes >= 36 && bytes < 0x2000)
        {
            dc->devmode = PrintAllocate(bytes);
            unsigned char copy[0x2000];
            if (dc->devmode.selector && Read(mode, copy, bytes))
            {
                Store(dc->devmode.memory, copy, bytes);
                data = dc->devmode.Far();
            }
        }
    }
    const unsigned device_far = dc->strings.Far(0), port_far = dc->strings.Far(0x40);
    // Enable pass 1: GDIINFO. The IC bit is 8000h of wStyle.
    const unsigned style = information ? 0x8000u : 0u;
    bool ok = false;
    {
        const unsigned push[] = {dc->gdiinfo.selector, 0, 1 | style, device_far >> 16, device_far & 0xffff,
                                 port_far >> 16, port_far & 0xffff, data >> 16, data & 0xffff};
        const unsigned size = PrintCall(*driver, DdiEnable, push, 9, ok) & 0xffff;
        if (!ok)
            return fail(8);
        unsigned char info[0x100] = {};
        Read(dc->gdiinfo.memory, info, sizeof info);
        const unsigned version = info[0] | info[1] << 8;
        if (!size || (version >= 0x300 ? size < 0x6e : size != 0x68))
            return fail(1797);
    }
    unsigned char info[0x100] = {};
    Read(dc->gdiinfo.memory, info, sizeof info);
    const unsigned device_size = info[0x1a] | info[0x1b] << 8;
    // the PDEVICE, GlobalAlloc(2002h, dpDEVICEsize), and Enable pass 2.
    dc->pdevice = PrintAllocate(device_size ? device_size : 2);
    if (!dc->pdevice.selector)
        return fail(8);
    {
        const unsigned push[] = {dc->pdevice.selector, 0, style, device_far >> 16, device_far & 0xffff,
                                 port_far >> 16, port_far & 0xffff, data >> 16, data & 0xffff};
        const unsigned done = PrintCall(*driver, DdiEnable, push, 9, ok) & 0xffff;
        if (!ok || !done)
            return fail(1797);
    }
    // sizes and resolutions must not be zero.
    auto word = [&](unsigned at) { return unsigned(info[at] | info[at + 1] << 8); };
    if (!word(4) || !word(6) || !word(8) || !word(10) || !word(0x58) || !word(0x5a))
    {
        const unsigned push[] = {dc->pdevice.selector, 0};
        PrintCall(*driver, DdiDisable, push, 2, ok);
        return fail(1797);
    }
    Gdi::NarrowHandles(true);
    const unsigned hdc = Gdi::CreateDeviceDC(info, 0x100, information);
    Gdi::NarrowHandles(false);
    if (!hdc)
    {
        const unsigned push[] = {dc->pdevice.selector, 0};
        PrintCall(*driver, DdiDisable, push, 2, ok);
        return fail(8);
    }
    dc->hdc = hdc;
    Gdi::SetDeviceEscape(hdc, dc, PrintEscapePair);
    PrintInstallHooks(*dc);
    // A new DC of GDI.EXE has the white brush and the black pen selected, and its default font is DEVICE_DEFAULT_FONT: the
    // routine that puts a DC back to its defaults selects the
    // stock pen [0x43A], brush [0x42C], palette [0x44A] and font [0x448],
    // and [0x448] is the font creates from the LOGFONT :
    // height 0, FIXED_PITCH, the system character set, face "Courier New".
    // On a printer the height 0 lets the driver pick its default size, so
    // text drawn before any font is selected (Notepad's header and footer
    // take their height from it, NOTEPAD 0x403E1C) comes out readable.
    // This port's stock DEVICE_DEFAULT_FONT is the System font, which is
    // what the display shows for it; the printer DC gets its own font object
    // with the original LOGFONT instead.
    static const unsigned stock_objects[] = {0, 7};
    for (const unsigned stock : stock_objects)
    {
        const unsigned get[] = {stock};
        const unsigned object = GdiApi("GetStockObject", get);
        const unsigned select[] = {hdc, object};
        if (object)
            GdiApi("SelectObject", select);
    }
    if (const unsigned logfont = Allocate(60))
    {
        unsigned char record[60] = {};
        record[27] = 1; // FIXED_PITCH
        memcpy(record + 28, "Courier New", 12);
        Store(logfont, record, sizeof record);
        const unsigned create[] = {logfont};
        dc->default_font = Api("CreateFontIndirectA", create);
        Free(logfont);
        const unsigned select[] = {hdc, dc->default_font};
        if (dc->default_font)
            GdiApi("SelectObject", select);
    }
    unsigned slot = 16;
    for (unsigned i = 0; i < 16; ++i)
        if (!print_host->dcs[i])
        {
            slot = i;
            break;
        }
    if (slot == 16)
    {
        Gdi::DeleteDC(hdc);
        const unsigned push[] = {dc->pdevice.selector, 0};
        PrintCall(*driver, DdiDisable, push, 2, ok);
        return fail(8);
    }
    print_host->dcs[slot] = dc;
    // a printer DC, not an IC and not the display technology, tells
    // the driver its handle with Control(SETABORTPROC, &hDC, NULL).
    if (!information && word(2) != 0)
    {
        Win16Word(dc->scratch.memory, hdc & 0xffff);
        const unsigned push[] = {dc->pdevice.selector, 0, 9, dc->scratch.selector, 0, 0, 0};
        PrintCall(*driver, DdiControl, push, 7, ok);
    }
    if (log)
    {
        char text[320];
        snprintf(text, sizeof text, "PRINT dc %08x driver %s device \"%s\" port \"%s\" res %ux%u dpi %ux%u "
                 "curves %04x lines %04x polygons %04x text %04x raster %04x", hdc,
                 driver->name, device, port, word(8), word(10), word(0x58), word(0x5a), word(0x1c), word(0x1e),
                 word(0x20), word(0x22), word(0x26));
        log(text);
    }
    return hdc;
}

// DeleteDC of a printer DC: Disable, then the blocks.
bool Application::PrinterDeleteDC(unsigned hdc)
{
    PrintDc *dc = PrintFind(hdc);
    if (!dc)
        return false;
    PrintReleaseObjects(*dc);
    if (dc->default_font)
    {
        const unsigned remove[] = {dc->default_font};
        Gdi::SelectObject(hdc, stock[13]);
        Api("DeleteObject", remove);
        dc->default_font = 0;
    }
    bool ok = false;
    const unsigned push[] = {dc->pdevice.selector, 0};
    PrintCall(*dc->driver, DdiDisable, push, 2, ok);
    for (auto *&slot : print_host->dcs)
        if (slot == dc)
            slot = nullptr;
    Gdi::DeleteDC(hdc);
    PrintRelease(dc->gdiinfo);
    PrintRelease(dc->pdevice);
    PrintRelease(dc->strings);
    PrintRelease(dc->devmode);
    PrintRelease(dc->scratch);
    if (dc->driver->references)
        --dc->driver->references;
    delete dc;
    return true;
}

// Control(lpPDevice, function, lpInData, lpOutData) with two words out.
int Application::PrintControlPair(PrintDc &dc, unsigned function, unsigned (&pair)[2])
{
    bool ok = false;
    Win16Word(dc.scratch.memory + 0x10, 0);
    Win16Word(dc.scratch.memory + 0x12, 0);
    const unsigned push[] = {dc.pdevice.selector, 0, function, 0, 0, dc.scratch.selector, 0x10};
    const int result = short(PrintCall(*dc.driver, DdiControl, push, 7, ok) & 0xffff);
    if (!ok)
        return 0;
    pair[0] = short(Win16Word(dc.scratch.memory + 0x10));
    pair[1] = short(Win16Word(dc.scratch.memory + 0x12));
    return result;
}

void Application::FreePrintHost()
{
    if (!print_host)
        return;
    for (auto *&dc : print_host->dcs)
    {
        delete dc;
        dc = nullptr;
    }
    delete print_host;
    print_host = nullptr;
}
} // namespace X86
