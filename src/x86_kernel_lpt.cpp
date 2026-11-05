// SPDX-License-Identifier: GPL-2.0-or-later
// The parallel port as VCOMM and LPT.VXD present it to KERNEL32: CreateFileA
// of "LPT1" (with or without colon, optionally behind \\.\) gives a comm
// handle, and WriteFile hands the bytes to the port. The port's far end is
// the host provider of src/printer_port.h.
// The spooler is the caller that matters. SPOOLSS's built-in local monitor
// opens the port in StartDocPort with CreateFileA("LPT1",
// GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 80h, NULL), calls
// SetEndOfFile (ignoring the answer), GetCommTimeouts and SetCommTimeouts,
// writes the job in WritePort with WriteFile and, when nothing was
// written, asks ClearCommError what went wrong; EndDocPort calls
// PurgeComm(h, 0Ah) and CloseHandle.
//
// VCOMM itself is not implemented here; the null-modem provider of
// src/x86_kernel_comm.inc is the model for COM1 and COM2. Only LPT1 exists,
// matching the one parallel port devnode of the installed hive
// (Enum\ACPI\*PNP0400\0).
#include "x86_app.h"
#include "printer_port.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
struct Application::LptState
{
    bool open = false, overlapped = false;
    unsigned generation = 1, errors = 0;
    unsigned timeouts[5] = {};
    static constexpr unsigned Prefix = 0x6a000000u;
    unsigned Handle() const { return Prefix | (generation << 8) | 1; }
    bool Owns(unsigned handle) const
    {
        return (handle & 0xff000000u) == Prefix && open && (handle & 255) == 1 &&
               ((handle >> 8) & 65535) == generation;
    }
};
Application::LptOwner::~LptOwner() { delete state; }

// LPT1 to LPT9 with or without a trailing colon, optionally behind \\.\.
unsigned Application::LptPort(unsigned pointer)
{
    if (!pointer)
        return 0;
    const char *s = ReadString(pointer);
    if (!s)
        return 0;
    if (s[0] == '\\' && s[1] == '\\' && s[2] == '.' && s[3] == '\\')
        s += 4;
    if ((s[0] | 32) != 'l' || (s[1] | 32) != 'p' || (s[2] | 32) != 't' || s[3] < '1' || s[3] > '9')
        return 0;
    if (s[4] && (s[4] != ':' || s[5]))
        return 0;
    return unsigned(s[3] - '0');
}
bool Application::LptHandle(unsigned handle) const
{
    return (handle & 0xff000000u) == LptState::Prefix;
}

unsigned Application::LptApi(ApiName name, const unsigned *a)
{
    if (!lpt.state)
        lpt.state = new LptState;
    auto &s = *lpt.state;
    const auto bad = [&](unsigned error) {
        last_error = error;
        return 0u;
    };
    auto *sink = PrinterPort::Installed();
    if (API_IS(name, "CreateFileA"))
    {
        const unsigned number = LptPort(a[0]);
        if (a[6] || (a[5] & ~0x5a0000a7u))
        {
            last_error = 120;
            return Files::Invalid;
        }
        // The hive has one parallel port, LPT1.
        if (number != 1)
        {
            last_error = 2;
            return Files::Invalid;
        }
        if (s.open)
        {
            last_error = 5;
            return Files::Invalid;
        }
        s.open = true;
        s.generation = s.generation % 65535 + 1;
        s.overlapped = (a[5] & 0x40000000u) != 0;
        s.errors = 0;
        memset(s.timeouts, 0, sizeof s.timeouts);
        if (sink && !sink->Open(1))
            s.errors = 0x800;
        return s.Handle();
    }
    if (!s.Owns(a[0]))
        return bad(6);
    if (API_IS(name, "CloseHandle"))
    {
        s.open = false;
        s.generation = s.generation % 65535 + 1;
        if (sink)
            sink->Close(1);
        return 1;
    }
    if (API_IS(name, "GetFileType"))
        return 2; // FILE_TYPE_CHAR
    if (API_IS(name, "WriteFile"))
    {
        if (a[3])
        {
            if (!virtual_memory.Access(a[3], 4, true))
                return bad(87);
            Write32(a[3], 0);
        }
        if (!a[3] && !a[4])
            return bad(87);
        if ((a[4] != 0) != s.overlapped)
            return bad(87);
        if (a[2] > 8u * 1024 * 1024)
            return bad(8);
        if (a[2] && !virtual_memory.Access(a[1], a[2], false))
            return bad(998);
        // No device at the far end: nothing is written, and ClearCommError
        // tells the monitor why (CE_DNS).
        if (!sink || (s.errors & 0x800))
        {
            s.errors |= 0x800;
            return bad(0x15);
        }
        unsigned char chunk[4096];
        for (unsigned done = 0; done < a[2];)
        {
            const unsigned n = a[2] - done < sizeof chunk ? a[2] - done : unsigned(sizeof chunk);
            Read(a[1] + done, chunk, n);
            if (!sink->Write(1, chunk, n))
            {
                s.errors |= 0x800;
                if (a[3])
                    Write32(a[3], done);
                return bad(0x15);
            }
            done += n;
        }
        if (a[3])
            Write32(a[3], a[2]);
        if (a[4])
        {
            Write32(a[4], 0);
            Write32(a[4] + 4, a[2]);
        }
        return 1;
    }
    if (API_IS(name, "SetEndOfFile"))
        return bad(6); // resolves file objects only
    if (API_IS(name, "GetCommTimeouts"))
    {
        if (!virtual_memory.Access(a[1], 20, true))
            return bad(87);
        Store(a[1], s.timeouts, sizeof s.timeouts);
        return 1;
    }
    if (API_IS(name, "SetCommTimeouts"))
    {
        if (!virtual_memory.Access(a[1], 20, false))
            return bad(87);
        Read(a[1], s.timeouts, sizeof s.timeouts);
        return 1;
    }
    if (API_IS(name, "ClearCommError"))
    {
        if (a[1])
        {
            if (!virtual_memory.Access(a[1], 4, true))
                return bad(87);
            Write32(a[1], s.errors);
        }
        if (a[2])
        {
            if (!virtual_memory.Access(a[2], 12, true))
                return bad(87);
            const unsigned status[3] = {};
            Store(a[2], status, sizeof status);
        }
        s.errors = 0;
        return 1;
    }
    if (API_IS(name, "PurgeComm"))
        return 1; // nothing is queued; the host takes every byte at once
    char text[160];
    snprintf(text, sizeof text, "%s on the parallel port is not built yet", name.text);
    Fail(text);
    return 0;
}
} // namespace X86
