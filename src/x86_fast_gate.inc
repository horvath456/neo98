// SPDX-License-Identifier: GPL-2.0-or-later
// API gates that answer without leaving uc_emu_start.
// An ordinary gate holds hlt. The hook remembers the address, stops the run,
// and Application::Dispatch serves the call with the engine at rest: it
// writes EAX, corrects ESP and sets EIP to the return address. A gate on the
// positive list below holds the return instruction of its own calling
// convention instead. The hook then runs the handler, writes EAX and returns,
// and the engine executes the ret itself, with the right stack correction.
// Every call may still take the ordinary way. FastGate answers false before it
// has changed anything, the hook falls back to the stop, and Dispatch runs
// as before. That is what makes EnterCriticalSection possible: the fast path
// takes the section only when nobody has to wait and nobody has to be woken,
// and hands the contended case back.
namespace X86 {
bool Application::fast_gates = true;
// The mark of a gate that jumps into the guest code of a critical section
// (Application::ArmCriticalBody). Its first byte is
// that jump.
constexpr unsigned CriticalBodyGate = 0xe9;
// The positive list. A handler belongs here only if it never waits, never
// changes thread or fiber, never calls back into the guest, never raises a
// guest exception and never ends the process. The list is deliberately short;
// only calls that benefit from this fast path belong here.
unsigned Application::FastGateReturn(const Import &item) const
{
    if (!fast_gates || stub_mode || trace_calls)
        return 0;
    // Only a plain argument count, with 0x80000000 for the caller's own stack
    // correction. 0x20000000 is the unsupported original result, 0x40000000 a
    // handler that sets EIP and ESP itself, 0xffffffff and 0xfffffffe are
    // unbound and FlatThunkGate is a transition; all of them are rejected here.
    if (item.argc & ~0x800000ffu)
        return 0;
    const unsigned count = item.argc & 255;
    if (count > 8)
        return 0;
    // Only the three general routes. The SHELL32, SHLWAPI and COMCTL32 routes
    // reach code that draws and calls back.
    // The gl* entries of the OpenGL driver's dispatch table: the context
    // computes and queues, draws only at a flush, and never calls back.
    if (item.route == RouteOpenGL)
        return item.route_argument < 1000 ? 0xc2u | (count * 4u) << 8 : 0;
    // The Glide driver computes, queues and draws without calling back;
    // the calls that wait or change the display mode are excluded.
    if (item.route == RouteGlide)
        return GlideFastEntry(item.route_argument) ? 0xc2u | (count * 4u) << 8 : 0;
    if (item.route != RouteApi && item.route != RouteGdi && item.route != RouteCrt)
        return 0;
    const char *const dll = item.dll, *const name = item.name;
    auto is = [name](const char *other) { return !strcmp(name, other); };
    bool listed = false;
    if (Equal(dll, "KERNEL32.DLL"))
        listed =
            // Clocks and identity. Every one of them reads a counter.
            is("GetTickCount") || is("GetCurrentThreadId") || is("GetCurrentProcessId") ||
            is("GetCurrentThread") || is("GetCurrentProcess") ||
            is("QueryPerformanceCounter") || is("QueryPerformanceFrequency") ||
            // The error word of the running thread.
            is("GetLastError") || is("SetLastError") ||
            // Thread-local storage: an index into the slot array of the thread.
            is("TlsGetValue") || is("TlsSetValue") ||
            // The interlocked family is arithmetic on one guest dword.
            is("InterlockedIncrement") || is("InterlockedDecrement") ||
            is("InterlockedExchange") || is("InterlockedExchangeAdd") ||
            is("InterlockedCompareExchange") ||
            // String length and comparison over guest memory.
            is("lstrlenA") || is("lstrlenW") || is("lstrcmpA") || is("lstrcmpiA") ||
            is("lstrcpyA") || is("lstrcpynA") ||
            // Conversion only; FastGate leaves NLS startup to Dispatch.
            is("GetCPInfo") || is("MultiByteToWideChar") || is("WideCharToMultiByte") ||
            // The pointer probes read the page table of the process.
            is("IsBadReadPtr") || is("IsBadWritePtr") || is("IsBadCodePtr") ||
            is("IsBadHugeReadPtr") || is("IsBadHugeWritePtr") ||
            is("IsBadStringPtrA") || is("IsBadStringPtrW") ||
            // One table lookup in the code page.
            is("IsDBCSLeadByte") || is("IsDBCSLeadByteEx") ||
            // The uncontended case only; FastGate asks CriticalUncontended.
            is("EnterCriticalSection") || is("LeaveCriticalSection");
    else if (Equal(dll, "USER32.DLL"))
        listed =
            // Character stepping over a guest string.
            is("CharNextA") || is("CharPrevA") ||
            // Rectangle arithmetic in guest memory.
            is("SetRect") || is("SetRectEmpty") || is("IsRectEmpty") || is("PtInRect") ||
            is("CopyRect") || is("OffsetRect") || is("InflateRect") || is("EqualRect") ||
            // Read-only questions about a window or the session.
            is("IsWindow") || is("GetPropA") || is("GetSysColor") || is("GetSystemMetrics") ||
            is("GetCursorPos");
    else if (Equal(dll, "GDI32.DLL"))
        listed =
            // Pure device-context state. None of them draws.
            is("GetBkMode") || is("SetBkMode") || is("GetBkColor") || is("SetBkColor") ||
            is("GetTextColor") || is("SetTextColor") || is("GetROP2") || is("SetROP2") ||
            is("GetMapMode") || is("GetStockObject") || is("GetTextAlign") || is("SetTextAlign");
    else if (Equal(dll, "WINMM.DLL"))
        listed = is("timeGetTime");
    if (!listed)
        return 0;
    // 0x80000000 is the caller's own stack correction, so cdecl.
    if (item.argc & 0x80000000u)
        return 0xc3;
    return 0xc2u | (count * 4u) << 8;
}
// Writes the return instruction of a bound import into its gate and drops the
// translated block of that gate, because uc_mem_write of Unicorn does not:
// invalidate_and_set_dirty in qemu/exec.c is empty in this engine.
void Application::ArmFastGate(Import &item, unsigned gate)
{
    // The fast gate is x86 code in the gate; an ARM64 process has none.
    if (Arm64Process())
        return;
    item.fast_return = FastGateReturn(item);
    if (!item.fast_return)
        return;
    // EnterCriticalSection and LeaveCriticalSection get a body of guest code
    // instead of a return. Without the code page they keep the return and the
    // host answers the uncontended case, as before.
    if (ArmCriticalBody(item, gate))
    {
        item.fast_return = CriticalBodyGate;
        return;
    }
    unsigned char code[16];
    memset(code, 0xf4, sizeof code);
    unsigned length = 1;
    code[0] = (unsigned char)item.fast_return;
    if (code[0] == 0xc2)
    {
        code[1] = (unsigned char)(item.fast_return >> 8);
        code[2] = (unsigned char)(item.fast_return >> 16);
        length = 3;
    }
    if (cpu->Backend() == CpuBackend::Native)
    {
        code[0] = 0xf4;
        length = 1;
    }
    // Not Store: the gate region is read and execute for the guest, and the
    // guest-visible protection is exactly what must stay that way. uc_mem_write
    // is the host writing, the way the hlt pattern got there before Protect.
    if (cpu->Write(gate, code, length) != CpuError::Ok ||
        !Check(cpu->InvalidateCode(gate, 16)))
        item.fast_return = 0;
}
// Puts hlt back and returns the import to the ordinary way, after a handler
// turned out to do something the positive list rules out.
void Application::WithdrawFastGate(Import &item, unsigned gate, const char *why)
{
    item.fast_return = 0;
    unsigned char code[16];
    memset(code, 0xf4, sizeof code);
    cpu->Write(gate, code, sizeof code);
    Check(cpu->InvalidateCode(gate, 16));
    ++fast_gate_withdrawn;
    if (log)
    {
        char text[256];
        snprintf(text, sizeof text, "FASTGATE withdrawn %s!%s reason=%s call=%u", item.dll, item.name,
                 why, api_calls);
        log(text);
    }
}
bool Application::FastGate(unsigned gate)
{
    if (!fast_gates || waiting || state == Failed || state == Exited)
        return false;
    if (gate < Gates || gate >= Gates + import_count * 16 || ((gate - Gates) & 15) || gate == Exit ||
        gate == Callback)
        return false;
    auto &item = imports[(gate - Gates) / 16];
    if (!item.fast_return || item.fast_return == TrivialBodyGate)
        return false;
    // The gate jumps into the guest code of the section, which takes the
    // uncontended case and otherwise comes back to GateEntry below.
    if (item.fast_return == CriticalBodyGate)
    {
        // Native executable bodies normally never enter this hook. Keep its
        // counters as host entries even when a host Call starts at the gate.
        if (!(cpu->Backend() == CpuBackend::Native && !trace_calls && !export_census))
        {
            ++api_calls;
            ++guest_gate_calls;
        }
        return true;
    }
    // Initializing NLS can take the process CRT lock. Only the initialized
    // conversion path belongs in the hook, including after a state restore.
    if (!system_locale.initialized &&
        (item.id == API_ID("GetCPInfo") || item.id == API_ID("MultiByteToWideChar") ||
         item.id == API_ID("WideCharToMultiByte")))
        return false;
    const unsigned count = item.argc & 255;
    const unsigned sp = Register(Reg::Esp);
    unsigned args[16] = {};
    if (count && !Read(sp + 4, args, count * 4))
        return false;
    // UTF-7 and the optional composition tables can fail the process.
    // Keep those conversions on Dispatch, before any output is written.
    if ((item.id == API_ID("MultiByteToWideChar") || item.id == API_ID("WideCharToMultiByte")) &&
        (args[0] == 65000 || args[1]))
        return false;
    const ApiName name(item.name, item.id);
    // A call on the screen's pixels takes the ordinary way while a primary
    // surface is locked; Dispatch waits there (src/x86_screen_lock.cpp).
    if (item.screen_pixels && User::ScreenLockActive())
        return false;
    // The contended critical section keeps the ordinary way, because only
    // there a thread waits or has to be woken. Nothing has changed yet.
    if ((name.id == API_ID("EnterCriticalSection") || name.id == API_ID("LeaveCriticalSection")) &&
        !CriticalUncontended(name, args[0]))
        return false;
    const unsigned ret = Read32(sp);
    ++api_calls;
    ++fast_gate_calls;
    const unsigned token = api_ring.Begin<true>(item.dll, item.name, ThreadId(), ret, args, count);
    if (!item.calls++ && log)
        log(item.name);
    const unsigned before_callbacks = callbacks, before_depth = depth, before_thread = current_thread;
    unsigned value;
    in_fast_gate = true;
    LoadError();
    switch (item.route)
    {
    case RouteGdi: value = GdiApi(name, args); break;
    case RouteCrt: value = CrtApi(name, args); break;
    case RouteOpenGL: value = OpenGLApi(item.route_argument, args); break;
    case RouteGlide: value = GlideApi(item.route_argument, args); break;
    default: value = Api(name, args); break;
    }
    if (state != Failed && state != Exited) StoreError();
    in_fast_gate = false;
    // A handler of the list must never end up here wanting to wait: the guest
    // is about to execute the ret and there is no way back to this gate.
    if (waiting)
    {
        char message[256];
        snprintf(message, sizeof message, "Fast gate handler asked to wait: %s!%s", item.dll, item.name);
        Fail(message);
    }
    if (state == Failed || state == Exited)
    {
        // The ret must not run. Stopping from a code hook keeps the engine
        // before the instruction, exactly as the ordinary gate does.
        cpu->Stop();
        return true;
    }
    api_ring.Finish(token, value);
    Register(Reg::Eax, value);
    // The call itself was correct even here, because a callback returns before
    // the handler does. Only the list was wrong, so this import goes back to
    // the ordinary way and the next call takes it.
    if (callbacks != before_callbacks || depth != before_depth || current_thread != before_thread)
        WithdrawFastGate(item, gate, "called back into the guest");
    // Native API entries trap at HLT. Return through the saved call frame
    // without resuming the tracee just to execute RET.
    unsigned selector = 0;
    if (cpu->Backend() == CpuBackend::Native && !cpu->Get(Reg::Cs, selector) && !(selector & 4))
    {
        Register(Reg::Esp, sp + 4 + ((item.argc & 0x80000000u) ? 0 : count * 4));
        Register(Reg::Eip, ret);
    }
    return true;
}
// The address the gate hook hands to Dispatch. The second half of a critical
// section gate is the ordinary entry of its import: the guest code jumps there
// with the stack as the caller left it whenever it cannot finish the call
// itself, and Dispatch serves it from the start of the gate. The call was
// counted at the jump already, and Dispatch counts it again.
unsigned Application::GateEntry(unsigned address)
{
    if (address < Gates || address >= Gates + import_count * 16 || ((address - Gates) & 15) != 8)
        return address;
    if (imports[(address - Gates) / 16].fast_return != CriticalBodyGate)
        return address;
    if (!(cpu->Backend() == CpuBackend::Native && !trace_calls && !export_census))
    {
        --api_calls;
        --guest_gate_calls;
    }
    return address - 8;
}
} // namespace X86
