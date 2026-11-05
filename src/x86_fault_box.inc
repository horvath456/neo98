// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 displays an unhandled-exception dialog unless
// SEM_NOGPFAULTERRORBOX or an application filter suppresses it. Load its
// template and strings from the user's KERNEL32.DLL. Details show registers,
// instruction bytes, and stack words. Hide Debug without an AeDebug debugger;
// do not write FAULTLOG.TXT. The host enables this interactive provider
// through Application::SetFaultBox. Without it, tests receive the usual
// process-error message.
namespace X86 {
bool Application::fault_box = false;
void Application::SetFaultBox(bool on) { fault_box = on; }
namespace {
constexpr unsigned FaultBoxToken = 0x71000058;
struct FaultBoxFrame
{
    Application *app;
    char title[64]{}, text[600]{}, details[4096]{};
    unsigned hwnd = 0;
};
FaultBoxFrame *fault_box_frames[8];
// One string of a string table block of a parsed PE, narrowed to ANSI.
bool FaultString(const Pe32::Image &image, unsigned id, char *out, unsigned capacity)
{
    Pe32::U32 size = 0;
    const unsigned char *block = image.Resource(6, id / 16 + 1, &size);
    if (!block || !capacity) return false;
    unsigned at = 0;
    for (unsigned i = 0; i < 16; ++i)
    {
        if (at + 2 > size) return false;
        const unsigned length = block[at] | block[at + 1] << 8;
        at += 2;
        if (at + length * 2 > size) return false;
        if (i == id % 16)
        {
            unsigned n = 0;
            for (unsigned k = 0; k < length && n + 1 < capacity; ++k)
            {
                const unsigned c = block[at + k * 2] | block[at + k * 2 + 1] << 8;
                out[n++] = c < 256 ? (char)c : '?';
            }
            out[n] = 0;
            return length != 0;
        }
        at += length * 2;
    }
    return false;
}
// FormatMessage for the inserts these strings use: %n is a string, %n!.0Wx!
// and %n!.0Wd! a number.
struct FaultInsert { const char *text; unsigned number; };
void FaultFormat(char *out, unsigned capacity, unsigned &end, const char *format, const FaultInsert *inserts, unsigned count)
{
    const auto put = [&](char c) { if (end + 1 < capacity) out[end++] = c; };
    for (const char *p = format; *p; ++p)
    {
        if (*p != '%' || p[1] < '1' || p[1] > '9') { if (*p == '\n') put('\r'); put(*p); continue; }
        const unsigned index = (unsigned)(p[1] - '1');
        ++p;
        char piece[64] = "";
        const char *text = piece;
        if (p[1] == '!')
        {
            const char *close = strchr(p + 2, '!');
            if (!close) continue;
            unsigned width = 0;
            for (const char *d = p + 2; d < close; ++d) if (*d >= '0' && *d <= '9') width = width * 10 + (unsigned)(*d - '0');
            const unsigned value = index < count ? inserts[index].number : 0;
            snprintf(piece, sizeof piece, close[-1] == 'd' ? "%0*u" : "%0*x", (int)width, value);
            p = close;
        }
        else if (index < count && inserts[index].text) text = inserts[index].text;
        for (; *text; ++text) put(*text);
    }
    out[end] = 0;
}
}
// One fault, one box. While the box of a process is open, its other
// windows get no message: under Win98 the faulting program stands in the box
// and its windows keep their last picture.
bool FaultBoxSilences(const Application *app, unsigned hwnd)
{
    for (const auto *frame : fault_box_frames)
    {
        if (!frame || frame->app != app) continue;
        if (!frame->hwnd) return false; // the box is being created
        for (unsigned h = hwnd, guard = 0; h && guard < 64; h = User::Parent(h), ++guard)
            if (h == frame->hwnd) return false;
        return true;
    }
    return false;
}
// Answers true when the box was shown and closed; false leaves the caller
// with the old stop.
bool Application::FaultBox(unsigned record, unsigned context)
{
    if (!fault_box || !user_initialized || state == Failed || state == Exited) return false;
    GuestExceptionContext registers;
    if (!Read(context, &registers, sizeof registers)) return false;
    const unsigned code = Read32(record), address = Read32(record + 12);
    char reason[128];
    snprintf(reason, sizeof reason, "Unhandled guest exception %08x at %08x", code, address);
    return FaultBox(code, address, registers.d, reason);
}
bool Application::FaultBox(unsigned code, unsigned address, const unsigned *d, const char *reason)
{
    if (!fault_box || !user_initialized || state == Failed || state == Exited) return false;
    // A second fault of the process while its box is open (a hook, a timer
    // procedure, another thread) gets no box of its own; the program ends as
    // it does after Close.
    for (const auto *frame : fault_box_frames)
        if (frame && frame->app == this)
        {
            if (log) log("FAULT BOX already open, the process ends");
            return true;
        }
    // The resources of the original on the drive.
    unsigned handle = 0, status = 0, size = 0, done = 0;
    if (files.Open("C:\\WINDOWS\\SYSTEM\\KERNEL32.DLL", 1, 1, 3, 0, handle, status)) return false;
    unsigned char *file = nullptr;
    if (!files.Size(handle, size) && size && size < 16u * 1024 * 1024)
    {
        file = new (std::nothrow) unsigned char[size];
        if (file && (files.Read(handle, file, size, done) || done != size)) { delete[] file; file = nullptr; }
    }
    files.Close(handle);
    if (!file) return false;
    struct Release { unsigned char *p; ~Release() { delete[] p; } } release{file};
    Pe32::Image kernel;
    if (!kernel.Parse(file, size)) return false;
    Pe32::U32 template_size = 0;
    const unsigned char *dialog_template = kernel.Resource(5, 1, &template_size);
    // 106..113 and 130 name the faults of the processor; any other exception
    // is "an exception xxH".
    const unsigned which = code == 0xc0000005u ? 113 : code == 0xc000001du ? 109 : code == 0xc0000094u ? 108
                         : code == 0xc0000095u ? 130 : code == 0xc00000fdu ? 111 : code == 0xc0000096u ? 112 : 107;
    char message[512], caused[256], unknown[64], line[160];
    if (!dialog_template || !FaultString(kernel, 104, message, sizeof message) ||
        !FaultString(kernel, which, caused, sizeof caused))
        return false;
    if (!FaultString(kernel, 103, unknown, sizeof unknown)) strcpy(unknown, "<unknown>");
    // The program by the name of its file without the extension, the module
    // that holds the address by its file name.
    char program[64] = "", title[64] = "";
    {
        const char *leaf = strrchr(program_name, '\\');
        leaf = leaf ? leaf + 1 : program_name;
        unsigned n = 0;
        for (; leaf[n] && leaf[n] != '.' && n + 1 < sizeof program; ++n)
        {
            const char c = leaf[n];
            program[n] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
            title[n] = n ? (c >= 'A' && c <= 'Z' ? (char)(c + 32) : c) : program[0];
        }
    }
    // d: 35 GS FS ES DS EDI ESI EBX EDX ECX EAX EBP EIP CS EFLAGS ESP SS.
    // A 16-bit task names the place as segment number and offset of its NE
    // module, the way KRNL386's faults read.
    const char *module = unknown;
    char win16_module[16];
    unsigned place = d[47];
    if (Win16Process())
    {
        unsigned segment = 0;
        if (Win16CodeModule(d[47], win16_module, sizeof win16_module, segment))
        {
            module = win16_module;
            place = segment;
        }
    }
    else
        for (unsigned i = 0; i < ModuleCapacity; ++i)
        {
            const auto &m = modules[i];
            if (!m.kind || !m.pe || address < m.pe->Base() || address - m.pe->Base() >= m.pe->SizeOfImage()) continue;
            const char *slash = strrchr(m.path, '\\');
            module = slash ? slash + 1 : m.path;
            break;
        }
    char details[4096];
    unsigned end = 0;
    {
        const FaultInsert inserts[] = {{program, 0}, {module, 0}, {nullptr, place}, {nullptr, address}, {nullptr, code}};
        FaultFormat(details, sizeof details, end, caused, inserts, 5);
    }
    const unsigned rows[4][4] = {{d[44], d[47], d[46], d[48]}, {d[41], d[50], d[49], d[45]},
                                 {d[43], d[38], d[40], d[36]}, {d[42], d[37], d[39], d[35]}};
    for (unsigned row = 0; row < 4; ++row)
        if (FaultString(kernel, 114 + row, line, sizeof line))
        {
            const FaultInsert inserts[] = {{nullptr, rows[row][0]}, {nullptr, rows[row][1]}, {nullptr, rows[row][2]}, {nullptr, rows[row][3]}};
            FaultFormat(details, sizeof details, end, line, inserts, 4);
        }
    char each[32];
    // CS:EIP and SS:ESP are flat on the 32-bit side and selector and offset
    // on the 16-bit one.
    const bool segmented = Win16Process();
    const unsigned code_at = segmented ? Win16Linear(d[47], d[46] & 0xffff) : d[46];
    const unsigned stack_at = segmented ? Win16Linear(d[50], d[49] & 0xffff) : d[49];
    if (FaultString(kernel, 118, line, sizeof line) && FaultString(kernel, 120, each, sizeof each))
    {
        FaultFormat(details, sizeof details, end, line, nullptr, 0);
        for (unsigned i = 0; i < 16 && code_at && virtual_memory.Access(code_at + i, 1, false); ++i)
        {
            unsigned char byte = 0;
            if (!Read(code_at + i, &byte, 1)) break;
            const FaultInsert insert{nullptr, byte};
            FaultFormat(details, sizeof details, end, each, &insert, 1);
        }
    }
    if (FaultString(kernel, 119, line, sizeof line) && FaultString(kernel, 122, each, sizeof each))
    {
        FaultFormat(details, sizeof details, end, line, nullptr, 0);
        for (unsigned i = 0; i < 16 && stack_at && virtual_memory.Access(stack_at + i * 4, 4, false); ++i)
        {
            const FaultInsert insert{nullptr, Read32(stack_at + i * 4)};
            FaultFormat(details, sizeof details, end, each, &insert, 1);
        }
    }
    // The first line of the details in the log, where a report finds it.
    if (log)
    {
        char text[400];
        unsigned n = 0;
        for (const char *p = details; *p && n + 1 < sizeof text; ++p)
        {
            if (*p == '\r') continue;
            if (*p == '\n') { if (!strncmp(p + 1, "Registers", 9)) break; text[n++] = ' '; continue; }
            text[n++] = *p;
        }
        text[n] = 0;
        char entry[480];
        snprintf(entry, sizeof entry, "FAULT BOX %s: %s", title, text);
        log(entry);
    }
    // The report of the host, of the guest as it stopped.
    if (reason)
        NotifyCrash(reason);
    fault_notified = true;
    char body[600];
    unsigned body_end = 0;
    FaultFormat(body, sizeof body, body_end, message, nullptr, 0);
    auto *held = new FaultBoxFrame{this};
    struct Holder { FaultBoxFrame *p; ~Holder() { delete p; } } holder{held};
    auto &frame = *held;
    memcpy(frame.title, title, sizeof title);
    memcpy(frame.text, body, body_end + 1);
    memcpy(frame.details, details, end + 1);
    unsigned slot = 0;
    while (slot < 8 && fault_box_frames[slot]) ++slot;
    if (slot == 8) return false;
    fault_box_frames[slot] = held;
    const unsigned hwnd = CreateDialogTemplate(dialog_template, template_size, 0, FaultBoxToken, 0, true);
    frame.hwnd = hwnd;
    const bool kept = hwnd && PushHostFrame(HostFaultBox, hwnd, slot, depth, code);
    if (hwnd) RunModal(hwnd, kept);
    if (kept) PopHostFrame();
    fault_box_frames[slot] = nullptr;
    if (log) log(hwnd ? "FAULT BOX closed" : "FAULT BOX could not be created");
    return hwnd != 0;
}
// A program that reaches a function this port does not carry stops in
// Fail. Under Win98 the program would have gone on; here it cannot, and
// ending it without a word left the person with a window that vanished.
// Where the runner provides the box, the stop now ends the way an
// unhandled exception ends: the same dialog of KERNEL32.DLL, string 107 with
// STATUS_NOT_IMPLEMENTED as the exception ("caused an exception C0000002H in
// module..."), at the place the guest stood. The report of the host was
// written by Fail before; the process ends as before once the box is closed.
// Only a stop on a thread of the guest gets it: the box is a modal loop on
// that thread, so a stop of the loader, of a state being loaded, of the
// teardown or of host code without a running fiber keeps the plain stop, as
// does a second stop while the box of the process is open.
void Application::FailBox(State before)
{
    if (!fault_box || !user_initialized || load_failure || (before != Running && before != Idle)) return;
    if (!HasProcessor() || current_thread >= ThreadCapacity) return;
    if (cpu_runs)
    {
        fail_box_deferred = true;
        fail_box_before = before;
        return;
    }
    const auto &thread = threads[current_thread];
    if (!thread.fiber || !thread.fiber->Active() || thread.done) return;
    for (const auto *frame : fault_box_frames)
        if (frame && frame->app == this) return;
    unsigned d[51] = {};
    d[0] = ContextFlags;
    for (unsigned i = 0; i < 16; ++i) d[35 + i] = Register(ContextRegisters[i]);
    // A stop inside a gate of this port stands at the gate's own code, which
    // is in no module. The place the program knows is its call: the return
    // address on top of the stack, a far one on the 16-bit side.
    if (Win16Process())
    {
        char leaf[16];
        unsigned segment = 0;
        const unsigned top = Win16Linear(d[50], d[49] & 0xffff);
        if (!Win16CodeModule(d[47], leaf, sizeof leaf, segment) && top && virtual_memory.Access(top, 4, false) &&
            Win16CodeModule(Win16Word(top + 2), leaf, sizeof leaf, segment))
        {
            d[46] = Win16Word(top);
            d[47] = Win16Word(top + 2);
            d[49] = (d[49] & 0xffff0000u) | ((d[49] + 4) & 0xffff);
        }
    }
    else
    {
        const auto inside = [&](unsigned address) {
            for (unsigned i = 0; i < ModuleCapacity; ++i)
            {
                const auto &m = modules[i];
                if (m.kind && m.pe && address >= m.pe->Base() && address - m.pe->Base() < m.pe->SizeOfImage())
                    return true;
            }
            return false;
        };
        if (!inside(d[46]) && virtual_memory.Access(d[49], 4, false) && inside(Read32(d[49])))
        {
            d[46] = Read32(d[49]);
            d[49] += 4;
        }
    }
    const unsigned address = Win16Process() ? d[46] & 0xffff : d[46];
    const State failed = state;
    state = before;
    FaultBox(0xc0000002u, address, d, nullptr);
    fault_notified = false;
    if (state != Exited) state = failed;
}
// A fault has no guest continuation: closing this final UI ends the process.
// Only the idle loop itself is a save point. A hook or a callback inside it
// must first return, so it cannot borrow the exception to bypass the checks.
bool Application::SavedFaultBoxThread(unsigned index) const
{
    const auto &t = threads[index];
    if (!t.host_waiting || t.host_frame_count < 2) return false;
    const auto &box = t.host_frames[t.host_frame_count - 2];
    const auto &modal = t.host_frames[t.host_frame_count - 1];
    return box.kind == HostFaultBox && modal.kind == HostModal && (modal.d & 2) &&
           modal.a == box.a && t.depth == box.c && box.b < 8 &&
           fault_box_frames[box.b] && fault_box_frames[box.b]->app == this &&
           fault_box_frames[box.b]->hwnd == box.a;
}
bool Application::ResumeFaultBox()
{
    auto &t = threads[current_thread];
    const unsigned at = t.host_frame_count - 2;
    const auto box = t.host_frames[at];
    // The handler stack is abandoned. Its registers remain owned by their
    // marks until the dialog has closed, but are never restored or executed.
    ExceptionState().handler_frames = 0;
    ExceptionState().calling_exception_handler = false;
    ExceptionState().abandon_from = ~0u;
    t.host_resume = true;
    t.host_resume_at = at + 1;
    t.host_resume_top = t.host_resume_level = depth;
    t.tail = TailNone;
    RunModal(box.a, true);
    t.host_resume = false;
    // Keep the fault registration through termination: it prevents focus
    // handover from calling the faulting window procedure again.
    if (state != Failed) TerminateSelf(box.d);
    delete fault_box_frames[box.b];
    fault_box_frames[box.b] = nullptr;
    for (unsigned i = 0; i < ThreadCapacity; ++i)
    {
        auto &thread = threads[i];
        // A loaded background fiber may already own these captures through
        // its rebuilt C++ chain. Its local marks remain valid until the
        // application's destructor lets that chain unwind.
        if (i != current_thread && thread.fiber && thread.fiber->Started() && !thread.fiber->Finished())
            continue;
        for (auto &mark : thread.call_marks)
        {
            if (mark.context) { DropWin16HostedContext(mark.context); cpu->FreeContext(mark.context); }
            mark = {};
        }
        for (unsigned i = 0; i < thread.host_frame_count; ++i)
        {
            const auto &frame = thread.host_frames[i];
            if (frame.kind == HostModal && (frame.d & 1) && User::IsWindow(frame.b))
                User::EnableWindow(frame.b, true);
            if (frame.kind == HostDialogBox && frame.a && User::IsWindow(frame.b))
                User::EnableWindow(frame.b, true);
        }
        thread.depth = thread.pending = thread.host_frame_count = 0;
        thread.host_waiting = thread.host_resume = false;
    }
    DiscardSavedMessageBoxes();
    DiscardSavedFileDialogs();
    DiscardSavedWin16FileDialogs();
    fault_notified = false;
    pending = depth = 0;
    if (log) log("FAULT BOX closed from saved state; process ended");
    return false;
}
unsigned Application::FaultBoxMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    (void)lp;
    FaultBoxFrame *frame = nullptr;
    for (auto *candidate : fault_box_frames)
        if (candidate && candidate->app == this && (candidate->hwnd == hwnd || (!candidate->hwnd && message == 0x110)))
            frame = candidate;
    if (!frame) return 0;
    const auto set = [&](unsigned window, const char *text) {
        const unsigned guest = String(text);
        if (!guest) return;
        Send(window, 0xc, 0, guest);
        Free(guest);
    };
    if (message == 0x110)
    {
        frame->hwnd = hwnd;
        set(hwnd, frame->title);
        set(User::GetDlgItem(hwnd, 3), frame->text);
        set(User::GetDlgItem(hwnd, 8), frame->details);
        // The stop sign, IDI_HAND.
        unsigned icon_arguments[] = {0, 32513};
        if (const unsigned icon = Api("LoadIconA", icon_arguments)) Send(User::GetDlgItem(hwnd, 9), 0x170, icon, 0);
        return 1;
    }
    if (message != 0x111) return 0;
    const unsigned id = wp & 0xffff;
    if (id == 1 || id == 2)
    {
        unsigned arguments[] = {hwnd, 1};
        Api("EndDialog", arguments);
        return 1;
    }
    if (id == 4)
    {
        // Details>>: the dialog grows down to the mark below the text field
        // (control 7 of the template), and the button has done its part.
        const unsigned memory = Allocate(48);
        if (!memory) return 1;
        const unsigned mark = User::GetDlgItem(hwnd, 7);
        unsigned window_arguments[] = {hwnd, memory}, mark_arguments[] = {mark, memory + 16};
        Api("GetWindowRect", window_arguments);
        Api("GetWindowRect", mark_arguments);
        const unsigned left = Read32(memory), top = Read32(memory + 4), right = Read32(memory + 8);
        const unsigned bottom = Read32(memory + 20); // top of the mark, in screen points
        unsigned frame_y[] = {8}; // SM_CYDLGFRAME, the border below the client area
        const unsigned border = Api("GetSystemMetrics", frame_y);
        if (mark && bottom > top)
        {
            unsigned position[] = {hwnd, 0, 0, 0, right - left, bottom - top + border, 6};
            Api("SetWindowPos", position);
        }
        if (log)
        {
            char text[96];
            snprintf(text, sizeof text, "FAULT BOX details height %u to %u", Read32(memory + 12) - top, bottom - top + border);
            log(text);
        }
        Free(memory);
        User::EnableWindow(User::GetDlgItem(hwnd, 4), false);
        unsigned focus[] = {User::GetDlgItem(hwnd, 1)};
        Api("SetFocus", focus);
        return 1;
    }
    return 0;
}
}
