// SPDX-License-Identifier: GPL-2.0-or-later
// The spool half of printing: the job API of GDI.EXE a printer driver writes
// its output through, and the client of GDI32 that hands the job to the
// spooler process SPOOL32.EXE.
// A printer driver (PSCRIPT.DRV) never opens a port. It calls the job API of
// GDI.EXE segment 8: OPENJOB, STARTSPOOLPAGE, WRITESPOOL, ENDSPOOLPAGE, CLOSEJOB and DELETEJOB.
// GDI.EXE reaches the spooler through the flat thunks of segment 43, whose 32-bit side is a client in GDI32. That client does not call SPOOLSS in the process
// of the program: it finds the hidden window "SpoolProcessClass" of
// SPOOL32.EXE, starting SPOOL32 if there is none, and hands every call over
// through a request block of ten DWORDs that SPOOLSS allocated in shared
// memory and stored in the window's GWL_USERDATA. The named mutex
// "SpoolMutex" serialises the callers, "SpoolDataEvent" wakes the server
// thread of SPOOL32, and "SpoolReturnEvent" gives the answer back. Only in
// the spooler process itself does GDI32 call the SPOOLSS exports directly.
// SPOOL32.EXE, SPOOLSS.DLL and the driver are the originals; this file is
// GDI.EXE and GDI32.
#include "x86_print.h"
#include "gdi.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
// The request ids of the GDI32 client, the
// message numbers the window procedure of SPOOL32 (0x4010F0) dispatches.
enum : unsigned
{
    RequestOpenPrinter = 0x401, RequestStartDoc = 0x402, RequestStartPage = 0x403, RequestWrite = 0x404,
    RequestEndPage = 0x405, RequestEndDoc = 0x406, RequestClose = 0x407, RequestSetJob = 0x40a,
    RequestAddJob = 0x41d, RequestGetPrinterData = 0x41f, RequestMessageBox = 0x426, RequestAbort = 0x42f
};
// SP_ERROR and its siblings, the answers of the job API.
constexpr int SpError = -1, SpAppAbort = -2, SpUserAbort = -3;
// JOB+0 flags of GDI.EXE: an EMF job, a job written to a file with _lwrite,
// a file that belongs to a spooler job (NNNNN.SPL).
constexpr unsigned JobEmf = 1, JobFile = 2, JobSpoolFile = 4;
constexpr unsigned HeapShared = 0x04000000, HeapZero = 8;
bool SameName(const char *a, const char *b)
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
} // namespace

// The GDI32 globals of the client. GDI32's data
// section is shared by every process under Win98; here each process keeps
// its own copy and finds the same window and the same buffer.
struct Application::SpoolClient
{
    unsigned window = 0, spooler_pid = 0, buffer = 0, heap = 0;
    bool launched = false;
    // The client record, 20h bytes in the shared heap: +0 pid,
    // +4 mutex, +8 data event, +0Ch return event, +14h flags, +18h the out
    // value the spooler writes.
    unsigned record = 0;
    bool busy = false;
    // The SPOOLSS exports of the in-process table, for the case
    // that the caller is SPOOL32 itself.
    unsigned spoolss = 0;
};
// One job of GDI.EXE (LocalAlloc(42h, 1Ah)).
struct Application::PrintJob
{
    unsigned handle = 0; // what the driver holds, the local handle >> 1
    unsigned flags = 0, hdc = 0;
    int state = 0;
    unsigned file = 0, printer = 0, job_id = 0;
    unsigned buffer = 0, capacity = 0, used = 0; // the page buffer of
    char name[264] = {};
};
struct Application::PrintJobs
{
    PrintJob jobs[8];
};

// A block of the shared heap of the client. Windows 98 allocates from the heap
// of GDI32, which the spooler process can read.
unsigned Application::SpoolShared(unsigned bytes)
{
    if (!spool)
        return 0;
    if (!spool->heap)
    {
        const unsigned create[] = {HeapShared, 0x1000, 0};
        spool->heap = Api("HeapCreate", create);
        if (!spool->heap)
            return 0;
    }
    const unsigned alloc[] = {spool->heap, HeapZero, bytes ? bytes : 4};
    return Api("HeapAlloc", alloc);
}
void Application::SpoolSharedFree(unsigned block)
{
    if (!spool || !spool->heap || !block)
        return;
    const unsigned free_block[] = {spool->heap, 0, block};
    Api("HeapFree", free_block);
}

// ConnectSpooler, GDI32.
bool Application::SpoolConnect(bool start)
{
    if (!spool)
        spool = new SpoolClient;
    if (!spool)
        return false;
    const unsigned text = Allocate(0x200);
    if (!text)
        return false;
    PutString(text, "SpoolProcessClass");
    bool found = false;
    for (unsigned tries = 0; tries < 20; ++tries)
    {
        const unsigned find[] = {text, 0};
        spool->window = Api("FindWindowA", find);
        if (spool->window)
        {
            found = true;
            break;
        }
        if (!start)
            break;
        if (!spool->launched)
        {
            // GetSystemDirectoryA + "\spool32.exe", CreateProcessA
            // with a STARTUPINFO of 44h bytes and wShowWindow 5.
            const unsigned path = text + 0x40, info = text + 0x150, process = text + 0x1a0;
            const unsigned directory[] = {path, 0x104};
            Api("GetSystemDirectoryA", directory);
            char full[0x110];
            snprintf(full, sizeof full, "%s\\spool32.exe", ReadString(path));
            PutString(path, full);
            unsigned char startup[0x44] = {};
            startup[0] = 0x44;
            startup[0x30] = 5;
            Store(info, startup, sizeof startup);
            const unsigned create[] = {0, path, 0, 0, 0, 0, 0, 0, info, process};
            if (Api("CreateProcessA", create))
            {
                const unsigned first[] = {Read32(process)}, second[] = {Read32(process + 4)};
                Api("CloseHandle", first);
                Api("CloseHandle", second);
            }
            spool->launched = true;
        }
        const unsigned second = 1000;
        Api("Sleep", &second);
        if (state == Failed || state == Exited)
            break;
    }
    if (found)
    {
        const unsigned owner[] = {spool->window, text};
        Api("GetWindowThreadProcessId", owner);
        spool->spooler_pid = Read32(text);
    }
    Free(text);
    return found;
}

// GetSpoolClient, GDI32: the client record of this process, with
// the handles of the three named objects.
unsigned Application::SpoolRecord()
{
    if ((!spool || !spool->window) && !SpoolConnect(true))
        return 0;
    if (spool->record)
        return spool->record;
    const unsigned record = SpoolShared(0x20);
    if (!record)
        return 0;
    const unsigned none[1] = {0};
    Write32(record, Api("GetCurrentProcessId", none));
    const unsigned text = Allocate(0x40);
    if (!text)
        return 0;
    PutString(text, "SpoolMutex");
    const unsigned mutex[] = {0x1f0001, 0, text};
    Write32(record + 4, Api("OpenMutexA", mutex));
    PutString(text, "SpoolDataEvent");
    const unsigned data[] = {0x1f0003, 0, text};
    Write32(record + 8, Api("OpenEventA", data));
    PutString(text, "SpoolReturnEvent");
    const unsigned back[] = {0x1f0003, 0, text};
    Write32(record + 0xc, Api("OpenEventA", back));
    Free(text);
    // Bit 3: a Win16 process waits with its message loop running.
    Write32(record + 0x14, Win16Process() ? 8u : 0u);
    spool->record = record;
    return record;
}

// True when this process is SPOOL32 itself.
bool Application::SpoolInProcess()
{
    if ((!spool || !spool->window) && !SpoolConnect(true))
        return false;
    const unsigned none[1] = {0};
    return spool->spooler_pid && Api("GetCurrentProcessId", none) == spool->spooler_pid;
}

// The in-process table, : the SPOOLSS export by name.
unsigned Application::SpoolExport(const char *name)
{
    const unsigned text = Allocate(0x40);
    if (!text)
        return 0;
    if (!spool->spoolss)
    {
        PutString(text, "spoolss");
        spool->spoolss = Api("GetModuleHandleA", &text);
        if (!spool->spoolss)
            spool->spoolss = Api("LoadLibraryA", &text);
    }
    unsigned procedure = 0;
    if (spool->spoolss)
    {
        PutString(text, name);
        const unsigned query[] = {spool->spoolss, text};
        procedure = Api("GetProcAddress", query);
    }
    Free(text);
    return procedure;
}

// Wait, : a Win32 process waits for the event, a Win16 process keeps
// its messages moving. WM_QUIT ends the wait with ERROR_PRINT_CANCELLED.
unsigned Application::SpoolWait(unsigned handle, bool win16_process)
{
    if (!win16_process)
    {
        const unsigned wait[] = {handle, 0xffffffffu};
        Api("WaitForSingleObject", wait);
        return 0;
    }
    const unsigned slot = Allocate(0x30);
    if (!slot)
        return 8;
    Write32(slot, handle);
    unsigned answer = 0;
    for (;;)
    {
        const unsigned wait[] = {1, slot, 0, 0xffffffffu, 0xff};
        if (Api("MsgWaitForMultipleObjects", wait) != 1)
            break;
        bool quit = false, more = true;
        for (unsigned i = 0; i < 10 && more; ++i)
        {
            const unsigned peek[] = {slot + 4, 0, 0, 0, 1};
            more = Api("PeekMessageA", peek) != 0;
            if (!more)
                break;
            if (Read32(slot + 8) == 0x12)
            {
                quit = true;
                break;
            }
            const unsigned message[] = {slot + 4};
            Api("TranslateMessage", message);
            Api("DispatchMessageA", message);
        }
        if (quit)
        {
            answer = 0x3f;
            break;
        }
        if (state == Failed || state == Exited)
            break;
    }
    Free(slot);
    return answer;
}

// SendSpoolRequest, GDI32: the ten DWORDs of the request go into
// the shared buffer, SPOOL32 answers with the error in the first one.
bool Application::SpoolRequest(unsigned id, const unsigned *arguments, unsigned count)
{
    const unsigned record = SpoolRecord();
    if (!record)
        return false;
    if (spool->busy)
    {
        last_error = 87;
        return false;
    }
    spool->busy = true;
    const bool win16_process = Read32(record + 0x14) & 8;
    unsigned error = SpoolWait(Read32(record + 4), win16_process);
    if (!error)
    {
        if (!spool->buffer)
        {
            const unsigned query[] = {spool->window, 0xffffffebu}; // GWL_USERDATA
            spool->buffer = Api("GetWindowLongA", query);
        }
        if (!spool->buffer)
        {
            spool->busy = false;
            Fail("SPOOL32 has no request buffer in GWL_USERDATA of SpoolProcessClass");
            return false;
        }
        unsigned request[10] = {id};
        for (unsigned i = 0; i < count && i < 9; ++i)
            request[1 + i] = arguments[i];
        Store(spool->buffer, request, sizeof request);
        const unsigned set[] = {Read32(record + 8)};
        Api("SetEvent", set);
        while (SpoolWait(Read32(record + 0xc), win16_process) && state != Failed && state != Exited)
        {
        }
        error = Read32(spool->buffer);
        const unsigned release[] = {Read32(record + 4)};
        Api("ReleaseMutex", release);
    }
    if (error)
        last_error = error;
    spool->busy = false;
    return !error;
}

// A private pointer (below 80000000h) goes into the shared heap before the
// request; one of the shared arena is handed over as it is.
unsigned Application::SpoolCopyIn(unsigned pointer, unsigned bytes, unsigned &owned)
{
    owned = 0;
    if (!pointer || pointer >= 0x80000000u)
        return pointer;
    owned = SpoolShared(bytes);
    if (!owned)
        return 0;
    unsigned char chunk[512];
    for (unsigned done = 0; done < bytes;)
    {
        const unsigned n = bytes - done < sizeof chunk ? bytes - done : unsigned(sizeof chunk);
        if (!Read(pointer + done, chunk, n) || !Store(owned + done, chunk, n))
            return 0;
        done += n;
    }
    return owned;
}
unsigned Application::SpoolStringIn(unsigned text, unsigned &owned)
{
    owned = 0;
    if (!text || text >= 0x80000000u)
        return text;
    const char *value = ReadString(text);
    return SpoolCopyIn(text, value ? unsigned(strlen(value)) + 1 : 1, owned);
}

// OpenPrinterA of GDI32, (request 401h). The name goes into a
// block of 20h bytes, longer names are ERROR_INVALID_NAME, and the defaults
// are not passed on.
bool Application::SpoolOpenPrinter(unsigned name, unsigned handle_out)
{
    const char *text = name ? ReadString(name) : nullptr;
    if (text && strlen(text) > 0x1f)
    {
        last_error = 0x7b;
        return false;
    }
    if (SpoolInProcess())
    {
        const unsigned procedure = SpoolExport("OpenPrinterA");
        const unsigned args[] = {name, handle_out, 0};
        return procedure && Call(procedure, args, 3);
    }
    const unsigned record = SpoolRecord();
    if (!record)
        return false;
    unsigned owned = 0;
    const unsigned block = name && name < 0x80000000u ? SpoolShared(0x20) : name;
    if (name && name < 0x80000000u)
    {
        if (!block)
        {
            last_error = 8;
            return false;
        }
        PutString(block, text);
        owned = block;
    }
    const unsigned args[] = {block, record + 0x18, 0};
    const bool ok = SpoolRequest(RequestOpenPrinter, args, 3);
    SpoolSharedFree(owned);
    if (handle_out)
        Write32(handle_out, ok ? Read32(record + 0x18) : 0);
    return ok;
}

// StartDocPrinterA of GDI32, : level 1 is request 402h, level 65h
// is AddJobA, request 41Dh, whose JobId comes back in pcbNeeded.
unsigned Application::SpoolStartDoc(unsigned printer, unsigned level, unsigned info)
{
    if (SpoolInProcess())
    {
        if (level > 2)
        {
            last_error = 0x7c;
            return 0;
        }
        const unsigned procedure = SpoolExport("StartDocPrinterA");
        const unsigned args[] = {printer, level, info};
        return procedure ? Call(procedure, args, 3) : 0;
    }
    const unsigned record = SpoolRecord();
    if (!record || (level != 1 && level != 0x65))
        return 0;
    // The DOC_INFO_1 and its three strings, copied as one block of
    // sum(strlen + 1) + 0Ch bytes when any of them is private.
    unsigned copy = info;
    bool owned = false;
    if (info)
    {
        unsigned pointers[3] = {Read32(info), Read32(info + 4), Read32(info + 8)};
        bool private_data = info < 0x80000000u;
        unsigned bytes = 0xc;
        for (unsigned p : pointers)
        {
            if (p && p < 0x80000000u)
                private_data = true;
            if (p)
                bytes += unsigned(strlen(ReadString(p))) + 1;
        }
        if (private_data)
        {
            copy = SpoolShared(bytes);
            if (!copy)
            {
                last_error = 8;
                return 0;
            }
            owned = true;
            unsigned at = copy + 0xc;
            for (unsigned i = 0; i < 3; ++i)
            {
                if (!pointers[i])
                {
                    Write32(copy + i * 4, 0);
                    continue;
                }
                const char *text = ReadString(pointers[i]);
                char value[512];
                snprintf(value, sizeof value, "%s", text ? text : "");
                PutString(at, value);
                Write32(copy + i * 4, at);
                at += unsigned(strlen(value)) + 1;
            }
        }
    }
    bool ok;
    if (level == 1)
    {
        const unsigned args[] = {printer, 1, copy, record + 0x18};
        ok = SpoolRequest(RequestStartDoc, args, 4);
    }
    else
    {
        const unsigned args[] = {printer, 0x65, copy, 0, record + 0x18};
        ok = SpoolRequest(RequestAddJob, args, 5);
    }
    if (owned)
        SpoolSharedFree(copy);
    return ok ? Read32(record + 0x18) : 0;
}

// The one-handle requests: StartPagePrinter 403h, EndPagePrinter 405h,
// EndDocPrinter 406h, ClosePrinter 407h.
bool Application::SpoolPrinterCall(unsigned id, const char *export_name, unsigned printer)
{
    if (SpoolInProcess())
    {
        const unsigned procedure = SpoolExport(export_name);
        const unsigned args[] = {printer};
        return procedure && Call(procedure, args, 1);
    }
    const unsigned args[] = {printer};
    return SpoolRequest(id, args, 1);
}

// WritePrinter of GDI32, (request 404h).
bool Application::SpoolWritePrinter(unsigned printer, unsigned data, unsigned bytes, unsigned written)
{
    if (SpoolInProcess())
    {
        const unsigned procedure = SpoolExport("WritePrinter");
        const unsigned args[] = {printer, data, bytes, written};
        return procedure && Call(procedure, args, 4);
    }
    const unsigned record = SpoolRecord();
    if (!record)
        return false;
    unsigned owned = 0;
    const unsigned block = SpoolCopyIn(data, bytes, owned);
    if (data && !block)
    {
        last_error = 8;
        return false;
    }
    const unsigned args[] = {printer, block, bytes, record + 0x18};
    const bool ok = SpoolRequest(RequestWrite, args, 4);
    SpoolSharedFree(owned);
    if (written)
        Write32(written, ok ? Read32(record + 0x18) : 0);
    return ok;
}

// AbortPrinter of GDI32, : posted, not sent (request 42Fh); SPOOL32
// aborts and closes the printer.
bool Application::SpoolAbortPrinter(unsigned printer)
{
    if (SpoolInProcess())
    {
        const unsigned abort = SpoolExport("AbortPrinter"), close = SpoolExport("ClosePrinter");
        const unsigned args[] = {printer};
        const bool ok = abort && Call(abort, args, 1);
        if (close)
            Call(close, args, 1);
        return ok;
    }
    if (!SpoolRecord())
        return false;
    const unsigned post[] = {spool->window, RequestAbort, 0, printer};
    return Api("PostMessageA", post) != 0;
}

// CancelJob, GDI32: SetJobA with JOB_CONTROL_CANCEL (request 40Ah).
bool Application::SpoolCancelJob(unsigned printer, unsigned job_id)
{
    const unsigned args[] = {printer, job_id, 0, 0, 3};
    return SpoolRequest(RequestSetJob, args, 5);
}

// GDI into GDI32: GetPrinterDataA of the spooler for a
// printer named, not opened. The wrapper opens the printer, sends request
// 41Fh with the value name and one buffer for data, type and size, and
// closes the printer again. Answers the error code.
unsigned Application::PrinterSpoolGetData(unsigned printer, unsigned profile, unsigned type, unsigned data,
                                          unsigned size, unsigned needed)
{
    const unsigned handle_slot = Allocate(8);
    if (!handle_slot)
        return 8;
    if (!SpoolOpenPrinter(printer, handle_slot))
    {
        Free(handle_slot);
        return last_error ? last_error : 1;
    }
    const unsigned handle = Read32(handle_slot);
    Free(handle_slot);
    const char *name = ReadString(profile);
    const unsigned name_bytes = name ? unsigned(strlen(name)) + 1 : 1;
    const unsigned block = SpoolShared(size + 8 + name_bytes);
    unsigned error = 8;
    if (block)
    {
        const unsigned block_type = block + size, block_needed = block + size + 4, block_name = block + size + 8;
        PutString(block_name, name ? name : "");
        const unsigned args[] = {handle, block_name, block_type, data ? block : 0, size, block_needed};
        error = SpoolRequest(RequestGetPrinterData, args, 6) ? 0 : last_error;
        if (type)
            Write32(type, Read32(block_type));
        if (needed)
            Write32(needed, Read32(block_needed));
        if (data && !error)
        {
            unsigned char chunk[512];
            for (unsigned done = 0; done < size;)
            {
                const unsigned n = size - done < sizeof chunk ? size - done : unsigned(sizeof chunk);
                Read(block + done, chunk, n);
                Store(data + done, chunk, n);
                done += n;
            }
        }
        SpoolSharedFree(block);
    }
    SpoolPrinterCall(RequestClose, "ClosePrinter", handle);
    return error;
}

// ------------------------------------------------------------ GDI.EXE jobs

Application::PrintJob *Application::PrintJobOf(unsigned handle)
{
    if (!print_jobs || !handle || (handle & 0x8000))
        return nullptr;
    for (auto &job : print_jobs->jobs)
        if (job.handle == handle)
            return &job;
    return nullptr;
}

// QUERYABORT, GDI: the abort procedure of the DC, if it has one.
bool Application::PrintQueryAbort(unsigned hdc)
{
    // Windows 98 answers TRUE unless a document is started, GDI is inside one of
    // its document calls and the program set an abort procedure.
    PrintDc *dc = PrintFind(hdc);
    const unsigned procedure = dc ? PrintAbortProcedure(*dc) : 0;
    if (!procedure || !(dc->document & 0x100) || !dc->in_call)
        return true;
    const unsigned args[] = {hdc, 0};
    return Call(procedure, args, 2) != 0;
}

// ClassifyPort, GDI: 1 for "FILE:", 0 for a port the spooler serves,
// 2 for a plain file name.
unsigned Application::PrintClassifyPort(const char *port)
{
    if (!port || !*port)
        return 0;
    char name[0x104];
    snprintf(name, sizeof name, "%s", port);
    // The atom lookup of ignores a trailing colon.
    const unsigned length = unsigned(strlen(name));
    char bare[0x104];
    snprintf(bare, sizeof bare, "%s", name);
    if (length && bare[length - 1] == ':')
        bare[length - 1] = 0;
    if (SameName(bare, "file"))
        return 1;
    if (SameName(bare, "LPT1"))
        return 0;
    if ((length == 4 || (length == 5 && name[4] == ':')) &&
        ((name[0] | 0x20) == 'l' || (name[0] | 0x20) == 'c'))
    {
        // "lpt1" to "lpt9" and "com1" to "com9".
        char low[4] = {char(name[0] | 0x20), char(name[1] | 0x20), char(name[2] | 0x20), name[3]};
        if ((!memcmp(low, "lpt", 3) || !memcmp(low, "com", 3)) && low[3] >= '1' && low[3] <= '9')
            return 0;
    }
    unsigned answer = 2;
    const unsigned text = Allocate(0x240);
    if (!text)
        return answer;
    // WIN.INI [ports] with the default "Bad$$", then the Ports key.
    PutString(text, "ports");
    PutString(text + 0x10, name);
    PutString(text + 0x120, "Bad$$");
    const unsigned profile[] = {text, text + 0x10, text + 0x120, text + 0x130, 7};
    Api("GetProfileStringA", profile);
    if (!SameName(ReadString(text + 0x130), "Bad$$"))
        answer = 0;
    else
    {
        PutString(text + 0x140, "System\\CurrentControlSet\\Control\\Print\\Ports");
        const unsigned open[] = {0x80000002u, text + 0x140, text + 0x138};
        if (!Api("RegOpenKeyA", open))
        {
            const unsigned key = Read32(text + 0x138);
            const unsigned sub[] = {key, text + 0x10, text + 0x13c};
            if (!Api("RegOpenKeyA", sub))
            {
                answer = 0;
                const unsigned close_sub[] = {Read32(text + 0x13c)};
                Api("RegCloseKey", close_sub);
            }
            const unsigned close[] = {key};
            Api("RegCloseKey", close);
        }
    }
    Free(text);
    return answer;
}

// OPENJOB, GDI with StartSpoolJob.
int Application::PrintOpenJob(unsigned output, unsigned title, unsigned hdc)
{
    if (!print_jobs)
        print_jobs = new PrintJobs;
    if (!print_jobs)
        return -5;
    PrintJob *job = nullptr;
    for (unsigned i = 0; i < 8 && !job; ++i)
        if (!print_jobs->jobs[i].handle)
        {
            job = &print_jobs->jobs[i];
            *job = {};
            job->handle = 0x40 + i * 2; // a local handle >> 1
        }
    if (!job)
        return -5; // SP_OUTOFMEMORY, LocalAlloc failed
    job->hdc = hdc;
    job->state = 1;
    const char *port = output ? ReadString(output) : nullptr;
    char port_name[0x104] = {};
    snprintf(port_name, sizeof port_name, "%s", port ? port : "");
    PrintDc *dc = PrintFind(hdc);
    const unsigned kind = port_name[0] ? PrintClassifyPort(port_name)
                          : dc                ? PrintClassifyPort(dc->port)
                                              : 0;
    auto release = [&](int answer) {
        *job = {};
        return answer;
    };
    const unsigned scratch = Allocate(0x400);
    if (!scratch)
        return release(-5);
    auto finish = [&](int answer) {
        Free(scratch);
        return answer;
    };
    if (kind != 0)
    {
        // A file: FILE: asks for the name (the save dialog of
        // COMMDLG), a plain name is taken as it is.
        if (kind == 1)
        {
            Free(scratch);
            *job = {};
            Fail("OpenJob to FILE: needs the print-to-file dialog of GDI, which is not built yet");
            return SpError;
        }
        snprintf(job->name, sizeof job->name, "%s", port_name);
        PutString(scratch, job->name);
        // _lcreat(name, 0), which opens in compatibility mode.
        const unsigned create[] = {scratch, 0};
        const unsigned file = Api("_lcreat", create);
        if (file == 0xffffffffu)
            return finish(release(SpError));
        job->flags = JobFile;
        job->file = file;
        return finish(int(job->handle));
    }
    if (dc && dc->job)
        return finish(release(SpError)); // the DC already has a job
    // DOC_INFO_1 of 32 bits: document, output, datatype "RAW".
    const unsigned info = scratch, printer_slot = scratch + 0x10, device_text = scratch + 0x20,
                   datatype = scratch + 0x130;
    PutString(datatype, "RAW");
    Write32(info, title);
    Write32(info + 4, 0);
    Write32(info + 8, datatype);
    bool ok;
    unsigned level = 1;
    if (dc && dc->device[0])
    {
        Write32(info + 4, output);
        PutString(device_text, dc->device);
        ok = SpoolOpenPrinter(device_text, printer_slot) || SpoolOpenPrinter(output, printer_slot);
    }
    else
        ok = SpoolOpenPrinter(output, printer_slot);
    if (!ok)
        job->state = SpError;
    else
    {
        job->printer = Read32(printer_slot);
        if (!(job->flags & JobEmf))
        {
            // an image for Windows 4.0 or later spools through
            // AddJobA and writes NNNNN.SPL itself.
            job->job_id = SpoolStartDoc(job->printer, 0x65, info);
            if (job->job_id)
            {
                const unsigned directory[] = {scratch + 0x140, 0x104};
                Api("GetWindowsDirectoryA", directory);
                char windows[0x110];
                snprintf(windows, sizeof windows, "%s", ReadString(scratch + 0x140));
                const size_t n = strlen(windows);
                if (n && windows[n - 1] == '\\')
                    windows[n - 1] = 0;
                snprintf(job->name, sizeof job->name, "%s\\spool\\printers\\%05u.SPL", windows,
                         job->job_id % 100000u);
                PutString(scratch + 0x140, job->name);
                // _lcreat(name, 0): compatibility mode, so the despooler of
                // SPOOL32 can read the file while GDI still writes it.
                const unsigned create[] = {scratch + 0x140, 0};
                const unsigned file = Api("_lcreat", create);
                if (file != 0xffffffffu)
                {
                    job->flags = JobFile | JobSpoolFile;
                    job->file = file;
                }
                else
                {
                    SpoolCancelJob(job->printer, job->job_id);
                    job->job_id = 0;
                }
            }
            else if (last_error != 0x704)
                job->job_id = SpoolStartDoc(job->printer, level, info);
        }
        else
            job->job_id = SpoolStartDoc(job->printer, level, info);
        if (!job->job_id)
        {
            job->state = SpError;
            SpoolPrinterCall(RequestClose, "ClosePrinter", job->printer);
        }
    }
    if (job->state < 0)
        return finish(release(job->state));
    if (dc)
        dc->job = job->handle;
    return finish(int(job->handle));
}

// STARTSPOOLPAGE, GDI.
int Application::PrintStartSpoolPage(unsigned handle)
{
    PrintJob *job = PrintJobOf(handle);
    if (!job)
        return (handle & 0x8000) || !handle ? int(short(handle)) : SpError;
    if (job->state < 0)
        return job->state;
    if (!PrintQueryAbort(job->hdc))
        job->state = SpAppAbort;
    else if (!(job->flags & (JobEmf | JobFile)) && !SpoolPrinterCall(RequestStartPage, "StartPagePrinter", job->printer))
        job->state = SpUserAbort;
    return job->state;
}

// Flush, GDI: the page buffer goes out in one WritePrinter.
bool Application::PrintFlushJob(PrintJob &job)
{
    if (!job.used)
        return true;
    const unsigned written = Allocate(4);
    const bool ok = written && SpoolWritePrinter(job.printer, job.buffer, job.used, written);
    if (written)
        Free(written);
    job.used = 0;
    return ok;
}

// WRITESPOOL, GDI.
int Application::PrintWriteSpool(unsigned handle, unsigned data, unsigned bytes)
{
    PrintJob *job = PrintJobOf(handle);
    if (!job)
        return (handle & 0x8000) || !handle ? int(short(handle)) : SpError;
    if (job->state < 0)
        return job->state;
    if (!PrintQueryAbort(job->hdc))
        job->state = SpAppAbort;
    if (job->flags & JobFile)
    {
        const unsigned done = Allocate(4);
        if (!done)
            return SpError;
        // _lwrite(hFile, lpData, cb).
        const unsigned write[] = {job->file, data, bytes};
        const unsigned n = Api("_lwrite", write);
        Free(done);
        if (n == 0xffffffffu)
            return SpError;
        if (!n && bytes)
        {
            // The disk-full dialog; it offers Retry and Cancel.
            Fail("WriteSpool found the disk full; the retry box of GDI is not built yet");
            return -4;
        }
        return job->state < 0 ? job->state : int(n);
    }
    // the page buffer, at least 4000h bytes, grown by the same step.
    if (job->used + bytes > job->capacity)
    {
        const unsigned step = bytes > 0x4000 ? bytes : 0x4000;
        const unsigned grown = Allocate(job->capacity + step);
        if (!grown)
            return 0;
        if (job->buffer)
        {
            unsigned char chunk[512];
            for (unsigned done = 0; done < job->used;)
            {
                const unsigned n = job->used - done < sizeof chunk ? job->used - done : unsigned(sizeof chunk);
                Read(job->buffer + done, chunk, n);
                Store(grown + done, chunk, n);
                done += n;
            }
            Free(job->buffer);
        }
        job->buffer = grown;
        job->capacity += step;
    }
    unsigned char chunk[512];
    for (unsigned done = 0; done < bytes;)
    {
        const unsigned n = bytes - done < sizeof chunk ? bytes - done : unsigned(sizeof chunk);
        Read(data + done, chunk, n);
        Store(job->buffer + job->used + done, chunk, n);
        done += n;
    }
    job->used += bytes;
    return job->state < 0 ? job->state : int(bytes);
}

// ENDSPOOLPAGE, GDI.
int Application::PrintEndSpoolPage(unsigned handle)
{
    PrintJob *job = PrintJobOf(handle);
    if (!job)
        return (handle & 0x8000) || !handle ? int(short(handle)) : SpError;
    if (job->state < 0)
        return job->state;
    if (job->flags == JobFile)
        return 1;
    if (!(job->flags & JobSpoolFile))
        PrintFlushJob(*job);
    if (!SpoolPrinterCall(RequestEndPage, "EndPagePrinter", job->printer))
        job->state = SpUserAbort;
    return job->state;
}

// DELETEJOB, GDI.
int Application::PrintDeleteJob(unsigned handle)
{
    PrintJob *job = PrintJobOf(handle);
    if (!job)
        return (handle & 0x8000) || !handle ? int(short(handle)) : SpError;
    if (job->flags & JobFile)
    {
        const unsigned close[] = {job->file};
        Api("_lclose", close);
        if (job->flags & JobSpoolFile)
            SpoolAbortPrinter(job->printer);
        else if (job->name[0])
        {
            const unsigned text = Allocate(0x110);
            if (text)
            {
                PutString(text, job->name);
                Api("DeleteFileA", &text);
                Free(text);
            }
        }
    }
    else
        SpoolAbortPrinter(job->printer);
    if (job->buffer)
        Free(job->buffer);
    if (PrintDc *dc = PrintFind(job->hdc))
        if (dc->job == job->handle)
            dc->job = 0;
    *job = {};
    return 1;
}

// CLOSEJOB, GDI. ClosePrinter ends the document; EndDocPrinter is
// not called, and a page buffer still full is not flushed.
int Application::PrintCloseJob(unsigned handle)
{
    PrintJob *job = PrintJobOf(handle);
    if (!job)
        return (handle & 0x8000) || !handle ? int(short(handle)) : SpError;
    if (job->state < 0)
        return PrintDeleteJob(handle);
    bool ok = true;
    if (job->flags & JobFile)
    {
        const unsigned close[] = {job->file};
        Api("_lclose", close);
        if (job->flags & JobSpoolFile)
            ok = SpoolPrinterCall(RequestClose, "ClosePrinter", job->printer);
    }
    else
    {
        ok = SpoolPrinterCall(RequestClose, "ClosePrinter", job->printer);
        if (job->buffer)
            Free(job->buffer);
    }
    if (PrintDc *dc = PrintFind(job->hdc))
        if (dc->job == job->handle)
            dc->job = 0;
    *job = {};
    return ok ? 1 : SpError;
}

unsigned Application::PrintAbortProcedure(PrintDc &dc) { return dc.abort_procedure; }

void Application::FreePrintSpool()
{
    delete print_jobs;
    print_jobs = nullptr;
    delete spool;
    spool = nullptr;
}
} // namespace X86
