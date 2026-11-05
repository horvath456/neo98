// SPDX-License-Identifier: GPL-2.0-or-later
// The COMMDLG gates of the 16-bit side, PLAN.md step 9, fifth part.
// COMMDLG.DLL is 16-bit code under Win98, so it is ours by the rule of section
// zero of the plan. It does not thunk anywhere: its module reference table
// names USER, KERNEL, GDI and KEYBOARD and nothing else, there is no
// CallProc32W among the KERNEL ordinals it binds, and its eight dialog
// templates are its own resources. The 16-bit entries are therefore a second
// entry side on the common dialogs this port already has, exactly like the
// USER and GDI gates.
// What has to happen at the gate is the structure. The 16-bit OPENFILENAME is
// 72 bytes with word handles and far pointers, the 32-bit one is 76 bytes with
// dword handles and flat pointers, and a flat address and a far pointer name
// the same byte here. The sizes are the ones COMMDLG.DLL itself checks:
// seg4:11D8 compares lStructSize against 0x48, seg6:0F72 against 0x20,
// seg5:0BEA against 0x2E and seg7:0EE5 against 0x34. A caller with another
// size gets CDERR_STRUCTSIZE, the way the original answers it.
// There are no success stubs. ChooseColor and ChooseFont are the colour and
// font dialogs of COMMDLG rebuilt natively (src/win16_choose_color.inc,
// src/win16_choose_font.inc).
//
#include "win16_machine.h"
#include "win98_ne_task.h"
#include "win98_win16_user.h"
#include "x86_app.h"
#include "x86_print.h"
#include "files.h"
#include "gdi.h"
#include "user.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
// The sizes COMMDLG.DLL checks, and the two structure widths beside them.
constexpr unsigned OpenFileName16 = 0x48;
constexpr unsigned ChooseColour16 = 0x20, ChooseFont16 = 0x2e;
constexpr unsigned PrintDialog16 = 0x34, PrintDialog32 = 66;
constexpr unsigned FindReplace16 = 0x24, FindReplace32 = 40;
constexpr unsigned FrEnableTemplate = 0x200, FrEnableTemplateHandle = 0x2000;
constexpr unsigned ErrorStructSize = 1; // CDERR_STRUCTSIZE
} // namespace

// A far pointer read out of a 16-bit structure, as the flat address it names.
unsigned Application::Win16FarPointer(unsigned at)
{
    const unsigned offset = Win16Word(at), selector = Win16Word(at + 2);
    return selector ? Win16Linear(selector, offset) : 0;
}

// A native controller owns the work after a hook, not just the hook.
// data: wp, lp, original flags, 0=hook/1=controller remainder, hook result.
struct Application::Win16ControllerPhase
{
    enum { Message = 1, ColorOK = 2, FontField = 3, FontEmpty = 4, OwnerHelp = 5, PrintField = 6, PrintNoPrinter = 7, PrintDriverCommand = 8, PrintDriverClose = 9 };
    Application &app;
    unsigned at = 0;
    bool kept = false, resumed = false;
    Win16ControllerPhase(Application &a, unsigned controller, unsigned hwnd, unsigned phase,
                         unsigned message, unsigned wp, unsigned lp, bool record, unsigned flags = 0) : app(a)
    {
        auto &t = app.threads[app.current_thread];
        if (t.host_resume && t.host_resume_at < t.host_frame_count)
        {
            const auto &f = t.host_frames[t.host_resume_at];
            if (f.kind == HostWin16Controller && f.a == hwnd && f.b == controller && f.c == phase)
            {
                at = t.host_resume_at++;
                kept = resumed = true;
            }
        }
        if (!resumed && record && app.PushHostFrame(HostWin16Controller, hwnd, controller, phase, message))
        {
            kept = true;
            at = t.host_frame_count - 1;
            auto &f = t.host_frames[at];
            f.data[0] = wp; f.data[1] = lp; f.data[2] = flags;
        }
    }
    ~Win16ControllerPhase() { if (kept) app.PopHostFrame(); }
    unsigned *Data() { return app.threads[app.current_thread].host_frames[at].data; }
    unsigned Flags(unsigned original) const
    { return resumed ? app.threads[app.current_thread].host_frames[at].data[2] : original; }
    bool AfterHook() const
    { return resumed && app.threads[app.current_thread].host_frames[at].data[3] != 0; }
    void Continue()
    { if (kept) app.threads[app.current_thread].host_frames[at].data[3] = 1; }
    unsigned Rest() { return app.ResumeInner(); }
    unsigned Warning(unsigned hwnd, unsigned text, unsigned caption, unsigned style)
    {
        if (resumed) return app.ResumeInner();
        if (kept) app.GiveTail(TailFresh);
        const unsigned arguments[] = {hwnd, text, caption, style};
        return app.Api("MessageBoxA", arguments);
    }
    unsigned Help(unsigned owner, unsigned message, unsigned hwnd, unsigned parameter)
    {
        if (resumed) return app.ResumeInner();
        if (kept) app.GiveTail(TailRecord);
        return app.Send(owner, message, hwnd, parameter);
    }
    unsigned Callback(unsigned mark, const unsigned *arguments, unsigned count)
    {
        unsigned answer = 0;
        if (resumed) answer = app.ResumeInner() & 0xffff;
        else if (mark)
        {
            if (kept) app.GiveTail(TailRecord);
            answer = app.Call(mark, arguments, count) & 0xffff;
        }
        if (kept)
        {
            auto &f = app.threads[app.current_thread].host_frames[at];
            f.data[4] = answer; f.data[3] = 1;
        }
        return answer;
    }
};

#include "win16_file_dialog.inc"
#include "win16_choose_color.inc"
#include "win16_choose_font.inc"
#include "win16_print_dialog.inc"

// A 16-bit global block as a native movable block with the same bytes.
unsigned Application::Win16ToNativeGlobal(unsigned handle16)
{
    if (!handle16)
        return 0;
    auto *block = Win16GlobalBlock(handle16);
    if (!block || !block->memory || !block->size)
        return 0;
    const unsigned flags = 2, size = block->size; // GMEM_MOVEABLE
    const unsigned allocate[] = {flags, size};
    unsigned handle = Api("GlobalAlloc", allocate);
    if (!handle)
        return 0;
    const unsigned target = Api("GlobalLock", &handle);
    if (!target)
    {
        Api("GlobalFree", &handle);
        return 0;
    }
    auto *bytes = new unsigned char[size];
    if (Read(block->memory, bytes, size))
        Store(target, bytes, size);
    delete[] bytes;
    Api("GlobalUnlock", &handle);
    return handle;
}
// The bytes of a native block into the 16-bit block `into`, which grows or
// shrinks to their size with GlobalReAlloc and keeps its handle, the way
// COMMDLG fills the caller's hDevMode; without one a
// new movable block is made.
unsigned Application::Win16FromNativeGlobal(unsigned handle, unsigned into)
{
    if (!handle)
        return 0;
    const unsigned size = Api("GlobalSize", &handle);
    const unsigned source = size ? Api("GlobalLock", &handle) : 0;
    if (!source)
        return 0;
    unsigned handle16 = into && Win16GlobalBlock(into) ? Win16GlobalReAlloc(into, size, Win98NeTask::Moveable) : 0;
    if (!handle16)
        handle16 = Win16GlobalAlloc(Win98NeTask::Moveable, size);
    auto *block = handle16 ? Win16GlobalBlock(handle16) : nullptr;
    if (block && block->memory)
    {
        auto *bytes = new unsigned char[size];
        if (Read(source, bytes, size))
            Store(block->memory, bytes, size);
        delete[] bytes;
    }
    Api("GlobalUnlock", &handle);
    return handle16;
}
// A dialog template of the 16-bit caller as a native block in the 32-bit
// form: the block behind a template handle, or the named resource of the
// module whose instance the structure carries. ConvertDialogTemplate is the
// converter the dialogs of USER.EXE take (src/x86_user_session.cpp).
unsigned Application::Win16NativeTemplate(unsigned instance, unsigned far_name, unsigned handle16)
{
    const unsigned char *bytes = nullptr;
    unsigned size = 0;
    unsigned char *copy = nullptr;
    if (handle16)
    {
        auto *block = Win16GlobalBlock(handle16);
        if (!block || !block->memory || !block->size)
            return 0;
        size = block->size;
        copy = new unsigned char[size];
        if (!Read(block->memory, copy, size))
        {
            delete[] copy;
            return 0;
        }
        bytes = copy;
    }
    else
    {
        auto &machine = *win16.state;
        Win16Module *module = nullptr;
        for (unsigned i = 0; i < machine.module_count; ++i)
            if (instance && (machine.modules[i].instance == instance || machine.modules[i].handle == instance))
                module = &machine.modules[i];
        if (!module)
            return 0;
        unsigned id = 0;
        char label[64] = {};
        if (!(far_name >> 16))
            id = 0x8000 | (far_name & 0x7fff);
        else if (const char *text = ReadString(Win16Linear(far_name >> 16, far_name & 0xffff)))
            snprintf(label, sizeof label, "%s", text);
        const unsigned offset = Win16Resource(*module, Win98Win16User::ResourceDialog, id, label[0] ? label : nullptr, size);
        if (!offset || !size)
            return 0;
        bytes = module->image.Data() + offset;
    }
    auto *converted = new unsigned char[4 * size + 64];
    unsigned length = 0;
    unsigned handle = 0;
    if (ConvertDialogTemplate(bytes, size, converted, 4 * size + 64, length))
    {
        const unsigned allocate[] = {2, length};
        handle = Api("GlobalAlloc", allocate);
        const unsigned target = handle ? Api("GlobalLock", &handle) : 0;
        if (target)
        {
            Store(target, converted, length);
            Api("GlobalUnlock", &handle);
        }
        else if (handle)
        {
            Api("GlobalFree", &handle);
            handle = 0;
        }
    }
    delete[] converted;
    delete[] copy;
    return handle;
}
// FindText and ReplaceText. COMMDLG and Windows 98 check lStructSize
// against 0x24 (high word zero) and answer CDERR_STRUCTSIZE; the rest of the
// checks are the ones of COMDLG32, which Win98Find::Validate implements,
// and run on the widened structure.
// 16-bit FINDREPLACE: lStructSize 0, hwndOwner 4, hInstance 6, Flags 8,
// lpstrFindWhat 0Ch, lpstrReplaceWith 10h, wFindWhatLen 14h,
// wReplaceWithLen 16h, lCustData 18h, lpfnHook 1Ch, lpTemplateName 20h.
bool Application::Win16FindDialog(const Win16Frame &frame, bool replace, unsigned &result)
{
    result = 0;
    const unsigned fr = frame.Pointer(0);
    if (!fr || Read32(fr) != FindReplace16)
    {
        Win16CommonDialogError(ErrorStructSize);
        return true;
    }
    Win16FindShadow *slot = nullptr;
    for (auto &shadow : win16_find)
    {
        if (shadow.wide && !User::IsWindow(shadow.hwnd))
        {
            Free(shadow.wide);
            shadow = {};
        }
        if (!shadow.wide && !slot)
            slot = &shadow;
    }
    const unsigned wide = slot ? Allocate(FindReplace32) : 0;
    if (!wide)
    {
        Win16CommonDialogError(9); // CDERR_MEMALLOCFAILURE
        return true;
    }
    unsigned char zero[FindReplace32] = {};
    Store(wide, zero, sizeof zero);
    const unsigned flags = Read32(fr + 0x08);
    Write32(wide + 0, FindReplace32);
    Write32(wide + 4, Win16Word(fr + 0x04));          // hwndOwner
    Write32(wide + 8, Win16Word(fr + 0x06));          // hInstance
    Write32(wide + 12, flags);
    Write32(wide + 16, Win16FarPointer(fr + 0x0c));   // lpstrFindWhat
    Write32(wide + 20, Win16FarPointer(fr + 0x10));   // lpstrReplaceWith
    Win16Word(wide + 24, Win16Word(fr + 0x14));       // wFindWhatLen
    Win16Word(wide + 26, Win16Word(fr + 0x16));       // wReplaceWithLen
    Write32(wide + 28, Read32(fr + 0x18));            // lCustData
    const unsigned hook = Win16Word(fr + 0x1c) | Win16Word(fr + 0x1e) << 16;
    Write32(wide + 32, hook ? Win16Mark(hook) : 0);
    // A template of the 16-bit side is a 16-bit DLGTEMPLATE, out of the
    // caller's module or its global block. The dialog gets it converted, as a
    // handle of its own (FR_ENABLETEMPLATEHANDLE); the caller keeps its flags.
    unsigned converted = 0;
    if (flags & (FrEnableTemplate | FrEnableTemplateHandle))
    {
        converted = Win16FindTemplate(fr, flags);
        if (!converted)
        {
            Free(wide);
            Win16CommonDialogError(flags & FrEnableTemplateHandle ? 8 : 6); // CDERR_LOADRESFAILURE, FINDRESFAILURE
            return true;
        }
        Write32(wide + 8, converted);
        Write32(wide + 12, (flags & ~FrEnableTemplate) | FrEnableTemplateHandle);
    }
    const unsigned a[] = {wide};
    const unsigned hwnd = Api(replace ? "ReplaceTextA" : "FindTextA", a);
    if (converted)
        Free(converted);
    if (!hwnd)
    {
        Free(wide);
        return true;
    }
    *slot = Win16FindShadow{wide, frame.Word(1) << 16 | frame.Word(0), flags & (FrEnableTemplate | FrEnableTemplateHandle),
                            hwnd};
    result = hwnd & 0xffff;
    return true;
}
unsigned Application::Win16FindTemplate(unsigned fr, unsigned flags)
{
    const unsigned char *source = nullptr;
    unsigned bytes = 0;
    unsigned char *copy = nullptr;
    if (flags & FrEnableTemplateHandle)
    {
        auto *block = Win16GlobalBlock(Win16Word(fr + 0x06));
        if (!block || !block->memory || !block->size)
            return 0;
        bytes = block->size;
        copy = new unsigned char[bytes];
        if (!copy || !Read(block->memory, copy, bytes))
        {
            delete[] copy;
            return 0;
        }
        source = copy;
    }
    else
    {
        auto &machine = *win16.state;
        const unsigned instance = Win16Word(fr + 0x06);
        Win16Module *module = nullptr;
        for (unsigned i = 0; i < machine.module_count; ++i)
            if (machine.modules[i].instance == instance || machine.modules[i].handle == instance)
                module = &machine.modules[i];
        if (!module)
            return 0;
        // MAKEINTRESOURCE has selector zero, a name is a far string.
        const unsigned name_offset = Win16Word(fr + 0x20), name_selector = Win16Word(fr + 0x22);
        const char *label = name_selector ? ReadString(Win16Linear(name_selector, name_offset)) : nullptr;
        const unsigned offset =
            Win16Resource(*module, Win98Win16User::ResourceDialog, name_selector ? 0 : name_offset, label, bytes);
        if (!offset || !bytes)
            return 0;
        source = module->image.Data() + offset;
    }
    auto *converted = new unsigned char[4 * bytes + 64];
    unsigned length = 0, handle = 0;
    if (converted && ConvertDialogTemplate(source, bytes, converted, 4 * bytes + 64, length) &&
        (handle = Allocate(length)) != 0)
        Store(handle, converted, length);
    delete[] converted;
    delete[] copy;
    return handle;
}
// The far pointer of the 16-bit FINDREPLACE behind a 32-bit one, with the
// flags the dialog set copied into it; zero for a structure of the 32-bit side.
unsigned Application::Win16FindNarrow(unsigned wide)
{
    if (!wide)
        return 0;
    for (const auto &shadow : win16_find)
        if (shadow.wide == wide)
        {
            const unsigned flags = Read32(wide + 12) & ~(FrEnableTemplate | FrEnableTemplateHandle);
            Write32(Win16Linear(shadow.narrow >> 16, shadow.narrow & 0xffff) + 0x08, flags | shadow.template_flags);
            return shadow.narrow;
        }
    return 0;
}

// The error word of the common dialogs. The 16-bit side and the 32-bit side
// share the one the native provider keeps, because they are one process.
void Application::Win16CommonDialogError(unsigned value) { CommonDialogError(value); }

bool Application::Win16CommDlgCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                   unsigned &words)
{
    switch (ordinal)
    {
    case 1: // GetOpenFileName(ofn)
        words = 2;
        return Win16FileDialog(frame, false, result);
    case 2: // GetSaveFileName(ofn)
        words = 2;
        return Win16FileDialog(frame, true, result);
    case 5: // ChooseColor(cc)
        words = 2;
        if (Read32(frame.Pointer(0)) != ChooseColour16)
        {
            Win16CommonDialogError(ErrorStructSize);
            result = 0;
            return true;
        }
        return Win16ChooseColor(frame, result);
    case 11: // FindText(fr)
    case 12: // ReplaceText(fr)
        words = 2;
        return Win16FindDialog(frame, ordinal == 12, result);
    case 15: // ChooseFont(cf)
        words = 2;
        if (Read32(frame.Pointer(0)) != ChooseFont16)
        {
            Win16CommonDialogError(ErrorStructSize);
            result = 0;
            return true;
        }
        return Win16ChooseFont(frame, result);
    case 20: // PrintDlg(pd)
        words = 2;
        return Win16PrintDialog(frame, result);
    case 26: // CommDlgExtendedError
        words = 0;
        result = Api("CommDlgExtendedError", nullptr);
        return true;
    case 27: // GetFileTitle(file, title, size)
    {
        words = 5;
        const unsigned a[] = {frame.Pointer(3), frame.Pointer(1), frame.Word(0)};
        result = Api("GetFileTitleA", a);
        return true;
    }
    default:
        break;
    }
    char text[240];
    snprintf(text, sizeof text,
             "Win16 COMMDLG ordinal %u is not ported yet: the entry table of COMMDLG.DLL carries 1, 2, "
             "5, 9, 11, 12, 15, 16, 20, 26, 27, 28 and 40",
             ordinal);
    Fail(text);
    return false;
}
bool Application::FinishWin16ChoicePreparation(unsigned hwnd, unsigned kind)
{
    unsigned *native = nullptr;
    bool ready = hwnd != 0 && state != Failed;
    if (kind == HostWin16Color)
    {
        auto *frame = Win16ColorFrameOf(this, hwnd, false);
        if (!frame) return false;
        native = &frame->native_template;
        ready = ready && frame->ready;
    }
    else if (kind == HostWin16Font)
    {
        auto *frame = Win16FontFrameOf(this, hwnd);
        if (!frame) return false;
        native = &frame->native_template;
    }
    else
    {
        auto *frame = Win16PrintFrameOf(this, hwnd, false);
        if (!frame) return false;
        native = &frame->native_template;
    }
    if (*native)
    {
        Api("GlobalUnlock", native);
        Api("GlobalFree", native);
        *native = 0;
    }
    if (!ready && kind == HostWin16Color && state != Failed) Win16CommonDialogError(0xffff);
    return ready;
}
unsigned Application::ResumeWin16ControllerPhase()
{
    const auto frame = threads[current_thread].host_frames[threads[current_thread].host_resume_at];
    if (frame.c == Win16ControllerPhase::Message)
    {
        if (frame.b == HostWin16Color)
            return Win16ColorDialogMessage(frame.a, frame.d, frame.data[0], frame.data[1]);
        if (frame.b == HostWin16Font)
            return Win16FontDialogMessage(frame.a, frame.d, frame.data[0], frame.data[1]);
        if (frame.b == HostWin16Print)
        {
            const auto *dialog = FindDialog(frame.a);
            return Win16PrintDialogMessage(frame.a, frame.d, frame.data[0], frame.data[1],
                                          dialog && dialog->procedure == Win16SetupDialogToken);
        }
    }
    if (frame.c == Win16ControllerPhase::ColorOK && frame.b == HostWin16Color)
    {
        auto *f = Win16ColorFrameOf(this, frame.a, false);
        if (!f) { Fail("A saved colour hook lost its controller"); return 0; }
        Win16ColorDialog e{*this, *f};
        Win16ControllerPhase phase(*this, HostWin16Color, frame.a, Win16ControllerPhase::ColorOK,
                                   0, 0, 0, false);
        if (phase.AfterHook()) return phase.Rest();
        const unsigned answer = phase.Callback(0, nullptr, 0);
        if (!answer) e.FinishClose(frame.data[0]);
        return 1;
    }
    if (frame.b == HostWin16Print && frame.c >= Win16ControllerPhase::PrintField &&
        frame.c <= Win16ControllerPhase::PrintDriverClose)
    {
        auto *f = Win16PrintFrameOf(this, frame.a, false);
        if (!f) { Fail("A saved print warning lost its controller"); return 0; }
        Win16PrintDialogs e(*this, f, f->pd);
        Win16ControllerPhase phase(*this, HostWin16Print, frame.a, frame.c, 0, 0, 0, false);
        if (frame.c == Win16ControllerPhase::PrintField)
        {
            if (!frame.data[3]) phase.Warning(0, 0, 0, 0);
            e.FieldFocus(frame.a, frame.data[0], phase, frame.data[3], frame.data[1]);
            return 0;
        }
        phase.Warning(0, 0, 0, 0);
        if (frame.c == Win16ControllerPhase::PrintNoPrinter) return e.InitFailed(frame.a);
        if (frame.c == Win16ControllerPhase::PrintDriverCommand || Win16PrintFrame::driver_failed) return 1;
        return e.FinishMissingSetupClose(frame.a, frame.data[0], frame.data[1], frame.data[2]);
    }
    if (frame.c == Win16ControllerPhase::OwnerHelp)
    {
        bool found = false;
        if (frame.b == HostWin16Color) found = Win16ColorFrameOf(this, frame.a, false) != nullptr;
        if (frame.b == HostWin16Font) found = Win16FontFrameOf(this, frame.a) != nullptr;
        if (frame.b == HostWin16Print) found = Win16PrintFrameOf(this, frame.a, false) != nullptr || Win16PrintFrameOf(this, frame.a, true) != nullptr;
        if (!found) { Fail("A saved Help callback lost its controller"); return 0; }
        Win16ControllerPhase phase(*this, frame.b, frame.a, frame.c, 0, 0, 0, false);
        phase.Help(0, 0, 0, 0);
        return frame.data[0];
    }
    if (frame.b == HostWin16Font &&
        (frame.c == Win16ControllerPhase::FontField || frame.c == Win16ControllerPhase::FontEmpty))
    {
        auto *f = Win16FontFrameOf(this, frame.a);
        if (!f) { Fail("A saved font warning lost its controller"); return 0; }
        Win16FontDialog e{*this, *f};
        Win16ControllerPhase phase(*this, HostWin16Font, frame.a, frame.c, 0, 0, 0, false);
        phase.Warning(0, 0, 0, 0);
        if (frame.c == Win16ControllerPhase::FontEmpty)
        {
            Win16CommonDialogError(0x2001);
            EndDialog(frame.a, 0);
            f->ended = true;
            return 0;
        }
        e.Call("PostMessageA", frame.a, 0x28, e.Item(frame.data[0]), 1);
        return 1;
    }
    Fail("A saved Win16 controller has no matching continuation");
    return 0;
}
} // namespace X86
