// SPDX-License-Identifier: GPL-2.0-or-later
// The USER and GDI gates of the 16-bit side, PLAN.md step 9, second part.
// The 16-bit side is a second entrance to the same native services. A 16-bit
// program calls USER.EXE and GDI.EXE ordinals with the Pascal frame; the gate
// takes the frame apart, hands the arguments to the native USER and GDI this
// project already has, and answers in DX:AX. Handles stay 16 bit, because that
// is what they are on both sides here.
// Three things are new against the 32-bit entrance:
// - A window procedure of a 16-bit program is a far pointer. USER keeps a
// flat mark for it, the way USER.EXE keeps a mark for a 32-bit procedure,
// and the mark is called back on the thread of the task with a Pascal frame.
// - The messages that pack wParam and lParam differently in 16 bit are
// translated in both directions; the rules are in win98_win16_user.h.
// - Resources come out of the NE image, not out of a PE resource directory.
// There are no success stubs here. An ordinal this port does not carry stops
// the run and names itself.
//
#include "gdi.h"
#include "gdi_font_catalog.h"
#include "dialog_template.h"
#include "win16_machine.h"
#include "win98_ne_image.h"
#include "win98_ne_task.h"
#include "win98_font_create.h"
#include "win98_shell_about.h"
#include "win98_win16_user.h"
#include "win98_win16_controls.h"
#include "x86_app.h"
#include "x86_comm_state.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace Win16U = Win98Win16User;
namespace Win16C = Win98Win16Controls;
namespace
{
bool SameText(const char *a, const char *b)
{
    while (*a || *b)
    {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
    }
    return true;
}
// The far return address a 16-bit callback returns to. It is the Callback slot
// of the gate region, so the existing code hook fires and the transition knows
// the procedure came back. See x86_app.cpp, where the same address ends a
// 32-bit callback.
constexpr unsigned CallbackReturn = 0x7000fff0;
} // namespace

// ------------------------------------------------------------------- marks
// A 16-bit window procedure is a far pointer, and every native path that
// dispatches a message carries a flat one. USER.EXE solves the same problem
// the other way round: it keeps a 16-bit mark for a 32-bit procedure and
// recognises it again when the mark comes back. The marks here live above
// 0x71010000, which is the range native USER already treats as guest code.
bool Application::Win16IsMark(unsigned procedure)
{
    return procedure >= Win16MarkBase && procedure < Win16MarkBase + 0x10000;
}
unsigned Application::Win16Mark(unsigned far_pointer)
{
    if (!far_pointer || !win16.state)
        return 0;
    auto &machine = *win16.state;
    for (unsigned i = 0; i < machine.procedure_count; ++i)
        if (machine.procedures[i].far_pointer == far_pointer)
            return Win16MarkBase + i;
    if (machine.procedure_count >= 256)
        return 0;
    const unsigned index = machine.procedure_count++;
    machine.procedures[index] = Win16Machine::Procedure{far_pointer, 0};
    return Win16MarkBase + index;
}
unsigned Application::Win16MarkPointer(unsigned mark) const
{
    if (!win16.state || !Win16IsMark(mark))
        return 0;
    const unsigned index = mark - Win16MarkBase;
    return index < win16.state->procedure_count ? win16.state->procedures[index].far_pointer : 0;
}
// The other direction: a procedure of ours handed to 16-bit code. It has to be
// a far pointer the program can store and hand back to CallWindowProc, so it
// becomes a gate of the gate region whose ordinal names the native procedure.
// The gate is never entered by a far call from the program in practice; what
// the program does is pass it back, and Win16Procedure recognises it.
unsigned Application::Win16NativePointer(unsigned procedure)
{
    if (!procedure || !win16.state)
        return 0;
    auto &machine = *win16.state;
    for (unsigned i = 0; i < machine.procedure_count; ++i)
        if (machine.procedures[i].native == procedure && machine.procedures[i].far_pointer)
            return machine.procedures[i].far_pointer;
    if (machine.procedure_count >= 256)
        return 0;
    const unsigned index = machine.procedure_count;
    char name[64];
    snprintf(name, sizeof name, "WindowProcedureThunk%u", index);
    const unsigned gate = Win16Gate("USER", name, Win16ThunkOrdinal + index);
    if (!gate)
        return 0;
    const unsigned pointer = Win16GatePointer(gate);
    if (!pointer)
        return 0;
    ++machine.procedure_count;
    machine.procedures[index] = Win16Machine::Procedure{pointer, procedure};
    return pointer;
}
// A far pointer the 16-bit side holds, turned back into something the native
// dispatch understands: a mark for original 16-bit code and the native
// procedure itself for one of ours.
unsigned Application::Win16Procedure(unsigned far_pointer)
{
    if (!far_pointer || !win16.state)
        return 0;
    for (unsigned i = 0; i < win16.state->procedure_count; ++i)
        if (win16.state->procedures[i].far_pointer == far_pointer && win16.state->procedures[i].native)
            return win16.state->procedures[i].native;
    return Win16Mark(far_pointer);
}
// And the value a 16-bit program reads back out of GWL_WNDPROC.
unsigned Application::Win16ProcedurePointer(unsigned procedure)
{
    if (!procedure)
        return 0;
    if (Win16IsMark(procedure))
        return Win16MarkPointer(procedure);
    return Win16NativePointer(procedure);
}

// ------------------------------------------------------ the 16-bit callback

// Call a Win16 window procedure using Pascal argument order: HWND, message,
// WORD wParam, and DWORD lParam. Return DX:AX. Build the frame on the task's
// stack and return through the callback gate. Initialize DS to the task's
// automatic data segment; a MakeProcInstance wrapper may replace it with the
// captured instance.
unsigned Application::Win16Invoke(unsigned far_pointer, unsigned hwnd, unsigned message, unsigned wparam,
                                  unsigned lparam)
{
    // Whether this callback is the last thing its caller does (GiveTail).
    const unsigned tail = TakeTail();
    Win16TaskStack task_stack(*this);
    if (!far_pointer || !win16.state || state == Failed || state == Exited)
        return 0;
    if (++depth > 32)
    {
        Fail("Win16 window callback nesting exceeded");
        --depth;
        return 0;
    }
    CpuContext *context = nullptr;
    if (!Check(cpu->NewContext(context)))
    {
        --depth;
        return 0;
    }
    Check(cpu->Save(context));
    const unsigned old_pending = pending;
    const bool old_waiting = waiting;
    waiting = false;
    const unsigned stack = Register(Reg::Ss);
    unsigned sp = (Register(Reg::Esp) & 0xffff) & ~1u;
    const unsigned base = Win16Linear(stack, 0);
    sp -= 14;
    Win16Word(base + sp + 4, lparam & 0xffff);
    Win16Word(base + sp + 6, lparam >> 16);
    Win16Word(base + sp + 8, wparam & 0xffff);
    Win16Word(base + sp + 10, message & 0xffff);
    Win16Word(base + sp + 12, hwnd & 0xffff);
    Win16Word(base + sp, CallbackReturn & 0xffff);
    Win16Word(base + sp + 2, Win16GateSelector());
    Register(Reg::Ss, stack);
    Register(Reg::Esp, sp);
    // A task's data segment is its stack segment. A library hosted in a
    // 32-bit process (SYSDM.CPL in RUNDLL32) has no task instance; its
    // procedure is entered with DS on the stack too, never with a null
    // selector: the dialog procedure of the page Hardware Profiles (SYSDM) hands wsprintf a near pointer to a local for "%s", and the
    // word USER reads as its segment is the DS the prologue saved.
    Register(Reg::Ds, win16.state->instance_handle ? win16.state->instance_handle : stack);
    Register(Reg::Eax, win16.state->instance_handle);
    // DX carries the high half of a long answer. A procedure that answers a
    // BOOL, which is what every dialog procedure answers, only writes AX, so
    // the high half is cleared before the call instead of being read out of
    // whatever the last call left behind.
    Register(Reg::Edx, 0);
    Register(Reg::Cs, far_pointer >> 16);
    Register(Reg::Eip, far_pointer & 0xffff);
    ++callbacks;
    // The mark a saved state takes this frame up again by
    // (Application::ResumeCall): the registers to put back, and the stack
    // pointer Win16Deliver puts back after them.
    {
        auto &t = threads[current_thread];
        t.call_marks[depth] = {0, 0, old_pending, context, task_stack.flat ? 0u : tail, t.host_frame_count, 1, t.tail_sp,
                              reinterpret_cast<unsigned long long>(__builtin_frame_address(0))};
    }
    bool returned = false;
    while (state != Failed && state != Exited && !threads[current_thread].done)
    {
        if (!Step())
            break;
        if (pending == CallbackReturn)
        {
            returned = true;
            break;
        }
        if (pending && !Service())
            break;
    }
    threads[current_thread].call_marks[depth] = {};
    const unsigned result = (Register(Reg::Eax) & 0xffff) | ((Register(Reg::Edx) & 0xffff) << 16);
    if (!returned && state != Failed && state != Exited && !threads[current_thread].done)
    {
        char reason[220];
        snprintf(reason, sizeof reason, "Win16 window callback interrupted procedure=%04x:%04x message=%04x",
                 far_pointer >> 16, far_pointer & 0xffff, message);
        Fail(reason);
    }
    Check(cpu->Restore(context));
    cpu->FreeContext(context);
    pending = old_pending;
    waiting = old_waiting;
    --depth;
    return result;
}
// EnumFonts passes 32-bit records through the shared font provider. Narrow
// those records and enter the original far callback with its 14-byte Pascal
// argument block: LOGFONT16, TEXTMETRIC16, font type and LPARAM.
unsigned Application::Win16FontCallback(unsigned far_pointer, const unsigned char *record, unsigned font_type,
                                        unsigned parameter)
{
    Win16TaskStack task_stack(*this);
    if (!far_pointer || !win16.state || state == Failed || state == Exited)
        return 0;
    // The record of the shared enumeration: LOGFONTA at 0, the full name at
    // 60, the style at 124, NEWTEXTMETRICA at 188.
    const unsigned char *logfont = record;
    Gdi::TEXTMETRICA metrics;
    memcpy(&metrics, record + 188, sizeof metrics);
    unsigned extra[4] = {};
    memcpy(extra, record + 188 + sizeof metrics, sizeof extra);
    // ENUMLOGFONT and NEWTEXTMETRIC of the 16-bit side: LOGFONT with WORD
    // sizes (50 bytes), 64 bytes of full name and 32 of style; TEXTMETRIC
    // with WORD sizes (31 bytes), ntmFlags, then ntmSizeEM, ntmCellHeight
    // and ntmAvgWidth as WORDs. GDI and share the body, so
    // EnumFonts and EnumFontFamilies hand over the same records.
    constexpr unsigned LogfontBytes = 50 + 64 + 32, MetricBytes = Win16U::TextMetricBytes + 10;
    const unsigned stack = Register(Reg::Ss);
    unsigned sp = (Register(Reg::Esp) & 0xffff) & ~1u;
    // USER and GDI check the task's stack before they set up a
    // callback, as Win16WordBreakCallback does.
    if (sp < LogfontBytes + MetricBytes + 18 + 64)
    {
        Fail("Win16 font callback exceeds its stack");
        return 0;
    }
    if (++depth > 32)
    {
        Fail("Win16 font callback nesting exceeded");
        --depth;
        return 0;
    }
    CpuContext *context = nullptr;
    if (!Check(cpu->NewContext(context)))
    {
        --depth;
        return 0;
    }
    Check(cpu->Save(context));
    const unsigned old_pending = pending;
    const bool old_waiting = waiting;
    waiting = false;
    const unsigned base = Win16Linear(stack, 0);
    sp = (sp - LogfontBytes) & ~1u;
    const unsigned logfont_at = sp;
    for (unsigned i = 0; i < 5; ++i)
    {
        unsigned value = 0;
        memcpy(&value, logfont + i * 4, sizeof value);
        Win16Word(base + logfont_at + i * 2, value);
    }
    Store(base + logfont_at + 10, logfont + 20, 40);
    Store(base + logfont_at + 50, record + 60, 64 + 32);
    sp = (sp - MetricBytes) & ~1u;
    const unsigned metrics_at = sp;
    const int values[] = {metrics.tmHeight, metrics.tmAscent, metrics.tmDescent,
                          metrics.tmInternalLeading, metrics.tmExternalLeading,
                          metrics.tmAveCharWidth, metrics.tmMaxCharWidth, metrics.tmWeight,
                          metrics.tmOverhang, metrics.tmDigitizedAspectX, metrics.tmDigitizedAspectY};
    for (unsigned i = 0; i < 11; ++i)
        Win16Word(base + metrics_at + i * 2, values[i]);
    Store(base + metrics_at + Win16U::TextMetricFirstChar, &metrics.tmFirstChar, 9);
    Store(base + metrics_at + Win16U::TextMetricBytes, &extra[0], 4);
    for (unsigned i = 1; i < 4; ++i)
        Win16Word(base + metrics_at + Win16U::TextMetricBytes + 2 + i * 2, extra[i]);
    sp -= 18;
    Win16Word(base + sp, CallbackReturn & 0xffff);
    Win16Word(base + sp + 2, Win16GateSelector());
    Win16Word(base + sp + 4, parameter);
    Win16Word(base + sp + 6, parameter >> 16);
    Win16Word(base + sp + 8, font_type);
    Win16Word(base + sp + 10, metrics_at);
    Win16Word(base + sp + 12, stack);
    Win16Word(base + sp + 14, logfont_at);
    Win16Word(base + sp + 16, stack);
    Register(Reg::Esp, sp);
    // A library hosted in a 32-bit process (a printer driver) gets its own
    // data segment back, the one it called GDI with; a task its instance.
    const unsigned data = win16.state->hosted ? Register(Reg::Ds) : win16.state->instance_handle;
    Register(Reg::Ds, data);
    Register(Reg::Eax, data);
    Register(Reg::Edx, 0);
    Register(Reg::Cs, far_pointer >> 16);
    Register(Reg::Eip, far_pointer & 0xffff);
    ++callbacks;
    bool returned = false;
    while (state != Failed && state != Exited && !threads[current_thread].done)
    {
        if (!Step()) break;
        if (pending == CallbackReturn) { returned = true; break; }
        if (pending && !Service()) break;
    }
    const unsigned result = Register(Reg::Eax) & 0xffff;
    if (!returned && state != Failed && state != Exited && !threads[current_thread].done)
    {
        char reason[220];
        snprintf(reason, sizeof reason, "Win16 font callback interrupted procedure=%04x:%04x",
                 far_pointer >> 16, far_pointer & 0xffff);
        Fail(reason);
    }
    Check(cpu->Restore(context));
    cpu->FreeContext(context);
    pending = old_pending;
    waiting = old_waiting;
    --depth;
    return result;
}
// A 16-bit hook procedure. USER enters the procedure of the record with
// the Pascal frame of a hook: the code, a 16-bit wParam and a 32-bit lParam,
// four words behind the far return address, and reads the answer as DX:AX.
// Where lParam points to a structure, the 16-bit side expects a far pointer to
// the 16-bit form of it. The producers of this port stage the 32-bit form
// (src/x86_hooks.cpp), so the record is narrowed onto the stack of the task
// here and copied back afterwards if the procedure changed it.
unsigned Application::Win16HookCallback(unsigned procedure, unsigned code, unsigned wparam, unsigned lparam)
{
    Win16TaskStack task_stack(*this);
    const unsigned far_pointer = Win16MarkPointer(procedure);
    if (!far_pointer || !win16.state || state == Failed || state == Exited)
        return 0;
    const int type = CurrentHookType();
    // WH_CBT carries a different structure per code (Win98CbtHooks::Code):
    // HCBT_MOVESIZE a RECT, HCBT_CREATEWND a CBT_CREATEWND with the
    // CREATESTRUCT behind it, HCBT_ACTIVATE a CBTACTIVATESTRUCT and
    // HCBT_CLICKSKIPPED a MOUSEHOOKSTRUCT. Each goes onto the stack of the task
    // in its 16-bit form, the way the callback gate of USER narrows it, and
    // the fields the procedure may change come back afterwards. The two
    // strings of a CREATESTRUCT go back as the far pointers the 16-bit caller
    // passed; a name that came from the 32-bit side (a dialog control of a
    // converted template) is copied behind the record, because a flat address
    // is nothing a 16-bit procedure can load with LES (CTL3DV2).
    const bool cbt_move = type == 5 && code == 0 && lparam, cbt_create = type == 5 && code == 3 && lparam,
               cbt_activate = type == 5 && code == 5 && lparam, cbt_click = type == 5 && code == 6 && lparam;
    constexpr unsigned CbtCreateBytes = 6, CbtStringBytes = 128;
    const bool msg = (type == -1 || type == 3) && lparam;
    const unsigned record_bytes = msg ? Win16U::MessageBytes : type == 4 && lparam ? 10u
                                  : (type == 7 || cbt_click) && lparam ? 12u
                                  : cbt_move ? Win16U::RectBytes
                                  : cbt_activate ? 4u
                                  : cbt_create ? CbtCreateBytes + Win16U::CreateBytes + 2 * CbtStringBytes : 0u;
    if (++depth > 32)
    {
        Fail("Win16 hook callback nesting exceeded");
        --depth;
        return 0;
    }
    const unsigned stack = Register(Reg::Ss);
    const unsigned base = Win16Linear(stack, 0);
    unsigned sp = (Register(Reg::Esp) & 0xffff) & ~1u;
    if (sp < record_bytes + 14)
    {
        Fail("Win16 hook callback exceeds its stack");
        --depth;
        return 0;
    }
    CpuContext *context = nullptr;
    if (!Check(cpu->NewContext(context)))
    {
        --depth;
        return 0;
    }
    Check(cpu->Save(context));
    const unsigned old_pending = pending;
    const bool old_waiting = waiting;
    waiting = false;
    unsigned staged = 0, lparam16 = lparam;
    User::Message message = {};
    unsigned char before[Win16U::MessageBytes] = {};
    unsigned char create_before[Win16U::CreateBytes] = {};
    if (record_bytes)
    {
        sp = (sp - record_bytes) & ~1u;
        staged = sp;
        if (msg)
        {
            Read(lparam, &message, sizeof message);
            Win16WriteMessage(base + staged, message);
            Read(base + staged, before, sizeof before);
        }
        else if (cbt_move)
        {
            for (unsigned k = 0; k < 4; ++k)
                Win16Word(base + staged + k * 2, Read32(lparam + k * 4));
        }
        else if (cbt_activate)
        {
            // CBTACTIVATESTRUCT: BOOL fMouse, HWND hWndActive.
            Win16Word(base + staged, Read32(lparam));
            Win16Word(base + staged + 2, Read32(lparam + 4));
        }
        else if (cbt_create)
        {
            // CBT_CREATEWND: LPCREATESTRUCT lpcs, HWND hwndInsertAfter.
            const unsigned create = Read32(lparam), create_at = staged + CbtCreateBytes;
            Win16Word(base + staged, create_at);
            Win16Word(base + staged + 2, stack);
            Win16Word(base + staged + 4, Read32(lparam + 4));
            Win16StructureIn(Win16U::StructureCreate, base + create_at, create);
            Read(base + create_at, create_before, sizeof create_before);
            unsigned text_at = create_at + Win16U::CreateBytes;
            const unsigned string_fields[2][2] = {{Win16U::CreateName, 36}, {Win16U::CreateClass, 40}};
            for (const auto &pair : string_fields)
            {
                const unsigned field = pair[0], flat = Read32(create + pair[1]);
                if (flat < 0x10000 || Win16FarOf(flat) != flat)
                    continue;
                char text[CbtStringBytes] = {};
                const char *source = ReadString(flat);
                snprintf(text, sizeof text, "%s", source ? source : "");
                Store(base + text_at, text, sizeof text);
                Write32(base + create_at + field, stack << 16 | text_at);
                text_at += CbtStringBytes;
            }
        }
        else if (type == 4)
        {
            // CWPSTRUCT: lParam, wParam, message, hwnd.
            unsigned r[4] = {};
            Read(lparam, r, sizeof r);
            Win16Word(base + staged, r[0] & 0xffff);
            Win16Word(base + staged + 2, r[0] >> 16);
            Win16Word(base + staged + 4, r[1]);
            Win16Word(base + staged + 6, r[2]);
            Win16Word(base + staged + 8, r[3]);
        }
        else
        {
            // MOUSEHOOKSTRUCT: point, window, hit-test code, extra information.
            unsigned r[5] = {};
            Read(lparam, r, sizeof r);
            for (unsigned i = 0; i < 4; ++i)
                Win16Word(base + staged + i * 2, r[i]);
            Win16Word(base + staged + 8, r[4] & 0xffff);
            Win16Word(base + staged + 10, r[4] >> 16);
        }
        lparam16 = stack << 16 | staged;
    }
    sp -= 12;
    Win16Word(base + sp, CallbackReturn & 0xffff);
    Win16Word(base + sp + 2, Win16GateSelector());
    Win16Word(base + sp + 4, lparam16 & 0xffff);
    Win16Word(base + sp + 6, lparam16 >> 16);
    Win16Word(base + sp + 8, wparam & 0xffff);
    Win16Word(base + sp + 10, code & 0xffff);
    Register(Reg::Esp, sp);
    Register(Reg::Ds, win16.state->instance_handle);
    Register(Reg::Eax, win16.state->instance_handle);
    Register(Reg::Edx, 0);
    Register(Reg::Cs, far_pointer >> 16);
    Register(Reg::Eip, far_pointer & 0xffff);
    ++callbacks;
    bool returned = false;
    while (state != Failed && state != Exited && !threads[current_thread].done)
    {
        if (!Step()) break;
        if (pending == CallbackReturn) { returned = true; break; }
        if (pending && !Service()) break;
    }
    const unsigned result = (Register(Reg::Eax) & 0xffff) | ((Register(Reg::Edx) & 0xffff) << 16);
    if (!returned && state != Failed && state != Exited && !threads[current_thread].done)
    {
        char reason[220];
        snprintf(reason, sizeof reason, "Win16 hook callback interrupted procedure=%04x:%04x code=%d",
                 far_pointer >> 16, far_pointer & 0xffff, (int)code);
        Fail(reason);
    }
    // CBT: the rectangle of a move, and of a creation the order and the
    // coordinates, style and extended style the procedure may have changed
    // (CbtCreate reads them out of the 32-bit CREATESTRUCT afterwards).
    if (state != Failed && state != Exited)
    {
        if (cbt_move)
            for (unsigned k = 0; k < 4; ++k)
                Write32(lparam + k * 4, (unsigned)(int)(short)Win16Word(base + staged + k * 2));
        if (cbt_create)
        {
            // Only a field the procedure changed goes back: CW_USEDEFAULT
            // has no 16-bit form that would come back as itself.
            const unsigned create = Read32(lparam), create_at = base + staged + CbtCreateBytes;
            if (Win16Word(base + staged + 4) != (Read32(lparam + 4) & 0xffff))
                Write32(lparam + 4, Win16Word(base + staged + 4));
            unsigned char after[Win16U::CreateBytes] = {};
            Read(create_at, after, sizeof after);
            auto changed = [&](unsigned at, unsigned bytes) { return memcmp(after + at, create_before + at, bytes) != 0; };
            auto narrow = [&](unsigned at) { return (unsigned)(int)(short)Win16Word(create_at + at); };
            const struct { unsigned at16, at32; } fields[] = {{Win16U::CreateHeight, 16}, {Win16U::CreateWidth, 20},
                                                              {Win16U::CreateY, 24}, {Win16U::CreateX, 28}};
            for (const auto &field : fields)
                if (changed(field.at16, 2))
                    Write32(create + field.at32, narrow(field.at16));
            if (changed(Win16U::CreateStyle, 4))
                Write32(create + 32, Read32(create_at + Win16U::CreateStyle));
            if (changed(Win16U::CreateExtended, 4))
                Write32(create + 44, Read32(create_at + Win16U::CreateExtended));
        }
    }
    // A message filter may change the message; the caller reads the 32-bit
    // record back out of its buffer. Only a changed record is translated, so a
    // message whose 16-bit form has no way back does not stop the process.
    if (msg && state != Failed && state != Exited)
    {
        unsigned char after[Win16U::MessageBytes] = {};
        Read(base + staged, after, sizeof after);
        if (memcmp(before, after, sizeof after) != 0)
        {
            Win16ReadMessage(base + staged, message);
            if (state != Failed)
                Store(lparam, &message, sizeof message);
        }
    }
    Check(cpu->Restore(context));
    cpu->FreeContext(context);
    pending = old_pending;
    waiting = old_waiting;
    --depth;
    return result;
}
int Application::Win16WordBreakCallback(unsigned procedure,const char*text,unsigned current,unsigned length,unsigned code)
{
    const unsigned far_pointer=Win16MarkPointer(procedure);
    if(!far_pointer||!text||length>0xff00||!win16.state||state==Failed||state==Exited)return int(current);
    if(++depth>32){Fail("Win16 word-break callback nesting exceeded");--depth;return int(current);}
    CpuContext *context=nullptr;
    if(!Check(cpu->NewContext(context))){--depth;return int(current);}
    Check(cpu->Save(context));
    const unsigned old_pending=pending;const bool old_waiting=waiting;waiting=false;
    const unsigned stack=Register(Reg::Ss),base=Win16Linear(stack,0);
    unsigned top=(Register(Reg::Esp)&0xffff)&~1u;
    const unsigned bytes=(length+2)&~1u;
    if(top<bytes+14){cpu->FreeContext(context);waiting=old_waiting;--depth;Fail("Win16 word-break callback exceeds its stack");return int(current);}
    const unsigned string_at=top-bytes;
    Store(base+string_at,text,length);const unsigned char zero=0;Store(base+string_at+length,&zero,1);
    const unsigned sp=string_at-14;
    Win16Word(base+sp,CallbackReturn&0xffff);Win16Word(base+sp+2,Win16GateSelector());
    Win16Word(base+sp+4,code);Win16Word(base+sp+6,length);Win16Word(base+sp+8,current);
    Win16Word(base+sp+10,string_at);Win16Word(base+sp+12,stack);
    Register(Reg::Esp,sp);Register(Reg::Ds,win16.state->instance_handle);
    Register(Reg::Eax,win16.state->instance_handle);Register(Reg::Edx,0);
    Register(Reg::Cs,far_pointer>>16);Register(Reg::Eip,far_pointer&0xffff);++callbacks;
    bool returned=false;
    while(state!=Failed&&state!=Exited&&!threads[current_thread].done){if(!Step())break;if(pending==CallbackReturn){returned=true;break;}if(pending&&!Service())break;}
    const int result=(short)(Register(Reg::Eax)&0xffff);
    if(!returned&&state!=Failed&&state!=Exited&&!threads[current_thread].done)Fail("Win16 word-break callback interrupted");
    Check(cpu->Restore(context));cpu->FreeContext(context);pending=old_pending;waiting=old_waiting;--depth;
    return result;
}
// The selector of the first window of the gate region, which is where the
// Callback slot lives.
unsigned Application::Win16GateSelector()
{
    auto &machine = *win16.state;
    if (!machine.gate_selector[0])
        machine.gate_selector[0] = Win16Segment(GateBase, 65535, true, false);
    return machine.gate_selector[0];
}

// --------------------------------------------------- the message translation

// One message on its way into 16-bit code. Structures and strings are staged
// on the stack of the task, below the frame the callback will use, so the
// 16-bit side sees a far pointer into its own stack segment and the copy back
// is a copy out of the same place.
unsigned Application::Win16Deliver(unsigned procedure, unsigned hwnd, unsigned message, unsigned wparam,
                                   unsigned lparam)
{
    const unsigned tail = TakeTail();
    Win16TaskStack task_stack(*this);
    const unsigned far_pointer = Win16MarkPointer(procedure);
    if (!far_pointer)
        return 0;
    // COPYDATA's payload selector lives until the synchronous call returns.
    // Keep this delivery on the native stack while that selector is borrowed.
    unsigned copy_selector = 0;
    struct CopySelectorBack
    {
        Application &app; unsigned &selector;
        ~CopySelectorBack() { if (selector) app.selectors.Unmap(selector << 16); }
    } copy_selector_back{*this, copy_selector};
    if (log && (message == 0x403 || message == 0x404))
    {
        char text[128];
        snprintf(text, sizeof text, "WIN16 delivery hwnd=%04x message=%04x wparam=%08x lparam=%08x", hwnd, message, wparam, lparam);
        log(text);
    }
    const unsigned kind = Win16U::KindOf(message, true);
    unsigned message16 = message, wparam16 = wparam & 0xffff, lparam16 = lparam;
    // WM_MOUSEWHEEL has its delta in the high word of wParam, for which a
    // 16-bit procedure has no place. The procedure of a 16-bit program that
    // does not know the message hands it on (CallWindowProc, DefWindowProc),
    // and there the delta is put back (Win16ToThirtyTwo), so the list box
    // behind a subclass of the File Manager scrolls.
    const unsigned wheel_before = win16_wheel;
    if (message == 0x20a)
        win16_wheel = 0x80000000u | (wparam >> 16);
    struct WheelBack
    {
        unsigned &slot, value;
        ~WheelBack() { slot = value; }
    } wheel_back{win16_wheel, wheel_before};
    // The staging area: the bytes below the stack pointer the callback frame
    // will then be built under. A window procedure of a hosted 16-bit module
    // (MCIAVI's playback window in a 32-bit process) can be reached while the
    // flat code of the process runs; then the area is the free part of the
    // thread's 16-bit stack, and the call goes through Win16HostedCall.
    unsigned stack = Register(Reg::Ss);
    unsigned sp = (Register(Reg::Esp) & 0xffff) & ~1u;
    unsigned hosted_low = 0;
    const bool hosted_flat = Win16HostedArea(stack, sp, hosted_low);
    const unsigned base = Win16Linear(stack, 0);
    unsigned staged = 0, staged_bytes = 0, structure = Win16U::StructureNone;
    auto reserve = [&](unsigned bytes) {
        sp = (sp - ((bytes + 1) & ~1u)) & 0xfffe;
        staged = sp;
        staged_bytes = bytes;
        return stack << 16 | sp;
    };
    unsigned control_buffer = Win16C::None, control_count = 0;
    const unsigned control = User::IsButton(hwnd) ? Win16C::ButtonControl : User::CommonControl(hwnd);
    if (Win16C::To16(control, message) != message)
    {
        message16 = Win16C::To16(control, message);
        control_buffer = Win16C::MessageBuffer(control, message, User::Style(hwnd));
        if (control == 6 && message == 0x140) { wparam16 = 0; lparam16 = 0; }
        if (lparam && control_buffer != Win16C::None)
        {
            unsigned bytes = 0;
            if (control_buffer == Win16C::StringIn)
                bytes = (unsigned)strlen(ReadString(lparam)) + 1;
            else if (control_buffer == Win16C::TextOut)
            {
                if (!Win16C::HasStrings(control, User::Style(hwnd)))
                    bytes = 4; // Owner-drawn list without LBS_HASSTRINGS: item DWORD.
                else
                {
                    const unsigned length = Send(hwnd, control == 6 ? 0x149 : 0x18a, wparam, 0);
                    bytes = (int)length >= 0 ? length + 1 : 1;
                }
            }
            else if (control_buffer == Win16C::RectOut)
                bytes = 8;
            else
            {
                control_count = wparam;
                if (control_count > 32767)
                {
                    Fail("Win16 control callback array exceeds a segment");
                    return 0;
                }
                bytes = control_count * 2;
            }
            if (bytes + 64 > sp)
            {
                Fail("Win16 control callback buffer exceeds its stack");
                return 0;
            }
            lparam16 = reserve(bytes ? bytes : 2);
            for (unsigned i = 0; i < (bytes + 1) / 2; ++i)
                Win16Word(base + staged + i * 2, 0);
            if (control_buffer == Win16C::StringIn)
                Store(base + staged, ReadString(lparam), bytes);
            else if (control_buffer == Win16C::WordsIn)
                for (unsigned i = 0; i < control_count; ++i)
                    Win16Word(base + staged + i * 2, Read32(lparam + i * 4));
        }
    }
    else if (message == 0x4a && lparam)
    {
        // COPYDATASTRUCT has three DWORDs on Win16 too; lpData is far.
        // The original DISPDIB reads dwData at +0 and lpData at +8.
        const unsigned bytes = Read32(lparam + 4), data = Read32(lparam + 8);
        if (sp < 80 || (bytes && !data)) return 0;
        if (data)
        {
            copy_selector = Win16Segment(data, bytes ? bytes - 1 : 0, false, false);
            if (!copy_selector) return 0;
        }
        lparam16 = reserve(12);
        Write32(base + staged, Read32(lparam));
        Write32(base + staged + 4, bytes);
        Write32(base + staged + 8, copy_selector << 16);
    }
    else if (message == 0x4e && lparam)
    {
        // WM_NOTIFY: the 16-bit shape of the block (src/win16_common_controls.cpp).
        const unsigned bytes = Win16NotifyBytes(lparam);
        if (bytes + 64 > sp)
        {
            Fail("Win16 WM_NOTIFY block exceeds its stack");
            return 0;
        }
        lparam16 = reserve(bytes);
        Win16NotifyTo16(lparam, base + staged, lparam16);
    }
    else switch (kind)
    {
    case Win16U::KindPlain:
        break;
    case Win16U::KindSwap:
    {
        const auto *rule = Win16U::RuleFor(message);
        Win16U::SwapTo16(*rule, wparam, lparam, wparam16, lparam16);
        break;
    }
    case Win16U::KindControlColor:
        message16 = Win16U::WmCtlColor;
        wparam16 = wparam & 0xffff;
        lparam16 = (lparam & 0xffff) | (Win16U::ControlColorType(message) << 16);
        break;
    case Win16U::KindMdiActivate:
        // 32 bit names both children; 16 bit names the one that is activated
        // and says with wParam whether this window is it.
        wparam16 = hwnd == (lparam & 0xffff);
        lparam16 = (lparam & 0xffff) | ((wparam & 0xffff) << 16);
        break;
    case Win16U::KindParentNotify:
        if (Win16U::ParentNotifyCarriesId(wparam & 0xffff))
        {
            wparam16 = wparam & 0xffff;
            lparam16 = (lparam & 0xffff) | ((wparam >> 16) << 16);
        }
        break;
    case Win16U::KindText:
    {
        // WM_SETTEXT carries a string in, WM_GETTEXT a buffer out.
        const unsigned bytes = message == Win16U::WmSetText
                                   ? (lparam ? (unsigned)strlen(ReadString(lparam)) + 1 : 1u)
                                   : (wparam ? wparam : 1u);
        if (bytes > 4096)
        {
            Fail("Win16 message text exceeds the staging area");
            return 0;
        }
        lparam16 = reserve(bytes);
        if (message == Win16U::WmSetText && lparam)
            Store(base + staged, ReadString(lparam), bytes);
        else
            Win16Word(base + staged, 0);
        break;
    }
    case Win16U::KindStructure:
        structure = Win16U::RuleFor(message)->structure;
        // WM_NCCALCSIZE carries the NCCALCSIZE_PARAMS only when wParam says
        // so; with a false wParam it is one rectangle, and reading the wider
        // shape reads past the block the sender allocated.
        if (message == Win16U::WmNcCalcSize && !wparam)
            structure = Win16U::StructureRect;
        lparam16 = reserve(Win16StructureBytes(structure));
        if (!Win16StructureIn(structure, base + staged, lparam))
            return 0;
        break;
    default:
    {
        char text[200];
        snprintf(text, sizeof text,
                 "Win16 message %04x has no translation into the 16-bit packing yet", message);
        Fail(text);
        return 0;
    }
    }
    // The frame goes under the staging area, so the callee cannot tread on it.
    const unsigned saved = Register(Reg::Esp);
    if (!hosted_flat)
        Register(Reg::Esp, sp);
    auto &machine = *win16.state;
    const bool recorded = staged && machine.delivery_count < 16;
    if (recorded)
        machine.deliveries[machine.delivery_count++] = {message16, base + staged, lparam16, lparam,
                                                        structure};
    unsigned result = 0;
    if (hosted_flat)
    {
        // The procedure loads its own data segment (the loader patched the
        // prologue of every export); DS is the one of its module.
        unsigned ds = 0;
        for (unsigned i = 0; i < machine.module_count && !ds; ++i)
            for (unsigned k = 1; k <= machine.modules[i].segments; ++k)
                if (machine.modules[i].selector[k] == far_pointer >> 16)
                    ds = machine.modules[i].instance;
        const unsigned push[] = {hwnd & 0xffff, message16 & 0xffff, wparam16 & 0xffff, lparam16 >> 16,
                                 lparam16 & 0xffff};
        Win16HostedSetLow(sp > 64 ? sp - 64 : 0);
        bool returned = false;
        result = Win16HostedCall(far_pointer, push, 5, ds, nullptr, &returned);
        Win16HostedSetLow(hosted_low);
    }
    else
    {
        const unsigned record_at = threads[current_thread].host_frame_count;
        const bool kept = tail && message != 0x4a && !task_stack.flat &&
                          PushHostFrame(HostWin16Delivery, saved, staged ? base + staged : 0, lparam, structure);
        if (kept)
        {
            auto &frame = threads[current_thread].host_frames[record_at];
            const unsigned values[] = {wheel_before, recorded, control, message, control_buffer,
                                       staged_bytes, control_count, wparam, lparam16, kind};
            memcpy(frame.data, values, sizeof values);
            threads[current_thread].tail_sp = saved;
            GiveTail(tail == TailAgain ? TailAgain : TailRecord);
            result = Win16Invoke(far_pointer, hwnd, message16, wparam16, lparam16);
            return FinishWin16Delivery(record_at, result);
        }
        // The tail position holds for this frame as well when nothing was
        // staged: then all that is left after the procedure is the stack
        // pointer.
        if (tail && !staged && !task_stack.flat && message != 0x20a && !(control == 6 && message == 0x140))
        {
            threads[current_thread].tail_sp = saved;
            GiveTail(tail);
        }
        result = Win16Invoke(far_pointer, hwnd, message16, wparam16, lparam16);
    }
    if (recorded)
        --machine.delivery_count;
    if (control == 6 && message == 0x140 && result != ~0u)
    {
        if (wparam) Write32(wparam, result & 0xffff);
        if (lparam) Write32(lparam, result >> 16);
    }
    if (lparam && staged && control_buffer == Win16C::TextOut && (int)result >= 0)
    {
        unsigned char *copy = new unsigned char[staged_bytes];
        Read(base + staged, copy, staged_bytes);
        Store(lparam, copy, staged_bytes);
        delete[] copy;
    }
    else if (lparam && staged && control_buffer == Win16C::RectOut)
        for (unsigned i = 0; i < 4; ++i)
            Write32(lparam + i * 4, Win16C::SignedWord(Win16Word(base + staged + i * 2)));
    else if (lparam && staged && control_buffer == Win16C::WordsOut && (int)result > 0)
        for (unsigned i = 0; i < result && i < control_count; ++i)
            Write32(lparam + i * 4, Win16C::SignedWord(Win16Word(base + staged + i * 2)));
    if (structure != Win16U::StructureNone)
        Win16StructureOut(structure, base + staged, lparam);
    if (message == 0x4e && lparam && staged)
        Win16NotifyBack(lparam, base + staged, lparam16);
    if (kind == Win16U::KindText && message == Win16U::WmGetText && lparam)
    {
        char text[4096];
        unsigned n = 0;
        while (n + 1 < staged_bytes && n + 1 < sizeof text)
        {
            unsigned char byte = 0;
            Read(base + staged + n, &byte, 1);
            if (!byte) break;
            text[n++] = (char)byte;
        }
        text[n] = 0;
        PutString(lparam, text, wparam);
    }
    if (!hosted_flat)
        Register(Reg::Esp, saved);
    return result;
}
unsigned Application::FinishWin16Delivery(unsigned record, unsigned result)
{
    auto &t = threads[current_thread];
    const auto frame = t.host_frames[record];
    auto &machine = *win16.state;
    const unsigned saved = frame.a, base = 0, staged = frame.b, lparam = frame.c, structure = frame.d;
    // data: wheel, delivery registration, control, message, buffer kind,
    // staged bytes, array count, original wParam, narrowed lParam, message kind.
    const unsigned wheel_before = frame.data[0], control = frame.data[2], message = frame.data[3];
    const unsigned control_buffer = frame.data[4], staged_bytes = frame.data[5], control_count = frame.data[6];
    const unsigned wparam = frame.data[7], lparam16 = frame.data[8], kind = frame.data[9];
    const bool recorded = frame.data[1] != 0;
    if (recorded)
        --machine.delivery_count;
    if (control == 6 && message == 0x140 && result != ~0u)
    {
        if (wparam) Write32(wparam, result & 0xffff);
        if (lparam) Write32(lparam, result >> 16);
    }
    if (lparam && staged && control_buffer == Win16C::TextOut && (int)result >= 0)
    {
        unsigned char *copy = new unsigned char[staged_bytes];
        Read(base + staged, copy, staged_bytes);
        Store(lparam, copy, staged_bytes);
        delete[] copy;
    }
    else if (lparam && staged && control_buffer == Win16C::RectOut)
        for (unsigned i = 0; i < 4; ++i)
            Write32(lparam + i * 4, Win16C::SignedWord(Win16Word(base + staged + i * 2)));
    else if (lparam && staged && control_buffer == Win16C::WordsOut && (int)result > 0)
        for (unsigned i = 0; i < result && i < control_count; ++i)
            Write32(lparam + i * 4, Win16C::SignedWord(Win16Word(base + staged + i * 2)));
    if (structure != Win16U::StructureNone)
        Win16StructureOut(structure, base + staged, lparam);
    if (message == 0x4e && lparam && staged)
        Win16NotifyBack(lparam, base + staged, lparam16);
    if (kind == Win16U::KindText && message == Win16U::WmGetText && lparam)
    {
        char text[4096];
        unsigned n = 0;
        while (n + 1 < staged_bytes && n + 1 < sizeof text)
        {
            unsigned char byte = 0;
            Read(base + staged + n, &byte, 1);
            if (!byte) break;
            text[n++] = (char)byte;
        }
        text[n] = 0;
        PutString(lparam, text, wparam);
    }
    Register(Reg::Esp, saved);
    win16_wheel = wheel_before;
    PopHostFrame();
    return result;
}
unsigned Application::Win16StructureBytes(unsigned structure)
{
    switch (structure)
    {
    case Win16U::StructureCreate: return Win16U::CreateBytes;
    case Win16U::StructureMinMax: return Win16U::MinMaxBytes;
    case Win16U::StructureWindowPos: return Win16U::WindowPosBytes;
    case Win16U::StructureCalcSize: return Win16U::CalcSizeBytes + Win16U::WindowPosBytes;
    case Win16U::StructureRect: return Win16U::RectBytes;
    case Win16U::StructurePoint: return Win16U::PointBytes;
    case Win16U::StructureMeasureItem: return Win16U::MeasureBytes;
    case Win16U::StructureDrawItem: return Win16U::DrawBytes;
    case Win16U::StructureDeleteItem: return Win16U::DeleteBytes;
    case Win16U::StructureCompareItem: return Win16U::CompareBytes;
    default: return 0;
    }
}
// Narrows one 32-bit structure into the 16-bit shape at `out`.
bool Application::Win16StructureIn(unsigned structure, unsigned out, unsigned source)
{
    if (!source)
        return true;
    auto word = [&](unsigned at, unsigned value) { Win16Word(out + at, value); };
    switch (structure)
    {
    case Win16U::StructureCreate:
        // CREATESTRUCT strings and the creation parameter remain far pointers
        // readable by the 16-bit caller. Recover the caller's parameter when
        // a gate translated it. An MDI child receives its own task's
        // MDICREATESTRUCT, not the widened copy it cannot address.
        Write32(out + Win16U::CreateParameter, Win16FarOf(Read32(source)));
        word(Win16U::CreateInstance, Read32(source + 4));
        word(Win16U::CreateMenu, Read32(source + 8));
        word(Win16U::CreateParent, Read32(source + 12));
        word(Win16U::CreateHeight, Read32(source + 16));
        word(Win16U::CreateWidth, Read32(source + 20));
        word(Win16U::CreateY, Read32(source + 24));
        word(Win16U::CreateX, Read32(source + 28));
        Write32(out + Win16U::CreateStyle, Read32(source + 32));
        // CreateWindow gates pass name and class as flat addresses. The
        // 16-bit caller reads them with LES, so restore its original far
        // pointers. WINFILE uses the first character of its window name as
        // the drive letter.
        Write32(out + Win16U::CreateName, Win16FarOf(Read32(source + 36)));
        Write32(out + Win16U::CreateClass, Win16FarOf(Read32(source + 40)));
        Write32(out + Win16U::CreateExtended, Read32(source + 44));
        return true;
    case Win16U::StructureMinMax:
        for (unsigned i = 0; i < 5; ++i)
        {
            word(i * 4, Read32(source + i * 8));
            word(i * 4 + 2, Read32(source + i * 8 + 4));
        }
        return true;
    case Win16U::StructureWindowPos:
        word(Win16U::WindowPosWindow, Read32(source));
        word(Win16U::WindowPosAfter, Read32(source + 4));
        word(Win16U::WindowPosX, Read32(source + 8));
        word(Win16U::WindowPosY, Read32(source + 12));
        word(Win16U::WindowPosWidth, Read32(source + 16));
        word(Win16U::WindowPosHeight, Read32(source + 20));
        word(Win16U::WindowPosFlags, Read32(source + 24));
        return true;
    case Win16U::StructureCalcSize:
    {
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned k = 0; k < 4; ++k)
                word(i * 8 + k * 2, Read32(source + i * 16 + k * 4));
        const unsigned position = out + Win16U::CalcSizeBytes;
        Win16StructureIn(Win16U::StructureWindowPos, position, Read32(source + 48));
        Write32(out + Win16U::CalcSizePosition, Win16Far(position));
        return true;
    }
    case Win16U::StructureRect:
        for (unsigned k = 0; k < 4; ++k)
            word(k * 2, Read32(source + k * 4));
        return true;
    case Win16U::StructurePoint:
        word(0, Read32(source));
        word(2, Read32(source + 4));
        return true;
    // The four owner-drawn control structures keep their field order across
    // 16-bit and 32-bit forms. itemData remains a DWORD because it stores
    // application data.
    case Win16U::StructureMeasureItem:
        word(Win16U::MeasureType, Read32(source));
        word(Win16U::MeasureId, Read32(source + 4));
        word(Win16U::MeasureItem, Read32(source + 8));
        word(Win16U::MeasureWidth, Read32(source + 12));
        word(Win16U::MeasureHeight, Read32(source + 16));
        Write32(out + Win16U::MeasureData, Read32(source + 20));
        return true;
    case Win16U::StructureDrawItem:
        word(Win16U::DrawType, Read32(source));
        word(Win16U::DrawId, Read32(source + 4));
        word(Win16U::DrawItem, Read32(source + 8));
        word(Win16U::DrawAction, Read32(source + 12));
        word(Win16U::DrawState, Read32(source + 16));
        word(Win16U::DrawWindow, Read32(source + 20));
        word(Win16U::DrawDc, Read32(source + 24));
        for (unsigned k = 0; k < 4; ++k)
            word(Win16U::DrawRect + k * 2, Read32(source + 28 + k * 4));
        Write32(out + Win16U::DrawData, Read32(source + 44));
        return true;
    case Win16U::StructureDeleteItem:
        word(Win16U::DeleteType, Read32(source));
        word(Win16U::DeleteId, Read32(source + 4));
        word(Win16U::DeleteItem, Read32(source + 8));
        word(Win16U::DeleteWindow, Read32(source + 12));
        Write32(out + Win16U::DeleteData, Read32(source + 16));
        return true;
    case Win16U::StructureCompareItem:
        word(Win16U::CompareType, Read32(source));
        word(Win16U::CompareId, Read32(source + 4));
        word(Win16U::CompareWindow, Read32(source + 8));
        word(Win16U::CompareFirst, Read32(source + 12));
        Write32(out + Win16U::CompareFirstData, Read32(source + 16));
        word(Win16U::CompareSecond, Read32(source + 20));
        Write32(out + Win16U::CompareSecondData, Read32(source + 24));
        return true;
    default:
        return false;
    }
}
// Record each caller-supplied far pointer and its flat address in a ring.
// Reverse conversion looks up the flat address to recover the far pointer.
void Application::Win16NoteFarAlias(unsigned flat, unsigned far_pointer)
{
    if (!flat || !far_pointer || !win16.state)
        return;
    auto &machine = *win16.state;
    for (auto &alias : machine.far_aliases)
        if (alias.flat == flat && alias.far_pointer == far_pointer)
            return;
    machine.far_aliases[machine.far_alias_next] = {flat, far_pointer};
    machine.far_alias_next = (machine.far_alias_next + 1) % 64;
}
unsigned Application::Win16RememberFar(unsigned far_pointer)
{
    if (!far_pointer || !(far_pointer >> 16) || !win16.state)
        return far_pointer ? Win16Linear(far_pointer >> 16, far_pointer & 0xffff) : 0;
    const unsigned flat = Win16Linear(far_pointer >> 16, far_pointer & 0xffff);
    Win16NoteFarAlias(flat, far_pointer);
    return flat;
}
// Reverse conversion leaves addresses unchanged if no gate translated them.
// Class atoms have no selector, and native memory does not belong to a 16-bit
// segment.
unsigned Application::Win16FarOf(unsigned flat)
{
    if (!flat || !win16.state)
        return flat;
    for (const auto &alias : win16.state->far_aliases)
        if (alias.flat == flat)
            return alias.far_pointer;
    return flat;
}
// And the way back, for the fields a window procedure is allowed to change.
void Application::Win16StructureOut(unsigned structure, unsigned staged, unsigned target)
{
    if (!target)
        return;
    auto word = [&](unsigned at) { return (int)(short)Win16Word(staged + at); };
    switch (structure)
    {
    case Win16U::StructureMinMax:
        for (unsigned i = 0; i < 5; ++i)
        {
            Write32(target + i * 8, (unsigned)word(i * 4));
            Write32(target + i * 8 + 4, (unsigned)word(i * 4 + 2));
        }
        return;
    case Win16U::StructureWindowPos:
        Write32(target + 4, Win16Word(staged + Win16U::WindowPosAfter));
        Write32(target + 8, (unsigned)word(Win16U::WindowPosX));
        Write32(target + 12, (unsigned)word(Win16U::WindowPosY));
        Write32(target + 16, (unsigned)word(Win16U::WindowPosWidth));
        Write32(target + 20, (unsigned)word(Win16U::WindowPosHeight));
        Write32(target + 24, Win16Word(staged + Win16U::WindowPosFlags));
        return;
    case Win16U::StructureCalcSize:
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned k = 0; k < 4; ++k)
                Write32(target + i * 16 + k * 4, (unsigned)word(i * 8 + k * 2));
        return;
    case Win16U::StructureRect:
        for (unsigned k = 0; k < 4; ++k)
            Write32(target + k * 4, (unsigned)word(k * 2));
        return;
    // Copy width and height back for WM_MEASUREITEM. The sender only reads
    // the other owner-draw structures.
    case Win16U::StructureMeasureItem:
        Write32(target + 12, Win16Word(staged + Win16U::MeasureWidth));
        Write32(target + 16, Win16Word(staged + Win16U::MeasureHeight));
        return;
    default:
        return;
    }
}
// One slot of the staging ring, for a structure a 16-bit caller owns on its way
// to a native reader. Eight slots of 64 bytes; the ring turns because one
// message can reach a window procedure that sends the next one.
unsigned Application::Win16Staging(unsigned bytes)
{
    auto &machine = *win16.state;
    if (bytes > Win16StagingSlotBytes)
    {
        Fail("Win16 structure exceeds the staging slot");
        return 0;
    }
    if (!machine.staging)
    {
        machine.staging = Allocate(Win16StagingSlots * Win16StagingSlotBytes);
        if (!machine.staging)
        {
            Fail("Win16 structure staging ring could not be allocated");
            return 0;
        }
    }
    const unsigned at = machine.staging + machine.staging_slot * Win16StagingSlotBytes;
    machine.staging_slot = (machine.staging_slot + 1) % Win16StagingSlots;
    unsigned char zero[Win16StagingSlotBytes] = {};
    Store(at, zero, sizeof zero);
    return at;
}
// Widens a structure a 16-bit caller owns into the 32-bit shape. The far
// pointers inside it name the same bytes on both sides, so they become the
// flat addresses of those bytes; every other field only grows.
unsigned Application::Win16WidenStructure(unsigned structure, unsigned far_pointer)
{
    if (!far_pointer)
        return 0;
    const unsigned source = Win16Linear(far_pointer >> 16, far_pointer & 0xffff);
    if (!source)
    {
        char text[160];
        snprintf(text, sizeof text, "Win16 structure pointer %04x:%04x names no segment of this task",
                 far_pointer >> 16, far_pointer & 0xffff);
        Fail(text);
        return 0;
    }
    auto word = [&](unsigned at) { return Win16Word(source + at); };
    auto longword = [&](unsigned at) { return word(at) | word(at + 2) << 16; };
    // CW_USEDEFAULT is 8000h in a word and 80000000h in a dword; every other
    // coordinate is a signed word.
    auto coordinate = [&](unsigned at) {
        const unsigned value = word(at);
        return value == 0x8000 ? 0x80000000u : (unsigned)(int)(short)value;
    };
    // Keep the far pointer in the ring so CREATESTRUCT can return it during
    // creation.
    auto flat = [&](unsigned at) { return Win16RememberFar(longword(at)); };
    switch (structure)
    {
    case Win16U::StructureMdiCreate:
    {
        const unsigned out = Win16Staging(Win16U::MdiCreate32Bytes);
        if (!out)
            return 0;
        // The MDI client passes this widened copy as child creation data.
        // Record its far pointer so the child's CREATESTRUCT can return it.
        Win16NoteFarAlias(out, far_pointer);
        Write32(out + Win16U::MdiCreate32Class, flat(Win16U::MdiCreateClass));
        Write32(out + Win16U::MdiCreate32Title, flat(Win16U::MdiCreateTitle));
        Write32(out + Win16U::MdiCreate32Owner, word(Win16U::MdiCreateOwner));
        Write32(out + Win16U::MdiCreate32X, coordinate(Win16U::MdiCreateX));
        Write32(out + Win16U::MdiCreate32Y, coordinate(Win16U::MdiCreateY));
        Write32(out + Win16U::MdiCreate32Width, coordinate(Win16U::MdiCreateWidth));
        Write32(out + Win16U::MdiCreate32Height, coordinate(Win16U::MdiCreateHeight));
        Write32(out + Win16U::MdiCreate32Style, longword(Win16U::MdiCreateStyle));
        Write32(out + Win16U::MdiCreate32Parameter, longword(Win16U::MdiCreateParameter));
        return out;
    }
    default:
        break;
    }
    Fail("Win16 structure has no widening into the 32-bit shape yet");
    return 0;
}
// The far pointer of a flat address that lies inside a segment the task owns.
// Only the stack is staged today, so that is the segment it looks in.
unsigned Application::Win16Far(unsigned flat)
{
    if (!win16.state || !flat)
        return 0;
    const unsigned stack = win16.state->stack_selector;
    const unsigned base = Win16Linear(stack, 0);
    if (flat >= base && flat - base <= Win16Limit(stack))
        return stack << 16 | (flat - base);
    return 0;
}

// ------------------------------------------------------------- the resources

// One resource of an NE module, staged into guest memory once and kept, the
// way a 16-bit program expects a resource handle to stay valid.
unsigned Application::Win16Resource(Win16Module &module, unsigned type, unsigned name, const char *label,
                                    unsigned &bytes)
{
    unsigned offset = 0;
    bytes = 0;
    if (!module.image.ResourceAt(type, name, label, offset, bytes))
        return 0;
    return offset;
}
// The identifier or the name a 16-bit resource call carries: a far pointer
// whose high word is zero is an integer identifier.
void Application::Win16ResourceName(const Win16Frame &frame, unsigned index, unsigned &id, char *label,
                                    unsigned capacity)
{
    label[0] = 0;
    id = 0;
    if (!frame.Word(index + 1))
    {
        id = 0x8000 | (frame.Word(index) & 0x7fff);
        return;
    }
    const unsigned pointer = frame.Pointer(index);
    const char *text = pointer ? ReadString(pointer) : nullptr;
    if (!text)
        return;
    if (text[0] == '#')
    {
        unsigned value = 0;
        for (const char *p = text + 1; *p >= '0' && *p <= '9'; ++p)
            value = value * 10 + (unsigned)(*p - '0');
        id = 0x8000 | (value & 0x7fff);
        return;
    }
    unsigned n = 0;
    while (text[n] && n + 1 < capacity)
    {
        label[n] = text[n];
        ++n;
    }
    label[n] = 0;
}

// -------------------------------------------------------------- the dialogs

// DialogBox and DialogBoxParam. The template is a 16-bit one, and the reader
// of src/dialog_template.cpp knows the 32-bit forms, so it goes through the
// converter src/x86_user_session.cpp already has for the dialogs of USER.EXE.
unsigned Application::Win16Dialog(unsigned instance, unsigned type_id, const char *label, unsigned owner,
                                  unsigned procedure, unsigned parameter, bool modal)
{
    auto &machine = *win16.state;
    Win16Module *module = nullptr;
    for (unsigned i = 0; i < machine.module_count; ++i)
        if (!instance || machine.modules[i].instance == instance || machine.modules[i].handle == instance)
            module = &machine.modules[i];
    if (!module)
        module = &machine.modules[0];
    unsigned bytes = 0;
    const unsigned offset = Win16Resource(*module, Win16U::ResourceDialog, type_id, label, bytes);
    if (log)
    {
        char trace[220];
        snprintf(trace, sizeof trace, "WIN16 dialog module=%s id=%04x name=%s bytes=%u procedure=%08x",
                 module->name, type_id, label ? label : "-", bytes, procedure);
        log(trace);
    }
    if (!offset || !bytes)
    {
        last_error = 1812; // ERROR_RESOURCE_TYPE_NOT_FOUND
        return 0;
    }
    return Win16DialogFrom(module->image.Data() + offset, bytes, Win16DialogInstance(*module), owner, procedure,
                           parameter, modal);
}
// The instance the dialog and its controls are made with. USER.EXE passes the
// hInstance of the caller on to CreateWindowEx, and a class the program
// registered with that hInstance is found under it; the native class store
// compares the value itself, so the data segment of the module goes, not the
// module data, which found no class of the program. A module
// without a data segment has only its handle.
unsigned Application::Win16DialogInstance(const Win16Module &module)
{
    return module.instance ? module.instance : module.handle;
}
unsigned Application::Win16DialogFrom(const unsigned char *bytes, unsigned size, unsigned dialog_instance,
                                      unsigned owner, unsigned procedure, unsigned parameter, bool modal)
{
    auto *converted = new unsigned char[4 * size + 64];
    if (!converted)
    {
        last_error = 8;
        return 0;
    }
    unsigned length = 0;
    if (!ConvertDialogTemplate(bytes, size, converted, 4 * size + 64, length))
    {
        delete[] converted;
        Fail("Win16 dialog template does not read");
        return 0;
    }
    // The handler of a gate, with nothing after the loop: a saved state can
    // take the call up again inside it (Application::ResumeHostCall).
    const bool gate = TakeFresh();
    const bool kept = gate && PushHostFrame(HostDialogBox, 0, 0, modal, 1);
    if (kept) GiveTail(TailRecord);
    const unsigned hwnd = CreateDialogTemplate(converted, length, owner, procedure, parameter, modal, dialog_instance);
    delete[] converted;
    if (kept) threads[current_thread].host_frames[threads[current_thread].host_frame_count - 1].d = 0;
    if (!hwnd)
    {
        if (kept) PopHostFrame();
        return 0;
    }
    const unsigned result = modal ? RunModal(hwnd, kept) : hwnd;
    if (kept) PopHostFrame();
    return result;
}
// DialogBoxIndirect, DialogBoxIndirectParam, CreateDialogIndirect and
// CreateDialogIndirectParam. The template is a block of the caller, in the
// same 16-bit form as a resource; the two DialogBox forms get it as a global
// handle and lock it (USER.EXE does GlobalLock on it before the common body),
// the two CreateDialog forms as a far pointer. Its length is not passed, so
// the converter reads at most up to the end of the block or segment, and it
// stops at the last item the header counts. The instance names the module
// whose resources the dialog uses, as for the resource forms.
unsigned Application::Win16DialogIndirect(unsigned instance, unsigned address, unsigned size, unsigned owner,
                                          unsigned procedure, unsigned parameter, bool modal)
{
    // DialogBox answers -1 when it makes no dialog, CreateDialog null.
    if (!address || !size)
    {
        last_error = 87; // ERROR_INVALID_PARAMETER
        return modal ? 0xffffu : 0u;
    }
    auto &machine = *win16.state;
    unsigned dialog_instance = 0;
    for (unsigned i = 0; i < machine.module_count; ++i)
        if (instance && (machine.modules[i].instance == instance || machine.modules[i].handle == instance))
            dialog_instance = Win16DialogInstance(machine.modules[i]);
    if (!dialog_instance && machine.module_count)
        dialog_instance = Win16DialogInstance(machine.modules[0]);
    if (size > 65536)
        size = 65536;
    auto *copy = new unsigned char[size];
    if (!copy || !Read(address, copy, size))
    {
        delete[] copy;
        return modal ? 0xffffu : 0u;
    }
    if (log)
    {
        char trace[160];
        snprintf(trace, sizeof trace, "WIN16 dialog indirect address=%08x bytes=%u procedure=%08x", address, size,
                 procedure);
        log(trace);
    }
    const unsigned result = Win16DialogFrom(copy, size, dialog_instance, owner, procedure, parameter, modal);
    delete[] copy;
    return result;
}

// ------------------------------------------------------------- the USER gates

// The argument words of one ordinal, and the name for the diagnosis.
unsigned Application::Win16UserWords(unsigned ordinal, const char *&name)
{
    unsigned count = 0;
    const auto *table = Win16U::UserOrdinals(count);
    return Win16U::WordsOf(table, count, ordinal, name);
}
unsigned Application::Win16GdiWords(unsigned ordinal, const char *&name)
{
    unsigned count = 0;
    const auto *table = Win16U::GdiOrdinals(count);
    return Win16U::WordsOf(table, count, ordinal, name);
}
bool Application::Win16UserCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words)
{
    // The installable driver entries (src/win16_driver.cpp).
    if (ordinal >= 251 && ordinal <= 256)
        return Win16DriverCall(ordinal, frame, result, words);
    // The communication functions of USER segment 22 (src/win16_comm.inc).
    if ((ordinal >= 200 && ordinal <= 215) || ordinal == 245)
        return Win16CommCall(ordinal, frame, result, words);
    const char *name = nullptr;
    words = Win16UserWords(ordinal, name);
    if (words == Win16U::Unported)
    {
        char text[240];
        snprintf(text, sizeof text,
                 "Win16 gate USER.%u has no native provider yet: the second part of PLAN.md step 9 carries "
                 "the ordinals this port names",
                 ordinal);
        Fail(text);
        return false;
    }
    auto &machine = *win16.state;
    // Arguments are counted from the last one, which is what Word(0) reads.
    auto arg = [&](unsigned position) { return frame.Word(words - 1 - position); };
    auto pointer = [&](unsigned position) { return frame.Pointer(words - 2 - position); };
    auto signed_word = [&](unsigned position) { return (unsigned)(int)(short)arg(position); };
    result = 0;
    switch (ordinal)
    {
    case 1: // MessageBox(hwnd, text, caption, type)
    {
        const unsigned hwnd = arg(0);
        const char *text = pointer(1) ? ReadString(pointer(1)) : "";
        const char *caption = pointer(3) ? ReadString(pointer(3)) : "Error";
        result = MessageBox(hwnd, text, caption, frame.Word(0));
        return true;
    }
    case 5: // InitApp(instance): the task gets its message queue.
        machine.initialized = true;
        result = 1;
        return true;
    case 6: // PostQuitMessage(code)
    {
        const unsigned a[] = {arg(0)};
        Api("PostQuitMessage", a);
        result = 0;
        return true;
    }
    case 13: // GetTickCount
        result = Api("GetTickCount", nullptr);
        return true;
    // Not User::SetFocus: the focus change of USER with WM_KILLFOCUS,
    // WM_SETFOCUS and the activation it may drag along belongs to
    // Application::SetFocus, which the 32-bit entry uses as well.
    case 22: result = SetFocus(arg(0)); return true;
    case 23: result = User::GetFocus(); return true;
    case 117: result = User::WindowFromDC(arg(0)); return true; // WindowFromDC(hdc), USER
    case 29: // ScreenToClient(hwnd, point)
    {
        // The 32-bit entry carries the window check, the desktop fast path and
        // the refusal of an unreadable point of USER; only the narrow
        // POINT is this gate's business.
        const unsigned at = pointer(1), scratch = Win16Scratch();
        if (!scratch)
            return false;
        if (at)
        {
            Write32(scratch, (unsigned)(int)(short)Win16Word(at));
            Write32(scratch + 4, (unsigned)(int)(short)Win16Word(at + 2));
        }
        const unsigned a[] = {arg(0), at ? scratch : 0u};
        Api("ScreenToClient", a);
        if (at)
        {
            Win16Word(at, Read32(scratch));
            Win16Word(at + 2, Read32(scratch + 4));
        }
        result = 0;
        return true;
    }
    case 31: { const unsigned a[] = {arg(0)}; result = Api("IsIconic", a); return true; }
    case 32: // GetWindowRect(hwnd, rect)
    case 33: // GetClientRect(hwnd, rect)
    {
        // Both queries share the body of USER with the 32-bit entry,
        // which also holds the window check and the refusal of an unwritable
        // rectangle. This gate only narrows the four fields to words.
        const unsigned at = pointer(1), scratch = Win16Scratch();
        if (!scratch)
            return false;
        const unsigned a[] = {arg(0), at ? scratch : 0u};
        result = Api(ordinal == 32 ? "GetWindowRect" : "GetClientRect", a);
        if (result && at)
        {
            Gdi::RECT rect{};
            rect.left = (int)Read32(scratch);
            rect.top = (int)Read32(scratch + 4);
            rect.right = (int)Read32(scratch + 8);
            rect.bottom = (int)Read32(scratch + 12);
            Win16WriteRect(at, rect);
        }
        return true;
    }
    // Not User::EnableWindow: USER sends WM_CANCELMODE, takes the focus
    // away and reports WM_ENABLE. That sequence sits behind the 32-bit entry.
    case 34: { const unsigned a[] = {arg(0), arg(1)}; result = Api("EnableWindow", a); return true; }
    case 35: result = User::IsEnabled(arg(0)); return true;
    // The caption of a window lives behind WM_GETTEXT, WM_SETTEXT and
    // WM_GETTEXTLENGTH. Reading or writing it without the
    // message left a control unable to store or repaint its own text, and a
    // Win16 window procedure that handles WM_SETTEXT saw nothing at all.
    case 36: // GetWindowText(hwnd, buffer, size)
    {
        const unsigned a[] = {arg(0), pointer(1), frame.Word(0)};
        result = Api("GetWindowTextA", a);
        return true;
    }
    case 37: { const unsigned a[] = {arg(0), pointer(1)}; result = Api("SetWindowTextA", a); return true; }
    case 38: { const unsigned a[] = {arg(0)}; result = Api("GetWindowTextLengthA", a); return true; }
    case 39: return Win16BeginPaint(frame, result, words);
    case 40: return Win16EndPaint(frame, result, words);
    case 41: return Win16CreateWindow(frame, result, words);
    case 42:
        // The whole show sequence, not just the visibility bit: WM_SHOWWINDOW,
        // the deferred WM_SIZE and WM_MOVE of an overlapped window and the
        // activation. Going straight to User::ShowWindow left an MDI frame
        // that never told its client how big it was.
        result = ShowWindow(arg(0), (int)(short)arg(1));
        return true;
    // BringWindowToTop is the SetWindowPos of USER, not a bare change
    // of the sibling order; the 32-bit entry raises the owner chain with it.
    case 45: { const unsigned a[] = {arg(0)}; result = Api("BringWindowToTop", a); return true; }
    // GetParent: the shared body carries the measured rule of USER now.
    case 46: { const unsigned a[] = {arg(0)}; result = Api("GetParent", a); return true; }
    case 47: result = User::IsWindow(arg(0)); return true;
    case 49: result = User::IsVisible(arg(0)); return true;
    // Not User::DestroyWindow: the 32-bit entry is CbtDestroyWindow, which
    // reports HCBT_DESTROYWND, hides the window, destroys the children and
    // sends WM_DESTROY and WM_NCDESTROY.
    case 53: { const unsigned a[] = {arg(0)}; result = Api("DestroyWindow", a); return true; }
    case 56:
    {
        // USER enters SetWindowPos, including the WINDOWPOS/WM_SIZE
        // callbacks used by a parent to lay out its child controls.
        const unsigned a[] = {arg(0), signed_word(1), signed_word(2), signed_word(3),
                              signed_word(4), arg(5)};
        result = Api("MoveWindow", a);
        return true;
    }
    case 57: return Win16RegisterClass(frame, result, words);
    case 58: // GetClassName(hwnd, buffer, size)
    {
        const unsigned a[] = {arg(0), pointer(1), frame.Word(0)};
        result = Api("GetClassNameA", a);
        return true;
    }
    case 66: result = User::GetDC(arg(0)); return true;
    case 68: result = User::ReleaseDC(arg(0), arg(1)); return true;
    case 69:
    {
        const unsigned native = Win16ImageNative(arg(0));
        const unsigned a[] = {native};
        const unsigned previous = Api("SetCursor", a);
        result = Win16ImageWord(previous);
        // The host can already have installed a system cursor before the
        // Win16 task calls SetCursor for the first time. It has no WORD
        // alias yet, but Win16 must still receive it as SetCursor's result.
        if (previous && !result)
            result = Win16ImageAlias(previous, 0, true, 0, nullptr);
        return true;
    }
    // The rectangle arithmetic of USER is implemented once in
    // src/win98_rect.cpp and checked against the original by
    // Win98::OriginalSelfTest. These gates read the four words, call it and
    // write the four words back; they no longer carry their own arithmetic.
    case 72: // SetRect(rect, left, top, right, bottom)
    {
        Gdi::RECT r{};
        result = (unsigned)Win98::SetRect(&r, signed_word(2), signed_word(3), signed_word(4), signed_word(5));
        Win16WriteRect(pointer(0), r);
        return true;
    }
    case 73: // SetRectEmpty(rect)
    {
        Gdi::RECT r{};
        result = (unsigned)Win98::SetRectEmpty(&r);
        Win16WriteRect(pointer(0), r);
        return true;
    }
    case 74: // CopyRect(destination, source)
    {
        Gdi::RECT source{}, out{};
        Win16ReadRect(pointer(2), source);
        result = (unsigned)Win98::CopyRect(&out, &source);
        Win16WriteRect(pointer(0), out);
        return true;
    }
    case 75: // IsRectEmpty(rect)
    {
        Gdi::RECT r{};
        Win16ReadRect(pointer(0), r);
        result = (unsigned)Win98::IsRectEmpty(&r);
        return true;
    }
    case 81: // FillRect(hdc, rect, brush)
    {
        // Not Gdi::FillRect: the 32-bit entry holds the rule that a brush value
        // of 32 or less names a system colour, which is the sixteen-bit
        // convention itself, and the metafile and path recording of a DC that
        // is not drawing to the screen.
        const unsigned at = pointer(1), scratch = Win16Scratch();
        if (!scratch)
            return false;
        Gdi::RECT r{};
        Win16ReadRect(at, r);
        Store(scratch, &r, sizeof r);
        const unsigned a[] = {arg(0), at ? scratch : 0u, frame.Word(0)};
        result = Api("FillRect", a);
        return true;
    }
    case 85: // DrawText(hdc, text, length, rect, flags)
    {
        // The seven words hold hdc, text far pointer, character count,
        // rectangle far pointer and flags. The rectangle selector is at word
        // index four, not three; treating the character count as a selector
        // would prevent drawing.
        const unsigned scratch = Win16Scratch(), at = pointer(4);
        if (!scratch || !at) { result = 0; return true; }
        for (unsigned k = 0; k < 4; ++k)
            Write32(scratch + k * 4, (unsigned)(int)(short)Win16Word(at + k * 2));
        const unsigned a[] = {arg(0), pointer(1), (unsigned)(int)(short)arg(3), scratch, frame.Word(0)};
        result = Api("DrawTextA", a);
        for (unsigned k = 0; k < 4; ++k)
            Win16Word(at + k * 2, Read32(scratch + k * 4));
        return true;
    }
    case 87:  // DialogBox(instance, template, owner, procedure)
    case 239: // DialogBoxParam(instance, template, owner, procedure, parameter)
    {
        unsigned id = 0;
        char label[64];
        Win16ResourceName(frame, words - 3, id, label, sizeof label);
        const unsigned owner = arg(3);
        const unsigned far_proc = ordinal == 87 ? frame.Long(0) : frame.Long(2);
        const unsigned parameter = ordinal == 87 ? 0 : frame.Long(0);
        result = Win16Dialog(arg(0), id, label[0] ? label : nullptr, owner, Win16Procedure(far_proc),
                             parameter, true);
        return true;
    }
    case 218: // DialogBoxIndirect(instance, template handle, owner, procedure)
    case 240: // DialogBoxIndirectParam(instance, template handle, owner, procedure, parameter)
    {
        const unsigned far_proc = ordinal == 218 ? frame.Long(0) : frame.Long(2);
        const unsigned parameter = ordinal == 218 ? 0 : frame.Long(0);
        const Win16Block *block = Win16GlobalBlock(arg(1));
        result = Win16DialogIndirect(arg(0), block ? block->memory : 0, block ? block->size : 0, arg(2),
                                     Win16Procedure(far_proc), parameter, true);
        return true;
    }
    case 219: // CreateDialogIndirect(instance, template, owner, procedure)
    case 242: // CreateDialogIndirectParam(instance, template, owner, procedure, parameter)
    {
        const unsigned far_proc = ordinal == 219 ? frame.Long(0) : frame.Long(2);
        const unsigned parameter = ordinal == 219 ? 0 : frame.Long(0);
        const unsigned offset = frame.Word(words - 3), selector = frame.Word(words - 2);
        const unsigned limit = selector ? Win16Limit(selector) : 0;
        const unsigned size = selector && limit >= offset ? limit - offset + 1 : 0;
        result = Win16DialogIndirect(arg(0), pointer(1), size, arg(3), Win16Procedure(far_proc), parameter,
                                     false);
        return true;
    }
    case 241: // CreateDialogParam(instance, template, owner, procedure, parameter)
    {
        unsigned id = 0;
        char label[64];
        Win16ResourceName(frame, words - 3, id, label, sizeof label);
        result = Win16Dialog(arg(0), id, label[0] ? label : nullptr, arg(3), Win16Procedure(frame.Long(2)),
                             frame.Long(0), false);
        return true;
    }
    case 88: // EndDialog(hwnd, result)
        EndDialog(arg(0), arg(1));
        result = 1;
        return true;
    case 91: result = User::GetDlgItem(arg(0), arg(1)); return true;
    case 92: // SetDlgItemText(hwnd, id, text)
    {
        const unsigned a[] = {arg(0), arg(1), pointer(2)};
        result = Api("SetDlgItemTextA", a);
        return true;
    }
    case 93: // GetDlgItemText(hwnd, id, buffer, size)
    {
        const unsigned a[] = {arg(0), arg(1), pointer(2), frame.Word(0)};
        result = Api("GetDlgItemTextA", a);
        return true;
    }
    case 101: // SendDlgItemMessage(hwnd, id, message, wparam, lparam)
    {
        const unsigned item = User::GetDlgItem(arg(0), arg(1));
        result = item ? Win16Send(item, arg(2), arg(3), frame.Long(0)) : 0;
        return true;
    }
    case 104: { const unsigned a[] = {arg(0)}; result = Api("MessageBeep", a); return true; }
    case 107: // DefWindowProc(hwnd, message, wparam, lparam)
    {
        unsigned message = arg(1), wparam = arg(2), lparam = frame.Long(0);
        Win16ControlTransfer transfer;
        if (!Win16ToThirtyTwo(arg(0), message, wparam, lparam, &transfer))
            return false;
        const bool gate = TakeFresh();
        if (gate && !transfer.buffer && !transfer.kind) GiveTail(TailFresh);
        result = Win16ControlAnswer(transfer, DefaultWindowMessage(arg(0), message, wparam, lparam));
        return true;
    }
    case 308: // DefDlgProc(hwnd, message, wparam, lparam): the port page of SERIALUI.DLL
    {
        unsigned message = arg(1), wparam = arg(2), lparam = frame.Long(0);
        Win16ControlTransfer transfer;
        if (!Win16ToThirtyTwo(arg(0), message, wparam, lparam, &transfer))
            return false;
        const unsigned a[] = {arg(0), message, wparam, lparam};
        result = Win16ControlAnswer(transfer, Api("DefDlgProcA", a));
        return true;
    }
    case 108: // GetMessage(msg, hwnd, first, last)
    case 109: // PeekMessage(msg, hwnd, first, last, remove)
        return Win16GetMessage(ordinal, frame, result, words);
    case 110: // PostMessage
    case 111: // SendMessage
    {
        // HWND_BROADCAST is FFFFh on the sixteen-bit side and FFFFFFFFh on the
        // native one. The recipient walk of USER hangs behind the 32-bit
        // entries; reaching User::PostMessage straight left a broadcast of a
        // Win16 program with no recipient at all.
        const unsigned hwnd = arg(0) == 0xffff ? 0xffffffffu : arg(0);
        unsigned message = arg(1), wparam = arg(2), lparam = frame.Long(0);
        if (ordinal == 111)
        {
            result = Win16Send(hwnd, message, wparam, lparam);
            return true;
        }
        // A control message with an array or a rectangle stays in the Win16
        // form in the queue: the far pointer is only copied when the message
        // is delivered, as USER's own 16-bit controls read it then.
        if (!Win16NeedsTransfer(hwnd, message, lparam) && !Win16ToThirtyTwo(hwnd, message, wparam, lparam))
            return false;
        const unsigned a[] = {hwnd, message, wparam, lparam};
        result = Api("PostMessageA", a);
        return true;
    }
    case 113: // TranslateMessage(msg)
    {
        User::Message message{};
        Win16ReadMessage(pointer(0), message);
        result = User::TranslateMessage(&message);
        return true;
    }
    case 114: // DispatchMessage(msg)
    {
        // Win16ReadMessage has already put the message into the numbering of
        // the native side, so the whole dispatch belongs to the 32-bit entry:
        // the TIMERPROC callback, the tick of a flashing caption
        // and the rule that a dispatched message keeps WH_CALLWNDPROC out.
        // Calling CallWindow here also missed the cross-thread send.
        User::Message message{};
        Win16ControlTransfer transfer;
        Win16ReadMessage(pointer(0), message, &transfer);
        if (state == Failed)
            return false;
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        Store(scratch, &message, sizeof message);
        const unsigned a[] = {scratch};
        // With an answer to carry back the call has work left after the
        // procedure, and a saved state cannot take it up there.
        if (transfer.kind || transfer.buffer) TakeTail();
        result = Win16ControlAnswer(transfer, Api("DispatchMessageA", a));
        return true;
    }
    case 122: // CallWindowProc(procedure, hwnd, message, wparam, lparam)
    {
        const unsigned procedure = Win16Procedure(frame.Long(words - 2));
        const unsigned hwnd = arg(2);
        unsigned message = arg(3), wparam = arg(4), lparam = frame.Long(0);
        if (Win16IsMark(procedure))
        {
            threads[current_thread].tail_sp = Register(Reg::Esp);
            TailOfGate(TailResult);
            result = Win16Invoke(Win16MarkPointer(procedure), hwnd, message, wparam, lparam);
            return true;
        }
        const bool gate = TakeFresh();
        Win16ControlTransfer transfer;
        if (!Win16ToThirtyTwo(hwnd, message, wparam, lparam, &transfer))
            return false;
        // Native built-in procedures are dispatch marks, not executable x86
        // addresses. Use the same procedure dispatcher as a 32-bit subclass.
        const unsigned a[] = {procedure, hwnd, message, wparam, lparam};
        if (gate && !transfer.kind && !transfer.buffer) GiveTail(TailFresh);
        result = Api("CallWindowProcA", a);
        result = Win16ControlAnswer(transfer, result);
        return true;
    }
    case 124: // UpdateWindow(hwnd)
    {
        const unsigned a[] = {arg(0)};
        Api("UpdateWindow", a);
        result = 1;
        return true;
    }
    case 125: // InvalidateRect(hwnd, rect, erase)
    case 127: // ValidateRect(hwnd, rect)
    {
        // ValidateRect refuses an invalid window with 1400 in PaintApi; going
        // straight to User::ValidateRect skipped that answer.
        const unsigned at = pointer(1);
        unsigned staged = 0;
        if (at)
        {
            const unsigned scratch = Win16Scratch();
            if (!scratch)
                return false;
            Gdi::RECT rect{};
            Win16ReadRect(at, rect);
            staged = scratch;
            Store(staged, &rect, sizeof rect);
        }
        if (ordinal == 125)
        {
            const unsigned a[] = {arg(0), staged, frame.Word(0)};
            result = Api("InvalidateRect", a);
            return true;
        }
        const unsigned a[] = {arg(0), staged};
        result = Api("ValidateRect", a);
        return true;
    }
    case 190: // GetUpdateRect(hwnd, rect, erase): the rectangle is the 16-bit
              // form, so it goes back as four words. Answers whether anything
              // is left to paint, and an empty region leaves a zero rectangle.
    {
        // Use the 32-bit entry's GetUpdateRect background-erasure behavior
        // and narrow only the returned rectangle.
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        const unsigned a[] = {arg(0), scratch, arg(3)};
        result = Api("GetUpdateRect", a);
        const unsigned at = pointer(1);
        if (at)
        {
            Gdi::RECT rect{};
            rect.left = (int)Read32(scratch);
            rect.top = (int)Read32(scratch + 4);
            rect.right = (int)Read32(scratch + 8);
            rect.bottom = (int)Read32(scratch + 12);
            Win16WriteRect(at, rect);
        }
        return true;
    }
    case 237: // GetUpdateRgn(hwnd, region, erase): the same question with a
              // region instead of a rectangle. The answer is the region type,
              // and the region handle is the narrow one of the 16-bit side.
    {
        const unsigned a[] = {arg(0), arg(1), arg(2)};
        result = Api("GetUpdateRgn", a);
        return true;
    }
    // Win16 reaches the fields of the window record through negative indices,
    // and GetWindowWord does so as well: GWW_ID is -12, GWW_HWNDPARENT -8 and
    // GWW_HINSTANCE -6, next to GWL_WNDPROC -4, GWL_STYLE -16, GWL_EXSTYLE -20
    // and GWL_USERDATA -21. User::WindowWord and User::WindowLong reject every
    // negative index but -21, because USER32 does; calling them straight from
    // here answered zero for the style of a 16-bit window and wrote nothing
    // back. Both directions now take the long entry of the 32-bit side, which
    // carries the WM_STYLECHANGING and WM_STYLECHANGED of USER and the
    // class procedure token of a native button, and the word entries keep only
    // the extra bytes, which is the one place the two widths differ.
    case 133: // GetWindowWord(hwnd, index)
    case 135: // GetWindowLong(hwnd, index)
    {
        const int index = (int)(short)arg(1);
        if (index == -6) // GWW_HINSTANCE
        {
            // The instance CreateWindow was given; a window made
            // without one answers the instance of the module the task started
            // from.
            result = User::WindowInstance(arg(0));
            if (!result)
                result = win16.state->module_count ? win16.state->modules[0].instance : 0u;
            if (ordinal == 133)
                result &= 0xffff;
            return true;
        }
        const unsigned a[] = {arg(0), (unsigned)index};
        if (index < 0)
        {
            const unsigned value = Api("GetWindowLongA", a);
            result = index == -4 ? Win16ProcedurePointer(value)
                     : ordinal == 133 ? value & 0xffff
                                      : value;
            return true;
        }
        result = Api(ordinal == 133 ? "GetWindowWord" : "GetWindowLongA", a);
        return true;
    }
    case 134: // SetWindowWord(hwnd, index, value)
    case 136: // SetWindowLong(hwnd, index, value)
    {
        // Both directions take the long entry of the 32-bit side for a negative
        // index; the word entries keep only the extra bytes. The shared body
        // now carries the measured rule of USER: the two style words are
        // written and nothing else, and WM_STYLECHANGING and WM_STYLECHANGED of
        // stay away from a window without bit 40h at window+2Eh, which
        // is every Win16 window. This is the write that makes the controls of
        // the Visual Basic runtimes visible.
        const int index = (int)(short)arg(1);
        unsigned value = ordinal == 134 ? arg(2) : frame.Long(0);
        if (index == -4) // GWL_WNDPROC: a far pointer here, a mark on the other side
        {
            const unsigned previous = Win16ProcedurePointer(User::Procedure(arg(0)));
            User::SetProcedure(arg(0), Win16Procedure(value));
            result = previous;
            return true;
        }
        if (index == -6) // GWW_HINSTANCE: the window's own record
        {
            const unsigned previous = User::SetWindowInstance(arg(0), ordinal == 134 ? value & 0xffff : value);
            result = ordinal == 134 ? previous & 0xffff : previous;
            return true;
        }
        const unsigned a[] = {arg(0), (unsigned)index, value};
        if (index < 0)
        {
            const unsigned previous = Api("SetWindowLongA", a);
            result = ordinal == 134 ? previous & 0xffff : previous;
            return true;
        }
        result = Api(ordinal == 134 ? "SetWindowWord" : "SetWindowLongA", a);
        return true;
    }
    case 157: result = User::GetMenu(arg(0)); return true;
    case 173: // LoadCursor(instance, name)
    case 174: // LoadIcon(instance, name)
    {
        unsigned id = 0;
        char label[64];
        Win16ResourceName(frame, 0, id, label, sizeof label);
        result = Win16LoadImage(arg(0), ordinal == 173, id, label);
        return true;
    }
    case 176: return Win16LoadString(frame, result, words);
    case 606: return Win16FormatMessage(frame, result);
    case 482: // EnableScrollBar(hwnd, wSBflags, wArrows): USER checks
              // wSBflags against SB_BOTH (3) and wArrows against ESB_DISABLE_BOTH
              // and ends in `retf 6`; the body is the one of USER32.
    {
        const unsigned a[] = {arg(0), arg(1), arg(2)};
        result = Api("EnableScrollBar", a);
        return true;
    }
    case 284: // GetFreeSystemResources(fuSysResource)
    {
        // USER over the helper, the same body SHELL.DLL's
        // About box reads (win98_shell_about.h).
        const unsigned gdi = Win98ShellAbout::Percent(Gdi::LiveObjects(), Gdi::ObjectCapacity());
        const unsigned windows = Win98ShellAbout::Percent(User::LiveWindows(), User::WindowCapacity);
        const unsigned menus = Win98ShellAbout::Percent(User::LiveMenus(), User::MenuCapacity());
        result = Win98ShellAbout::FreeSystemResources(arg(0), gdi, windows <= menus ? windows : menus) & 0xffff;
        return true;
    }
    case 389: // LoadImage(instance, name, type, cx, cy, flags)
    {
        // USER is the one body behind LoadIcon, LoadCursor and
        // LoadImage; the type picks the resource group, and a bitmap goes to
        // the bitmap loader. LR_LOADFROMFILE (10h) has no module to read from.
        const unsigned type = arg(3), cx = arg(4), cy = arg(5), flags = arg(6);
        unsigned id = 0;
        char label[260];
        Win16ResourceName(frame, 4, id, label, sizeof label);
        if (flags & 0x10)
        {
            // SYSDM probes OEMLOGO.BMP next to OEMINFO.INI. The file loader
            // is the same one USER32 uses; a missing optional logo returns
            // zero instead of ending the process.
            const unsigned path = String(label);
            const unsigned a[] = {0, path, type, cx, cy, flags};
            result = path ? ImageApi("LoadImageA", a) : 0;
            if (result && (type == 1 || type == 2))
                result = Win16ImageAlias(result, 0, type == 2, 0, label);
            if (path) Free(path);
            return true;
        }
        if (type == 0)
            result = Win16LoadBitmap(arg(0), id, label, flags);
        else if (type == 1 || type == 2)
            result = Win16LoadImage(arg(0), type == 2, id, label, cx, cy);
        else
            result = 0;
        return true;
    }
    case 179:
    {
        const unsigned a[] = {arg(0)};
        result = Api("GetSystemMetrics", a);
        return true;
    }
    case 180:
    {
        const unsigned a[] = {arg(0)};
        result = Api("GetSysColor", a);
        return true;
    }
    case 281: // GetSysColorBrush(index): the same brushes USER32 hands out
    {
        const unsigned a[] = {arg(0)};
        result = Api("GetSysColorBrush", a);
        return true;
    }
    case 232: // SetWindowPos(hwnd, after, x, y, cx, cy, flags)
    {
        const unsigned a[] = {arg(0), arg(1), signed_word(2), signed_word(3),
                              signed_word(4), signed_word(5), arg(6)};
        result = Api("SetWindowPos", a);
        return true;
    }
    case 262: { const unsigned a[] = {arg(0), arg(1)}; result = Api("GetWindow", a); return true; }
    case 483: // SystemParametersInfo(action, parameter, buffer, flags)
    {
        // The only actions a 16-bit program asks for with a structure are the
        // rectangle ones, and a 16-bit RECT is four words where the native one
        // is four dwords. SPI_GETWORKAREA is 48.
        const unsigned action = arg(0), at = pointer(2);
        if (action == 48)
        {
            const unsigned scratch = Win16Scratch();
            if (!scratch) return false;
            const unsigned a[] = {action, arg(1), scratch, frame.Word(0)};
            result = Api("SystemParametersInfoA", a);
            if (result && at)
                for (unsigned k = 0; k < 4; ++k)
                    Win16Word(at + k * 2, Read32(scratch + k * 4));
            return true;
        }
        const unsigned a[] = {action, arg(1), at, frame.Word(0)};
        result = Api("SystemParametersInfoA", a);
        return true;
    }
    // GetDesktopHwnd (278) and GetDesktopWindow (286) are one entry of
    // USER.EXE.
    case 278:
    case 286: { const unsigned a[] = {}; result = Api("GetDesktopWindow", a); return true; }
    case 287: { const unsigned a[] = {arg(0)}; result = Api("GetLastActivePopup", a); return true; }
    case 604: // BroadcastSystemMessage: USER thunks the frame to USER32
              // as it is, with the recipients' far pointer made flat.
    {
        const unsigned recipients = frame.Pointer(4);
        const unsigned a[] = {frame.Long(6), recipients, arg(4), arg(5), frame.Long(0)};
        result = Api("BroadcastSystemMessage", a);
        return true;
    }
    case 420: // wsprintf, the one cdecl entry
        return Win16Printf(frame, result, words);
    // ---- the rectangle arithmetic of USER, on the 16-bit shape directly
    case 76: // PtInRect(rect, point)
    {
        Gdi::RECT r{};
        Win16ReadRect(pointer(0), r);
        // x is the low word of the point: USER compares [sp+4] with
        // the left edge.
        result = (unsigned)Win98::PtInRect(&r, (int)(short)frame.Word(0), (int)(short)frame.Word(1));
        return true;
    }
    case 77: // OffsetRect(rect, dx, dy)
    case 78: // InflateRect(rect, dx, dy)
    {
        if (!pointer(1))
        {
            result = 0;
            return true;
        }
        Gdi::RECT r{};
        Win16ReadRect(pointer(1), r);
        result = (unsigned)(ordinal == 77 ? Win98::OffsetRect(&r, (int)(short)arg(1), (int)(short)arg(2))
                                          : Win98::InflateRect(&r, (int)(short)arg(1), (int)(short)arg(2)));
        Win16WriteRect(pointer(1), r);
        return true;
    }
    case 79: // IntersectRect(out, a, b)
    case 80: // UnionRect(out, a, b)
    {
        Gdi::RECT first{}, second{}, out{};
        Win16ReadRect(pointer(2), first);
        Win16ReadRect(pointer(4), second);
        result = (unsigned)(ordinal == 79 ? Win98::IntersectRect(&out, &first, &second)
                                          : Win98::UnionRect(&out, &first, &second));
        if (pointer(0))
            Win16WriteRect(pointer(0), out);
        return true;
    }
    // ---- the scroll bars, the caret, the cursor and the keyboard state
    case 61: // ScrollWindow(hwnd, dx, dy, scroll, clip)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        unsigned scroll = 0, clip = 0;
        Gdi::RECT r{};
        if (pointer(3))
        {
            Win16ReadRect(pointer(3), r);
            scroll = scratch;
            Store(scroll, &r, sizeof r);
        }
        if (pointer(5))
        {
            Win16ReadRect(pointer(5), r);
            clip = scratch + 16;
            Store(clip, &r, sizeof r);
        }
        const unsigned a[] = {arg(0), signed_word(1), signed_word(2), scroll, clip};
        result = Api("ScrollWindow", a);
        return true;
    }
    case 62: // SetScrollPos(hwnd, bar, position, redraw)
    {
        const unsigned a[] = {arg(0), arg(1), signed_word(2), arg(3)};
        result = Api("SetScrollPos", a);
        return true;
    }
    case 63: // GetScrollPos(hwnd, bar)
    {
        const unsigned a[] = {arg(0), arg(1)};
        result = Api("GetScrollPos", a);
        return true;
    }
    case 64: // SetScrollRange(hwnd, bar, min, max, redraw)
    {
        const unsigned a[] = {arg(0), arg(1), signed_word(2), signed_word(3), arg(4)};
        result = Api("SetScrollRange", a);
        return true;
    }
    case 65: // GetScrollRange(hwnd, bar, min, max)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        const unsigned a[] = {arg(0), arg(1), scratch, scratch + 4};
        result = Api("GetScrollRange", a);
        if (pointer(2))
            Win16Word(pointer(2), Read32(scratch));
        if (pointer(4))
            Win16Word(pointer(4), Read32(scratch + 4));
        return true;
    }
    case 71: // ShowCursor(show)
    {
        const unsigned a[] = {arg(0)};
        result = Api("ShowCursor", a);
        return true;
    }
    case 106: // GetKeyState(key)
        result = User::GetKeyState(arg(0)) & 0xffff;
        return true;
    case 163: // CreateCaret(hwnd, bitmap, width, height)
    {
        const unsigned a[] = {arg(0), arg(1), signed_word(2), signed_word(3)};
        result = Api("CreateCaret", a);
        return true;
    }
    case 164: result = Api("DestroyCaret", nullptr); return true;
    case 165:
    {
        const unsigned a[] = {signed_word(0), signed_word(1)};
        result = Api("SetCaretPos", a);
        return true;
    }
    case 166: // HideCaret(hwnd)
    case 167: // ShowCaret(hwnd)
    {
        const unsigned a[] = {arg(0)};
        result = Api(ordinal == 166 ? "HideCaret" : "ShowCaret", a);
        return true;
    }
    // ---- windows, classes, menus and icons
    case 7: // ExitWindows(dwReturnCode, reserved)
    {
        // USER passes the return code to session shutdown unchanged, with no
        // flags: EW_RESTARTWINDOWS 42h, EW_REBOOTSYSTEM 43h or zero.
        result = EndSession(0, arg(0) << 16 | arg(1));
        return true;
    }
    case 96: // CheckRadioButton(dialog, first, last, check)
    {
        const unsigned a[] = {arg(0), arg(1), arg(2), arg(3)};
        result = Api("CheckRadioButton", a);
        return true;
    }
    case 118: // RegisterWindowMessage(name)
    {
        const unsigned a[] = {pointer(0)};
        result = Api("RegisterWindowMessageA", a);
        return true;
    }
    case 126: // InvalidateRgn(hwnd, region, erase)
    {
        const unsigned a[] = {arg(0), arg(1), arg(2)};
        result = Api("InvalidateRgn", a);
        return true;
    }
    case 129: // GetClassWord(hwnd, index)
    {
        const int index = (int)(short)arg(1);
        const unsigned a[] = {arg(0), unsigned(index)};
        const unsigned value = Api(index == -12 || index == -14 ? "GetClassLongA" : "GetClassWord", a);
        result = index == -12 || index == -14 ? Win16ImageWord(value) : value & 0xffff;
        return true;
    }
    case 130: // SetClassWord(hwnd, index, value)
    {
        const int index = (int)(short)arg(1);
        const unsigned value = index == -12 || index == -14 ? Win16ImageNative(arg(2)) : arg(2);
        const unsigned a[] = {arg(0), unsigned(index), value};
        const unsigned previous = Api(index == -12 || index == -14 ? "SetClassLongA" : "SetClassWord", a);
        result = index == -12 || index == -14 ? Win16ImageWord(previous) : previous & 0xffff;
        return true;
    }
    case 268: // GlobalAddAtom(name)
    case 270: // GlobalFindAtom(name)
    {
        const unsigned a[] = {pointer(0)};
        result = Api(ordinal == 268 ? "GlobalAddAtomA" : "GlobalFindAtomA", a);
        return true;
    }
    case 271: // GlobalGetAtomName(atom, buffer, count): the page Settings of
              // DESK.CPL asks before it opens the adapter's sheet.
    {
        const unsigned a[] = {arg(0), pointer(1), signed_word(3)};
        result = Api("GlobalGetAtomNameA", a) & 0xffff;
        return true;
    }
    case 269: // GlobalDeleteAtom(atom)
    {
        const unsigned a[] = {arg(0)};
        result = Api("GlobalDeleteAtom", a);
        return true;
    }
    case 272: // IsZoomed(hwnd)
    {
        const unsigned a[] = {arg(0)};
        result = Api("IsZoomed", a);
        return true;
    }
    case 84: // DrawIcon(dc, x, y, icon)
    {
        const unsigned a[] = {arg(0), signed_word(1), signed_word(2), Win16ImageNative(arg(3))};
        result = Api("DrawIcon", a);
        return true;
    }
    case 407: // CreateIcon(instance, width, height, planes, depth, AND bits, XOR bits)
    {
        const unsigned a[] = {arg(0), signed_word(1), signed_word(2), arg(3) & 255,
                              arg(4) & 255, frame.Pointer(2), frame.Pointer(0)};
        const unsigned native = Api("CreateIcon", a);
        result = Win16ImageAlias(native, arg(0), false, 0, nullptr);
        if (native && !result)
        {
            const unsigned destroy[] = {native};
            Api("DestroyIcon", destroy);
        }
        return true;
    }
    case 395: // GetIconInfo(icon, info): USER, `retf 6`. ICONINFO of the
              // 16-bit side is five words: fIcon, the hot spot, the mask and
              // the colour bitmap. SETUPX takes the bitmaps of a class icon
              // for its image list with it.
    {
        const unsigned info = frame.Pointer(0), block = Allocate(20);
        result = 0;
        if (!info || !block)
        {
            if (block)
                Free(block);
            return true;
        }
        const unsigned a[] = {Win16ImageNative(arg(0)), block};
        result = Api("GetIconInfo", a) ? 1 : 0;
        if (result)
            for (unsigned i = 0; i < 5; ++i)
                Win16Word(info + i * 2, Read32(block + i * 4) & 0xffff);
        Free(block);
        return true;
    }
    case 368: // CopyIcon(instance, icon): the 16-bit entry names the instance.
    {
        const unsigned a[] = {Win16ImageNative(arg(1))};
        const unsigned native = Api("CopyIcon", a);
        result = Win16ImageAlias(native, arg(0), false, 0, nullptr);
        if (native && !result)
        {
            const unsigned destroy[] = {native};
            Api("DestroyIcon", destroy);
        }
        return true;
    }
    case 457: // DestroyIcon(icon)
    {
        result = Win16DestroyImage(arg(0));
        return true;
    }
    case 413: // DeleteMenu(menu, item, flags)
    {
        const unsigned a[] = {arg(0), arg(1), arg(2)};
        result = Api("DeleteMenu", a);
        return true;
    }
    case 414: // ModifyMenu(menu, item, flags, id, text)
    {
        const unsigned a[] = {arg(0), arg(1), arg(2), arg(3),
                              (arg(2) & 0x0410) ? frame.Long(0) : pointer(4)};
        result = Api("ModifyMenuA", a);
        return true;
    }
    case 433: // IsCharAlpha(character)
    case 434: // IsCharAlphaNumeric(character)
    {
        const unsigned a[] = {arg(0) & 255};
        result = Api(ordinal == 433 ? "IsCharAlphaA" : "IsCharAlphaNumericA", a);
        return true;
    }
    case 466: // DrawFocusRect(dc, rect)
    {
        const unsigned scratch = Win16Scratch();
        Gdi::RECT r{};
        if (!scratch)
            return false;
        Win16ReadRect(pointer(1), r);
        Store(scratch, &r, sizeof r);
        const unsigned a[] = {arg(0), scratch};
        result = Api("DrawFocusRect", a);
        return true;
    }
    case 370: // GetWindowPlacement(hwnd, placement)
    case 371: // SetWindowPlacement(hwnd, placement)
    {
        // WINDOWPLACEMENT is eleven words in 16 bit and eleven dwords in 32;
        // the first two are lengths and flags, then the show command, two
        // points and a rectangle.
        const unsigned scratch = Win16Scratch(), at = pointer(1);
        if (!scratch || !at)
        {
            result = 0;
            return true;
        }
        if (ordinal == 371)
            for (unsigned k = 0; k < 11; ++k)
                Write32(scratch + k * 4, (unsigned)(int)(short)Win16Word(at + k * 2));
        Write32(scratch, 44);
        const unsigned a[] = {arg(0), scratch};
        result = Api(ordinal == 370 ? "GetWindowPlacement" : "SetWindowPlacement", a);
        if (ordinal == 370)
        {
            Win16Word(at, 22);
            for (unsigned k = 1; k < 11; ++k)
                Win16Word(at + k * 2, Read32(scratch + k * 4));
        }
        return true;
    }
    case 171: // WinHelp(hwnd, file, command, data)
    {
        const unsigned a[] = {arg(0), pointer(1), arg(3), frame.Long(0)};
        result = Api("WinHelpA", a);
        return true;
    }
    case 50: // FindWindow(class, title)
    {
        // A class is a far pointer to a name or an atom in the offset word
        // with a zero selector, and a null pointer means any. The selector is
        // the deeper of the two words.
        const unsigned klass = arg(0) ? pointer(0) : arg(1);
        const unsigned a[] = {klass, pointer(2)};
        result = Api("FindWindowA", a);
        return true;
    }
    case 427: // FindWindowEx(parent, child after, class, title) over the body of FindWindowExA
    {
        const unsigned klass = arg(2) ? pointer(2) : arg(3);
        const unsigned a[] = {arg(0), arg(1), klass, pointer(4)};
        result = Api("FindWindowExA", a);
        return true;
    }
    case 244: // EqualRect(lprc1, lprc2): the four words of both rectangles
    {
        const unsigned first = pointer(0), second = pointer(2);
        unsigned char x[8] = {}, y[8] = {};
        result = first && second && Read(first, x, 8) && Read(second, y, 8) && !memcmp(x, y, 8);
        return true;
    }
    case 121: // SetWindowsHook(id, procedure)
    {
        // The hook procedure is a far pointer; it goes through the same mark
        // native USER already treats as guest code, and the answer is the far
        // pointer of whatever hung there before.
        const unsigned procedure = Win16Procedure(frame.Long(0));
        const unsigned a[] = {signed_word(0), procedure};
        result = Win16ProcedurePointer(Api("SetWindowsHookA", a));
        return true;
    }
    case 235: // DefHookProc(code, wparam, lparam, next)
    {
        // The next hook stands behind a far pointer to a far pointer; the
        // 32-bit entry takes the handle itself.
        const unsigned at = pointer(4);
        const unsigned next = at ? Win16Procedure(Win16Word(at) | Win16Word(at + 2) << 16) : 0u;
        // lParam sits at the positions two and three, behind the code and the
        // wParam and in front of the far pointer to the next hook.
        const unsigned a[] = {next, signed_word(0), arg(1), frame.Long(2)};
        result = Api("CallNextHookEx", a);
        return true;
    }
    case 293: // CallNextHookEx(hook, code, wparam, lparam)
    {
        // The handle is the DWORD SetWindowsHookEx (USER.291) answered, the
        // one the 32-bit side made; lParam goes on the way DefHookProc passes it.
        const unsigned a[] = {frame.Long(4), signed_word(2), arg(3), frame.Long(0)};
        result = Api("CallNextHookEx", a);
        return true;
    }
    case 188: // SetSysModalWindow(hwnd): Win98 has no system modal window.
        result = 0;
        return true;
    case 437: // AnsiUpperBuff(text, length)
    case 438: // AnsiLowerBuff(text, length). A length of zero means 65536.
    {
        const unsigned at = pointer(0);
        unsigned length = frame.Word(0);
        if (!length)
            length = 65536;
        result = length;
        if (!at)
            return true;
        for (unsigned i = 0; i < length; ++i)
        {
            unsigned char byte = 0;
            if (!Read(at + i, &byte, 1))
                break;
            unsigned char folded = byte;
            if (ordinal == 437 && byte >= 'a' && byte <= 'z') folded -= 32;
            if (ordinal == 438 && byte >= 'A' && byte <= 'Z') folded += 32;
            if (folded != byte)
                Store(at + i, &folded, 1);
        }
        return true;
    }
    case 431: // AnsiUpper / AnsiLower. A far pointer whose high word is zero
    case 432: // is a single character, as the 16-bit entry defines it.
    {
        const unsigned selector = frame.Word(1);
        if (!selector)
        {
            unsigned ch = frame.Word(0) & 255;
            if (ordinal == 431 && ch >= 'a' && ch <= 'z') ch -= 32;
            if (ordinal == 432 && ch >= 'A' && ch <= 'Z') ch += 32;
            result = ch;
            return true;
        }
        const unsigned at = pointer(0);
        for (unsigned i = 0; at && i < 32768; ++i)
        {
            unsigned char byte = 0;
            Read(at + i, &byte, 1);
            if (!byte) break;
            if (ordinal == 431 && byte >= 'a' && byte <= 'z') byte -= 32;
            if (ordinal == 432 && byte >= 'A' && byte <= 'Z') byte += 32;
            Store(at + i, &byte, 1);
        }
        result = frame.Long(0);
        return true;
    }
    case 445: // DefFrameProc(frame, client, message, wparam, lparam)
    {
        unsigned message = arg(2), wparam = arg(3), lparam = frame.Long(0);
        if (!Win16ToThirtyTwo(arg(0), message, wparam, lparam))
            return false;
        result = DefFrameMessage(arg(0), arg(1), message, wparam, lparam);
        return true;
    }
    case 447: // DefMDIChildProc(child, message, wparam, lparam)
    {
        unsigned message = arg(1), wparam = arg(2), lparam = frame.Long(0);
        if (!Win16ToThirtyTwo(arg(0), message, wparam, lparam))
            return false;
        result = DefMdiChildMessage(arg(0), message, wparam, lparam);
        return true;
    }
    case 451: // TranslateMDISysAccel(client, message)
    {
        User::Message message{};
        Win16ReadMessage(pointer(1), message);
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        Store(scratch, &message, sizeof message);
        const unsigned a[] = {arg(0), scratch};
        result = Api("TranslateMDISysAccel", a);
        return true;
    }
    case 24: // RemoveProp(hwnd, name)
    case 25: // GetProp(hwnd, name)
    {
        const unsigned a[] = {arg(0), frame.Word(1) ? pointer(1) : frame.Word(0)};
        result = Api(ordinal == 24 ? "RemovePropA" : "GetPropA", a);
        return true;
    }
    case 26: // SetProp(hwnd, name, data)
    {
        const unsigned a[] = {arg(0), frame.Word(2) ? pointer(1) : frame.Word(1), frame.Word(0)};
        result = Api("SetPropA", a);
        return true;
    }
    case 59: { const unsigned a[] = {arg(0)}; result = Api("SetActiveWindow", a); return true; }
    // USER.60 is GetActiveWindow, not GetForegroundWindow: the answer is the
    // active window of the calling queue, which is what ActivationApi reads.
    case 60: { const unsigned a[] = {}; result = Api("GetActiveWindow", a); return true; }
    case 89: // CreateDialog(instance, template, owner, procedure)
    {
        unsigned id = 0;
        char label[64];
        Win16ResourceName(frame, words - 3, id, label, sizeof label);
        result = Win16Dialog(arg(0), id, label[0] ? label : nullptr, arg(3),
                             Win16Procedure(frame.Long(0)), 0, false);
        return true;
    }
    case 90: // IsDialogMessage(dialog, message)
    {
        User::Message message{};
        Win16ReadMessage(pointer(1), message);
        result = DialogMessage(arg(0), message);
        return true;
    }
    // BM_SETCHECK and BM_GETCHECK go to the button through User::ControlMessage
    // behind the 32-bit entry. Sending them with Win16Send put a 32-bit number
    // through the 16-bit translation, which is not what either side means.
    case 97: // CheckDlgButton(hwnd, id, check)
    {
        const unsigned a[] = {arg(0), arg(1), arg(2)};
        result = Api("CheckDlgButton", a);
        return true;
    }
    case 98: // IsDlgButtonChecked(hwnd, id)
    {
        const unsigned a[] = {arg(0), arg(1)};
        result = Api("IsDlgButtonChecked", a);
        return true;
    }
    case 154: // CheckMenuItem(menu, item, flags)
    case 155: // EnableMenuItem(menu, item, flags)
    {
        const unsigned a[] = {arg(0), arg(1), arg(2)};
        result = Api(ordinal == 154 ? "CheckMenuItem" : "EnableMenuItem", a);
        return true;
    }
    case 156: { const unsigned a[] = {arg(0), arg(1)}; result = Api("GetSystemMenu", a); return true; }
    case 159: result = User::GetSubMenu(arg(0), arg(1)); return true;
    case 177: // LoadAccelerators(instance, name)
    {
        unsigned id = 0;
        char label[64];
        Win16ResourceName(frame, 0, id, label, sizeof label);
        result = Win16LoadAccelerators(arg(0), id, label);
        return true;
    }
    case 178: // TranslateAccelerator(hwnd, table, message)
    {
        User::Message message{};
        Win16ReadMessage(pointer(2), message);
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        Store(scratch, &message, sizeof message);
        const unsigned handle = arg(1);
        if (!handle || handle > 64 || !win16.state->accelerators[handle - 1].native)
        {
            result = 0;
            return true;
        }
        const unsigned a[] = {arg(0), win16.state->accelerators[handle - 1].native, scratch};
        result = Api("TranslateAcceleratorA", a);
        return true;
    }
    case 430: // lstrcmp
    case 471: // lstrcmpi
    {
        const char *a = pointer(0) ? ReadString(pointer(0)) : "";
        char first[512];
        snprintf(first, sizeof first, "%s", a);
        const char *b = pointer(2) ? ReadString(pointer(2)) : "";
        int order = 0;
        for (unsigned i = 0; !order; ++i)
        {
            unsigned x = (unsigned char)first[i], y = (unsigned char)b[i];
            if (ordinal == 471)
            {
                if (x >= 'a' && x <= 'z') x -= 32;
                if (y >= 'a' && y <= 'z') y -= 32;
            }
            order = (int)x - (int)y;
            if (!x || !y) break;
        }
        result = (unsigned)order;
        return true;
    }
    // ------------------------------------------------- the breadth of part four
    case 10: // SetTimer(hwnd, id, elapse, procedure)
    {
        // The timer procedure is a far pointer, so it becomes a mark the same
        // way a window procedure does; the native timer calls WM_TIMER back
        // through the marked procedure.
        const unsigned procedure = frame.Long(0) ? Win16Procedure(frame.Long(0)) : 0;
        const unsigned a[] = {arg(0), arg(1), arg(2), procedure};
        result = Api("SetTimer", a);
        return true;
    }
    case 12: { const unsigned a[] = {arg(0), arg(1)}; result = Api("KillTimer", a); return true; }
    case 15: result = Api("GetTickCount", nullptr); return true; // GetCurrentTime is GetTickCount
    // GetMessagePos and GetMessageTime: the DWORD of the message the task
    // took last, in DX:AX. SYSDM asks for the position when the tree of the
    // Device Manager reports a click.
    case 119: result = Api("GetMessagePos", nullptr); return true;
    case 120: result = Api("GetMessageTime", nullptr); return true;
    case 16: // ClipCursor(rect): a null pointer releases the clip.
    {
        const unsigned at = pointer(0);
        if (!at)
        {
            const unsigned a[] = {0};
            result = Api("ClipCursor", a);
            return true;
        }
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        for (unsigned i = 0; i < 4; ++i)
            Write32(scratch + i * 4, (unsigned)(int)(short)Win16Word(at + i * 2));
        const unsigned a[] = {scratch};
        result = Api("ClipCursor", a);
        return true;
    }
    case 17: // GetCursorPos(point)
    {
        const unsigned at = pointer(0), scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {scratch};
        result = Api("GetCursorPos", a);
        if (at)
        {
            Win16Word(at, Read32(scratch));
            Win16Word(at + 2, Read32(scratch + 4));
        }
        return true;
    }
    case 18: { const unsigned a[] = {arg(0)}; result = Api("SetCapture", a); return true; }
    case 19: { const unsigned a[] = {}; result = Api("ReleaseCapture", a); return true; }
    case 236: { const unsigned a[] = {}; result = Api("GetCapture", a); return true; }
    case 28: // ClientToScreen(hwnd, point)
    {
        const unsigned at = pointer(1), scratch = Win16Scratch();
        if (!scratch || !at) { result = 0; return true; }
        Write32(scratch, (unsigned)(int)(short)Win16Word(at));
        Write32(scratch + 4, (unsigned)(int)(short)Win16Word(at + 2));
        const unsigned a[] = {arg(0), scratch};
        Api("ClientToScreen", a);
        Win16Word(at, Read32(scratch));
        Win16Word(at + 2, Read32(scratch + 4));
        result = 0;
        return true;
    }
    case 258: // MapWindowPoints(from, to, points, count)
    {
        // USER 3.1 returns nothing; the printer drivers' setup dialogs map
        // the rectangles of their controls with it (MSPRINT, the Apple
        // LaserWriter of Add Printer).
        const unsigned at = pointer(2), scratch = Win16Scratch();
        const unsigned count = arg(4) < 4096 / 8 ? arg(4) : 4096 / 8;
        result = 0;
        if (!scratch || !at || !count)
            return true;
        for (unsigned i = 0; i < count; ++i)
        {
            Write32(scratch + i * 8, (unsigned)(int)(short)Win16Word(at + i * 4));
            Write32(scratch + i * 8 + 4, (unsigned)(int)(short)Win16Word(at + i * 4 + 2));
        }
        const unsigned a[] = {arg(0), arg(1), scratch, count};
        Api("MapWindowPoints", a);
        for (unsigned i = 0; i < count; ++i)
        {
            Win16Word(at + i * 4, Read32(scratch + i * 8));
            Win16Word(at + i * 4 + 2, Read32(scratch + i * 8 + 4));
        }
        return true;
    }
    case 448: // DrawAnimatedRects(hwnd, idAni, from, to)
    {
        // The two narrow RECTs widened for the 32-bit entry. MSPRINT opens
        // its "Building driver information database" window with it.
        const unsigned scratch = Win16Scratch();
        if (!scratch) { result = 0; return true; }
        const unsigned rects[] = {pointer(2), pointer(4)};
        for (unsigned r = 0; r < 2; ++r)
            for (unsigned i = 0; rects[r] && i < 4; ++i)
                Write32(scratch + r * 16 + i * 4, (unsigned)(int)(short)Win16Word(rects[r] + i * 2));
        const unsigned a[] = {arg(0), signed_word(1), rects[0] ? scratch : 0u, rects[1] ? scratch + 16 : 0u};
        result = Api("DrawAnimatedRects", a);
        return true;
    }
    case 30: // WindowFromPoint(point): the point is one long argument, the
             // memory image of the POINT, so x is its low word.
    {
        const unsigned a[] = {(unsigned)(int)(short)frame.Word(0), (unsigned)(int)(short)frame.Word(1)};
        result = Api("WindowFromPoint", a);
        return true;
    }
    case 43: { const unsigned a[] = {arg(0)}; result = Api("CloseWindow", a); return true; }
    case 48: { const unsigned a[] = {arg(0), arg(1)}; result = Api("IsChild", a); return true; }
    case 67: { const unsigned a[] = {arg(0)}; result = Api("GetWindowDC", a); return true; }
    case 70: { const unsigned a[] = {signed_word(0), signed_word(1)}; result = Api("SetCursorPos", a); return true; }
    case 83: // FrameRect(hdc, rect, brush)
    {
        const unsigned at = pointer(1), scratch = Win16Scratch();
        if (!scratch || !at) { result = 0; return true; }
        for (unsigned i = 0; i < 4; ++i)
            Write32(scratch + i * 4, (unsigned)(int)(short)Win16Word(at + i * 2));
        const unsigned a[] = {arg(0), scratch, arg(3)};
        result = Api("FrameRect", a);
        return true;
    }
    case 94:
    {
        const unsigned a[] = {arg(0), arg(1), arg(2), arg(3)};
        result = Api("SetDlgItemInt", a);
        return true;
    }
    case 95: // GetDlgItemInt(hwnd, id, translated, signed)
    {
        const unsigned at = pointer(2), scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), arg(1), at ? scratch : 0u, frame.Word(0)};
        result = Api("GetDlgItemInt", a);
        if (at)
            Win16Word(at, Read32(scratch));
        return true;
    }
    case 100: // DlgDirList(hwnd, path, list, static, attributes)
    {
        const unsigned a[] = {arg(0), pointer(1), arg(3), arg(4), arg(5)};
        result = Api("DlgDirListA", a);
        return true;
    }
    case 195: // DlgDirListComboBox(hwnd, path, combo, static, attributes)
    {
        // Use the shared DlgDirList implementation with the combo-box variant
        // selected.
        const unsigned a[] = {arg(0), pointer(1), arg(3), arg(4), arg(5)};
        result = Api("DlgDirListComboBoxA", a);
        return true;
    }
    case 99:  // DlgDirSelect(hdlg, text, list)
    case 194: // DlgDirSelectComboBox(hdlg, text, combo)
    {
        // These entry points provide no length. Windows 98 writes at most 128
        // bytes including the terminator, so pass that capacity to the shared
        // implementation.
        const unsigned a[] = {arg(0), pointer(1), 128u, arg(3)};
        result = Api(ordinal == 99 ? "DlgDirSelectExA" : "DlgDirSelectComboBoxExA", a);
        return true;
    }
    case 422: // DlgDirSelectEx(hdlg, text, length, list)
    case 423: // DlgDirSelectComboBoxEx(hdlg, text, length, combo)
    {
        // These variants supply an explicit length; otherwise the behavior is
        // identical.
        const unsigned a[] = {arg(0), pointer(1), arg(3), arg(4)};
        result = Api(ordinal == 422 ? "DlgDirSelectExA" : "DlgDirSelectComboBoxExA", a);
        return true;
    }
    case 102: // AdjustWindowRect(rect, style, menu)
    {
        const unsigned at = pointer(0), scratch = Win16Scratch();
        if (!scratch || !at) { result = 0; return true; }
        for (unsigned i = 0; i < 4; ++i)
            Write32(scratch + i * 4, (unsigned)(int)(short)Win16Word(at + i * 2));
        const unsigned a[] = {scratch, frame.Long(1), frame.Word(0)};
        result = Api("AdjustWindowRect", a);
        for (unsigned i = 0; result && i < 4; ++i)
            Win16Word(at + i * 2, Read32(scratch + i * 4));
        return true;
    }
    case 454: // AdjustWindowRectEx(rect, style, menu, extended style): Solitaire of Win98
    {
        const unsigned at = pointer(0), scratch = Win16Scratch();
        if (!scratch || !at) { result = 0; return true; }
        for (unsigned i = 0; i < 4; ++i)
            Write32(scratch + i * 4, (unsigned)(int)(short)Win16Word(at + i * 2));
        const unsigned a[] = {scratch, frame.Long(3), frame.Word(2), frame.Long(0)};
        result = Api("AdjustWindowRectEx", a);
        for (unsigned i = 0; result && i < 4; ++i)
            Win16Word(at + i * 2, Read32(scratch + i * 4));
        return true;
    }
    case 105: { const unsigned a[] = {arg(0), arg(1)}; result = Api("FlashWindow", a); return true; }
    case 112: // WaitMessage: the native wait, which gives the processor up the
              // same way the scheduler rule of the task federation asks.
    {
        const unsigned a[] = {};
        result = Api("WaitMessage", a);
        return state != Failed;
    }
    case 116: // PostAppMessage(task, message, wparam, lparam)
    {
        unsigned message = arg(1), wparam = arg(2), lparam = frame.Long(0);
        if (!Win16ToThirtyTwo(0, message, wparam, lparam))
            return false;
        const unsigned a[] = {Win16TaskThread(arg(0)), message, wparam, lparam};
        result = Api("PostThreadMessageA", a);
        return true;
    }
    case 131: { const unsigned a[] = {arg(0), signed_word(1)}; result = Api("GetClassLongA", a); return true; }
    case 132:
    {
        const unsigned a[] = {arg(0), signed_word(1), frame.Long(0)};
        result = Api("SetClassLongA", a);
        return true;
    }
    case 137: { const unsigned a[] = {arg(0)}; result = Api("OpenClipboard", a); return true; }
    case 138: { const unsigned a[] = {}; result = Api("CloseClipboard", a); return true; }
    case 139: { const unsigned a[] = {}; result = Api("EmptyClipboard", a); return true; }
    case 140: { const unsigned a[] = {}; result = Api("GetClipboardOwner", a); return true; }
    case 142: return Win16GetClipboardData(frame, result, words);
    case 141: return Win16SetClipboardData(frame, result, words);
    case 143: { const unsigned a[] = {}; result = Api("CountClipboardFormats", a); return true; }
    case 144: { const unsigned a[] = {arg(0)}; result = Api("EnumClipboardFormats", a); return true; }
    case 216: // UserSeeUserDo(request, first, second, third), USER
        // MSISYS asks for the version while exporting its system page.
        // The other requests need USER's local heap or private data.
        if (arg(0) != 16) return false;
        words = 4;
        result = 0x0001102e;
        return true;
    case 257: // GetNextDriver(HDRVR, DWORD flags)
        // USER's legacy driver chain has no modules here: the display,
        // sound and input providers are native. MSISYS.OCX walks this list
        // while MSINFO32 exports its "System Information" page.
        words = 3;
        result = 0;
        return true;
    case 146: // GetClipboardFormatName(format, buffer, count)
    {
        // The far pointer takes the words 1 and 2.
        const unsigned a[] = {arg(0), pointer(1), signed_word(3)};
        result = Api("GetClipboardFormatNameA", a);
        return true;
    }
    case 147: { const unsigned a[] = {arg(0)}; result = Api("SetClipboardViewer", a); return true; }
    case 148: { const unsigned a[] = {}; result = Api("GetClipboardViewer", a); return true; }
    case 149:
    {
        const unsigned a[] = {arg(0), arg(1)};
        result = Api("ChangeClipboardChain", a);
        return true;
    }
    case 145:
    {
        const unsigned a[] = {pointer(0)};
        result = Api("RegisterClipboardFormatA", a);
        return true;
    }
    case 150: // LoadMenu(instance, name)
    {
        unsigned id = 0;
        char label[64];
        Win16ResourceName(frame, 0, id, label, sizeof label);
        result = Win16LoadMenu(arg(0), id, label);
        return true;
    }
    case 151: result = User::CreateMenu(); return true;
    case 152: { const unsigned a[] = {arg(0)}; result = Api("DestroyMenu", a); return true; }
    case 158: { const unsigned a[] = {arg(0), arg(1)}; result = Api("SetMenu", a); return true; }
    case 160: { const unsigned a[] = {arg(0)}; result = Api("DrawMenuBar", a); return true; }
    case 161: // GetMenuString(menu, item, buffer, size, flags)
    {
        char text[256];
        const unsigned a[] = {arg(0), arg(1), 0, 0, frame.Word(0)};
        unsigned copy[5] = {a[0], a[1], 0, 0, a[4]};
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        copy[2] = scratch;
        copy[3] = sizeof text < 256 ? sizeof text : 256;
        result = Api("GetMenuStringA", copy);
        snprintf(text, sizeof text, "%s", ReadString(scratch));
        result = pointer(2) ? PutString(pointer(2), text, frame.Word(1)) : result;
        return true;
    }
    case 175: // LoadBitmap(instance, name)
    {
        unsigned id = 0;
        char label[64];
        Win16ResourceName(frame, 0, id, label, sizeof label);
        result = Win16LoadBitmap(arg(0), id, label);
        return true;
    }
    case 224: // GetWindowTask: a task is a process here, and the thread of the
              // window is what a 16-bit program compares against its own task.
    {
        const unsigned a[] = {arg(0), 0};
        result = Api("GetWindowThreadProcessId", a);
        return true;
    }
    case 228:
    {
        const unsigned a[] = {arg(0), arg(1), arg(2)};
        result = Api("GetNextDlgTabItem", a);
        return true;
    }
    case 229: { const unsigned a[] = {arg(0)}; result = Api("GetTopWindow", a); return true; }
    case 230: { const unsigned a[] = {arg(0), arg(1)}; result = Api("GetWindow", a); return true; }
    case 243: { const unsigned a[] = {}; result = Api("GetDialogBaseUnits", a); return true; }
    case 250:
    {
        const unsigned a[] = {arg(0), arg(1), arg(2)};
        result = Api("GetMenuState", a);
        return true;
    }
    case 263: { const unsigned a[] = {arg(0)}; result = Api("GetMenuItemCount", a); return true; }
    case 264: { const unsigned a[] = {arg(0), arg(1)}; result = Api("GetMenuItemID", a); return true; }
    case 266: // SetMessageQueue(size): the queue of this port grows with what
              // is posted, so the queue a task has is at least the one it asks
              // for and the answer of USER.EXE is the true one.
        result = 1;
        return true;
    case 277: { const unsigned a[] = {arg(0)}; result = Api("GetDlgCtrlID", a); return true; }
    case 282:
    {
        const unsigned a[] = {arg(0), arg(1), arg(2)};
        result = Api("SelectPalette", a);
        return true;
    }
    case 283: { const unsigned a[] = {arg(0)}; result = Api("RealizePalette", a); return true; }
    case 290: // RedrawWindow(hwnd, rect, region, flags)
    {
        const unsigned at = pointer(1), scratch = Win16Scratch();
        unsigned rect = 0;
        if (at)
        {
            if (!scratch) return false;
            rect = scratch;
            for (unsigned i = 0; i < 4; ++i)
                Write32(rect + i * 4, (unsigned)(int)(short)Win16Word(at + i * 2));
        }
        const unsigned a[] = {arg(0), rect, arg(3), arg(4)};
        result = Api("RedrawWindow", a);
        return true;
    }
    case 403: // UnregisterClass(name, instance)
    {
        const unsigned a[] = {frame.Word(2) ? pointer(0) : frame.Word(1), arg(2)};
        result = Api("UnregisterClassA", a);
        return true;
    }
    case 410: // InsertMenu(menu, position, flags, id, text)
    case 411: // AppendMenu(menu, flags, id, text)
    {
        const unsigned text = frame.Word(1) ? frame.Pointer(0) : frame.Word(0);
        if (ordinal == 410)
        {
            const unsigned a[] = {arg(0), arg(1), arg(2), arg(3), text};
            result = Api("InsertMenuA", a);
            return true;
        }
        const unsigned a[] = {arg(0), arg(1), arg(2), text};
        result = Api("AppendMenuA", a);
        return true;
    }
    case 452: return Win16CreateWindow(frame, result, words, true);
    case 421: // wvsprintf(buffer, format, arguments): the same formatter as
              // wsprintf, but the arguments are a far pointer to the words the
              // caller already laid out.
        return Win16Printf(frame, result, words, true);
    case 404: return Win16GetClassInfo(frame, result, words);
    case 512: // WNetGetConnection(local, remote, length): no network
              // redirector belongs to this port, and WN_NOT_SUPPORTED is what
              // the entry of USER.EXE answers when none is installed.
        result = 1;
        return true;
    case 513: // WNetGetCaps(index). refuses an index below -1 or
              // above eleven with E001h, and Windows 98 answers zero for -1 and
              // otherwise the capability word the network driver left in its
              // table at DGROUP+0D16h. No network driver belongs to this port,
              // so every word of that table is zero and so is the answer, the
              // way Win98 answers it with no network installed.
        result = (int)(short)arg(0) < -1 || (int)(short)arg(0) > 11 ? 0xe001u : 0u;
        return true;
    case 291: // SetWindowsHookEx(kind, procedure, instance, task)
    {
        // hTask is the task database GetCurrentTask answered; the hook of
        // the 32-bit side belongs to the thread of that task. CTL3DV2 hooks
        // its own task this way (WH_CBT) and got null with the bare word

        const unsigned a[] = {signed_word(0), Win16Procedure(frame.Long(2)), arg(3), Win16TaskThread(arg(4))};
        result = Api("SetWindowsHookExA", a);
        return true;
    }
    case 292: // UnhookWindowsHookEx(hook): USER ends in `retf 4`; the
              // handle is the DWORD USER.291 answered. With one word the
              // program's stack came back two bytes short (WinPac 2
              // REGISTER.EXE).
    {
        const unsigned a[] = {frame.Long(0)};
        result = Api("UnhookWindowsHookEx", a);
        return true;
    }
    case 234: // UnhookWindowsHook(id, procedure)
    {
        // Validate the hook type in -1..12, then use the shared
        // UnhookWindowsHookEx implementation to find the hook by type and
        // procedure. Convert the far procedure pointer through the same token
        // as USER.121.
        const unsigned a[] = {signed_word(0), Win16Procedure(frame.Long(0))};
        result = Api("UnhookWindowsHook", a);
        return true;
    }
    case 472: // AnsiNext(text): the next character of a single byte page.
    {
        const unsigned at = pointer(0);
        unsigned char byte = 0;
        if (at)
            Read(at, &byte, 1);
        result = at && byte ? frame.Long(0) + 1 : frame.Long(0);
        return true;
    }
    case 473: // AnsiPrev(start, current)
    {
        const unsigned start = frame.Long(2), current = frame.Long(0);
        result = (current & 0xffff) > (start & 0xffff) ? current - 1 : start;
        return true;
    }
    default:
    {
        // The ordinals of the pass over the whole export table,
        // src/win16_user_more.inc.
        bool handled = false;
        const bool answered = Win16UserMore(ordinal, frame, words, result, handled);
        if (handled)
            return answered && state != Failed;
        break;
    }
    }
    char text[200];
    snprintf(text, sizeof text, "Win16 gate USER.%u (%s) is named but not wired", ordinal,
             name ? name : "?");
    Fail(text);
    return false;
}

// ---------------------------------------------------------- the USER helpers

// The 16-bit RECT is four words where the native one is four longs.
void Application::Win16ReadRect(unsigned at, Gdi::RECT &rect)
{
    if (!at)
    {
        rect = {0, 0, 0, 0};
        return;
    }
    rect = {(int)(short)Win16Word(at + Win16U::RectLeft), (int)(short)Win16Word(at + Win16U::RectTop),
            (int)(short)Win16Word(at + Win16U::RectRight), (int)(short)Win16Word(at + Win16U::RectBottom)};
}
void Application::Win16WriteRect(unsigned at, const Gdi::RECT &rect)
{
    if (!at)
        return;
    Win16Word(at + Win16U::RectLeft, (unsigned)rect.left);
    Win16Word(at + Win16U::RectTop, (unsigned)rect.top);
    Win16Word(at + Win16U::RectRight, (unsigned)rect.right);
    Win16Word(at + Win16U::RectBottom, (unsigned)rect.bottom);
}

// USER.420 wsprintf. The entry is the one cdecl entry of USER.EXE, so the
// caller removes the arguments and the gate takes none off the stack.
//
// The formatting itself is shared with src/win98_printf.cpp; only
// the widths of the arguments differ, because an int is a word on this side and
// a pointer is a far pointer. The gate therefore walks the format string once
// with the same parser the formatter uses, lays the arguments out as the dwords
// the formatter reads, and lets the formatter do the rest. A conversion the
// formatter does not know stops the run inside it, not here.
bool Application::Win16Printf(const Win16Frame &frame, unsigned &result, unsigned &words)
{
    return Win16Printf(frame, result, words, false);
}
// wvsprintf is the same entry with the arguments behind a far pointer instead
// of on the stack of the caller, and with the Pascal frame of six words.
bool Application::Win16Printf(const Win16Frame &frame, unsigned &result, unsigned &words, bool list)
{
    words = list ? 6u : 0u;
    const unsigned out = list ? frame.Pointer(4) : frame.Pointer(0);
    const unsigned format = list ? frame.Pointer(2) : frame.Pointer(2);
    const unsigned arguments = list ? frame.Pointer(0) : 0u;
    result = 0;
    if (!out || !format || (list && !arguments))
        return true;
    result = Win16FormatWords(out, format, list ? arguments : 0, &frame);
    if (log && trace_calls)
    {
        // What a 16-bit program formats, often its own diagnostics.
        char text[320];
        const char *written = ReadString(out);
        snprintf(text, sizeof text, "WIN16PRINTF %.280s", written ? written : "");
        log(text);
    }
    return state != Failed;
}
// The formatter of wsprintf and wvsprintf over the argument words: in the
// block at `arguments`, or on the stack of the cdecl frame behind the format.
unsigned Application::Win16FormatWords(unsigned out, unsigned format, unsigned arguments, const Win16Frame *frame)
{
    const bool list = arguments != 0;
    auto word_at = [&](unsigned index) {
        return list ? Win16Word(arguments + (index - 4) * 2) : frame->Word(index);
    };
    auto byte = [&](unsigned at) {
        unsigned char value = 0;
        Read(at, &value, 1);
        return (unsigned)value;
    };
    unsigned slots[128] = {}, count = 0, word = 4, at = format;
    // The format for the shared formatter. In USER's wsprintf a size prefix
    // in front of %s or %c says near or far ("%ls" is a far string), and
    // every string is a far pointer anyway; the formatter of the 32-bit side
    // reads "%ls" as a string of wide characters. The prefix is left out
    // there: MCIPIONR builds "COM1" with "%ls%1d" and got garbage that ran
    // over its buffer.
    char shared[1024];
    unsigned shared_length = 0, copied = format;
    auto copy_until = [&](unsigned end) {
        for (; copied < end; ++copied)
            if (shared_length + 1 < sizeof shared) shared[shared_length++] = (char)byte(copied);
    };
    for (unsigned budget = 0; byte(at) && budget < 4096; ++budget)
    {
        if (byte(at) != '%')
        {
            ++at;
            continue;
        }
        ++at;
        while (byte(at) == '-' || byte(at) == '#')
            ++at;
        if (byte(at) == '0')
            ++at;
        while (byte(at) >= '0' && byte(at) <= '9')
            ++at;
        if (byte(at) == '.')
        {
            ++at;
            while (byte(at) >= '0' && byte(at) <= '9')
                ++at;
        }
        bool wide = false;
        unsigned prefix = 0;
        if (byte(at) == 'w' || byte(at) == 'l' || byte(at) == 't' || byte(at) == 'h')
        {
            wide = byte(at) == 'l';
            prefix = at;
            ++at;
        }
        const unsigned kind = byte(at);
        if (!kind || count >= 128)
            break;
        if (prefix && (kind == 's' || kind == 'S' || kind == 'c' || kind == 'C'))
        {
            copy_until(prefix);
            copied = prefix + 1;
        }
        ++at;
        if (kind == 's' || kind == 'S')
        {
            // A string is a far pointer on this side and a flat one on the
            // other; both name the same bytes.
            const unsigned pointer = word_at(word) | word_at(word + 1) << 16;
            slots[count++] = pointer ? Win16Linear(pointer >> 16, pointer & 0xffff) : 0u;
            word += 2;
            continue;
        }
        if (kind == 'c' || kind == 'C')
        {
            slots[count++] = word_at(word);
            word += 1;
            continue;
        }
        if (kind != 'd' && kind != 'i' && kind != 'u' && kind != 'x' && kind != 'X')
            continue;
        if (wide)
        {
            slots[count++] = word_at(word) | word_at(word + 1) << 16;
            word += 2;
            continue;
        }
        const unsigned value = word_at(word);
        // A signed word reaches the formatter as the dword of the same number.
        slots[count++] = kind == 'd' || kind == 'i' ? (unsigned)(int)(short)value : value;
        word += 1;
    }
    const unsigned block = Allocate(sizeof slots);
    if (!block)
    {
        last_error = 8;
        return 0;
    }
    for (unsigned i = 0; i < count; ++i)
        Write32(block + i * 4, slots[i]);
    // The rest of the format, to its end.
    while (byte(at)) ++at;
    copy_until(at);
    shared[shared_length] = 0;
    const unsigned text = String(shared);
    const unsigned result = text ? Printf(out, text, block) : 0;
    if (text) Free(text);
    Free(block);
    return result;
}

// USER.606 FormatMessage(dwFlags, lpSource, wMessageId, wLanguageId, lpBuffer,
// nSize, lpArgs), eleven words, the 16-bit body (SYSDM's
// Performance page writes "%1!u!%% free" with it):
// 0C58 a language, a buffer without selector, or a flag in FFFFC000h fail
// 0CB3 FORMAT_MESSAGE_FROM_STRING takes lpSource; FROM_HMODULE loads string
// wMessageId of the module in the low word of lpSource (0CF5, at most
// FFh bytes); FROM_SYSTEM the string of USER's own module (0C46)
// 0D41 "%1" to "%99": the insert, with "!spec!" or "%s"; the arguments are
// dwords behind lpArgs, read in order and kept, and one dword more for
// each "*" of the spec (at most two); the formatter is USER's
// wsprintf. FORMAT_MESSAGE_IGNORE_INSERTS or no lpArgs
// writes the insert back as "%n" or "%n!spec!" (0F1C)
// 0F6A "%0" ends the text, "%!" "%t" "%b" "%r" and "%" with a line feed
// are the escapes, any other "%c" is c (with the "%" before it under
// IGNORE_INSERTS)
// 1032 a carriage return (with the line feed after it) is a line break
// without a width in the low byte of dwFlags and a space with one; a
// space at or past the width breaks the line
// 109A the text with its zero must fit nSize, the answer is its length
// FORMAT_MESSAGE_ALLOCATE_BUFFER (10A9) takes the text into a fixed block of
// the local heap of DS, which the entry does not change, so it is the caller's
// own heap, and writes the handle as a word to lpBuffer; SYSDM puts it into
// its dialogs as a near pointer and gives it back with LocalFree.
bool Application::Win16FormatMessage(const Win16Frame &frame, unsigned &result)
{
    const unsigned args_far = frame.Long(0), size = frame.Word(2), buffer_far = frame.Long(3);
    const unsigned language = frame.Word(5), id = frame.Word(6), source_far = frame.Long(7);
    const unsigned flags = frame.Long(9);
    result = 0;
    if (language || !(buffer_far >> 16) || (flags & 0xffffc000u))
        return true;
    if (flags & 0x1000)
    {
        Fail("Win16 USER.606 FormatMessage with FORMAT_MESSAGE_FROM_SYSTEM needs USER's own string table");
        return false;
    }
    const unsigned width = flags & 0xff;
    const bool ignore = flags & 0x200;
    constexpr unsigned Capacity = 0x10000;
    const unsigned scratch = Allocate(Capacity + 0x400);
    if (!scratch)
        return true;
    struct Release
    {
        Application &app;
        unsigned block;
        ~Release() { app.Free(block); }
    } release{*this, scratch};
    const unsigned text = scratch + Capacity, spec = text + 0x100, words = spec + 0x40;
    unsigned source = 0;
    if (flags & 0x400)
    {
        if (!(source_far >> 16))
            return true;
        source = Win16Linear(source_far >> 16, source_far & 0xffff);
    }
    else if (flags & 0x800)
    {
        const unsigned module = source_far & 0xffff;
        if (!module || !Win16LoadStringInto(module, id, text, 0xff))
            return true;
        source = text;
    }
    else
        return true;
    auto byte = [&](unsigned at) {
        unsigned char value = 0;
        Read(at, &value, 1);
        return unsigned(value);
    };
    unsigned out = scratch, column = 0, line = 0;
    auto put = [&](unsigned char c) {
        if (out < scratch + Capacity - 1)
            Store(out++, &c, 1);
    };
    unsigned cache[100] = {}, fetched = 0, next = args_far ? Win16Linear(args_far >> 16, args_far & 0xffff) : 0;
    auto argument = [&]() {
        const unsigned value = next ? Read32(next) : 0;
        next += 4;
        if (fetched < 100)
            cache[fetched++] = value;
        return value;
    };
    unsigned at = source;
    for (unsigned guard = 0; guard < Capacity; ++guard)
    {
        const unsigned c = byte(at);
        if (!c)
            break;
        if (c == '%')
        {
            line = out;
            ++at;
            const unsigned d = byte(at);
            if (d >= '1' && d <= '9')
            {
                unsigned n = d - '0';
                ++at;
                if (byte(at) >= '0' && byte(at) <= '9')
                    n = n * 10 + (byte(at++) - '0');
                --n;
                unsigned stars = 0;
                char format[40] = "%s";
                if (byte(at) == '!')
                {
                    ++at;
                    unsigned k = 1;
                    format[0] = '%';
                    while (byte(at) != '!')
                    {
                        if (!byte(at) || k >= 32)
                            return true;
                        if (byte(at) == '*' && stars++ > 1)
                            return true;
                        format[k++] = char(byte(at++));
                    }
                    format[k] = 0;
                    ++at;
                }
                char staged[64];
                if (ignore || !next)
                {
                    if (!strcmp(format, "%s"))
                        snprintf(staged, sizeof staged, "%%%u", n + 1);
                    else
                        snprintf(staged, sizeof staged, "%%%u!%s!", n + 1, format + 1);
                    for (const char *p = staged; *p; ++p)
                        put((unsigned char)*p);
                }
                else
                {
                    while (fetched <= n && fetched < 100)
                        argument();
                    const unsigned value = n < 100 ? cache[n] : 0;
                    unsigned extra[2] = {};
                    for (unsigned i = 0; i < stars && i < 2; ++i)
                        extra[i] = argument();
                    const unsigned list[3] = {value, extra[0], extra[1]};
                    for (unsigned i = 0; i < 3; ++i)
                    {
                        Win16Word(words + i * 4, list[i] & 0xffff);
                        Win16Word(words + i * 4 + 2, list[i] >> 16);
                    }
                    Store(spec, format, unsigned(strlen(format)) + 1);
                    if (out + 0x400 < scratch + Capacity)
                        out += Win16FormatWords(out, spec, words, nullptr);
                }
            }
            else
            {
                ++at;
                if (d == '0')
                    break;
                if (!d)
                    return true;
                if (d == '!')
                    put('!');
                else if (d == 't')
                {
                    put(9);
                    column = column & 7 ? (column + 7) & ~7u : column + 8;
                }
                else if (d == 'b')
                    put(' ');
                else if (d == 'r')
                {
                    put(13);
                    line = 0;
                }
                else if (d == 10)
                {
                    put(13);
                    put(10);
                    line = 0;
                }
                else
                {
                    if (ignore)
                        put('%');
                    put((unsigned char)d);
                }
            }
            column = line ? column + (out - line) : 0;
            continue;
        }
        ++at;
        unsigned ch = c;
        if (ch == 13)
        {
            if (byte(at) == 10)
                ++at;
            ch = width ? ' ' : 10;
        }
        if (ch == 10 || (ch == ' ' && width && width != 0xff && column >= width))
        {
            put(13);
            put(10);
            column = 0;
            continue;
        }
        put((unsigned char)ch);
        ++column;
    }
    put(0);
    const unsigned length = out - scratch;
    const unsigned buffer = Win16Linear(buffer_far >> 16, buffer_far & 0xffff);
    unsigned target = buffer;
    if (flags & 0x100)
    {
        Win16Word(buffer, 0);
        const unsigned handle = Win16LocalOperation(Win16LocalAllocate, 0x40, length);
        if (!handle)
            return true;
        target = Win16Linear(Register(Reg::Ds), handle);
        Win16Word(buffer, handle);
    }
    else if (size < length)
        return true;
    auto *bytes = new unsigned char[length];
    if (Read(scratch, bytes, length))
        Store(target, bytes, length);
    delete[] bytes;
    result = length - 1;
    return true;
}

// EM_GETHANDLE and EM_SETHANDLE. The edit control of the 16-bit USER keeps its
// text in a movable block of a local heap and hands the handle out, so the
// program grows the block itself and gives it back; that is how SYSEDIT loads
// every file it shows. The native edit control keeps its text natively, so the
// gate writes the text into a block of the task's own local heap and reads it
// back out of one. The heap is the one LocalReAlloc between the two messages
// works on, because both take the automatic data segment out of DS.
// This is not the original arrangement: there the control owns the block and
// the program only borrows it. Here the block belongs to the task and the text
// is copied at both ends, which is what the native control can answer for.
unsigned Application::Win16EditHandle(unsigned hwnd, unsigned message, unsigned wparam)
{
    if (!hwnd || !User::IsWindow(hwnd) || !win16.state)
        return 0;
    const unsigned base = Win16Linear(Register(Reg::Ds), 0);
    if (!base)
        return 0;
    auto &machine = *win16.state;
    auto *record = &machine.edit_buffers[0];
    for (auto &entry : machine.edit_buffers)
        if (entry.hwnd == hwnd || (!entry.hwnd && !record->hwnd))
            record = &entry;
    if (message == Win16U::EmSetHandle)
    {
        const unsigned offset = Win16LocalOperation(Win16LocalLock, wparam);
        if (!offset)
            return 0;
        record->hwnd = hwnd;
        record->handle = wparam;
        CallWindow(hwnd, 0x000c, 0, base + offset); // WM_SETTEXT
        return 0;
    }
    if (record->hwnd == hwnd && record->handle)
        Win16LocalOperation(Win16LocalFree, record->handle);
    char *text = new char[65536];
    char *converted = new char[131072];
    User::GetWindowText(hwnd, text, 65536);
    unsigned n = 0;
    for (unsigned i = 0; text[i] && n + 2 < 131072; ++i)
    {
        if (text[i] == '\n')
            converted[n++] = '\r';
        converted[n++] = text[i];
    }
    converted[n] = 0;
    const unsigned handle = Win16LocalOperation(Win16LocalAllocate, 0x0002, n + 1);
    const unsigned offset = handle ? Win16LocalOperation(Win16LocalLock, handle) : 0u;
    if (offset)
        Store(base + offset, converted, n + 1);
    delete[] text;
    delete[] converted;
    if (!offset)
        return 0;
    record->hwnd = hwnd;
    record->handle = handle;
    return handle;
}
unsigned Application::Win16Send(unsigned hwnd, unsigned message, unsigned wparam, unsigned lparam)
{
    if (User::IsEdit(hwnd) && (message == Win16U::EmGetHandle || message == Win16U::EmSetHandle))
        return Win16EditHandle(hwnd, message, wparam);
    // The tree view of the 16-bit COMMCTRL (src/win16_common_controls.cpp).
    if (unsigned answer = 0; Win16TreeSend(hwnd, message, wparam, lparam, answer) ||
                             Win16ListSend(hwnd, message, wparam, lparam, answer))
        return answer;
    const unsigned message16 = message;
    Win16ControlTransfer transfer;
    if (!Win16ToThirtyTwo(hwnd, message, wparam, lparam, &transfer))
        return 0;
    // The 32-bit entry, not Send and not the window procedure directly: a
    // window of another task is reached through the cross-process and
    // cross-thread SendMessage that is already there, which is the rule
    // PLAN.md step 9 names for the federation, and HWND_BROADCAST takes the
    // recipient walk of USER there as well.
    const unsigned a[] = {hwnd, message, wparam, lparam};
    // An answer to carry back is work after the procedure (GiveTail).
    if (transfer.kind || transfer.buffer || message16 == Win16U::WmMdiGetActive) TakeTail();
    return Win16ControlAnswer(transfer, Win16Answer(message16, Api("SendMessageA", a)));
}
unsigned Application::Win16ControlAnswer(Win16ControlTransfer &transfer, unsigned result)
{
    // The previous icon of a STATIC is a native handle; the 16-bit caller
    // gets the word the LoadIcon gate gave out for it.
    if (transfer.kind == Win16C::ImageAnswer) return Win16ImageWord(result);
    if (!transfer.buffer) return result;
    if (transfer.kind == Win16C::RectOut)
        for (unsigned i = 0; i < transfer.count; ++i)
            Win16Word(transfer.target + i * 2, Read32(transfer.buffer + i * 4));
    else if (transfer.kind == Win16C::WordsOut && (int)result > 0)
        for (unsigned i = 0; i < result && i < transfer.count; ++i)
            Win16Word(transfer.target + i * 2, Read32(transfer.buffer + i * 4));
    Free(transfer.buffer);
    transfer.buffer = 0;
    return result;
}
// The answer of a message whose two sides package the result differently.
unsigned Application::Win16Answer(unsigned message16, unsigned result)
{
    auto &machine = *win16.state;
    if (message16 == Win16U::WmMdiGetActive && machine.answer_staged)
    {
        // MAKELONG(hwndActive, fMaximized), the shape USER answers.
        result = (result & 0xffff) | ((Read32(machine.answer_staged) ? 1u : 0u) << 16);
        machine.answer_staged = 0;
    }
    return result;
}
// A message on its way out of 16-bit code into the native packing. The
// structures a 16-bit caller hands over are named by a far pointer that is
// already a flat address here, so only the widths move.
bool Application::Win16ToThirtyTwo(unsigned hwnd, unsigned &message, unsigned &wparam, unsigned &lparam,
                                 Win16ControlTransfer *transfer)
{
    // The delta of the WM_MOUSEWHEEL a 16-bit procedure is handing on.
    if (message == 0x20a && !(wparam >> 16) && (win16_wheel & 0x80000000u))
        wparam |= (win16_wheel & 0xffff) << 16;
    // WM_NOTIFY of 16-bit code names its block by a far pointer; the header
    // has the places of the 32-bit one.
    if (message == 0x4e && lparam >> 16)
    {
        lparam = Win16Linear(lparam >> 16, lparam & 0xffff);
        return true;
    }
    // PSM_ADDPAGE to the native frame of a 16-bit sheet carries a 16-bit page
    // handle; the frame takes a native one (src/win16_property_sheet.cpp).
    if (message == 0x467 && PropertySheetWindow(hwnd))
    {
        lparam = lparam >> 16 ? Win16NativePage(lparam) : 0u;
        return true;
    }
    // PSM_SETTITLE and PSM_SETFINISHTEXT carry a string by a far pointer
    // (or a resource identifier in the low word).
    if ((message == 0x46f || message == 0x473) && PropertySheetWindow(hwnd))
    {
        if (lparam >> 16)
            lparam = Win16Linear(lparam >> 16, lparam & 0xffff);
        return true;
    }
    if (User::IsLabel(hwnd) && Win16C::StaticTo32(message) != message)
    {
        // The icon travels as the 16-bit word of the image alias and reaches
        // the native STATIC as its handle (src/x86_static_control.cpp).
        message = Win16C::StaticTo32(message);
        if (message == 0x170) wparam = Win16ImageNative(wparam & 0xffff);
        else if (message == 0x172 && (wparam == 1 || wparam == 2)) lparam = Win16ImageNative(lparam & 0xffff);
        // A bitmap is a GDI word in the low half; the high half is whatever
        // the caller had there (MODEM.CPL passes its DS, the picture of the
        // modem wizard).
        else if (message == 0x172 && wparam == 0) lparam &= 0xffff;
        if (transfer && (message == 0x170 || message == 0x171 || wparam == 1 || wparam == 2))
            *transfer = {0, 0, 0, Win16C::ImageAnswer};
        return true;
    }
    const unsigned control = User::IsEdit(hwnd)     ? 2u
                             : User::IsButton(hwnd) ? Win16C::ButtonControl
                                                    : User::CommonControl(hwnd);
    if (Win16C::To32(control, message) != message)
    {
        message = Win16C::To32(control, message);
        Win16C::Params32(control, message, wparam, lparam);
        const unsigned buffer = Win16C::MessageBuffer(control, message, User::Style(hwnd));
        if (!lparam || buffer == Win16C::None) return true;
        lparam = Win16RememberFar(lparam);
        if (buffer == Win16C::StringIn || buffer == Win16C::TextOut) return true;
        if (!transfer)
        {
            Fail("Win16 control array/rectangle requires synchronous message delivery");
            return false;
        }
        const unsigned count = (buffer == Win16C::RectIn || buffer == Win16C::RectOut) ? 4 : wparam;
        if (count > 32767)
        {
            Fail("Win16 control array exceeds a segment");
            return false;
        }
        if (!count) return true;
        const unsigned wide = Allocate(count * 4);
        if (!wide) return false;
        *transfer = {lparam, wide, count, buffer};
        for (unsigned i = 0; i < count; ++i)
            Write32(wide + i * 4, buffer == Win16C::WordsIn || buffer == Win16C::RectIn
                                     ? Win16C::SignedWord(Win16Word(lparam + i * 2)) : 0);
        lparam = wide;
        return true;
    }
    const unsigned kind = Win16U::KindOf(message, false);
    switch (kind)
    {
    case Win16U::KindPlain:
        return true;
    case Win16U::KindSwap:
    {
        const auto *rule = Win16U::RuleFor(message);
        unsigned wide = 0, out = 0;
        Win16U::SwapTo32(*rule, wparam, lparam, wide, out);
        wparam = wide;
        lparam = out;
        return true;
    }
    case Win16U::KindControlColor:
        message = Win16U::ControlColor32(lparam >> 16);
        lparam = lparam & 0xffff;
        if (!message)
        {
            Fail("Win16 WM_CTLCOLOR names a control type the 32-bit side has no message for");
            return false;
        }
        return true;
    case Win16U::KindText:
        // A far pointer to text names the same bytes on both sides.
        if (lparam)
            lparam = Win16Linear(lparam >> 16, lparam & 0xffff);
        return true;
    case Win16U::KindParentNotify:
        // The exact inverse of the packing the delivery uses: a creation or a
        // destruction carries the child in the low half and its identifier in
        // the high half of the 16-bit lParam.
        if (Win16U::ParentNotifyCarriesId(wparam & 0xffff))
        {
            wparam = (wparam & 0xffff) | ((lparam >> 16) << 16);
            lparam = lparam & 0xffff;
        }
        return true;
    case Win16U::KindMdiActivate:
        // 16: lParam names the deactivated window in one half and the
        // activated one in the other; 32 puts one in each parameter.
        wparam = lparam >> 16;
        lparam = lparam & 0xffff;
        return true;
    case Win16U::KindStructureUp:
    {
        // WM_MDICREATE: the 16-bit caller owns the MDICREATESTRUCT and the
        // native MDI client reads the 32-bit shape, so the widened copy goes
        // into the staging ring.
        if (!lparam)
            return true;
        const unsigned widened = Win16WidenStructure(Win16U::RuleFor(message)->structure, lparam);
        if (!widened)
            return false;
        lparam = widened;
        return true;
    }
    case Win16U::KindMdiGetActive:
    {
        // The 32-bit form writes the maximized flag through lParam; the word
        // the 16-bit caller expects in the high half of the answer is read out
        // of the staging slot again in Win16Answer.
        auto &machine = *win16.state;
        machine.answer_staged = Win16Staging(4);
        if (!machine.answer_staged)
            return false;
        wparam = 0;
        lparam = machine.answer_staged;
        return true;
    }
    case Win16U::KindMdiSetMenu:
    {
        // 16: wParam = refresh flag, lParam = frame menu | window menu << 16.
        // 32: wParam = frame menu, lParam = window menu. The refresh flag has
        // no 32-bit counterpart; the native client always refreshes.
        const unsigned window_menu = lparam >> 16;
        wparam = lparam & 0xffff;
        lparam = window_menu;
        return true;
    }
    case Win16U::KindStructure:
    {
        // A window procedure that hands the message on to DefWindowProc gives
        // back the far pointer it was given. The 32-bit structure behind it is
        // the one the delivery staged, so the changed fields go back into it
        // and the native reader sees its own shape again.
        auto &machine = *win16.state;
        for (unsigned i = machine.delivery_count; i--;)
        {
            auto &record = machine.deliveries[i];
            if (record.message != message || record.staged_far != lparam)
                continue;
            Win16StructureOut(record.structure, record.staged, record.original);
            lparam = record.original;
            return true;
        }
        // VB can copy NCCALCSIZE to its own stack before forwarding it.
        // Such a pointer does not match a delivery record. Widen its signed
        // rectangles and WINDOWPOS for the synchronous call, then copy the
        // resulting rectangles back to the caller's original buffer.
        if (message == Win16U::WmNcCalcSize && transfer && lparam)
        {
            const unsigned source = Win16RememberFar(lparam);
            const unsigned wide = Allocate(wparam ? 80 : 16);
            if (!source || !wide) { if (wide) Free(wide); return false; }
            const unsigned count = wparam ? 12 : 4;
            for (unsigned i = 0; i < count; ++i)
                Write32(wide + i * 4, (unsigned)(int)(short)Win16Word(source + i * 2));
            if (wparam)
            {
                const unsigned position = Win16RememberFar(Read32(source + Win16U::CalcSizePosition));
                Write32(wide + 48, position ? wide + 52 : 0);
                if (position)
                    for (unsigned i = 0; i < 7; ++i)
                    {
                        unsigned value = Win16Word(position + i * 2);
                        if (i >= 2 && i <= 5) value = (unsigned)(int)(short)value;
                        Write32(wide + 52 + i * 4, value);
                    }
            }
            *transfer = {source, wide, count, Win16C::RectOut};
            lparam = wide;
            return true;
        }
        // WM_GETMINMAXINFO uses the caller's own buffer. The 16-bit form
        // contains five points of two words each; the 32-bit form uses five
        // pairs of doublewords. VB1 applications query their own window
        // limits this way.
        if (message == Win16U::WmGetMinMaxInfo && transfer && lparam)
        {
            const unsigned source = Win16RememberFar(lparam);
            const unsigned wide = Allocate(40);
            if (!source || !wide) { if (wide) Free(wide); return false; }
            for (unsigned i = 0; i < 10; ++i)
                Write32(wide + i * 4, (unsigned)(int)(short)Win16Word(source + i * 2));
            *transfer = {source, wide, 10, Win16C::RectOut};
            lparam = wide;
            return true;
        }
        // Nothing of this process staged that pointer, so a 16-bit structure
        // would reach a 32-bit reader. The gate says so instead.
        break;
    }
    default:
        break;
    }
    char text[200];
    snprintf(text, sizeof text, "Win16 message %04x has no translation into the 32-bit packing yet",
             message);
    Fail(text);
    return false;
}
bool Application::Win16NeedsTransfer(unsigned hwnd, unsigned message, unsigned lparam)
{
    if (!lparam || !User::IsWindow(hwnd) || User::IsLabel(hwnd)) return false;
    const unsigned control = User::IsEdit(hwnd)     ? 2u
                             : User::IsButton(hwnd) ? Win16C::ButtonControl
                                                    : User::CommonControl(hwnd);
    const unsigned native = Win16C::To32(control, message);
    if (native == message) return false;
    const unsigned buffer = Win16C::MessageBuffer(control, native, User::Style(hwnd));
    return buffer != Win16C::None && buffer != Win16C::StringIn && buffer != Win16C::TextOut;
}
void Application::Win16ReadMessage(unsigned at, User::Message &out, Win16ControlTransfer *transfer)
{
    out.hwnd = Win16Word(at + Win16U::MessageWindow);
    out.message = Win16Word(at + Win16U::MessageMessage);
    out.wparam = Win16Word(at + Win16U::MessageWparam);
    out.lparam = Win16Word(at + Win16U::MessageLparam) | (Win16Word(at + Win16U::MessageLparam + 2) << 16);
    // The delta of the WM_MOUSEWHEEL this MSG was written from.
    if (out.message == 0x20a && out.hwnd == win16_wheel_window && out.lparam == win16_wheel_place)
        out.wparam |= win16_wheel_queued << 16;
    if (out.message == 0x113 && out.lparam)
        out.lparam = Win16Procedure(out.lparam);
    out.time = Win16Word(at + Win16U::MessageTime) | (Win16Word(at + Win16U::MessageTime + 2) << 16);
    out.x = (short)Win16Word(at + Win16U::MessagePoint);
    out.y = (short)Win16Word(at + Win16U::MessagePoint + 2);
    unsigned message = out.message, wparam = out.wparam, lparam = out.lparam;
    // Only the delivery copies an array or a rectangle; a reader that merely
    // looks at the message (TranslateMessage, IsDialogMessage) keeps it.
    if (!transfer && Win16NeedsTransfer(out.hwnd, message, lparam))
        return;
    if (Win16ToThirtyTwo(out.hwnd, message, wparam, lparam, transfer))
    {
        out.message = message;
        out.wparam = wparam;
        out.lparam = lparam;
    }
}
void Application::Win16WriteMessage(unsigned at, const User::Message &message)
{
    unsigned number = message.message, wparam = message.wparam, lparam = message.lparam;
    if (number == 0x113 && lparam)
        lparam = Win16ProcedurePointer(lparam);
    // The MSG of 16-bit code has no place for the delta of WM_MOUSEWHEEL;
    // it is kept beside it for the DispatchMessage that follows.
    if (number == 0x20a)
    {
        win16_wheel_window = message.hwnd;
        win16_wheel_place = lparam;
        win16_wheel_queued = (wparam >> 16) & 0xffff;
    }
    const unsigned control = User::IsEdit(message.hwnd)     ? 2u
                             : User::IsButton(message.hwnd) ? Win16C::ButtonControl
                                                            : User::CommonControl(message.hwnd);
    const unsigned number16 = Win16C::To16(control, number);
    if (number16 != number)
    {
        number = number16;
        // Win16 EM_SETSEL is the scalar control message in this family whose
        // two indices have a different queue representation: both are packed
        // into lParam. Pointer-bearing messages still need synchronous staging.
        if (control == 2 && message.message == 0xb1)
        {
            wparam = 0;
            lparam = (message.wparam & 0xffff) | (message.lparam << 16);
        }
    }
    const unsigned kind = Win16U::KindOf(message.message, true);
    if (number16 == message.message && kind == Win16U::KindSwap)
    {
        const auto *rule = Win16U::RuleFor(message.message);
        Win16U::SwapTo16(*rule, message.wparam, message.lparam, wparam, lparam);
    }
    else if (number16 == message.message && kind == Win16U::KindControlColor)
    {
        number = Win16U::WmCtlColor;
        lparam = (message.lparam & 0xffff) | (Win16U::ControlColorType(message.message) << 16);
    }
    Win16Word(at + Win16U::MessageWindow, message.hwnd);
    Win16Word(at + Win16U::MessageMessage, number);
    Win16Word(at + Win16U::MessageWparam, wparam);
    Win16Word(at + Win16U::MessageLparam, lparam & 0xffff);
    Win16Word(at + Win16U::MessageLparam + 2, lparam >> 16);
    Win16Word(at + Win16U::MessageTime, message.time & 0xffff);
    Win16Word(at + Win16U::MessageTime + 2, message.time >> 16);
    Win16Word(at + Win16U::MessagePoint, (unsigned)message.x);
    Win16Word(at + Win16U::MessagePoint + 2, (unsigned)message.y);
}
bool Application::Win16GetMessage(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                  unsigned &words)
{
    auto arg = [&](unsigned position) { return frame.Word(words - 1 - position); };
    const unsigned out = frame.Pointer(words - 2);
    const unsigned scratch = Win16Scratch();
    if (!scratch)
        return false;
    // Parameter word indices start at zero; the message pointer occupies two
    // words. The window filter is at index 2, range limits at 3 and 4, and
    // PeekMessage's removal flag at 5.
    // Both ordinals use the 32-bit entry's wait and paint handling. If
    // GetMessage finds nothing, retain waiting and leave the frame intact so
    // the gate can be reentered after wakeup. Message retrieval is also where
    // a 16-bit task receives cross-process broadcasts.
    if (HostRequestPending() || host_desktop_pending || (host_icon_wanted && !host_icon_tried))
    {
        ServeHostRequests();
        if (state == Failed || state == Exited)
            return false;
    }
    const unsigned a[] = {scratch, arg(2), arg(3), arg(4), ordinal == 109 ? arg(5) : 0u};
    result = Api(ordinal == 109 ? "PeekMessageA" : "GetMessageA", a);
    if (waiting || state == Failed || state == Exited)
        return state != Failed;
    if (out)
    {
        User::Message message{};
        Read(scratch, &message, sizeof message);
        Win16WriteMessage(out, message);
    }
    return true;
}
// A block of guest memory the gates stage 32-bit structures in. One per
// process, because a gate never runs while another gate of the same process
// is in the middle of its own staging.
unsigned Application::Win16Scratch()
{
    auto &machine = *win16.state;
    // The depth of the gate that asks: callbacks nest at most 32 deep
    // (Win16Invoke), and a gate runs one level above its caller.
    if (win16_gate_depth >= Win16Machine::ScratchLevels)
    {
        Fail("Win16 gate staging nests deeper than its blocks");
        return 0;
    }
    unsigned &block = machine.scratch[win16_gate_depth];
    if (!block)
    {
        // 4 KiB, because the widest staging is an array: the spacing of
        // ExtTextOut and the points of DPtoLP.
        block = Allocate(4096);
        if (!block)
        {
            Fail("Win16 gate staging block could not be allocated");
            return 0;
        }
    }
    unsigned char zero[4096] = {};
    Store(block, zero, sizeof zero);
    return block;
}
bool Application::Win16BeginPaint(const Win16Frame &frame, unsigned &result, unsigned &words)
{
    const unsigned hwnd = frame.Word(words - 1), paint = frame.Pointer(words - 3);
    User::Paint info{};
    // The whole body, including the WM_ERASEBKGND callback of USER,
    // belongs to Application::BeginPaintWindow; this gate only narrows
    // PAINTSTRUCT to its sixteen-bit shape.
    const unsigned dc = BeginPaintWindow(hwnd, info);
    if (!dc && !User::IsWindow(hwnd))
    {
        result = 0;
        return true;
    }
    if (paint)
    {
        Win16Word(paint + Win16U::PaintDc, dc);
        Win16Word(paint + Win16U::PaintErase, info.erase);
        Win16Word(paint + Win16U::PaintRect + Win16U::RectLeft, (unsigned)info.rect.left);
        Win16Word(paint + Win16U::PaintRect + Win16U::RectTop, (unsigned)info.rect.top);
        Win16Word(paint + Win16U::PaintRect + Win16U::RectRight, (unsigned)info.rect.right);
        Win16Word(paint + Win16U::PaintRect + Win16U::RectBottom, (unsigned)info.rect.bottom);
        Win16Word(paint + Win16U::PaintRestore, 0);
        Win16Word(paint + Win16U::PaintIncremental, 0);
    }
    result = dc;
    return true;
}
bool Application::Win16EndPaint(const Win16Frame &frame, unsigned &result, unsigned &words)
{
    const unsigned hwnd = frame.Word(words - 1), paint = frame.Pointer(words - 3);
    User::Paint info{};
    if (paint)
    {
        info.dc = Win16Word(paint + Win16U::PaintDc);
        info.erase = Win16Word(paint + Win16U::PaintErase) != 0;
        info.rect.left = (short)Win16Word(paint + Win16U::PaintRect + Win16U::RectLeft);
        info.rect.top = (short)Win16Word(paint + Win16U::PaintRect + Win16U::RectTop);
        info.rect.right = (short)Win16Word(paint + Win16U::PaintRect + Win16U::RectRight);
        info.rect.bottom = (short)Win16Word(paint + Win16U::PaintRect + Win16U::RectBottom);
    }
    result = User::EndPaint(hwnd, info);
    return true;
}
bool Application::Win16RegisterClass(const Win16Frame &frame, unsigned &result, unsigned &words)
{
    const unsigned at = frame.Pointer(words - 2);
    result = at ? Win16RegisterClassAt(at, 0) : 0;
    return true;
}
// The WNDCLASS of the 16-bit side at `at`, and the small icon of the Ex
// form (USER.397), which a WNDCLASS has not.
unsigned Application::Win16RegisterClassAt(unsigned at, unsigned small_icon)
{
    unsigned result = 0;
    const unsigned procedure = Win16Word(at + Win16U::ClassProcedure) |
                               (Win16Word(at + Win16U::ClassProcedure + 2) << 16);
    const unsigned name_pointer = Win16Linear(Win16Word(at + Win16U::ClassName + 2),
                                              Win16Word(at + Win16U::ClassName));
    const unsigned menu_pointer = Win16Word(at + Win16U::ClassMenu + 2)
                                      ? Win16Linear(Win16Word(at + Win16U::ClassMenu + 2),
                                                    Win16Word(at + Win16U::ClassMenu))
                                      : 0;
    if (!name_pointer)
        return 0;
    char name[128];
    snprintf(name, sizeof name, "%s", ReadString(name_pointer));
    char menu[128] = {};
    if (menu_pointer)
        snprintf(menu, sizeof menu, "%s", ReadString(menu_pointer));
    // The field order is the one the native class store reads: the size of
    // the record, the style, the procedure, the two extra counts, the
    // instance, the icon, the cursor, the background, the menu, the name and
    // the small icon. A 16-bit WNDCLASS has no small icon.
    const unsigned fields[12] = {48,
                                 Win16Word(at + Win16U::ClassStyle),
                                 Win16Procedure(procedure),
                                 Win16Word(at + Win16U::ClassExtra),
                                 Win16Word(at + Win16U::ClassWindowExtra),
                                 Win16Word(at + Win16U::ClassInstance),
                                 Win16ImageNative(Win16Word(at + Win16U::ClassIcon)),
                                 Win16ImageNative(Win16Word(at + Win16U::ClassCursor)),
                                 Win16Word(at + Win16U::ClassBackground),
                                 menu_pointer ? 0u : Win16Word(at + Win16U::ClassMenu),
                                 0,
                                 small_icon};
    result = User::RegisterNativeClass(fields, name, menu_pointer && menu[0] ? menu : nullptr);
    if (log && trace_calls)
    {
        char trace[300];
        snprintf(trace, sizeof trace,
                 "WIN16 RegisterClass %s style=%04x proc=%08x clsextra=%u wndextra=%u instance=%04x "
                 "icon=%04x cursor=%04x brush=%04x menu=%04x/%s result=%u error=%u",
                 name, fields[1], fields[2], fields[3], fields[4], fields[5], fields[6], fields[7],
                 fields[8], fields[9], menu, result, result ? 0u : User::NativeClassError());
        log(trace);
    }
    if (!result)
        last_error = User::NativeClassError();
    return result;
}
bool Application::Win16CreateWindow(const Win16Frame &frame, unsigned &result, unsigned &words)
{
    return Win16CreateWindow(frame, result, words, false);
}
// CreateWindowEx (USER.452) is the same entry with a leading extended style of
// two words, so every argument behind it sits two words higher.
bool Application::Win16CreateWindow(const Win16Frame &frame, unsigned &result, unsigned &words,
                                    bool extended)
{
    const bool gate = TakeFresh();
    const unsigned shift = extended ? 2u : 0u;
    const unsigned extended_style = extended ? frame.Long(words - 2) : 0u;
    auto arg = [&](unsigned position) { return frame.Word(words - 1 - shift - position); };
    auto pointer = [&](unsigned position) { return frame.Pointer(words - 2 - shift - position); };
    // A class is either a far pointer to a name or an atom in the offset word
    // with a zero selector, which is how the 16-bit side writes an atom. The
    // selector is the deeper of the two words, so it is arg(0).
    const unsigned class_selector = arg(0), class_offset = arg(1);
    // Pass name and class as flat addresses while retaining their far
    // pointers in the ring for creation-message CREATESTRUCT conversion.
    const unsigned klass = class_selector ? Win16RememberFar((class_selector << 16) | class_offset)
                                          : class_offset;
    char name[128] = {};
    if (class_selector && klass)
        snprintf(name, sizeof name, "%s", ReadString(klass));
    const unsigned style = frame.Long(words - 6 - shift);
    const unsigned parameter = frame.Long(0);
    // CW_USEDEFAULT is a word in 16 bit, 8000h, and the same name is
    // 80000000h in 32 bit. Every other coordinate is a signed word.
    auto coordinate = [&](unsigned position) {
        const unsigned word = arg(position);
        return word == 0x8000 ? 0x80000000u : (unsigned)(int)(short)word;
    };
    unsigned parameter32 = parameter;
    // The MDI client reads its CLIENTCREATESTRUCT itself, and the native one
    // is two dwords where the 16-bit one is two words. Nothing else of the
    // creation parameter is read by anything but the program itself, so only
    // this class needs the wider shape.
    if (SameText(name, "mdiclient") && parameter)
    {
        const unsigned at = Win16Linear(parameter >> 16, parameter & 0xffff);
        const unsigned block = Allocate(8);
        if (!at || !block)
        {
            last_error = 8;
            result = 0;
            return true;
        }
        Write32(block, Win16Word(at));
        Write32(block + 4, Win16Word(at + 2));
        parameter32 = block;
    }
    // The ordinary creation path, the same one a 32-bit caller takes: the
    // class menu, WM_NCCREATE, WM_NCCALCSIZE, WM_CREATE, WM_PARENTNOTIFY, the
    // deferred WM_SIZE of an overlapped window and the show at the end.
    const unsigned arguments[12] = {extended_style,
                                    klass,
                                    Win16RememberFar((arg(2) << 16) | arg(3)),
                                    style,
                                    coordinate(6),
                                    coordinate(7),
                                    coordinate(8),
                                    coordinate(9),
                                    arg(10),
                                    arg(11),
                                    arg(12),
                                    parameter32};
    const unsigned limit = SameText(class_selector ? name : "", "edit") ? ((style & 4u) ? 65535u : 32767u) : 0;
    const bool kept = gate && PushHostFrame(HostWin16Create, limit);
    if (kept) GiveTail(TailRecord);
    const unsigned hwnd = CreateWindow(arguments);
    if (kept) PopHostFrame();
    // The text limit USER.EXE gives an edit control it just made: 32 KiB for a
    // single line control and 64 KiB for a multi line one. A 32-bit
    // caller gets the limit of USER32 instead, so the rule sits here and not in
    // the common creation path.
    if (hwnd && SameText(class_selector ? name : "", "edit"))
    {
        const bool multiline = (style & 0x0004) != 0; // ES_MULTILINE
        Win16Send(hwnd, 0x00c5 /* EM_LIMITTEXT */, multiline ? 65535u : 32767u, 0);
    }
    if (log && trace_calls)
    {
        char trace[300];
        snprintf(trace, sizeof trace,
                 "WIN16 CreateWindow class=%s title=%s style=%08x x=%d y=%d w=%d h=%d parent=%04x "
                 "menu=%04x instance=%04x hwnd=%u error=%u",
                 class_selector ? name : "#atom", pointer(2) ? ReadString(pointer(2)) : "", style,
                 (int)coordinate(6), (int)coordinate(7), (int)coordinate(8), (int)coordinate(9), arg(10),
                 arg(11), arg(12), hwnd, User::CreateWindowError());
        log(trace);
    }
    result = hwnd;
    return state != Failed;
}
// LoadString out of the string table of the NE module: sixteen counted strings
// per block, the block numbered by the identifier shifted right by four.
bool Application::Win16LoadString(const Win16Frame &frame, unsigned &result, unsigned &words)
{
    const unsigned instance = frame.Word(words - 1), id = frame.Word(words - 2);
    const unsigned buffer = frame.Pointer(words - 4), capacity = frame.Word(0);
    result = Win16LoadStringInto(instance, id, buffer, capacity);
    return true;
}
// USER.176 LoadString with its arguments at hand: the string `id` of the
// module behind `instance` into the flat `buffer` of `capacity` bytes.
unsigned Application::Win16LoadStringInto(unsigned instance, unsigned id, unsigned buffer, unsigned capacity)
{
    auto &machine = *win16.state;
    Win16Module *module = nullptr;
    for (unsigned i = 0; i < machine.module_count; ++i)
        if (!instance || machine.modules[i].instance == instance || machine.modules[i].handle == instance)
            module = &machine.modules[i];
    // The handle of a module of our side (GetModuleHandle("USER")) names the
    // file of the tree Win98 had loaded for it; its string table answers.
    const Win98Ne::Image *image = module ? nullptr : Win16SystemImage(instance);
    if (!module && !image)
        module = &machine.modules[0];
    if (module)
        image = &module->image;
    unsigned bytes = 0, offset = 0;
    if (!image->ResourceAt(Win16U::ResourceString, 0x8000 | Win16U::StringBlock(id), nullptr, offset, bytes))
        offset = 0;
    if (!offset || !buffer || !capacity)
    {
        if (buffer && capacity)
            PutString(buffer, "", capacity);
        return 0;
    }
    const unsigned char *data = image->Data() + offset;
    unsigned at = 0;
    for (unsigned i = 0; i < Win16U::StringIndex(id); ++i)
    {
        if (at >= bytes)
            break;
        at += 1u + data[at];
    }
    if (at >= bytes)
    {
        PutString(buffer, "", capacity);
        return 0;
    }
    unsigned length = data[at];
    if (length + 1 > capacity)
        length = capacity - 1;
    char text[512];
    if (length > sizeof text - 1)
        length = sizeof text - 1;
    memcpy(text, data + at + 1, length);
    text[length] = 0;
    return PutString(buffer, text, capacity);
}
// LoadAccelerators out of the NE table. A 16-bit accelerator record is five
// bytes, the flags, the key and the command; the native ACCEL is six bytes.
// Bit 7 of the flags is the last record, as the 16-bit loader reads it.
unsigned Application::Win16LoadAccelerators(unsigned instance, unsigned id, const char *label)
{
    auto &machine = *win16.state;
    Win16Module *module = nullptr;
    for (unsigned i = 0; i < machine.module_count; ++i)
        if (!instance || machine.modules[i].instance == instance || machine.modules[i].handle == instance)
            module = &machine.modules[i];
    if (!module)
        module = &machine.modules[0];
    unsigned bytes = 0;
    const unsigned offset = Win16Resource(*module, Win16U::ResourceAccelerator, id, label, bytes);
    if (!offset || bytes < 5)
        return 0;
    const unsigned char *data = module->image.Data() + offset;
    unsigned slot = 64;
    for (unsigned i = 0; i < 64; ++i)
    {
        const auto &table = machine.accelerators[i];
        if (table.native && table.module == module->handle && table.offset == offset)
            return i + 1;
        if (!table.native && slot == 64) slot = i;
    }
    if (slot == 64) { last_error = 8; return 0; }
    const unsigned count = bytes / 5;
    const unsigned block = Allocate(count * 6);
    if (!block)
    {
        last_error = 8;
        return 0;
    }
    unsigned written = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned flags = data[i * 5];
        const unsigned key = data[i * 5 + 1] | (data[i * 5 + 2] << 8);
        const unsigned command = data[i * 5 + 3] | (data[i * 5 + 4] << 8);
        unsigned short record[3] = {(unsigned short)(flags & 0x7f), (unsigned short)key,
                                    (unsigned short)command};
        Store(block + written * 6, record, sizeof record);
        ++written;
        if (flags & 0x80)
            break;
    }
    const unsigned a[] = {block, written};
    const unsigned table = Api("CreateAcceleratorTableA", a);
    Free(block);
    if (!table) return 0;
    machine.accelerators[slot] = {module->handle, offset, table};
    return slot + 1;
}
// GetClassInfo on the 16-bit side. The 16-bit WNDCLASS is twenty six bytes:
// the style word, a far procedure, the two extra counts, the instance, the
// icon, the cursor, the brush and two far pointers for the menu name and the
// class name. The native one widens every field to a dword and puts the style
// first, so the answer is written back field by field. The two names stay the
// pointers the caller passed, because a name of ours is not in the address
// space of the task.
bool Application::Win16GetClassInfo(const Win16Frame &frame, unsigned &result, unsigned &words)
{
    words = 5;
    const unsigned instance = frame.Word(4), out = frame.Pointer(0);
    const unsigned klass = frame.Word(3) ? frame.Pointer(2) : frame.Word(2);
    result = Win16ClassInfoInto(instance, klass, out);
    return state != Failed;
}
// The WNDCLASS of the 16-bit side written at `out`; GetClassInfoEx
// (USER.398) writes it behind its cbSize.
unsigned Application::Win16ClassInfoInto(unsigned instance, unsigned klass, unsigned out)
{
    const unsigned scratch = Win16Scratch();
    if (!scratch)
        return 0;
    const unsigned a[] = {instance, klass, scratch};
    const unsigned result = Api("GetClassInfoA", a);
    if (!result || !out)
        return result;
    Win16Word(out + 0, Read32(scratch));                                    // style
    const unsigned procedure = Read32(scratch + 4);
    const unsigned far_pointer = procedure ? Win16ProcedurePointer(procedure) : 0;
    Win16Word(out + 2, far_pointer & 0xffff);
    Win16Word(out + 4, far_pointer >> 16);
    Win16Word(out + 6, Read32(scratch + 8));                                // cbClsExtra
    Win16Word(out + 8, Read32(scratch + 12));                               // cbWndExtra
    Win16Word(out + 10, instance);
    Win16Word(out + 12, Read32(scratch + 20));                              // icon
    Win16Word(out + 14, Read32(scratch + 24));                              // cursor
    Win16Word(out + 16, Read32(scratch + 28));                              // brush
    return result;
}
// The module a resource call names. An instance of zero and an instance this
// run does not know both fall back to the program itself, the way KRNL386
// resolves a module handle with GetExePtr.
Application::Win16Module *Application::Win16ModuleOf(unsigned instance)
{
    auto &machine = *win16.state;
    Win16Module *module = nullptr;
    for (unsigned i = 0; i < machine.module_count; ++i)
        if (!instance || machine.modules[i].instance == instance || machine.modules[i].handle == instance)
            module = &machine.modules[i];
    return module ? module : &machine.modules[0];
}
// Whether an instance a window carries is a 16-bit module of this run: the
// pages of a 16-bit property sheet live in the native sheet of a 32-bit
// process (RUNDLL32 with SYSDM.CPL), and their controls load resources the
// way 16-bit USER does, out of the NE module.
bool Application::Win16OwnsInstance(unsigned instance)
{
    if (!win16.state || !instance || instance > 0xffff)
        return false;
    auto &machine = *win16.state;
    for (unsigned i = 0; i < machine.module_count; ++i)
        if (machine.modules[i].instance == instance || machine.modules[i].handle == instance)
            return true;
    return false;
}
// The menu template of a 16-bit module against the one of 32 bit. Header and
// item chain are the same: a version word, a header length, and behind it a
// flag word, an identifier word for a command item and the name. Only the name
// differs. The 16-bit template writes it as bytes, the 32-bit one as wide
// characters, and the reader of this port (`Application::ParseMenu`) reads wide
// characters, so the 16-bit resource has to be widened first. Without that
// every name came out as question marks and the reader ran into the bytes
// behind the template.
// The walk is the one KRNL386 and USER walk: MF_POPUP (0x0010) says the item
// carries no identifier and a nested chain follows it, MF_END (0x0080) closes
// the current chain. A field that points out of the resource ends the
// conversion; a template is not guessed.
bool Application::Win16WidenMenuTemplate(const unsigned char *data, unsigned length, unsigned &position,
                                         unsigned level, unsigned char *out, unsigned limit, unsigned &written)
{
    if (level > 8)
        return false;
    for (;;)
    {
        if (position > length || 2 > length - position)
            return false;
        const unsigned flags = data[position] | (data[position + 1] << 8);
        position += 2;
        if (written + 2 > limit)
            return false;
        out[written++] = (unsigned char)(flags & 0xff);
        out[written++] = (unsigned char)(flags >> 8);
        if (!(flags & 0x10))
        {
            if (position > length || 2 > length - position)
                return false;
            if (written + 2 > limit)
                return false;
            out[written++] = data[position];
            out[written++] = data[position + 1];
            position += 2;
        }
        for (;;)
        {
            if (position >= length)
                return false;
            const unsigned char ch = data[position++];
            if (written + 2 > limit)
                return false;
            out[written++] = ch;
            out[written++] = 0;
            if (!ch)
                break;
        }
        if ((flags & 0x10) && !Win16WidenMenuTemplate(data, length, position, level + 1, out, limit, written))
            return false;
        if (flags & 0x80)
            return true;
    }
}
// LoadMenu out of the NE resource table. The template is widened to the form
// the native reader takes and handed to LoadMenuIndirect.
unsigned Application::Win16LoadMenu(unsigned instance, unsigned id, const char *label)
{
    Win16Module &module = *Win16ModuleOf(instance);
    unsigned bytes = 0;
    const unsigned offset = Win16Resource(module, Win16U::ResourceMenu, id, label, bytes);
    if (!offset || bytes < 4)
    {
        last_error = 1812; // ERROR_RESOURCE_TYPE_NOT_FOUND
        return 0;
    }
    return Win16MenuFromTemplate(module.image.Data() + offset, bytes);
}
// A menu template of the 16-bit side, widened and handed to the native
// LoadMenuIndirect: the resource of LoadMenu and the block of
// LoadMenuIndirect (USER.220).
unsigned Application::Win16MenuFromTemplate(const unsigned char *source, unsigned bytes)
{
    if (bytes < 4)
        return 0;
    const unsigned version = source[0] | (source[1] << 8);
    if (version)
    {
        // A 16-bit module has no MENUEX resource; the version word of the
        // classic template is zero. Anything else is not this format.
        Fail("Unsupported 16-bit menu template version");
        return 0;
    }
    const unsigned header = source[2] | (source[3] << 8);
    if (header > bytes - 4)
    {
        Fail("Truncated 16-bit menu template");
        return 0;
    }
    // Every name grows from one byte to two, so twice the resource plus the
    // header is always enough.
    const unsigned limit = 4 + 2 * bytes;
    auto *wide = new unsigned char[limit];
    wide[0] = wide[1] = wide[2] = wide[3] = 0;
    unsigned written = 4, position = 4 + header;
    if (!Win16WidenMenuTemplate(source, bytes, position, 0, wide, limit, written))
    {
        delete[] wide;
        Fail("Invalid 16-bit menu template");
        return 0;
    }
    const unsigned block = Allocate(written);
    if (!block)
    {
        delete[] wide;
        last_error = 8;
        return 0;
    }
    Store(block, wide, written);
    delete[] wide;
    const unsigned a[] = {block};
    const unsigned menu = Api("LoadMenuIndirectA", a);
    Free(block);
    return menu;
}
// LoadBitmap out of the NE resource table. A bitmap resource is a device
// independent bitmap without the file header: the information header, the
// colour table and the bits behind it. The 16-bit form carries a
// BITMAPCOREHEADER of twelve bytes in an old module and the Windows header of
// forty in a newer one; the first dword says which, so nothing is guessed.
unsigned Application::Win16LoadBitmap(unsigned instance, unsigned id, const char *label, unsigned flags)
{
    Win16Module &module = *Win16ModuleOf(instance);
    unsigned bytes = 0;
    const unsigned offset = Win16Resource(module, Win16U::ResourceBitmap, id, label, bytes);
    if (!offset || bytes < 12)
    {
        last_error = 1812;
        return 0;
    }
    const unsigned char *data = module.image.Data() + offset;
    const unsigned header = (unsigned)data[0] | ((unsigned)data[1] << 8) | ((unsigned)data[2] << 16) |
                            ((unsigned)data[3] << 24);
    if (header != 12 && header != 40)
    {
        last_error = 1812;
        return 0;
    }
    unsigned depth = 0, colours = 0;
    if (header == 12)
        depth = (unsigned)data[10] | ((unsigned)data[11] << 8);
    else
    {
        depth = (unsigned)data[14] | ((unsigned)data[15] << 8);
        colours = (unsigned)data[32] | ((unsigned)data[33] << 8) | ((unsigned)data[34] << 16) |
                  ((unsigned)data[35] << 24);
    }
    if (!colours && depth <= 8)
        colours = 1u << depth;
    const unsigned entry = header == 12 ? 3u : 4u;
    const unsigned table = header + colours * entry;
    if (table >= bytes)
    {
        last_error = 1812;
        return 0;
    }
    const unsigned block = Allocate(bytes);
    if (!block)
    {
        last_error = 8;
        return 0;
    }
    Store(block, data, bytes);
    // LoadImage's colour mapping of a palette bitmap: LR_LOADMAP3DCOLORS
    // (1000h) turns the three greys into the 3D colours of the system, and
    // LR_LOADTRANSPARENT (20h) turns the colour of the first pixel into the
    // window colour, or into the face colour with LR_LOADMAP3DCOLORS. SYSDM
    // loads its computer picture this way, magenta around it.
    if ((flags & 0x1020) && depth <= 8 && colours)
    {
        auto sys = [&](unsigned index) {
            const unsigned a[] = {index};
            return Api("GetSysColor", a);
        };
        auto put = [&](unsigned slot, unsigned colour) {
            const unsigned char rgb[3] = {(unsigned char)(colour >> 16), (unsigned char)(colour >> 8),
                                          (unsigned char)colour};
            Store(block + header + slot * entry, rgb, 3);
        };
        const unsigned char *table_bytes = data + header;
        unsigned first = 0;
        if (flags & 0x20 && table + 3 <= bytes)
        {
            // The first pixel of the bits; a run-length bitmap (BI_RLE8 1,
            // BI_RLE4 2) starts with a count and the pixel, or with an escape
            // zero, a count and the pixels in absolute mode.
            const unsigned compression = header == 40 ? unsigned(data[16]) : 0u;
            unsigned char pixel = data[table];
            if (compression == 1 || compression == 2)
                pixel = data[table] ? data[table + 1] : data[table + 2];
            first = depth == 8 ? pixel : depth == 4 ? pixel >> 4 : depth == 1 ? pixel >> 7 : 0;
        }
        for (unsigned i = 0; i < colours; ++i)
        {
            const unsigned char *e = table_bytes + i * entry;
            const unsigned rgb = unsigned(e[2]) << 16 | unsigned(e[1]) << 8 | e[0];
            if ((flags & 0x20) && i == first)
                put(i, sys(flags & 0x1000 ? 15 : 5));
            else if (flags & 0x1000)
            {
                if (rgb == 0x808080)
                    put(i, sys(16));
                else if (rgb == 0xc0c0c0)
                    put(i, sys(15));
                else if (rgb == 0xdfdfdf)
                    put(i, sys(22));
            }
        }
    }
    // A screen context, because a resource bitmap is a device bitmap of the
    // display; CreateDIBitmap with CBM_INIT is the call GDI.EXE makes.
    const unsigned dc_args[] = {0};
    const unsigned dc = Api("GetDC", dc_args);
    const unsigned a[] = {dc, block, 4u /* CBM_INIT */, block + table, block, 0u /* DIB_RGB_COLORS */};
    const unsigned bitmap = Api("CreateDIBitmap", a);
    const unsigned release[] = {0, dc};
    Api("ReleaseDC", release);
    Free(block);
    return bitmap;
}
// The clipboard of the 16-bit side. A program hands over a global block of its
// own and gives up ownership, so the bytes are copied into a block of the
// native clipboard; the other direction copies them back into a 16-bit global
// block the task owns. That is not the arrangement of the original, where the
// same handle travels, but a 16-bit handle is a selector of this task and a
// native one is not. CF_BITMAP, CF_PALETTE and CF_DSPBITMAP carry a GDI
// handle instead of a block; a GDI handle is the same word on
// both sides, so it goes over as it is. GRINNIN.DLL of Schnauz hands each
// card to Visual Basic that way.
static bool Win16GdiClipboardFormat(unsigned format) { return format == 2 || format == 9 || format == 0x82; }
bool Application::Win16SetClipboardData(const Win16Frame &frame, unsigned &result, unsigned &words)
{
    words = 2;
    const unsigned format = frame.Word(1), handle = frame.Word(0);
    result = 0;
    // A null handle is delayed rendering: the entry is made, and the owner
    // gets WM_RENDERFORMAT when someone asks. The answer is the
    // handle, zero.
    if (Win16GdiClipboardFormat(format) || !handle)
    {
        const unsigned a[] = {format, handle};
        result = Api("SetClipboardData", a) ? handle : 0;
        return true;
    }
    auto *block = Win16GlobalBlock(handle);
    if (!block || !block->memory || !block->size)
        return true;
    const unsigned alloc[] = {0x0002 /* GMEM_MOVEABLE */, block->size};
    const unsigned native = Api("GlobalAlloc", alloc);
    if (!native)
        return true;
    const unsigned lock[] = {native};
    const unsigned at = Api("GlobalLock", lock);
    if (at)
    {
        unsigned char page[1024];
        for (unsigned done = 0; done < block->size; done += sizeof page)
        {
            const unsigned part = block->size - done < sizeof page ? block->size - done : sizeof page;
            Read(block->memory + done, page, part);
            Store(at + done, page, part);
        }
        const unsigned unlock[] = {native};
        Api("GlobalUnlock", unlock);
    }
    const unsigned a[] = {format, native};
    result = Api("SetClipboardData", a) ? handle : 0;
    return true;
}
bool Application::Win16GetClipboardData(const Win16Frame &frame, unsigned &result, unsigned &words)
{
    words = 1;
    const unsigned format = frame.Word(0);
    result = 0;
    const unsigned a[] = {format};
    const unsigned native = Api("GetClipboardData", a);
    if (!native)
        return true;
    if (Win16GdiClipboardFormat(format))
    {
        result = native & 0xffff;
        return true;
    }
    const unsigned size_args[] = {native};
    const unsigned size = Api("GlobalSize", size_args);
    if (!size)
        return true;
    const unsigned handle = Win16GlobalAlloc(0x0002 /* GMEM_MOVEABLE */, size);
    auto *block = Win16GlobalBlock(handle);
    if (!block || !block->memory)
        return true;
    const unsigned lock[] = {native};
    const unsigned at = Api("GlobalLock", lock);
    if (at)
    {
        unsigned char page[1024];
        for (unsigned done = 0; done < size; done += sizeof page)
        {
            const unsigned part = size - done < sizeof page ? size - done : sizeof page;
            Read(at + done, page, part);
            Store(block->memory + done, page, part);
        }
        const unsigned unlock[] = {native};
        Api("GlobalUnlock", unlock);
    }
    result = handle;
    return true;
}
// LoadIcon and LoadCursor. The 16-bit resource is the original icon or cursor
// directory of the module; the native side already rasters those shapes for
// the 32-bit entrance. USER's native handles carry a tag and generation above
// bit 15, so Win16 receives a stable WORD alias owned by this application.
unsigned Application::Win16ImageAlias(unsigned native, unsigned instance, bool is_cursor,
                                      unsigned id, const char *label)
{
    if (!native || !win16.state)
        return 0;
    auto &aliases = win16.state->image_aliases;
    for (unsigned i = 0; i < 256; ++i)
        if (aliases[i].native == native)
            return 0x4000 | ((aliases[i].generation & 63) << 8) | i;
    for (unsigned i = 0; i < 256; ++i)
        if (!aliases[i].native)
        {
            aliases[i].native = native;
            aliases[i].instance = instance;
            aliases[i].id = id;
            aliases[i].cursor = is_cursor;
            if (label)
                snprintf(aliases[i].label, sizeof aliases[i].label, "%s", label);
            return 0x4000 | ((aliases[i].generation & 63) << 8) | i;
        }
    return 0;
}
unsigned Application::Win16ImageNative(unsigned word) const
{
    if (!win16.state || (word & 0xc000) != 0x4000)
        return 0;
    const auto &alias = win16.state->image_aliases[word & 255];
    return alias.native && (alias.generation & 63) == ((word >> 8) & 63) ? alias.native : 0;
}
unsigned Application::Win16ImageWord(unsigned native) const
{
    if (!native || !win16.state)
        return 0;
    for (unsigned i = 0; i < 256; ++i)
        if (win16.state->image_aliases[i].native == native)
            return 0x4000 | ((win16.state->image_aliases[i].generation & 63) << 8) | i;
    return 0;
}
unsigned Application::Win16DestroyImage(unsigned word)
{
    const unsigned native = Win16ImageNative(word);
    if (!native)
        return 0;
    const unsigned a[] = {native};
    const unsigned result = Api("DestroyIcon", a);
    // LR_SHARED and system images survive DestroyIcon and keep their alias.
    // An owned image retires the alias with the native generation.
    if (!FindIcon(native))
    {
        auto &alias = win16.state->image_aliases[word & 255];
        const unsigned generation = alias.generation % 63 + 1;
        alias = {};
        alias.generation = generation;
    }
    return result;
}
unsigned Application::Win16LoadImage(unsigned instance, bool cursor, unsigned id, const char *label,
                                     unsigned cx, unsigned cy)
{
    auto &machine = *win16.state;
    // For 16-bit LoadImage, resolve a null or USER instance to the calling
    // application's resource module when the resource is named. USER's OEM
    // images use numeric identifiers. This lets applications register named
    // icons such as ICON_1 for their window classes.
    if (!instance && label && label[0] && machine.module_count)
        instance = machine.modules[0].instance;
    for (unsigned i = 0; i < 256; ++i)
    {
        const auto &alias = machine.image_aliases[i];
        if (!alias.native || alias.instance != instance || alias.cursor != cursor || alias.id != id)
            continue;
        if ((!label || !label[0]) ? !alias.label[0] : !strcmp(alias.label, label))
            return 0x4000 | ((alias.generation & 63) << 8) | i;
    }
    if (!instance)
    {
        // A system cursor or icon: the identifier is one of the standard ones
        // and the native provider already owns those shapes.
        const unsigned a[] = {0, id & 0x7fff};
        return Win16ImageAlias(Api(cursor ? "LoadCursorA" : "LoadIconA", a),
                               instance, cursor, id, label);
    }
    Win16Module *module = nullptr;
    for (unsigned i = 0; i < machine.module_count; ++i)
        if (machine.modules[i].instance == instance || machine.modules[i].handle == instance)
            module = &machine.modules[i];
    if (!module)
        module = &machine.modules[0];
    unsigned bytes = 0;
    const unsigned group = cursor ? Win16U::ResourceGroupCursor : Win16U::ResourceGroupIcon;
    const unsigned offset = Win16Resource(*module, group, id, label, bytes);
    if (!offset)
        return 0;
    // The group names the members; LoadIcon and LoadCursor give the first
    // one. LoadImage with a size takes the member of that size, the way
    // LookupIconIdFromDirectoryEx picks it: the smallest difference in width
    // and height, then the most colours.
    const unsigned char *data = module->image.Data() + offset;
    if (bytes < 20)
        return 0;
    unsigned member = data[18] | (data[19] << 8);
    if (cx || cy)
    {
        const unsigned count = data[4] | (data[5] << 8);
        unsigned best = ~0u, best_bits = 0;
        for (unsigned i = 0; i < count && 6 + i * 14 + 14 <= bytes; ++i)
        {
            const unsigned char *entry = data + 6 + i * 14;
            const unsigned width = cursor ? unsigned(entry[0] | entry[1] << 8) : (entry[0] ? entry[0] : 256u);
            const unsigned height = cursor ? unsigned(entry[2] | entry[3] << 8) / 2 : (entry[1] ? entry[1] : 256u);
            const unsigned bits = entry[6] | (entry[7] << 8);
            const unsigned want_x = cx ? cx : width, want_y = cy ? cy : height;
            const unsigned distance = (width > want_x ? width - want_x : want_x - width) +
                                      (height > want_y ? height - want_y : want_y - height);
            if (distance < best || (distance == best && bits > best_bits))
            {
                best = distance;
                best_bits = bits;
                member = entry[12] | (entry[13] << 8);
            }
        }
    }
    unsigned image_bytes = 0;
    const unsigned image = Win16Resource(*module, cursor ? Win16U::ResourceCursor : Win16U::ResourceIcon,
                                         0x8000 | member, nullptr, image_bytes);
    if (!image || !image_bytes)
        return 0;
    const unsigned block = Allocate(image_bytes);
    if (!block)
        return 0;
    Store(block, module->image.Data() + image, image_bytes);
    const unsigned a[] = {block, image_bytes, cursor ? 0u : 1u, 0x00030000, 0, 0, 0};
    const unsigned native = Api("CreateIconFromResourceEx", a);
    const unsigned word = Win16ImageAlias(native, instance, cursor, id, label);
    if (native && !word)
    {
        const unsigned destroy[] = {native};
        Api(cursor ? "DestroyCursor" : "DestroyIcon", destroy);
    }
    return word;
}

// -------------------------------------------------------------- the GDI gates

// GetObject on the 16-bit side. The four objects a program asks about have a
// narrower structure here: a BITMAP is fourteen bytes against twenty four, a
// LOGPEN ten against sixteen, a LOGBRUSH eight against twelve and a LOGFONT
// fifty against sixty. The count the caller passes names the structure, the way
// GDI.EXE names it, and a count that matches none of them answers zero rather
// than guessing a shape.
bool Application::Win16GetObject(const Win16Frame &frame, unsigned &result, unsigned &words)
{
    words = 4;
    const unsigned object = frame.Word(3), count = frame.Word(2), at = frame.Pointer(0);
    const unsigned scratch = Win16Scratch();
    if (!scratch)
        return false;
    result = 0;
    if (!object)
        return true;
    if (!at)
    {
        // GDI: a null output pointer is a size query whatever the count
        // says, and the answer is the width of the record. The gate answered
        // zero instead, so a program that asked how wide a record is got
        // nothing. The native entry names its own widths; the sixteen-bit ones
        // are BITMAP 14, LOGPEN 10, LOGBRUSH 8, LOGFONT 50 and a palette 2.
        const unsigned a[] = {object, 0, 0};
        const unsigned native = Api("GetObjectA", a);
        result = native == 24 ? 14u : native == 16 ? 10u : native == 12 ? 8u
                 : native == 60 ? 50u : native == 2 ? 2u : 0u;
        return true;
    }
    if (!count)
        return true;
    if (count == 2)
    {
        // A palette answers its entry count, and the record is one word on both
        // sides. Narrowing it to zero made every palette unreadable although
        // the native entry answers it.
        const unsigned a[] = {object, 2, scratch};
        result = Api("GetObjectA", a);
        if (result)
            Win16Word(at, Read32(scratch) & 0xffff);
        return true;
    }
    const unsigned wide = count == 14 ? 24u : count == 10 ? 16u : count == 8 ? 12u : count == 50 ? 60u : 0u;
    if (!wide)
    {
        last_error = 87;
        return true;
    }
    const unsigned a[] = {object, wide, scratch};
    if (!Api("GetObjectA", a))
        return true;
    switch (count)
    {
    case 14: // BITMAP16: four WORD, two BYTE and a far bits pointer.
        for (unsigned i = 0; i < 4; ++i)
            Win16Word(at + i * 2, Read32(scratch + i * 4));
        {
            const unsigned packed = Read32(scratch + 16);
            const unsigned char planes_and_depth[2] = {
                static_cast<unsigned char>(packed), static_cast<unsigned char>(packed >> 16)};
            Store(at + 8, planes_and_depth, sizeof planes_and_depth);
        }
        Win16Word(at + 10, 0); // bmBits: a 16-bit program cannot read a flat one
        Win16Word(at + 12, 0);
        break;
    case 10: // LOGPEN: style, the width point, the colour.
        Win16Word(at + 0, Read32(scratch));
        Win16Word(at + 2, Read32(scratch + 4));
        Win16Word(at + 4, Read32(scratch + 8));
        Win16Word(at + 6, Read32(scratch + 12) & 0xffff);
        Win16Word(at + 8, Read32(scratch + 12) >> 16);
        break;
    case 8: // LOGBRUSH: style, colour, hatch.
        Win16Word(at + 0, Read32(scratch));
        Win16Word(at + 2, Read32(scratch + 4) & 0xffff);
        Win16Word(at + 4, Read32(scratch + 4) >> 16);
        Win16Word(at + 6, Read32(scratch + 8));
        break;
    case 50: // LOGFONT: five words, eight bytes, the face.
    {
        for (unsigned i = 0; i < 5; ++i)
            Win16Word(at + i * 2, Read32(scratch + i * 4));
        unsigned char tail[8 + 32] = {};
        Read(scratch + 20, tail, sizeof tail);
        Store(at + 10, tail, sizeof tail);
        break;
    }
    default:
        break;
    }
    result = count;
    return true;
}
bool Application::Win16GdiCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words)
{
    // The callback GDI hands a printer driver's EnumDFonts, a
    // private gate of the GDI module (src/x86_print_draw.cpp).
    if (ordinal == PrintEnumOrdinal)
    {
        words = 7;
        result = PrintEnumCallback(frame);
        return true;
    }
    // GdiSeeGdiDo(wReqType, wParam1, wParam2, wParam3): the looks into GDI's
    // own local heap a diagnosis program takes (MSISYS.OCX of MSINFO32). This
    // GDI has no such heap; every request answers zero.
    if (ordinal == 452)
    {
        words = 4;
        result = 0;
        return true;
    }
    const char *name = nullptr;
    words = Win16GdiWords(ordinal, name);
    if (words == Win16U::Unported)
    {
        char text[240];
        snprintf(text, sizeof text,
                 "Win16 gate GDI.%u has no native provider yet: the second part of PLAN.md step 9 carries "
                 "the ordinals this port names",
                 ordinal);
        Fail(text);
        return false;
    }
    auto arg = [&](unsigned position) { return frame.Word(words - 1 - position); };
    auto number = [&](unsigned position) { return (unsigned)(int)(short)arg(position); };
    auto pointer = [&](unsigned position) { return frame.Pointer(words - 2 - position); };
    result = 0;
    // Metafiles have their own API provider in src/win16_metafile.cpp.
    if (Win16MetafileCall(ordinal, frame, result, words))
        return true;
    // Printer segments, src/win16_print.cpp.
    if (Win16PrintCall(ordinal, frame, result, words))
        return true;
    switch (ordinal)
    {
    case 121: // GDI validates the DC, then calls DISPLAY.Disable.
    case 122: // GDI calls DISPLAY.Enable with the three far pointers.
        if (!Gdi::DisplayLikeDC(arg(0))) { result = 0; return true; }
        // The native display keeps its backing while VGA owns scanout.
        // VGA.DRV Windows 98 returns FFFFh after disabling its device.
        User::SetExclusiveMode(ordinal == 121);
        if (ordinal == 122) EndVga();
        result = ordinal == 121 ? 0xffff : 1;
        return true;
    case 1: { const unsigned a[] = {arg(0), frame.Long(0)}; result = Api("SetBkColor", a); return true; }
    case 2: { const unsigned a[] = {arg(0), arg(1)}; result = Api("SetBkMode", a); return true; }
    case 9: { const unsigned a[] = {arg(0), frame.Long(0)}; result = Api("SetTextColor", a); return true; }
    case 19: { const unsigned a[] = {arg(0), number(1), number(2)}; result = Api("LineTo", a); return true; }
    case 20: // MoveTo answers the previous position in DX:AX.
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), number(1), number(2), scratch};
        Api("MoveToEx", a);
        result = ((Read32(scratch + 4) & 0xffff) << 16) | (Read32(scratch) & 0xffff);
        return true;
    }
    case 33: // TextOut(hdc, x, y, text, length)
    {
        const unsigned a[] = {arg(0), number(1), number(2), frame.Pointer(1), frame.Word(0)};
        result = Api("TextOutA", a);
        return true;
    }
    case 25: // FloodFill(hdc, x, y, colour)
    {
        const unsigned a[] = {arg(0), number(1), number(2), frame.Long(0)};
        result = Api("FloodFill", a);
        return true;
    }
    case 45: { const unsigned a[] = {arg(0), arg(1)}; result = Api("SelectObject", a); return true; }
    case 154: // GetNearestColor(hdc, colour)
    {
        const unsigned a[] = {arg(0), frame.Long(0)};
        result = Api("GetNearestColor", a);
        return true;
    }
    case 57: // CreateFontIndirect(logfont): 16-bit LOGFONT is 50 bytes, five
             // words, eight bytes and the 32 byte face; the native one widens
             // the five words to dwords.
    {
        // Win98FontCreate::Expand is the implementation of GDI and the
        // inverse of the narrowing CreateFontIndirectA itself uses; writing the
        // same expansion again here would be a second copy of it.
        const unsigned at = pointer(0), scratch = Win16Scratch();
        if (!scratch || !at) { result = 0; return true; }
        unsigned char narrow[50] = {}, wide[60] = {};
        Read(at, narrow, sizeof narrow);
        Win98FontCreate::Expand(narrow, wide);
        Store(scratch, wide, sizeof wide);
        const unsigned a[] = {scratch};
        result = Api("CreateFontIndirectA", a);
        return true;
    }
    case 56: // CreateFont: fifteen words, five signed and eight bytes, then the
             // face. GDI reads its first argument at bp+22h and its last
             // at bp+6, so the frame is thirty bytes; the table said fourteen
             // words, which read every number one word off and left the caller
             // two bytes of stack behind.
    {
        // The argument packing of CreateFontA matches the same
        // entry (Win98FontCreate::Construct), so the gate hands the fourteen
        // arguments over instead of building a LOGFONT of its own.
        const unsigned a[] = {number(0),   number(1),   number(2),   number(3),   number(4),
                              arg(5) & 255, arg(6) & 255, arg(7) & 255, arg(8) & 255,
                              arg(9) & 255, arg(10) & 255, arg(11) & 255, arg(12) & 255,
                              frame.Pointer(0)};
        result = Api("CreateFontA", a);
        return true;
    }
    case 66: { const unsigned a[] = {frame.Long(0)}; result = Api("CreateSolidBrush", a); return true; }
    case 58: // CreateHatchBrush(style, color)
    {
        const unsigned a[] = {(unsigned)(int)(short)arg(0), frame.Long(0)};
        result = Api("CreateHatchBrush", a);
        return true;
    }
    case 79: // GetDCOrg(hdc): DX:AX is y:x.
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        const unsigned a[] = {arg(0), scratch};
        result = Api("GetDCOrgEx", a) ? (Read32(scratch) & 0xffff) | (Read32(scratch + 4) << 16) : 0;
        return true;
    }
    case 403: // GDI (named GDIINIT2): reads or sets the word at +0Eh
              // of a GDI object; zero reads it, FFFFh clears it, anything
              // else stores it, and the answer is the word. MSVIDEO's DrawDib
              // asks a bitmap for it to reach the global block
              // GDI32 keeps beside a DIB section. No code of this port stores
              // one, so a bitmap answers zero unless a caller stored a value.
    {
        const unsigned handle = arg(0), value = arg(1);
        unsigned *slot = nullptr;
        for (auto &entry : gdi_object_words)
            if (entry.handle == handle)
                slot = &entry.value;
        if (!slot && value && value != 0xffff)
            for (auto &entry : gdi_object_words)
                if (!entry.handle)
                {
                    entry.handle = handle;
                    slot = &entry.value;
                    break;
                }
        if (value == 0xffff && slot)
            *slot = 0;
        else if (value && slot)
            *slot = value;
        result = slot ? *slot : 0;
        return true;
    }
    case 461: // SetObjectOwner(object, owner), GDI: exchanges the owner
              // word at object+0Ch and answers the previous one; handle validation
              // and the stock-object rule live in Gdi::SetObjectOwner.
              // MSVIDEO hands the DCI provider DC to owner zero.
              // The 16-bit owner word goes in unchanged; zero, which the
              // original never matches with a dying task, is read by
              // FreeGdi as an object of this process.
        result = Gdi::SetObjectOwner(arg(0), arg(1)) & 0xffff;
        return true;
    case 462: // IsGDIObject(handle): the 16-bit object type, zero for none.
    {
        // GDI.EXE numbers its types pen 1, brush 2, font 3, palette 4, bitmap
        // 5, region 6, DC 7, metafile DC 9, metafile 10; GetObjectType of the
        // 32-bit side numbers the same objects its own way.
        static const unsigned char to16[] = {0, 1, 2, 7, 9, 4, 3, 5, 6, 10, 7, 1};
        const unsigned a[] = {arg(0)};
        const unsigned type = Api("GetObjectType", a);
        result = type < sizeof to16 ? to16[type] : 0;
        return true;
    }
    case 69:
    {
        const unsigned a[] = {arg(0)};
        result = Api("DeleteObject", a);
        // The selector GDI.489 made over the bits of a DIB section goes
        // with the bitmap.
        if (result)
            for (auto &section : win16_dib_sections)
                if (section.handle && section.handle == arg(0))
                {
                    selectors.Unmap(section.selector << 16);
                    section = {};
                }
        return true;
    }
    case 489: // CreateDIBSection(hdc, bmi, usage, bits, section, offset)
    {
        // GDI answers zero for a file mapping at once; makes
        // the bitmap and hands the bits out as a far pointer whose selector
        // spans all of them, which the USE32 loops of MSVIDEO segment 7 walk
        // with 32-bit offsets. The bitmap itself is the one of GDI32 here;
        // the selector lies over its flat bits.
        const unsigned bits_far = frame.Long(4), info = pointer(1);
        if (frame.Long(2) || !info)
        {
            result = 0;
            return true;
        }
        Win16DibSection *slot = nullptr;
        for (auto &section : win16_dib_sections)
            if (!section.handle)
            {
                slot = &section;
                break;
            }
        const unsigned scratch = Win16Scratch();
        if (!slot || !scratch)
        {
            Fail("GDI.489 CreateDIBSection: no room for the selector of another DIB section");
            return false;
        }
        Write32(scratch, 0);
        const unsigned a[] = {arg(0), info, arg(3), scratch, 0, 0};
        result = GdiApi("CreateDIBSection", a) & 0xffff;
        const unsigned bits = Read32(scratch);
        if (!result || !bits)
        {
            result = 0;
            return true;
        }
        // The size of the bits: rows of whole dwords, or biSizeImage for a
        // compressed format.
        const int width = int(Read32(info + 4)), height = int(Read32(info + 8));
        const unsigned count = Read32(info + 14) & 0xffff, compression = Read32(info + 16);
        const unsigned stride = ((unsigned(width < 0 ? -width : width) * count + 31) / 32) * 4;
        unsigned size = stride * unsigned(height < 0 ? -height : height);
        if (compression != 0 && compression != 3)
            size = Read32(info + 20);
        const unsigned selector = Win16Segment(bits, size ? size - 1 : 0, false, false);
        if (!selector)
        {
            const unsigned d[] = {result};
            Api("DeleteObject", d);
            result = 0;
            return true;
        }
        slot->handle = result;
        slot->selector = selector;
        if (bits_far >> 16)
            Write32(selectors.Linear(bits_far), selector << 16);
        return true;
    }
    case 80: // GetDeviceCaps(hdc, index)
    {
        // GDI.EXE answers a 16-bit caller from the GDIINFO of the
        // device, with two changes of its own: RASTERCAPS (26h) always has
        // RC_GDI20_STATE (20h), and on a device of more than eight bits per
        // pixel ([GDIINFO+0Ch] above 8) NUMBRUSHES, NUMPENS and NUMCOLORS
        // (10h, 12h, 18h) answer 800h instead of the -1 of the driver. The
        // Entertainment Pack's WEP4UTIL.DLL takes NUMCOLORS 2 or less as a
        // monochrome screen, so Chip's Challenge drew its board in black and
        // white on the -1. An index above 6Ch answers zero.
        const unsigned index = arg(1);
        if (index > 0x6c)
        {
            result = 0;
            return true;
        }
        const unsigned a[] = {arg(0), index};
        result = Api("GetDeviceCaps", a);
        if (index == 0x26)
            result |= 0x20;
        else if (index == 0x10 || index == 0x12 || index == 0x18)
        {
            const unsigned bits[] = {arg(0), 12};
            if (Api("GetDeviceCaps", bits) > 8)
                result = 0x800;
        }
        return true;
    }
    case 87: { const unsigned a[] = {arg(0)}; result = Api("GetStockObject", a); return true; }
    case 91: // GetTextExtent(hdc, text, length): DX:AX is height:width.
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), pointer(1), frame.Word(0), scratch};
        Api("GetTextExtentPoint32A", a);
        result = ((Read32(scratch + 4) & 0xffff) << 16) | (Read32(scratch) & 0xffff);
        return true;
    }
    case 93: // GetTextMetrics(hdc, metrics): the 16-bit TEXTMETRIC is words.
    {
        const unsigned at = pointer(1), scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), scratch};
        result = Api("GetTextMetricsA", a);
        if (!result || !at)
            return true;
        for (unsigned i = 0; i < 11; ++i)
            Win16Word(at + i * 2, Read32(scratch + i * 4));
        unsigned char tail[9] = {};
        Read(scratch + 44, tail, sizeof tail);
        Store(at + Win16U::TextMetricFirstChar, tail, sizeof tail);
        return true;
    }
    // ------------------------------------------------- the breadth of part four
    case 3: { const unsigned a[] = {arg(0), number(1)}; result = Api("SetMapMode", a); return true; }
    case 4: { const unsigned a[] = {arg(0), number(1)}; result = Api("SetROP2", a); return true; }
    case 7: { const unsigned a[] = {arg(0), number(1)}; result = Api("SetStretchBltMode", a); return true; }
    case 148: // SetBrushOrg also returns the previous signed pair.
    case 11: // SetWindowOrg, SetWindowExt, SetViewportOrg and SetViewportExt
    case 12: // all answer the previous pair in DX:AX, so they go through the
    case 13: // Ex form with a scratch POINT or SIZE.
    case 14:
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const char *entry = ordinal == 148 ? "SetBrushOrgEx"
                            : ordinal == 11   ? "SetWindowOrgEx"
                            : ordinal == 12 ? "SetWindowExtEx"
                            : ordinal == 13 ? "SetViewportOrgEx"
                                            : "SetViewportExtEx";
        const unsigned a[] = {arg(0), number(1), number(2), scratch};
        if (!Api(entry, a))
        {
            result = 0;
            return true;
        }
        result = ((Read32(scratch + 4) & 0xffff) << 16) | (Read32(scratch) & 0xffff);
        return true;
    }
    case 24: // GDI Ellipse uses the same five WORD arguments.
    case 27:
    {
        const unsigned a[] = {arg(0), number(1), number(2), number(3), number(4)};
        result = Api(ordinal == 24 ? "Ellipse" : "Rectangle", a);
        return true;
    }
    // GDI Pie and Chord: the DC and eight signed WORD
    // coordinates, which GDI32 Pie and Chord take as they are.
    case 26:
    case 348:
    {
        const unsigned a[] = {arg(0),    number(1), number(2), number(3), number(4),
                              number(5), number(6), number(7), number(8)};
        result = Api(ordinal == 26 ? "Pie" : "Chord", a);
        return true;
    }
    case 29:
    {
        const unsigned a[] = {arg(0), number(1), number(2), number(3), number(4), frame.Long(0)};
        result = Api("PatBlt", a);
        return true;
    }
    case 30: { const unsigned a[] = {arg(0)}; result = Api("SaveDC", a); return true; }
    case 31:
    {
        const unsigned a[] = {arg(0), number(1), number(2), frame.Long(0)};
        result = Api("SetPixel", a);
        return true;
    }
    case 34:
    {
        const unsigned a[] = {arg(0),    number(1), number(2), number(3),
                              number(4), arg(5),    number(6), number(7), frame.Long(0)};
        result = Api("BitBlt", a);
        return true;
    }
    case 35:
    {
        const unsigned a[] = {arg(0),    number(1), number(2), number(3),  number(4), arg(5),
                              number(6), number(7), number(8), number(9), frame.Long(0)};
        result = Api("StretchBlt", a);
        return true;
    }
    case 38: // Escape(hdc, escape, count, in, out)
    {
        // GDI reads the context at bp+12h, the number at bp+10h, the
        // count at bp+0Eh and the two far pointers below them. The answers are
        // not all zero: 0FFFFh for a context GDI rejects and for a refused
        // document number, the marker 5250h for a number QUERYESCSUPPORT finds,
        // and the DIB answer of QUERYDIBSUPPORT. All of that is the body of the
        // 32-bit entry; answering zero here told a program that probes
        // QUERYESCSUPPORT that nothing at all is supported.
        // SETABORTPROC (9) hands the procedure itself as the input; it is
        // a far pointer to 16-bit code, not a buffer.
        const unsigned input = arg(1) == 9 ? (frame.Long(2) ? Win16Mark(frame.Long(2)) : 0) : pointer(3);
        const unsigned a[] = {arg(0), arg(1), number(2), input, pointer(5)};
        result = Api("Escape", a);
        return true;
    }
    case 39: { const unsigned a[] = {arg(0), number(1)}; result = Api("RestoreDC", a); return true; }
    case 44: { const unsigned a[] = {arg(0), arg(1)}; result = Api("SelectClipRgn", a); return true; }
    case 47:
    {
        const unsigned a[] = {arg(0), arg(1), arg(2), arg(3)};
        result = Api("CombineRgn", a);
        return true;
    }
    case 48: // CreateBitmap(width, height, planes, bits, data)
    {
        const unsigned a[] = {number(0), number(1), arg(2) & 0xff, arg(3) & 0xff, frame.Pointer(0)};
        result = Api("CreateBitmap", a);
        return true;
    }
    case 50: // LOGBRUSH16: WORD style, COLORREF, WORD hatch.
    {
        const unsigned at = pointer(0), scratch = Win16Scratch();
        if (!at || !scratch) { result = 0; return true; }
        Write32(scratch, Win16Word(at));
        Write32(scratch + 4, Read32(at + 2));
        Write32(scratch + 8, Win16Word(at + 6));
        const unsigned a[] = {scratch};
        result = Api("CreateBrushIndirect", a);
        return true;
    }
    case 51:
    {
        const unsigned a[] = {arg(0), number(1), number(2)};
        result = Api("CreateCompatibleBitmap", a);
        return true;
    }
    case 52: { const unsigned a[] = {arg(0)}; result = Api("CreateCompatibleDC", a); return true; }
    case 53: // CreateDC(driver, device, output, initialisation)
    {
        const unsigned a[] = {pointer(0), pointer(2), pointer(4), frame.Pointer(0)};
        result = Api("CreateDCA", a);
        return true;
    }
    case 60: { const unsigned a[] = {arg(0)}; result = Api("CreatePatternBrush", a); return true; }
    case 61:
    {
        const unsigned a[] = {number(0), number(1), frame.Long(0)};
        result = Api("CreatePen", a);
        return true;
    }
    case 62: // GDI: LOGPEN16 contains a WORD, POINT16 and COLORREF.
    {
        const unsigned at = pointer(0);
        if (!at) { result = 0; return true; }
        const unsigned a[] = {Win16Word(at), (unsigned)(int)(short)Win16Word(at + 2), Read32(at + 6)};
        result = Api("CreatePen", a);
        return true;
    }
    case 64:
    {
        const unsigned a[] = {number(0), number(1), number(2), number(3)};
        result = Api("CreateRectRgn", a);
        return true;
    }
    case 65: // CreateRectRgnIndirect: the Win16 RECT is four signed words.
    {
        const unsigned at = pointer(0), scratch = Win16Scratch();
        if (!at || !scratch) { result = 0; return true; }
        for (unsigned i = 0; i < 4; ++i)
            Write32(scratch + i * 4, (unsigned)(int)(short)Win16Word(at + i * 2));
        const unsigned a[] = {scratch};
        result = Api("CreateRectRgnIndirect", a);
        return true;
    }
    case 99: // LPtoDP uses the same packed POINT array.
    case 67: // DPtoLP(hdc, points, count): a 16-bit POINT is two words.
    {
        const unsigned at = pointer(1), count = frame.Word(0), scratch = Win16Scratch();
        if (!scratch || !at || count > 256) { result = 0; return true; }
        for (unsigned i = 0; i < count; ++i)
        {
            Write32(scratch + i * 8, (unsigned)(int)(short)Win16Word(at + i * 4));
            Write32(scratch + i * 8 + 4, (unsigned)(int)(short)Win16Word(at + i * 4 + 2));
        }
        const unsigned a[] = {arg(0), scratch, count};
        result = Api(ordinal == 99 ? "LPtoDP" : "DPtoLP", a);
        for (unsigned i = 0; result && i < count; ++i)
        {
            Win16Word(at + i * 4, Read32(scratch + i * 8));
            Win16Word(at + i * 4 + 2, Read32(scratch + i * 8 + 4));
        }
        return true;
    }
    case 68: { const unsigned a[] = {arg(0)}; result = Api("DeleteDC", a); return true; }
    case 70: // EnumFonts(hdc, face, callback, parameter), GDI.
    case 330: // EnumFontFamilies(hdc, family, callback, parameter), GDI.
    {
        // Both entries share one body with the same frame of seven words. The gate hands them to the shared enumeration of
        // src/x86_font_query.cpp with the callback as a mark; its callback
        // provider recognises the mark and enters Win16FontCallback.
        const unsigned callback = frame.Long(2);
        if (!callback) { result = 0; return true; }
        const unsigned mark = Win16Mark(callback);
        if (!mark) { Fail("Win16 font enumeration has no procedure mark left"); return false; }
        const unsigned a[] = {arg(0), frame.Pointer(4), mark, frame.Long(0)};
        result = Api(ordinal == 70 ? "EnumFontsA" : "EnumFontFamiliesA", a) & 0xffff;
        return true;
    }
    case 74: // GetBitmapBits(bitmap, count, bits)
    {
        // The five words are the bitmap, the LONG count and the far pointer.
        // The count therefore begins at word two; word one is the selector of
        // the pointer, and reading the long from there handed the native entry
        // a byte count of several million.
        const unsigned a[] = {arg(0), frame.Long(2), frame.Pointer(0)};
        result = Api("GetBitmapBits", a);
        return true;
    }
    case 77: // GetClipBox(hdc, rect)
    {
        const unsigned at = pointer(1), scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), scratch};
        result = Api("GetClipBox", a);
        if (at)
            for (unsigned i = 0; i < 4; ++i)
                Win16Word(at + i * 2, Read32(scratch + i * 4));
        return true;
    }
    case 81: { const unsigned a[] = {arg(0)}; result = Api("GetMapMode", a); return true; }
    case 82: return Win16GetObject(frame, result, words);
    case 83:
    {
        const unsigned a[] = {arg(0), number(1), number(2)};
        result = Api("GetPixel", a);
        return true;
    }
    case 104: // RectVisible(hdc, rect)
    {
        const unsigned at = pointer(1), scratch = Win16Scratch();
        if (!scratch || !at) { result = 0; return true; }
        for (unsigned i = 0; i < 4; ++i)
            Write32(scratch + i * 4, (unsigned)(int)(short)Win16Word(at + i * 2));
        const unsigned a[] = {arg(0), scratch};
        result = Api("RectVisible", a);
        return true;
    }
    case 106: // SetBitmapBits(bitmap, count, bits): the count begins at word two
    {
        const unsigned a[] = {arg(0), frame.Long(2), frame.Pointer(0)};
        result = Api("SetBitmapBits", a);
        return true;
    }
    case 128:
    {
        const unsigned a[] = {number(0), number(1), number(2)};
        result = Api("MulDiv", a);
        return true;
    }
    case 346: // SetTextAlign(hdc, align), GDI, `retf 4`.
    {
        const unsigned a[] = {arg(0), arg(1)};
        result = Api("SetTextAlign", a);
        return true;
    }
    case 351: // ExtTextOut(hdc, x, y, options, rect, text, count, spacing)
    {
        // Pascal words from the top: spacing[0:1], count[2], text[3:4],
        // rectangle[5:6], options[7], y[8], x[9], hdc[10].
        const unsigned rect = frame.Pointer(5), spacing = frame.Pointer(0);
        unsigned wide_rect = 0, wide_spacing = 0;
        const unsigned count = frame.Word(2);
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        if (rect)
        {
            wide_rect = scratch;
            for (unsigned i = 0; i < 4; ++i)
                Write32(wide_rect + i * 4, (unsigned)(int)(short)Win16Word(rect + i * 2));
        }
        // Up to 512 entries fit behind the rectangle in the scratch block; a
        // longer text gets a block of its own, so GDI always draws with the
        // spacing it was given.
        unsigned own_block = 0;
        if (spacing && count)
        {
            if (count <= 512)
                wide_spacing = scratch + 32;
            else if (!(wide_spacing = own_block = Allocate(count * 4)))
            {
                result = 0;
                return true;
            }
            for (unsigned i = 0; i < count; ++i)
                Write32(wide_spacing + i * 4, (unsigned)(int)(short)Win16Word(spacing + i * 2));
        }
        const unsigned a[] = {arg(0),    number(1),      number(2), arg(3),
                              wide_rect, frame.Pointer(3), count,   wide_spacing};
        result = Api("ExtTextOutA", a);
        if (own_block)
            Free(own_block);
        return true;
    }
    case 442: // CreateDIBitmap(hdc, header, init, bits, info, usage): every
              // structure of this entry has the same bytes in 16 and 32 bit.
    {
        const unsigned a[] = {arg(0),           frame.Pointer(7), frame.Long(5),
                              frame.Pointer(3), frame.Pointer(1), frame.Word(0)};
        result = Api("CreateDIBitmap", a);
        return true;
    }
    case 119: // AddFontResource(file): the shared body reads the name
              // and installs the file into the font list.
    {
        const unsigned a[] = {pointer(0)};
        result = Api("AddFontResourceA", a);
        return true;
    }
    case 153: // CreateIC(driver, device, output, initialisation)
    {
        const unsigned a[] = {pointer(0), pointer(2), pointer(4), frame.Pointer(0)};
        result = Api("CreateICA", a);
        return true;
    }
    case 440: // SetDIBits(hdc, hbm, start, lines, bits, info, usage)
    case 441: // GetDIBits(hdc, hbm, start, lines, bits, info, usage): GDI.EXE
              // checks the handles and the usage word and hands the call to
              // the 32-bit GDI; BITMAPINFO has the same bytes in 16 and 32
              // bit, and the bits may be a huge block of several selectors.
    {
        const unsigned a[] = {arg(0), arg(1), arg(2), arg(3), pointer(4), pointer(6), arg(8)};
        result = Api(ordinal == 441 ? "GetDIBits" : "SetDIBits", a);
        return true;
    }
    case 439: // StretchDIBits: eight signed coordinates, two FAR pointers, usage and ROP.
    {
        const unsigned a[] = {arg(0), number(1), number(2), number(3), number(4),
                              number(5), number(6), number(7), number(8),
                              pointer(9), pointer(11), frame.Word(2), frame.Long(0)};
        result = Api("StretchDIBits", a);
        return true;
    }
    case 443: // SetDIBitsToDevice(hdc, xd, yd, w, h, xs, ys, start, scans,
              // bits, info and usage complete the fourteen-word argument
              // list. Coordinates are signed words; BITMAPINFO has the same
              // layout on both sides.
    {
        const unsigned a[] = {arg(0), number(1), number(2), number(3), number(4),
                              number(5), number(6), arg(7), arg(8),
                              pointer(9), pointer(11), frame.Word(0)};
        result = Api("SetDIBitsToDevice", a);
        return true;
    }
    case 94: // GetViewportExt(hdc)
    case 95: // GetViewportOrg(hdc): return x in AX and y in DX.
    case 96: // GetWindowExt(hdc)
    case 97: // GetWindowOrg(hdc). All four return a coordinate pair.
             // Return the DC coordinate pair in DX:AX.
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        const char *const name = ordinal == 94   ? "GetViewportExtEx"
                                 : ordinal == 95 ? "GetViewportOrgEx"
                                 : ordinal == 96 ? "GetWindowExtEx"
                                                 : "GetWindowOrgEx";
        const unsigned a[] = {arg(0), scratch};
        if (!Api(name, a))
            return true;
        int point[2] = {};
        Read(scratch, point, sizeof point);
        result = (unsigned)(unsigned short)point[0] | ((unsigned)(unsigned short)point[1] << 16);
        return true;
    }
    case 15: // OffsetWindowOrg(hdc, x, y): the previous origin in DX:AX.
    case 17: // OffsetViewportOrg(hdc, x, y)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        const unsigned a[] = {arg(0), number(1), number(2), scratch};
        if (!Api(ordinal == 15 ? "OffsetWindowOrgEx" : "OffsetViewportOrgEx", a))
            return true;
        int point[2] = {};
        Read(scratch, point, sizeof point);
        result = (unsigned)(unsigned short)point[0] | ((unsigned)(unsigned short)point[1] << 16);
        return true;
    }
    case 21: // ExcludeClipRect(hdc, left, top, right, bottom)
    case 22: // IntersectClipRect(hdc, left, top, right, bottom)
    {
        const unsigned a[] = {arg(0), number(1), number(2), number(3), number(4)};
        result = Api(ordinal == 21 ? "ExcludeClipRect" : "IntersectClipRect", a);
        return true;
    }
    case 75: // GetBkColor(hdc): a COLORREF, so the answer fills DX:AX.
    case 76: // GetBkMode(hdc)
    case 85: // GetROP2(hdc)
    case 90: // GetTextColor(hdc)
    {
        const unsigned a[] = {arg(0)};
        result = Api(ordinal == 75   ? "GetBkColor"
                     : ordinal == 76 ? "GetBkMode"
                     : ordinal == 85 ? "GetROP2"
                                     : "GetTextColor",
                     a);
        return true;
    }
    case 92: // GetTextFace(hdc, count, buffer)
    {
        const unsigned a[] = {arg(0), arg(1), pointer(2)};
        result = Api("GetTextFaceA", a);
        return true;
    }
    case 172: // SetRectRgn(region, left, top, right, bottom)
    {
        const unsigned a[] = {arg(0), number(1), number(2), number(3), number(4)};
        result = Api("SetRectRgn", a);
        return true;
    }
    case 161: // PtInRegion(region, x, y)
    {
        const unsigned a[] = {arg(0), number(1), number(2)};
        result = Api("PtInRegion", a);
        return true;
    }
    case 360: // CreatePalette(logpalette): the 16-bit LOGPALETTE has the same
              // shape as the 32-bit one, two words and PALETTEENTRY quadruples.
    {
        const unsigned a[] = {pointer(0)};
        result = Api("CreatePalette", a);
        return true;
    }
    case 150: // UnrealizeObject(object): one WORD argument.
    {
        const unsigned a[] = {arg(0)};
        result = Api("UnrealizeObject", a);
        return true;
    }
    case 375: // GetSystemPaletteEntries(dc, start, count, entries).
              // Return the device system palette as PALETTEENTRY records.
              // WLOADER.EXE reads it after checking color support.
    {
        const unsigned a[] = {arg(0), arg(1), arg(2), pointer(3)};
        result = Api("GetSystemPaletteEntries", a);
        return true;
    }
    case 373: // SetSystemPaletteUse(dc, use)
    {
        const unsigned a[] = {arg(0), arg(1)};
        result = Api("SetSystemPaletteUse", a);
        return true;
    }
    case 374: // GetSystemPaletteUse(dc)
    case 366: // UpdateColors(dc)
    {
        const unsigned a[] = {arg(0)};
        result = Api(ordinal == 374 ? "GetSystemPaletteUse" : "UpdateColors", a);
        return true;
    }
    case 367: // AnimatePalette(palette, start, count, entries): no Win16 result;
              // PALETTEENTRY has the same layout as ordinal 363.
    {
        const unsigned a[] = {arg(0), arg(1), arg(2), pointer(3)};
        Api("AnimatePalette", a);
        result = 0;
        return true;
    }
    case 368: // ResizePalette(palette, entries)
    {
        const unsigned a[] = {arg(0), arg(1)};
        result = Api("ResizePalette", a);
        return true;
    }
    case 370: // GetNearestPaletteIndex(palette, colour): DWORD color.
    {
        const unsigned a[] = {arg(0), arg(1) << 16 | arg(2)};
        result = Api("GetNearestPaletteIndex", a);
        return true;
    }
    case 364: // SetPaletteEntries(palette, start, count, entries)
    {
        const unsigned a[] = {arg(0), arg(1), arg(2), pointer(3)};
        result = Api("SetPaletteEntries", a);
        return true;
    }
    case 363: // GetPaletteEntries(palette, start, count, entries).
              // PALETTEENTRY is four bytes on both sides, so copy the block
              // unchanged.
    {
        const unsigned a[] = {arg(0), arg(1), arg(2), pointer(3)};
        result = Api("GetPaletteEntries", a);
        return true;
    }
    default:
    {
        // The ordinals of the pass over the whole export table,
        // src/win16_gdi_more.inc.
        bool handled = false;
        const bool answered = Win16GdiMore(ordinal, frame, words, result, handled);
        if (handled)
            return answered && state != Failed;
        break;
    }
    }
    char text[200];
    snprintf(text, sizeof text, "Win16 gate GDI.%u (%s) is named but not wired", ordinal, name ? name : "?");
    Fail(text);
    return false;
}

// ----------------------------------------------------------- the module entry

// The initialization call of a 16-bit DLL the program ships with. The contract
// is the one KRNL386 uses: DS is the automatic data segment of the library, CX
// the size of its local heap, DI the instance and SI the command line, and the
// answer in AX decides whether the library stays. LibMain runs as the original
// code it is, so the entry has to be entered like any other far procedure.
// the exit procedure of a library, WEP by name. Only a library of
// version 3.00 or later with segments and a loaded data segment has one,
// and only once (bit 4000h of the module flags, which the initialization
// set). WEP(nExitType) is Pascal with one word, and DelModule passes 0,
// WEP_FREE_DLL; the call runs with DS, ES and SS of the caller.
bool Application::Win16CallExit(Win16Module &module)
{
    if (!module.entered || module.image.ExpectedVersion() < 0x300 || !module.segments ||
        (module.image.AutoDataSegment() && !module.instance))
        return true;
    module.entered = false;
    Win98Ne::Entry entry;
    const unsigned number = Win16Ordinal(module, "WEP");
    if (!number || !module.image.EntryAt(number, entry) || !module.selector[entry.segment])
        return true;
    const unsigned far_pointer = module.selector[entry.segment] << 16 | entry.offset;
    CpuContext *context = nullptr;
    if (!Check(cpu->NewContext(context)))
        return false;
    Check(cpu->Save(context));
    const unsigned stack = Register(Reg::Ss);
    unsigned sp = (Register(Reg::Esp) & 0xffff) & ~1u;
    const unsigned base = Win16Linear(stack, 0);
    sp -= 6;
    Win16Word(base + sp, CallbackReturn & 0xffff);
    Win16Word(base + sp + 2, Win16GateSelector());
    Win16Word(base + sp + 4, 0); // WEP_FREE_DLL
    Register(Reg::Esp, sp);
    Register(Reg::Ds, stack);
    Register(Reg::Es, stack);
    Register(Reg::Cs, far_pointer >> 16);
    Register(Reg::Eip, far_pointer & 0xffff);
    const unsigned old_pending = pending;
    bool returned = false;
    while (state != Failed && state != Exited)
    {
        if (!Step())
            break;
        if (pending == CallbackReturn)
        {
            returned = true;
            break;
        }
        if (pending && !Service())
            break;
    }
    Check(cpu->Restore(context));
    cpu->FreeContext(context);
    pending = old_pending;
    if (state == Failed || state == Exited)
        return false;
    if (!returned)
    {
        char text[200];
        snprintf(text, sizeof text, "Win16 library %s did not return from WEP", module.name);
        Fail(text);
        return false;
    }
    return true;
}
bool Application::Win16EnterLibrary(Win16Module &module)
{
    if (module.entered || !module.library)
        return true;
    module.entered = true;
    const unsigned segment = module.image.EntrySegment();
    if (!segment || segment > module.segments || !module.selector[segment])
        return true; // a library without an entry point needs no call
    const unsigned far_pointer = module.selector[segment] << 16 | module.image.EntryOffset();
    Win16ToolhelpStartDll(module, far_pointer);
    if (state == Failed)
        return false;
    CpuContext *context = nullptr;
    if (!Check(cpu->NewContext(context)))
        return false;
    Check(cpu->Save(context));
    const unsigned stack = Register(Reg::Ss);
    unsigned sp = (Register(Reg::Esp) & 0xffff) & ~1u;
    const unsigned base = Win16Linear(stack, 0);
    sp -= 4;
    Win16Word(base + sp, CallbackReturn & 0xffff);
    Win16Word(base + sp + 2, Win16GateSelector());
    Register(Reg::Esp, sp);
    Register(Reg::Ds, module.instance ? module.instance : win16.state->instance_handle);
    Register(Reg::Edi, module.instance);
    Register(Reg::Ecx, module.image.HeapSize());
    Register(Reg::Esi, 0);
    Register(Reg::Ebx, 1);
    Register(Reg::Es, win16.state->prefix);
    Register(Reg::Cs, far_pointer >> 16);
    Register(Reg::Eip, far_pointer & 0xffff);
    const unsigned old_pending = pending;
    bool returned = false;
    while (state != Failed && state != Exited)
    {
        if (!Step())
            break;
        if (pending == CallbackReturn)
        {
            returned = true;
            break;
        }
        if (pending && !Service())
            break;
    }
    const unsigned answer = Register(Reg::Eax) & 0xffff;
    Check(cpu->Restore(context));
    cpu->FreeContext(context);
    pending = old_pending;
    if (state == Failed || state == Exited)
        return false;
    if (!returned)
    {
        char text[200];
        snprintf(text, sizeof text, "Win16 library %s did not return from its initialization entry",
                 module.name);
        Fail(text);
        return false;
    }
    if (!answer)
    {
        char text[200];
        snprintf(text, sizeof text, "Win16 library %s refused its initialization", module.name);
        Fail(text);
        return false;
    }
    if (log)
    {
        char text[200];
        snprintf(text, sizeof text, "WIN16 library %s initialized, entry %04x:%04x", module.name,
                 far_pointer >> 16, far_pointer & 0xffff);
        log(text);
    }
    return Win16LibraryEntryPoint(module);
}
// KRNL386: after LibMain of a library marked for Windows 4.0 or later
// comes its DLLENTRYPOINT export, with the Pascal arguments dwReason (1,
// DLL_PROCESS_ATTACH), the instance, the data segment, the heap size and
// three zero words; a zero answer fails the load. Win16HostedEnter does the
// same for a module a 32-bit process hosts. MSPRINT.DLL connects its thunk
// blocks to MSPRINT2.DLL there.
bool Application::Win16LibraryEntryPoint(Win16Module &module)
{
    if (module.image.ExpectedVersion() < 0x400)
        return true;
    const unsigned number = Win16Ordinal(module, "DLLENTRYPOINT");
    Win98Ne::Entry entry;
    if (!number || !module.image.EntryAt(number, entry) || !entry.segment || entry.segment > module.segments ||
        !module.selector[entry.segment])
        return true;
    const unsigned push[] = {0, 1, module.instance, module.instance, module.image.HeapSize(), 0, 0, 0};
    CpuContext *context = nullptr;
    if (!Check(cpu->NewContext(context)))
        return false;
    Check(cpu->Save(context));
    const unsigned stack = Register(Reg::Ss);
    unsigned sp = (Register(Reg::Esp) & 0xffff) & ~1u;
    const unsigned base = Win16Linear(stack, 0);
    for (unsigned word : push)
    {
        sp -= 2;
        Win16Word(base + sp, word & 0xffff);
    }
    sp -= 4;
    Win16Word(base + sp, CallbackReturn & 0xffff);
    Win16Word(base + sp + 2, Win16GateSelector());
    Register(Reg::Esp, sp);
    Register(Reg::Ds, module.instance ? module.instance : win16.state->instance_handle);
    Register(Reg::Cs, module.selector[entry.segment]);
    Register(Reg::Eip, entry.offset);
    const unsigned old_pending = pending;
    bool returned = false;
    while (state != Failed && state != Exited)
    {
        if (!Step())
            break;
        if (pending == CallbackReturn)
        {
            returned = true;
            break;
        }
        if (pending && !Service())
            break;
    }
    const unsigned answer = Register(Reg::Eax) & 0xffff;
    Check(cpu->Restore(context));
    cpu->FreeContext(context);
    pending = old_pending;
    if (state == Failed || state == Exited)
        return false;
    if (!returned)
    {
        char text[200];
        snprintf(text, sizeof text, "Win16 library %s did not return from DLLENTRYPOINT", module.name);
        Fail(text);
        return false;
    }
    if (!answer)
    {
        char text[200];
        snprintf(text, sizeof text, "Win16 library %s refused DLLENTRYPOINT", module.name);
        Fail(text);
        return false;
    }
    if (log)
    {
        char text[200];
        snprintf(text, sizeof text, "WIN16 library %s DLLENTRYPOINT %04x:%04x", module.name,
                 module.selector[entry.segment], entry.offset);
        log(text);
    }
    return true;
}
// Every shipped library whose entry has not run yet. The loader links a
// library when the program's relocations need it, which is before the program
// runs, so this is the point where they all come up.
bool Application::Win16EnterLibraries()
{
    if (!win16.state)
        return true;
    for (unsigned i = 0; i < win16.state->module_count; ++i)
        if (win16.state->modules[i].library && !Win16EnterLibraries(win16.state->modules[i]))
            return false;
    return true;
}
bool Application::Win16EnterLibraries(Win16Module &module) { return Win16EnterLibrary(module); }
#include "win16_comm.inc"
#include "win16_user_more.inc"
#include "win16_gdi_more.inc"
} // namespace X86
