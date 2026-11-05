// SPDX-License-Identifier: GPL-2.0-or-later
// The installable drivers of the 16-bit side: OpenDriver, SendDriverMessage,
// CloseDriver, GetDriverModuleHandle and DefDriverProc of USER.EXE, ordinals
// 251 to 255. MMSYSTEM opens its MCI drivers through them (MMSYSTEM), MSVIDEO its 16-bit codecs. The drivers are original 16-bit
// code, hosted in the process that opens them (src/x86_win16_hosted.cpp); what
// is ours is USER's table.
// The places in USER.EXE of the tree:
// OpenDriver(lpDriverName, lpSectionName, lParam2), six words.
// the worker: the driver gets an entry of 90h bytes, then
// DRV_OPEN with lParam1 the far address of the parameters that
// follow the file name on its SYSTEM.INI line and lParam2 the
// caller's; zero from DRV_OPEN frees the entry again.
// The handle is the one-based index of the entry.
// looks the name up in SYSTEM.INI (section "drivers" when the
// caller names none), loads the file with LoadLibrary, takes
// "DriverProc" with GetProcAddress and keeps the module, the
// procedure, the driver identifier and the name. The first entry of
// a module sends DRV_LOAD and DRV_ENABLE.
// SendDriverMessage(hDriver, wMessage, lParam1, lParam2): the call
// DriverProc(dwDriverID, hDriver, wMessage, lParam1, lParam2).
// CloseDriver(hDriver, lParam1, lParam2): DRV_CLOSE, and for the
// last entry of its module DRV_DISABLE and DRV_FREE and FreeLibrary.
// GetDriverModuleHandle(hDriver).
// DefDriverProc: one for DRV_LOAD, DRV_ENABLE, DRV_CLOSE,
// DRV_DISABLE, DRV_FREE, DRV_INSTALL, DRV_REMOVE, zero otherwise.
#include "win16_machine.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
namespace X86
{
namespace
{
constexpr unsigned DrvLoad = 1, DrvEnable = 2, DrvOpen = 3, DrvClose = 4, DrvDisable = 5, DrvFree = 6;
} // namespace

Application::Win16Driver *Application::Win16DriverOf(unsigned handle)
{
    if (!handle || handle > sizeof win16_drivers / sizeof win16_drivers[0])
        return nullptr;
    Win16Driver &entry = win16_drivers[handle - 1];
    return entry.module || entry.camera ? &entry : nullptr;
}

// DriverProc(dwDriverID, hDriver, wMessage, lParam1, lParam2), Pascal order.
unsigned Application::Win16SendDriverMessage(unsigned handle, unsigned message, unsigned first, unsigned second)
{
    Win16Driver *entry = Win16DriverOf(handle);
    if (entry && entry->camera)
        return Win16CaptureMessage(*entry, message & 0xffff, first, second);
    if (!entry || !entry->procedure)
        return 0;
    const unsigned push[] = {entry->driver_id >> 16, entry->driver_id & 0xffff, handle, message & 0xffff,
                             first >> 16,           first & 0xffff,             second >> 16, second & 0xffff};
    bool returned = false;
    const unsigned result = Win16HostedCall(entry->procedure, push, 8, Win16HostedInstance(entry->module), nullptr,
                                            &returned);
    return returned ? result : 0;
}

unsigned Application::Win16OpenDriver(const char *name, const char *section, unsigned second)
{
    if (!name || !*name || !Win16HostedReady())
        return 0;
    unsigned handle = 0;
    for (unsigned i = 0; i < sizeof win16_drivers / sizeof win16_drivers[0]; ++i)
        if (!win16_drivers[i].module && !win16_drivers[i].camera)
        {
            handle = i + 1;
            break;
        }
    if (!handle)
    {
        Fail("USER's table of 16-bit installable drivers is full (16 entries)");
        return 0;
    }
    // the SYSTEM.INI line, the file up to the first blank and the
    // parameters behind it.
    const unsigned block = Allocate(0x300);
    if (!block)
        return 0;
    PutString(block, section && *section ? section : "drivers");
    PutString(block + 0x80, name);
    PutString(block + 0x2e0, "system.ini");
    const unsigned query[] = {block, block + 0x80, block + 0x80, block + 0x100, 0x100, block + 0x2e0};
    Api("GetPrivateProfileStringA", query);
    char line[0x101] = {};
    snprintf(line, sizeof line, "%s", ReadString(block + 0x100));
    Free(block);
    char *parameters = line + strlen(line);
    for (char *p = line; *p; ++p)
        if (*p == ' ')
        {
            *p = 0;
            parameters = p + 1;
            break;
        }
    // NEO98CAP.DRV is the native capture driver of the host camera, with no
    // module behind it (src/win16_capture.cpp).
    const char *leaf = strrchr(line, '\\') ? strrchr(line, '\\') + 1 : line;
    if (!strcasecmp(leaf, "neo98cap.drv"))
    {
        Win16Driver &entry = win16_drivers[handle - 1];
        entry = {};
        entry.camera = true;
        snprintf(entry.name, sizeof entry.name, "%s", name);
        entry.driver_id = handle;
        const unsigned identifier = Win16CaptureMessage(entry, DrvOpen, 0, second);
        if (log)
        {
            char text[160];
            snprintf(text, sizeof text, "WIN16 OpenDriver %s: native capture driver, id=%u", name, identifier);
            log(text);
        }
        if (!identifier)
        {
            entry = {};
            return 0;
        }
        entry.driver_id = identifier;
        return handle;
    }
    unsigned module = 0, error = 0;
    const bool qualified = strchr(line, '\\') || strchr(line, ':');
    const char *roots[] = {"C:\\WINDOWS\\SYSTEM", "C:\\WINDOWS"};
    for (const char *root : roots)
    {
        char path[300];
        if (qualified)
            snprintf(path, sizeof path, "%s", line);
        else
            snprintf(path, sizeof path, "%s\\%s", root, line);
        module = Win16HostedLibrary(path, error);
        if (module || qualified || (error != 2 && error != 3))
            break;
    }
    if (!module)
    {
        if (log)
        {
            char text[320];
            snprintf(text, sizeof text, "WIN16 OpenDriver %s: %s not loaded, error %u", name, line, error);
            log(text);
        }
        return 0;
    }
    unsigned procedure = 0;
    for (unsigned i = 0; i < win16.state->module_count; ++i)
        if (win16.state->modules[i].handle == module)
        {
            Win16Module &found = win16.state->modules[i];
            const unsigned number = Win16Ordinal(found, "DRIVERPROC");
            Win98Ne::Entry entry;
            if (number && found.image.EntryAt(number, entry) && entry.segment && entry.segment <= found.segments &&
                found.selector[entry.segment])
                procedure = found.selector[entry.segment] << 16 | entry.offset;
        }
    if (!procedure)
    {
        Win16FreeLibrary(module);
        return 0;
    }
    Win16Driver &entry = win16_drivers[handle - 1];
    entry = {};
    entry.module = module;
    entry.procedure = procedure;
    snprintf(entry.name, sizeof entry.name, "%s", name);
    unsigned users = 0;
    for (const auto &other : win16_drivers)
        if (other.module == module)
            ++users;
    if (users == 1)
    {
        if (!Win16SendDriverMessage(handle, DrvLoad, 0, 0))
        {
            Win16FreeLibrary(module);
            entry = {};
            return 0;
        }
        Win16SendDriverMessage(handle, DrvEnable, 0, 0);
    }
    // DRV_OPEN: lParam1 is the far address of the parameters of the line.
    unsigned text = Win16GlobalAlloc(0x40, 0x80);
    Win16Block *memory = text ? Win16GlobalBlock(text) : nullptr;
    unsigned far_parameters = 0;
    if (memory)
    {
        char copy[0x80] = {};
        snprintf(copy, sizeof copy, "%s", parameters);
        Store(memory->memory, copy, sizeof copy);
        far_parameters = memory->selector << 16;
    }
    entry.driver_id = handle; // what DRV_OPEN itself sees in dwDriverID
    const unsigned identifier = Win16SendDriverMessage(handle, DrvOpen, far_parameters, second);
    if (text)
        Win16GlobalFree(text);
    if (!identifier)
    {
        Win16CloseDriver(handle, 0, 0, false);
        return 0;
    }
    entry.driver_id = identifier;
    if (log)
    {
        char line_text[320];
        snprintf(line_text, sizeof line_text, "WIN16 OpenDriver %s handle=%u module=%04x id=%08x", name, handle,
                 module, identifier);
        log(line_text);
    }
    return handle;
}

// `close` is false when the entry only goes back after a
// failed DRV_OPEN, which sends no DRV_CLOSE.
unsigned Application::Win16CloseDriver(unsigned handle, unsigned first, unsigned second, bool close)
{
    Win16Driver *entry = Win16DriverOf(handle);
    if (!entry)
        return 0;
    if (entry->camera)
    {
        const unsigned result = Win16CaptureMessage(*entry, DrvClose, first, second);
        *entry = {};
        return result;
    }
    const unsigned result = close ? Win16SendDriverMessage(handle, DrvClose, first, second) : 0;
    unsigned users = 0;
    for (const auto &other : win16_drivers)
        if (other.module == entry->module)
            ++users;
    entry->driver_id = 0;
    if (users == 1)
    {
        Win16SendDriverMessage(handle, DrvDisable, 0, 0);
        Win16SendDriverMessage(handle, DrvFree, 0, 0);
    }
    Win16FreeLibrary(entry->module);
    *entry = {};
    return result;
}

// USER.251 to USER.255 for a 16-bit caller.
bool Application::Win16DriverCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words)
{
    result = 0;
    switch (ordinal)
    {
    case 251: // SendDriverMessage(hDriver, wMessage, lParam1, lParam2)
        words = 6;
        result = Win16SendDriverMessage(frame.Word(5), frame.Word(4), frame.Long(2), frame.Long(0));
        return state != Failed;
    case 252: // OpenDriver(lpDriverName, lpSectionName, lParam2)
    {
        words = 6;
        const unsigned name = frame.Pointer(4), section = frame.Pointer(2);
        char driver[128] = {}, group[128] = {};
        snprintf(driver, sizeof driver, "%s", name ? ReadString(name) : "");
        snprintf(group, sizeof group, "%s", section ? ReadString(section) : "");
        result = Win16OpenDriver(driver, group, frame.Long(0));
        return state != Failed;
    }
    case 253: // CloseDriver(hDriver, lParam1, lParam2)
        words = 5;
        result = Win16CloseDriver(frame.Word(4), frame.Long(2), frame.Long(0), true);
        return state != Failed;
    case 254: // GetDriverModuleHandle(hDriver)
    {
        words = 1;
        const Win16Driver *entry = Win16DriverOf(frame.Word(0));
        result = entry ? entry->module : 0;
        return true;
    }
    case 256: // GetDriverInfo(hDriver, info): USER refuses a
              // DRIVERINFOSTRUCT whose length word is not 86h, the handle,
              // the module and the 128 bytes of the alias name.
    {
        words = 3;
        const Win16Driver *entry = Win16DriverOf(frame.Word(2));
        const unsigned info = frame.Pointer(0);
        if (!entry || !info || Win16Word(info) != 0x86)
            return true;
        Win16Word(info + 2, frame.Word(2));
        Win16Word(info + 4, entry->module);
        char name[128] = {};
        snprintf(name, sizeof name, "%s", entry->name);
        Store(info + 6, name, sizeof name);
        result = 1;
        return true;
    }
    case 255: // DefDriverProc(dwDriverID, hDriver, wMessage, lParam1, lParam2)
    {
        words = 8;
        const unsigned message = frame.Word(4);
        result = message == DrvLoad || message == DrvEnable || message == DrvClose || message == DrvDisable ||
                         message == DrvFree || message == 9 || message == 10
                     ? 1u
                     : 0u;
        return true;
    }
    default:
        break;
    }
    return false;
}
} // namespace X86
