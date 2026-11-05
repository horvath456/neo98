// SPDX-License-Identifier: GPL-2.0-or-later
// The installable drivers of WINMM: OpenDriver, SendDriverMessage,
// CloseDriver, GetDriverModuleHandle and GetDriverFlags, and the Drv forms
// the linker folded onto them. The video codecs of [drivers32] (ICCVID.DLL,
// IR32_32.DLL, MSVIDC32.DLL, MSRLE32.DLL and the rest) are opened this way:
// MSVFW32 7963_1FE1 calls OpenDriver with the file name the [drivers32] line
// gives, takes the module with GetDriverModuleHandle and calls the codec's
// DriverProc itself.
// The places in WINMM.DLL of the tree:
//  OpenDriver (wide) and OpenDriverA: the worker
// with the flag for the wide form.
//  probes both names with IsBadStringPtr for 104h characters,
// gives the driver a table entry and sends DRV_OPEN
// with the rest of the SYSTEM.INI line as a wide string in
// lParam1 and the caller's lParam in lParam2. The entry's
// driver identifier is the handle until DRV_OPEN answers, and its
// answer afterwards. Zero from DRV_OPEN closes the entry again.
//  looks up the name in SYSTEM.INI (: section "drivers"
// when the caller names none, the name itself as the default, the
// file ends at the first blank and the rest are the parameters),
// loads the file with LoadLibraryA under SetErrorMode(1), takes
// "DriverProc", and sends DRV_LOAD and DRV_ENABLE when this is the
// first entry of the module in the process. A file LoadLibraryA
// cannot load goes to the 16-bit driver list.
//  SendDriverMessage: the handle is an index into the table,
// one-based; the entry must be in use and belong to the calling
// process. The call is DriverProc(identifier, handle, message,
// lParam1, lParam2).
//  CloseDriver: DRV_CLOSE, then : DRV_DISABLE and DRV_FREE
// for the last entry of the module, FreeLibrary for every entry.
//  GetDriverModuleHandle, also DrvGetModuleHandle: the module of a
// 32-bit entry in use, else zero.
//  GetDriverFlags: zero for an entry not in use, else 80000000h
// for a 16-bit driver and 01000000h for one opened by the wide
// form.
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned DrvLoad = 1, DrvEnable = 2, DrvOpen = 3, DrvClose = 4, DrvDisable = 5, DrvFree = 6;
// The flag word of an entry at +14h.
constexpr unsigned short InUse = 1, SixteenBit = 2, Wide = 4;
} // namespace

Application::InstallableDriver *Application::InstallableDriverOf(unsigned handle)
{
    if (!handle || handle > sizeof installable_drivers / sizeof installable_drivers[0])
        return nullptr;
    InstallableDriver &entry = installable_drivers[handle - 1];
    return entry.flags & InUse ? &entry : nullptr;
}

unsigned Application::InstallableDriverSend(unsigned handle, unsigned message, unsigned first, unsigned second)
{
    InstallableDriver *entry = InstallableDriverOf(handle);
    if (!entry || entry->process != ProcessId())
        return 0;
    if (entry->flags & SixteenBit)
    {
        Fail("SendDriverMessage to a 16-bit installable driver is not built");
        return 0;
    }
    const unsigned arguments[] = {entry->driver_id, handle, message, first, second};
    return Call(entry->procedure, arguments, 5);
}

// take an entry out of the table. The last entry of its module in
// the process sends DRV_DISABLE (when asked to) and DRV_FREE first; every
// entry frees its reference on the module.
unsigned Application::InstallableDriverRelease(unsigned handle, bool disable)
{
    InstallableDriver *entry = InstallableDriverOf(handle);
    if (!entry || entry->flags & SixteenBit)
        return 0;
    unsigned sharing = 0;
    for (auto &other : installable_drivers)
        if (other.flags & InUse && other.module == entry->module && other.process == entry->process)
        {
            ++sharing;
            --other.usage;
        }
    const unsigned module = entry->module;
    entry->driver_id = 0;
    if (sharing == 1)
    {
        if (disable)
            InstallableDriverSend(handle, DrvDisable, 0, 0);
        InstallableDriverSend(handle, DrvFree, 0, 0);
    }
    const unsigned free[] = {module};
    Api("FreeLibrary", free);
    *entry = {};
    return sharing - 1;
}

unsigned Application::InstallableDriverOpen(const unsigned *a, bool wide)
{
    const unsigned name = a[0], section = a[1], parameter = a[2];
    // The names as ANSI strings; the wide form is probed and narrowed first.
    char name_text[0x105] = {}, section_text[0x105] = {};
    auto narrow = [&](unsigned address, char *out) {
        const unsigned probe[] = {address, 0x104};
        if (Api(wide ? "IsBadStringPtrW" : "IsBadStringPtrA", probe))
            return false;
        if (wide)
        {
            unsigned i = 0;
            for (; i < 0x104; ++i)
            {
                unsigned short c = 0;
                if (!Read(address + i * 2, &c, 2) || !c)
                    break;
                out[i] = c < 0x100 ? char(c) : '?';
            }
            out[i] = 0;
        }
        else
            snprintf(out, 0x105, "%s", ReadString(address));
        return true;
    };
    if (!name || !narrow(name, name_text))
        return 0;
    if (section && !narrow(section, section_text))
        return 0;
    InstallableDriver *entry = nullptr;
    unsigned handle = 0;
    for (unsigned i = 0; i < sizeof installable_drivers / sizeof installable_drivers[0]; ++i)
        if (!(installable_drivers[i].flags & InUse))
        {
            entry = &installable_drivers[i];
            handle = i + 1;
            break;
        }
    if (!entry)
    {
        Fail("WINMM's table of installable drivers is full (64 entries)");
        return 0;
    }
    // GetPrivateProfileStringA(section or "drivers", name, name,
    // line, 184h, "system.ini"); the file ends at the first blank.
    const unsigned block = Allocate(0x500);
    if (!block)
        return 0;
    const unsigned section_at = block, name_at = block + 0x108, line_at = block + 0x210;
    const unsigned wide_at = block + 0x3a0, ini_at = block + 0x4a0;
    PutString(section_at, section_text[0] ? section_text : "drivers");
    PutString(name_at, name_text);
    PutString(ini_at, "system.ini");
    const unsigned query[] = {section_at, name_at, name_at, line_at, 0x184, ini_at};
    Api("GetPrivateProfileStringA", query);
    char line[0x185] = {};
    snprintf(line, sizeof line, "%s", ReadString(line_at));
    char *parameters = line + strlen(line);
    for (char *p = line; *p; ++p)
        if (*p == ' ')
        {
            *p = 0;
            parameters = p + 1;
            break;
        }
    PutString(line_at, line);
    const unsigned quiet[] = {1};
    const unsigned mode = Api("SetErrorMode", quiet);
    const unsigned load[] = {line_at};
    const unsigned module = Api("LoadLibraryA", load);
    const unsigned error = module ? 0 : Api("GetLastError", nullptr);
    const unsigned restore[] = {mode};
    Api("SetErrorMode", restore);
    if (!module)
    {
        //  hands the name to the 16-bit driver list of MMSYSTEM. A
        // file that is there but is no Win32 image is such a driver.
        if (error == 11 || error == 193)
        {
            char text[320];
            snprintf(text, sizeof text,
                     "OpenDriver of the 16-bit installable driver %s is not built", line);
            Fail(text);
        }
        Free(block);
        return 0;
    }
    PutString(name_at, "DriverProc");
    const unsigned lookup[] = {module, name_at};
    const unsigned procedure = Api("GetProcAddress", lookup);
    if (!procedure)
    {
        const unsigned free[] = {module};
        Api("FreeLibrary", free);
        Free(block);
        return 0;
    }
    *entry = {};
    entry->module = module;
    entry->process = ProcessId();
    entry->procedure = procedure;
    entry->flags = InUse | (wide ? Wide : 0);
    unsigned short usage = 0;
    for (auto &other : installable_drivers)
        if (other.flags & InUse && other.module == module && other.process == entry->process)
            ++usage;
    entry->usage = usage;
    if (usage == 1)
    {
        if (!InstallableDriverSend(handle, DrvLoad, 0, 0))
        {
            const unsigned free[] = {module};
            Api("FreeLibrary", free);
            *entry = {};
            Free(block);
            return 0;
        }
        InstallableDriverSend(handle, DrvEnable, 0, 0);
    }
    // DRV_OPEN: the parameters as a wide string of at most 80h characters.
    for (unsigned i = 0; i < 0x80; ++i)
    {
        const unsigned short c = (unsigned char)parameters[i];
        Store(wide_at + i * 2, &c, 2);
        if (!c)
            break;
    }
    const unsigned short end = 0;
    Store(wide_at + 0xfe, &end, 2);
    entry->driver_id = handle;
    const unsigned identifier = InstallableDriverSend(handle, DrvOpen, wide_at, parameter);
    Free(block);
    if (!identifier)
    {
        InstallableDriverRelease(handle, true);
        return 0;
    }
    if (InstallableDriver *opened = InstallableDriverOf(handle))
        opened->driver_id = identifier;
    return handle;
}

unsigned Application::InstallableDriverApi(ApiName name, const unsigned *a)
{
#define API(n) case API_ID(#n):
    switch (name.id)
    {
    API(OpenDriver) API(DrvOpen)
        return InstallableDriverOpen(a, true);
    API(OpenDriverA) API(DrvOpenA)
        return InstallableDriverOpen(a, false);
    API(SendDriverMessage) API(DrvSendMessage)
        return InstallableDriverSend(a[0], a[1], a[2], a[3]);
    API(CloseDriver) API(DrvClose)
    {
        InstallableDriver *entry = InstallableDriverOf(a[0]);
        if (!entry)
            return 0;
        if (entry->flags & SixteenBit)
        {
            Fail("CloseDriver of a 16-bit installable driver is not built");
            return 0;
        }
        const unsigned result = InstallableDriverSend(a[0], DrvClose, a[1], a[2]);
        InstallableDriverRelease(a[0], true);
        return result;
    }
    API(GetDriverModuleHandle) API(DrvGetModuleHandle)
    {
        const InstallableDriver *entry = InstallableDriverOf(a[0]);
        return entry && !(entry->flags & SixteenBit) ? entry->module : 0;
    }
    API(GetDriverFlags)
    {
        const InstallableDriver *entry = InstallableDriverOf(a[0]);
        if (!entry)
            return 0;
        return (entry->flags & SixteenBit ? 0x80000000u : 0) | (entry->flags & Wide ? 0x1000000u : 0);
    }
    default:
        break;
    }
    Fail(name);
    return 0;
#undef API
}
} // namespace X86
