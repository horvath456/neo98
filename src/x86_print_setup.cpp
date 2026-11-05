// SPDX-License-Identifier: GPL-2.0-or-later
// GDI32 ordinals 100–104 query a printer driver without a DC: far-call
// trampoline, ExtDeviceModePropSheet, ExtDeviceMode, AdvancedSetupDialog, and
// DeviceCapabilities. Convert flat pointers to 16:16 aliases and preserve
// WORD arguments. Ordinals 100–103 return sign-extended AX; 104 returns
// DX:AX. Resolve the driver through the registry, load it, call its ordinal,
// then release it.
#include "x86_print.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
using namespace Print;
namespace
{
constexpr unsigned DcVersion = 10;
} // namespace

// the model ("Printer Driver") and the driver file ("Driver" of the
// model's key) of a printer. The two queries are not checked; only the
// opening of the keys is.
bool Application::PrintFindDriver(const char *printer, char *model, unsigned model_bytes, char *file,
                                  unsigned file_bytes)
{
    // No name is no printer: "Printers\\" would name the Printers key itself,
    // which is not the key of a printer.
    if (!printer || !*printer)
        return false;
    char path[0x140];
    snprintf(path, sizeof path, "Printers\\%s", printer);
    char found_model[0x20] = {};
    if (!PrintRegistryString(path, "Printer Driver", found_model, sizeof found_model) &&
        !PrintRegistryKeyExists(path))
        return false;
    if (model)
        snprintf(model, model_bytes, "%s", found_model);
    if (!file)
        return true;
    snprintf(path, sizeof path, "Environments\\Windows 4.0\\Drivers\\%s", found_model);
    char found_file[0x104] = {};
    if (!PrintRegistryString(path, "Driver", found_file, sizeof found_file) && !PrintRegistryKeyExists(path))
        return false;
    snprintf(file, file_bytes, "%s", found_file);
    return true;
}
bool Application::PrintRegistryKeyExists(const char *path)
{
    const unsigned text = Allocate(0x160);
    if (!text)
        return false;
    char full[0x150];
    snprintf(full, sizeof full, "System\\CurrentControlSet\\Control\\Print\\%s", path);
    PutString(text, full);
    const unsigned open[] = {0x80000002u, text, text + 0x150};
    const bool found = !Api("RegOpenKeyA", open);
    if (found)
    {
        const unsigned close[] = {Read32(text + 0x150)};
        Api("RegCloseKey", close);
    }
    Free(text);
    return found;
}

// LoadLibrary of the driver file as the workers call it (KERNEL.95), and the
// entry by ordinal or name (KERNEL.50).
unsigned Application::PrintSetupLibrary(const char *file)
{
    unsigned error = 0;
    char bare[16] = {};
    snprintf(bare, sizeof bare, "%s", file);
    PrintDriver *driver = PrintLoadDriver(bare, error);
    return driver ? driver->module : 0;
}

// A 16-bit call with the arguments as Pascal pushes them; a far pointer is
// two words, high first.
unsigned Application::PrintSetupCall(unsigned module, unsigned entry, const unsigned *push, unsigned count,
                                     bool &ok)
{
    ok = false;
    if (!entry)
        return 0;
    const unsigned result = Win16HostedCall(entry, push, count, Win16HostedInstance(module), nullptr, &ok);
    return result;
}

// The GDI32 thunks of ordinals 100 to 104.
unsigned Application::PrinterSetupApi(unsigned ordinal, const unsigned *a)
{
    if (!Win16HostedReady())
        return ~0u;
    // SMapLS of every pointer argument, freed after the call.
    unsigned mapped[8] = {};
    unsigned count = 0;
    auto map = [&](unsigned flat) {
        const unsigned far = PrintMapFlat(flat);
        if (far && count < 8)
            mapped[count++] = far;
        return far;
    };
    auto unmap = [&] {
        for (unsigned i = 0; i < count; ++i)
            PrintUnmapFar(mapped[i]);
    };
    auto push_far = [](unsigned *out, unsigned far) {
        out[0] = far >> 16;
        out[1] = far & 0xffff;
    };
    char model[0x20] = {}, file[0x104] = {};
    switch (ordinal)
    {
    case 104: // DeviceCapabilities(lpDevice, lpPort, wIndex, lpOutput, lpDevMode),
    {
        const char *device = a[0] ? ReadString(a[0]) : "";
        if (!PrintFindDriver(device, model, sizeof model, file, sizeof file))
            return ~0u;
        const unsigned module = PrintSetupLibrary(file);
        const unsigned entry = module ? Win16HostedProcedure(module, DdiDeviceCapabilities) : 0;
        if (!entry)
            return ~0u;
        // The DC_VERSION probe with the model name decides which name the
        // driver gets: a driver of Windows 4.0 or later takes the printer's
        // own name, an older one the model.
        const unsigned names = Allocate(0x40);
        if (!names)
            return ~0u;
        PutString(names, model);
        const unsigned model_far = map(names), port_far = map(a[1]);
        unsigned push[9];
        push_far(push, model_far);
        push_far(push + 2, port_far);
        push[4] = DcVersion;
        push[5] = push[6] = push[7] = push[8] = 0;
        bool ok = false;
        const int version = short(PrintSetupCall(module, entry, push, 9, ok) & 0xffff);
        const unsigned device_far = version < 0 || version >= 0x400 ? map(a[0]) : model_far;
        push_far(push, device_far);
        push[4] = a[2] & 0xffff;
        push_far(push + 5, map(a[3]));
        push_far(push + 7, map(a[4]));
        const unsigned result = PrintSetupCall(module, entry, push, 9, ok);
        unmap();
        Free(names);
        return ok ? result : ~0u;
    }
    case 102: // ExtDeviceMode(hWnd, lpdmOut, lpDeviceName, lpPort, lpdmIn, lpProfile, wMode),
    {
        const char *device = a[2] ? ReadString(a[2]) : "";
        if (!PrintFindDriver(device, model, sizeof model, file, sizeof file))
            return ~0u;
        const unsigned module = PrintSetupLibrary(file);
        if (!module)
            return ~0u;
        const unsigned capabilities = Win16HostedProcedure(module, DdiDeviceCapabilities);
        const unsigned names = Allocate(0x40);
        if (!names)
            return ~0u;
        PutString(names, model);
        const unsigned model_far = map(names), port_far = map(a[3]);
        int version = 0;
        bool ok = false;
        if (capabilities)
        {
            unsigned probe[9];
            push_far(probe, model_far);
            push_far(probe + 2, port_far);
            probe[4] = DcVersion;
            probe[5] = probe[6] = probe[7] = probe[8] = 0;
            version = short(PrintSetupCall(module, capabilities, probe, 9, ok) & 0xffff);
        }
        const unsigned entry = Win16HostedProcedure(module, DdiExtDeviceMode);
        if (!entry)
        {
            unmap();
            Free(names);
            return ~0u;
        }
        // A driver older than 4.0 gets the model name in dmDeviceName of the
        // input DEVMODE and gives the printer's name back afterwards.
        if (version < 0x400 && a[4])
            PutString(a[4], model);
        const unsigned device_far = version >= 0x400 ? map(a[2]) : model_far;
        unsigned push[13];
        push[0] = a[0] & 0xffff;
        push[1] = module & 0xffff;
        push_far(push + 2, map(a[1]));
        push_far(push + 4, device_far);
        push_far(push + 6, port_far);
        push_far(push + 8, map(a[4]));
        push_far(push + 10, map(a[5]));
        push[12] = a[6] & 0xffff;
        const int result = short(PrintSetupCall(module, entry, push, 13, ok) & 0xffff);
        if (version < 0x400)
        {
            const char *name = ReadString(a[2]);
            char printer[0x40] = {};
            snprintf(printer, sizeof printer, "%s", name ? name : "");
            if (a[4])
                PutString(a[4], printer);
            if (a[1])
                PutString(a[1], printer);
        }
        unmap();
        Free(names);
        return ok ? unsigned(result) : ~0u;
    }
    case 103: // AdvancedSetupDialog(hWnd, lpDevice, lpdmIn, lpdmOut),
    {
        const char *device = a[1] ? ReadString(a[1]) : "";
        if (!PrintFindDriver(device, nullptr, 0, file, sizeof file))
            return ~0u;
        const unsigned module = PrintSetupLibrary(file);
        const unsigned entry = module ? Win16HostedProcedure(module, DdiAdvancedSetupDialog) : 0;
        if (!entry)
            return ~0u;
        unsigned push[6];
        push[0] = a[0] & 0xffff;
        push[1] = module & 0xffff;
        push_far(push + 2, map(a[2]));
        push_far(push + 4, map(a[3]));
        bool ok = false;
        const int result = short(PrintSetupCall(module, entry, push, 6, ok) & 0xffff);
        unmap();
        return ok ? unsigned(result) : ~0u;
    }
    case 101: // ExtDeviceModePropSheet(hWnd, lpDevice, lpfnAddPage, lParam),
    {
        // Like its neighbours the worker first looks for the driver with
        // and answers -1 when there is none; only the pages of a driver
        // that exists need the AddPage callback.
        const char *device = a[1] ? ReadString(a[1]) : "";
        if (!PrintFindDriver(device, nullptr, 0, file, sizeof file))
            return ~0u;
        Fail("GDI32 ordinal 101, ExtDeviceModePropSheet with the AddPage callback of GDI, is not built "
             "yet");
        return ~0u;
    }
    case 100:
    {
        // the raw first DWORD is a 16:16 procedure; the rest is
        // WORD a2 and three far pointers.
        unsigned push[7];
        push[0] = a[1] & 0xffff;
        push_far(push + 1, map(a[2]));
        push_far(push + 3, map(a[3]));
        push_far(push + 5, map(a[4]));
        bool ok = false;
        const unsigned result = Win16HostedCall(a[0], push, 7, 0, nullptr, &ok);
        unmap();
        return ok ? unsigned(int(short(result & 0xffff))) : 0;
    }
    default:
        return ~0u;
    }
}
} // namespace X86
