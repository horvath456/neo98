// SPDX-License-Identifier: GPL-2.0-or-later
#include "dialog_template.h"
#include "win98_indirect_dialog.h"
#include "win98_dialog_dispatch.h"
#include "x86_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
unsigned Application::IndirectDialogApi(ApiName name, const unsigned *a)
{
    const bool modal = API_IS(name,"DialogBoxIndirectParamA");
    const unsigned failure = modal ? ~0u : 0;
    // Only the handler of a gate keeps a record (BuildMessageBox).
    const bool gate = TakeFresh();
    if (!a[1]) { last_error = 87; return failure; }
    // New provider: snapshot only readable guest pages, at most one MiB.
    // The bounded shared decoder below determines the template's exact end.
    constexpr unsigned limit = 1024 * 1024;
    unsigned length = 0;
    while (length < limit && a[1] <= ~0u - length)
    {
        unsigned at = a[1] + length;
        VirtualMemory::Info info{};
        if (!virtual_memory.Query(at, info) || info.base > at ||
            info.size <= at - info.base) break;
        unsigned span = info.size - (at - info.base);
        if (span > limit - length) span = limit - length;
        if (span > ~0u - at || !virtual_memory.Access(at, span, false)) break;
        length += span;
    }
    if (length < 18) { last_error = 87; return failure; }
    auto *bytes = new unsigned char[length];
    if (!bytes) { last_error = 8; return failure; }
    if (!Read(a[1], bytes, length)) { delete[] bytes; return failure; }
    auto *definition = new DialogResource::Template;
    bool valid = definition && definition->Parse(bytes, length);
    unsigned consumed = valid ? definition->consumed : 0;
    delete definition;
    if (!valid) { delete[] bytes; last_error = 87; return failure; }
    unsigned owner = a[2];
    if (owner && !User::IsWindow(owner)) { delete[] bytes; last_error = 1400; return failure; }
    if (modal)
    {
        struct E {
            unsigned Desktop() { return User::GetDesktopWindow(); }
            unsigned Style(unsigned h) { return User::Style(h); }
            unsigned Root(unsigned h) { while ((User::Style(h) & 0xc0000000u) == 0x40000000u) { unsigned p=User::Parent(h); if(!p)break; h=p; } return h; }
        } e;
        owner = Win98IndirectDialog::ModalOwner(e, owner);
    }
    const bool restore = modal && owner && User::IsEnabled(owner);
    // USER disables the modal owner before WM_INITDIALOG.
    if (modal && owner) { const unsigned enable[] = {owner, 0}; Api("EnableWindow", enable); }
    const bool kept = gate && PushHostFrame(HostDialogBox, restore, owner, modal, 1);
    if (kept) GiveTail(TailRecord);
    unsigned hwnd = CreateDialogTemplate(bytes, consumed, owner, a[3], a[4], modal, a[0]);
    delete[] bytes;
    if (auto *record = FindDialog(hwnd)) record->restore_owner = restore;
    // What is left to do after the loop is in a record of the thread, so a
    // saved state can take the call up again (ResumeHostCall).
    if (kept) threads[current_thread].host_frames[threads[current_thread].host_frame_count - 1].d = 0;
    unsigned result = modal ? (hwnd ? RunModal(hwnd, kept) : failure) : hwnd;
    if (kept) PopHostFrame();
    if (restore && User::IsWindow(owner)) { const unsigned enable[] = {owner, 1}; Api("EnableWindow", enable); }
    return result;
}
Application::Dialog *Application::DialogSet::Find(unsigned hwnd) const
{
    if (!hwnd) return nullptr;
    for (unsigned i = 0; i < count; ++i)
        if (records[i]->hwnd == hwnd)
            return records[i];
    return nullptr;
}
// A free record first, otherwise one more. The array of pointers may move; the
// records it points at never do, which is what the callers that hold a pointer
// across a guest callback rely on.
Application::Dialog *Application::DialogSet::Take()
{
    for (unsigned i = 0; i < count; ++i)
        if (!records[i]->hwnd)
            return records[i];
    if (count == capacity)
    {
        const unsigned wanted = capacity ? capacity * 2 : 16;
        auto **grown = (Dialog **)realloc(records, wanted * sizeof(Dialog *));
        if (!grown) return nullptr;
        records = grown;
        capacity = wanted;
    }
    auto *record = new Dialog;
    if (!record) return nullptr;
    records[count++] = record;
    return record;
}
void Application::DialogSet::Release()
{
    for (unsigned i = 0; i < count; ++i) delete records[i];
    free(records);
    records = nullptr;
    count = capacity = 0;
}
Application::Dialog *Application::FindDialog(unsigned hwnd)
{
    return dialogs.Find(hwnd);
}
void Application::DrawControl(unsigned parent, unsigned control, unsigned dc, const Gdi::RECT &rect,
                              unsigned state)
{
    unsigned data[] = {4,
                       User::ControlID(control),
                       0,
                       1,
                       state,
                       control,
                       dc,
                       (unsigned)rect.left,
                       (unsigned)rect.top,
                       (unsigned)rect.right,
                       (unsigned)rect.bottom,
                       0};
    unsigned memory = Allocate(sizeof data);
    if (!memory)
        return;
    Store(memory, data, sizeof data);
    SendFromNative(parent, 0x2b, User::ControlID(control), memory);
    Free(memory);
}
unsigned Application::CreateDialog(unsigned instance, unsigned resource, unsigned owner, unsigned procedure,
                                   unsigned parameter, bool modal)
{
    // The template comes from the module the caller names: SHELL32 runs its
    // Find and Run dialogs out of its own resources, COMCTL32 its property
    // sheets. A zero instance means the process's own image.
    // The loader lock covers finding and copying the template only. Held
    // through CreateDialogTemplate it covered WM_INITDIALOG and the dialog
    // procedure too: Explorer's Run dialog calls SetForegroundWindow there,
    // which sends WM_NCACTIVATE to the tray thread, and that thread waited
    // for the lock in LoadCursorA.
    if (!LockLoader())
        return 0;
    struct Unlock { Application &app; bool held = true; void Release() { if (held) { held = false; app.UnlockLoader(); } } ~Unlock() { Release(); } } unlock{*this};
    const unsigned index = instance ? ModuleIndex(instance, true) : 0;
    const auto *pe = index < ModuleCapacity ? modules[index].pe : nullptr;
    if (!pe)
    {
        Fail("Dialog template requested from an unknown module");
        return 0;
    }
    Pe32::Image::ResourceKey type, id;
    unsigned short name[260];
    type.id = 5;
    if (!ResourceName(resource, id, name))
        return 0;
    Pe32::Image::ResourceEntry entry;
    unsigned error = 0;
    bool found = pe->FindResource(type, id, 0, entry, &error);
    if (!found && error == 1815)
        found = pe->FindResource(type, id, 0x409, entry, &error) || pe->FindResource(type, id, ~0u, entry, &error);
    if (!found || !entry.size || entry.size > 1024 * 1024)
    {
        // Win98 USER finds no template and CreateDialogParam answers NULL
        // (DialogBoxParam -1) with FindResource's error; the caller goes on.
        // SHDOCVW's Active Setup progress dialog 13120 lives only in
        // SHDOCLC.DLL, and SHDOCVW asks for it in its own module, so on an
        // English Win98 SE the stubs run without it. The log line
        // names what was asked for: by number or by name, out of which module.
        char message[320];
        if (resource < 65536)
            snprintf(message, sizeof message, "Missing dialog resource %u in module %08x", resource,
                     modules[index].handle);
        else
            snprintf(message, sizeof message, "Missing dialog resource %s in module %08x", ReadString(resource),
                     modules[index].handle);
        if (log)
            log(message);
        last_error = found ? 1812 : (error ? error : 1814);
        return 0;
    }
    auto *copy = static_cast<unsigned char *>(malloc(entry.size));
    if (!copy || !Read(pe->Base() + entry.data, copy, entry.size))
    {
        free(copy);
        Fail("Dialog template unreadable");
        return 0;
    }
    // The dialog and its controls belong to the module the template came
    // from, as CreateDialogParam's hInstance says: Mozilla's setup registers
    // its dialog class MozillaSetupDlg for SETUPRSC.DLL and creates the
    // dialogs out of it.
    const unsigned module = instance ? modules[index].handle : image.Base();
    unlock.Release();
    const unsigned result = CreateDialogTemplate(copy, entry.size, owner, procedure, parameter, modal, module);
    free(copy);
    return result;
}
unsigned Application::CreateDialogTemplate(const unsigned char *data,unsigned length,unsigned owner,
                                           unsigned procedure,unsigned parameter,bool modal)
{
    return CreateDialogTemplate(data,length,owner,procedure,parameter,modal,image.Base());
}
unsigned Application::CreateDialogTemplate(const unsigned char *data, unsigned length, unsigned owner,
                                            unsigned procedure, unsigned parameter, bool modal, unsigned instance)
{
    const unsigned tail = threads[current_thread].tail;
    const bool fresh = TakeFresh();
    const bool may_record = fresh || tail == TailRecord;
    const unsigned previous_focus = User::GetFocus();
    if (!instance) instance=image.Base();
    auto *definition = new DialogResource::Template;
    if (!definition->Parse(data, length))
    {
        delete definition;
        Fail("Unsupported or invalid dialog template");
        return 0;
    }
    Dialog *slot = dialogs.Take();
    if (!slot)
    {
        delete definition;
        last_error = 8;
        return 0;
    }
    *slot = Dialog{};
    unsigned font = system;
    if (definition->point)
    {
        unsigned memory = Allocate(60);
        unsigned char logfont[60] = {};
        // USER: lfHeight = -MulDiv(points, LOGPIXELSY, 72), which
        // rounds; 8 points at 96 dpi are 11 pixels, not 10.
        int height = -(int)((definition->point * 96 + 36) / 72);
        memcpy(logfont, &height, 4);
        // USER: an extended template brings its own weight, a
        // standard one asks for FW_BOLD, the dialog font of Windows 3.x;
        // DS_3DLOOK makes either FW_NORMAL. gives every dialog
        // of a module that expects 4.0 the DS_3DLOOK, so only a 3.x program
        // (or a 32-bit one with subsystem version 3.10) keeps the bold font
        // and its wider base units.
        const unsigned expected = InstanceVersion(instance);
        const bool look3d = (definition->style & 4) || !expected || expected >= 0x400;
        unsigned weight = definition->extended_template ? definition->weight : 700;
        if (look3d)
            weight = 400;
        if (!weight)
            weight = 400;
        memcpy(logfont + 16, &weight, 4);
        logfont[20] = definition->italic;
        logfont[23] = definition->charset;
        // "Helv" and "MS Shell Dlg" become "MS Sans Serif",
        // "Tms Rmn" becomes "MS Serif".
        const char *face = definition->face;
        auto same = [](const char *a, const char *b) {
            for (;; ++a, ++b)
            {
                const unsigned x = (unsigned char)(*a >= 'a' && *a <= 'z' ? *a - 32 : *a);
                const unsigned y = (unsigned char)(*b >= 'a' && *b <= 'z' ? *b - 32 : *b);
                if (x != y) return false;
                if (!x) return true;
            }
        };
        if (same(face, "Helv") || same(face, "MS Shell Dlg"))
            face = "MS Sans Serif";
        else if (same(face, "Tms Rmn"))
            face = "MS Serif";
        unsigned n = strlen(face);
        if (n > 31)
            n = 31;
        memcpy(logfont + 28, face, n);
        Store(memory, logfont, sizeof logfont);
        font = Api("CreateFontIndirectA", &memory);
        Free(memory);
    }
    unsigned dc = User::GetDC(0);
    auto old = Gdi::SelectObject(dc, font);
    Gdi::SIZE extent = {};
    Gdi::TEXTMETRICA metrics = {};
    Gdi::GetTextExtentPoint32A(dc, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 52, &extent);
    Gdi::GetTextMetricsA(dc, &metrics);
    Gdi::SelectObject(dc, old);
    User::ReleaseDC(0, dc);
    unsigned xunit = (extent.cx / 26 + 1) / 2, yunit = metrics.tmHeight;
    if (!xunit || !yunit)
    {
        delete definition;
        Fail("Invalid dialog base units");
        return 0;
    }
    Gdi::RECT r = {definition->x, definition->y, definition->width, definition->height};
    Win98Dialog::MapRect(&r, xunit, yunit);
    if (definition->x == -32768)
    {
        r.left = 80;
        r.top = 65;
    }
    // Shared original USER dialog style preparation.
    Win98IndirectDialog::Styles(definition->style, definition->extended,
                               image.SubsystemVersion() >= 0x40000);
    // USER loads the menu of a template from the module of the dialog, by
    // number or by name (SNDREC32 names "APPMENU").
    unsigned menu = definition->menu_name[0] ? LoadNamedMenu(definition->menu_name, instance)
                    : definition->menu         ? LoadMenu(definition->menu, instance)
                                               : 0;
    Gdi::RECT outer = {0, 0, r.right, r.bottom};
    User::AdjustWindowRect(&outer, definition->style, menu != 0, definition->extended);
    // USER: DS_CENTER and DS_CENTERMOUSE for a 4.0 module, else
    // the template position relative to the owner's client area (unless
    // DS_ABSALIGN), then kept on the work area. The Shut Down dialog
    // of SHELL32 and Kaggz's start dialog are DS_CENTER.
    if (definition->x != -32768)
    {
        Win98IndirectDialog::Placement place;
        place.style = definition->style;
        place.extended = definition->extended;
        place.win40 = image.SubsystemVersion() >= 0x40000;
        int screen_width = 800, screen_height = 600, depth = 0;
        User::ScreenMode(screen_width, screen_height, depth);
        place.monitor = place.screen = {0, 0, screen_width, screen_height};
        Gdi::RECT work = {0, 0, screen_width, screen_height};
        User::WorkArea(&work);
        place.work = {work.left, work.top, work.right, work.bottom};
        Gdi::POINT cursor = {};
        User::CursorPosition(&cursor);
        place.cursor_x = cursor.x;
        place.cursor_y = cursor.y;
        if (owner && User::IsWindow(owner))
        {
            place.owner = true;
            Gdi::RECT client = {};
            if (User::ClientScreenRect(owner, &client))
            {
                place.owner_x = client.left;
                place.owner_y = client.top;
            }
        }
        int left = r.left, top = r.top;
        Win98IndirectDialog::Place(place, r.left, r.top, outer.right - outer.left, outer.bottom - outer.top, left, top);
        r.left = left;
        r.top = top;
    }
    const char *klass = definition->klass[0] ? definition->klass : "#32770";
    unsigned name = String(klass), title = String(definition->title);
    unsigned args[] = {definition->extended,
                       name,
                       title,
                       definition->style & ~0x10000000u,
                       (unsigned)r.left,
                       (unsigned)r.top,
                       (unsigned)(outer.right - outer.left),
                       (unsigned)(outer.bottom - outer.top),
                       owner,
                       menu,
                       instance,
                       0};
    unsigned hwnd = CreateWindow(args);
    if (!hwnd && instance != image.Base())
    {
        // A class the process registered for its own image, used by a
        // template out of a DLL.
        args[10] = image.Base();
        hwnd = CreateWindow(args);
    }
    Free(name);
    Free(title);
    if (!hwnd)
    {
        delete definition;
        return 0;
    }
    *slot = Dialog{};
    slot->hwnd = hwnd;
    slot->modal = modal;
    slot->procedure = procedure;
    slot->owner = owner;
    slot->font = font;
    slot->previous_focus = previous_focus;
    slot->xunit = xunit;
    slot->yunit = yunit;
    if (procedure && font != system)
        Send(hwnd, 0x30, font, 0);
    if (!User::IsWindow(hwnd))
    {
        delete definition;
        return hwnd;
    }
    // USERx hands the template's extended style to CreateWindowEx and
    // never writes it again. A second write after the creation dropped the
    // WS_EX_TOPMOST the window inherits from a topmost owner,
    // so the Shut Down dialog fell behind SHELL32's dimmed full-screen owner

    for (unsigned i = 0; i < definition->count && state != Failed; ++i)
    {
        auto &item = definition->items[i];
        const char *type = item.klass;
        if (item.class_id)
        {
            const char *names[] = {"BUTTON", "EDIT", "STATIC", "LISTBOX", "SCROLLBAR", "COMBOBOX"};
            if (item.class_id < 128 || item.class_id > 133)
            {
                Fail("Unknown dialog control class");
                break;
            }
            type = names[item.class_id - 128];
        }
        Gdi::RECT bounds = {item.x, item.y, item.width, item.height};
        Win98Dialog::MapRect(&bounds, xunit, yunit);
        // Dialog templates can identify icon resources by a numeric marker.
        // Pass these as #number strings because their binary representation
        // cannot survive a null-terminated string copy.
        char ordinal[16];
        const char *text = item.title;
        auto is_static = [](const char *name) {
            const char *want = "STATIC";
            for (; name && *want; ++name, ++want)
                if ((*name >= 'a' && *name <= 'z' ? *name - 32 : *name) != *want) return false;
            return name && !*name;
        };
        // The same holds for SS_BITMAP (0Eh), whose Windows 98 loads the bitmap
        // out of that name; Kaggz's start dialog names its picture as 114.
        if (item.title_id && !item.title[0] && ((item.style & 0x1f) == 3 || (item.style & 0x1f) == 0xe) &&
            is_static(type))
        {
            snprintf(ordinal, sizeof ordinal, "#%u", item.title_id);
            text = ordinal;
        }
        unsigned class_name=String(type), title=String(text);
        // USER sets WS_EX_NOPARENTNOTIFY before creating each
        // template control, including extended templates.
        unsigned args[]={item.extended|4u,class_name,title,item.style|0x40000000,
                         (unsigned)bounds.left,(unsigned)bounds.top,(unsigned)bounds.right,
                         (unsigned)bounds.bottom,hwnd,item.id,instance,0};
        unsigned control=CreateWindow(args);
        Free(class_name);Free(title);
        if (!control)
        {
            // Name the item, so a report says which class or capacity it was.
            char text[200];
            snprintf(text, sizeof text,
                     "Unsupported dialog control or capacity exceeded: item %u of %u class=%s id=%u style=%08x error=%u",
                     i, definition->count, type ? type : "(null)", item.id, item.style, last_error);
            Fail(text);
            break;
        }
        // Only a DS_SETFONT template hands its font to each control; without
        // it Win98 SE sends no WM_SETFONT (dialog_end_create contract).
        if (definition->point) Send(control, 0x30, font, 0);
        // WM_CREATE and WM_SETFONT returned into the guest. A modeless dialog
        // whose handler called EndDialog is gone by now and
        // CbtDestroyEnvironment::Free has released this slot, so the record is
        // resolved again through the window handle before it is written.
        slot = FindDialog(hwnd);
        if (!slot)
            break;
        if ((item.style & 15) == 1 && User::IsButton(control))
            slot->default_id = item.id;
    }
    bool visible = definition->style & 0x10000000;
    delete definition;
    if (!User::IsWindow(hwnd))
        return hwnd;
    unsigned first = User::NextDialogItem(hwnd, 0, false);
    const bool kept = may_record && PushHostFrame(HostDialogInit, hwnd, first, visible);
    if (kept) GiveTail(TailRecord);
    const unsigned result = procedure ? CallWindow(hwnd, 0x110, first, parameter) : 0;
    if (kept) PopHostFrame();
    return FinishDialogInit(hwnd, first, visible, result);
}
unsigned Application::FinishDialogInit(unsigned hwnd, unsigned first, bool visible, unsigned result)
{
    if (result && first && User::IsWindow(hwnd) && (visible || !(User::Style(hwnd) & 0x400u)))
    {
        // The procedure may have hidden or disabled that control ("Copy
        // Profile" of SYSDM.CPL hides its combo box and shows an edit
        // control): the focus goes to the first tab stop there is now.
        first = User::NextDialogItem(hwnd, 0, false);
        if (first)
            SetFocus(first);
    }
    // EndDialog out of WM_INITDIALOG is the documented way to refuse a dialog.
    if (visible && User::IsWindow(hwnd) && FindDialog(hwnd) && !FindDialog(hwnd)->ended)
    {
        ShowWindow(hwnd, 1);
        // The show is a SetWindowPos, whose end erases the window of this
        // thread at once (PositionWindows): a dialog shown before a long
        // loop without messages has its background (SETUPX).
        if (User::IsWindow(hwnd) && User::WindowThread(hwnd) == User::CurrentThread())
            RedrawImmediate(hwnd, 2, 8);
    }
    return hwnd;

}
void Application::EndDialog(unsigned hwnd, unsigned result)
{
    auto *dialog = FindDialog(hwnd);
    if (!dialog)
        return;
    // USER remembers activation before enabling the owner. EndDialog
    // hides both template and custom dialogs; the modal loop owns destruction.
    const unsigned queue = User::WindowThread(hwnd);
    const bool was_active = User::ThreadActivation(queue).active == hwnd;
    const unsigned owner = (User::Style(hwnd) & 0xc0000000u) == 0x40000000u
        ? User::Parent(hwnd) : User::Owner(hwnd);
    const bool restore = !dialog->modal || dialog->restore_owner;
    if (restore && owner && User::IsWindow(owner) && !User::IsEnabled(owner))
    {
        { const unsigned enable[] = {owner, 1}; Api("EnableWindow", enable); }
        dialog = FindDialog(hwnd);
        if (!dialog || !User::IsWindow(hwnd)) return;
    }
    dialog->result = result;
    dialog->ended = true;
    // moves a focused descendant back to the dialog before hiding.
    if (User::ThreadActivation(queue).active == hwnd)
    {
        unsigned focus = User::ThreadActivation(queue).focus;
        unsigned ancestor = User::Parent(focus);
        for (unsigned n = 0; ancestor && ancestor != hwnd && n < User::WindowCapacity; ++n)
            ancestor = User::Parent(ancestor);
        if (ancestor == hwnd) SetFocus(hwnd);
    }
    if (!User::IsWindow(hwnd)) return;
    unsigned position[1][7] = {{hwnd, 0, 0, 0, 0, 0, 0x97}};
    PositionWindows(1, position);
    if (!User::IsWindow(hwnd)) return;
    // activates the owner only after the hide notifications.
    if (was_active && owner && User::IsWindow(owner)) ActivateWindow(owner);
    else if (User::ThreadActivation(queue).active == hwnd)
    {
        auto state = User::ThreadActivation(queue);
        state.active = state.focus = 0;
        User::WriteThreadActivation(queue, state);
    }
    if (User::IsWindow(hwnd)) User::PostMessage(hwnd, 0, 0, 0);
}
bool Application::PushHostFrame(unsigned kind, unsigned a, unsigned b, unsigned c, unsigned d)
{
    auto &t = threads[current_thread];
    if (t.host_frame_count >= Thread::HostFrameCapacity) return false;
    t.host_frames[t.host_frame_count++] = {kind, a, b, c, d};
    return true;
}
void Application::PopHostFrame()
{
    auto &t = threads[current_thread];
    if (t.host_frame_count) t.host_frames[--t.host_frame_count] = {};
}
unsigned Application::ResumeHostCall()
{
    auto &t = threads[current_thread];
    if (t.host_resume_at >= t.host_frame_count)
    {
        t.host_resume = false;
        Fail("A thread of a saved state has no record of the call it stood in");
        return 0;
    }
    const unsigned frame_at = t.host_resume_at;
    const Thread::HostFrame frame = t.host_frames[frame_at];
    switch (frame.kind)
    {
    case HostModal:
        // A caller with no work of its own after the loop.
        return RunModal(frame.a, true);
    case HostDialogBox:
    {
        ++t.host_resume_at;
        unsigned hwnd = t.host_resume_at < t.host_frame_count ? t.host_frames[t.host_resume_at].a : 0;
        unsigned result = 0;
        if (frame.d == 1)
        {
            hwnd = ResumeHostCall();
            if (auto *dialog = FindDialog(hwnd)) dialog->restore_owner = frame.a;
            t.host_frames[frame_at].d = 0;
            result = frame.c ? (hwnd ? RunModal(hwnd, true) : ~0u) : hwnd;
        }
        else result = RunModal(hwnd, true);
        PopHostFrame();
        if (frame.a && User::IsWindow(frame.b)) { const unsigned enable[] = {frame.b, 1}; Api("EnableWindow", enable); }
        return result;
    }
    case HostMessageBox:
    {
        ++t.host_resume_at;
        return FinishMessageBox(nullptr, RunModal(frame.a, true));
    }
    case HostPropertyApi:
        return ResumePropertyApi(frame.a,frame.b!=0);
    case HostPropertySheet:
        return ResumePropertySheet(frame.a);
    case HostWin16Sheet:
        ++t.host_resume_at;
        return FinishWin16PropertySheet(frame.a, ResumeInner(), true);
    case HostWin16PageDestroy:
        ++t.host_resume_at;
        ResumeInner();
        PopHostFrame();
        Win16GlobalFree(frame.a >> 16);
        return 1;
    case HostWin16PageProcedure:
        ++t.host_resume_at;
        return ResumeWin16PropertyPage(frame.a, frame.b, ResumeInner());
    case HostSerialDialog:
        return ResumeSerialUiDialog(frame.a, false);
    case HostSerialAdvanced:
        return ResumeSerialUiDialog(frame.a, true);
    case HostCommConfig:
    {
        ++t.host_resume_at;
        const unsigned result = ResumeInner();
        PopHostFrame();
        return FinishCommConfigDialog(result, frame.a);
    }
    case HostSerialMessage:
        ++t.host_resume_at;
        ResumeInner();
        PopHostFrame();
        return 1;
    case HostPropertyPage:
        return ResumePropertyPage(frame.a,frame.b!=0);
    case HostPropertyCollect:
        return ContinuePropertyCollect(frame.a,frame.data[0],frame.data[1]!=0,true);
    case HostEndTask:
    {
        ++t.host_resume_at;
        const unsigned result = ResumeInner();
        PopHostFrame();
        return FinishEndTask(result, frame.a);
    }
    case HostEndTaskDialog:
    {
        ++t.host_resume_at;
        const unsigned result = RunModal(frame.a, true);
        PopHostFrame();
        return result == 2 || result == 3 || result == 4 ? result : 3;
    }
    case HostCloseProgram:
    {
        ++t.host_resume_at;
        const unsigned answer = ResumeInner();
        if (frame_at < t.host_frame_count) t.host_frames[frame_at].d = 2;
        FinishCloseProgramDialog(answer, frame.b);
        PopHostFrame();
        return 0;
    }
    case HostCloseProgramMessage:
    {
        ++t.host_resume_at;
        const unsigned answer = ResumeInner();
        PopHostFrame();
        if (answer == 3 && state != Failed && state != Exited) EndDialog(frame.a, 0);
        return 1;
    }
    case HostPifSheet:
    {
        ++t.host_resume_at;
        const unsigned result = ResumeInner();
        if (frame_at < t.host_frame_count) t.host_frames[frame_at].d = 2;
        FinishPifPropertySheet(frame.a);
        PopHostFrame();
        return result;
    }
    case HostPifAdvanced:
    {
        ++t.host_resume_at;
        const unsigned result = RunModal(frame.a, true);
        FinishPifAdvancedDialog();
        PopHostFrame();
        return result;
    }
    case HostPifAdvancedMessage:
        ++t.host_resume_at;
        ResumeInner();
        FinishPifAdvancedMessage(frame.a, frame.b);
        PopHostFrame();
        return 1;
    case HostDosToolbar:
        ++t.host_resume_at;
        ResumeInner();
        FinishDosVmToolbar(frame.a);
        PopHostFrame();
        return 0;
    case HostSettingsApi:
    {
        ++t.host_resume_at;
        ResumeInner();
        unsigned args[5] = {};
        const bool peek = Read32(frame.a) != 0;
        Read(frame.a + 4, args, sizeof args);
        Free(frame.a);
        PopHostFrame();
        return state == Failed || state == Exited ? 0 : Api(peek ? "PeekMessageA" : "GetMessageA", args);
    }
    case HostPropertyReleaseInput:
        return ContinuePropertyReleaseInput(frame.a,true);
    case HostPropertyNotify:case HostPropertyPageWindow:case HostPropertyActivate:
    case HostPropertyInit:case HostPropertyRelease:case HostPropertyProcedure:
    case HostPropertyModalFinish:case HostPropertyNonClient:case HostPropertyRemove:case HostPropertyCommand:case HostPropertyApply:case HostPropertyQuery:case HostPropertyAdd:case HostPropertyPageNonClient:case HostPropertyRemoveRequest:
        return ResumePropertyWork(frame.kind,frame_at);
    case HostDialogProcedure:
        return DialogProcedure(frame.a, frame.b, frame.c, frame.d);
    case HostPopupMenu:
    {
        ++t.host_resume_at;
        const unsigned result = RunMenuLoop();
        PopHostFrame();
        return User::FinishPopupTracking(result, frame.a);
    }
    case HostMenu:
        return RunMenuLoop();
    case HostBarMenu:
        ++t.host_resume_at;
        if (frame.a == 2)
        {
            ResumeInner();
            User::MenuKeyFlags(frame.b, 0x18, 0);
            PopHostFrame();return 0;
        }
        return FinishBarMenu(RunMenuLoop(), frame.a != 0);
    case HostSizeMove:
    {
        ++t.host_resume_at;
        const unsigned result = RunFrameTrackLoop();
        PopHostFrame();
        return result;
    }
    case HostFrameTrack:
        return RunFrameTrackLoop();
    case HostWin16HostedLoad:
        ++t.host_resume_at; return ResumeWin16HostedLoad(frame.a);
    case HostWin16HostedLibrary:
        return ResumeWin16HostedLibrary(frame.a);
    case HostWin16HostedEnter:
        return ResumeWin16HostedEnter(frame.a, frame.b);
    case HostWin16HostedCall:
        ++t.host_resume_at; return ResumeWin16HostedCall(t.host_resume_at - 1);
    case HostWin16FileMessage:case HostWin16FileAccept:case HostWin16FileOutput:case HostWin16FileSelect:case HostWin16FileDrive:
        return ResumeWin16FileWork(frame.kind,frame_at);
    case HostWin16Controller:
        return ResumeWin16ControllerPhase();
    case HostWin16PrintApi:
    {
        ++t.host_resume_at;
        const unsigned result = ResumeHostCall();
        PopHostFrame();
        return FinishWin16PrintApi(frame.a, frame.b, result);
    }
    case HostWin16Print:
    {
        if ((frame.b & 0xffff0000u) == 0x80000000u)
        {
            const auto *dialog = FindDialog(frame.a);
            return Win16PrintDialogMessage(frame.a, frame.b & 0xffff, frame.c, frame.d,
                dialog && dialog->procedure == 0x7100004a);
        }
        ++t.host_resume_at;
        if (frame.d == 1) return FinishWin16PrintSetup(frame.a, ResumeHostCall());
        unsigned hwnd = frame.a;
        if (frame.d == 2)
        {
            hwnd = ResumeHostCall();
            t.host_frames[frame_at].a = hwnd;
            t.host_frames[frame_at].d = 0;
            FinishWin16ChoicePreparation(hwnd, frame.kind);
        }
        const unsigned result = hwnd ? RunModal(hwnd, true) : 0xffffffffu;
        PopHostFrame();
        return FinishWin16PrintDialog(hwnd, frame.b != 0, result);
    }
    case HostWin16Color:
    case HostWin16Font:
    {
        if ((frame.b & 0xffff0000u) == 0x80000000u)
            return frame.kind == HostWin16Color ? Win16ColorDialogMessage(frame.a, frame.b & 0xffff, frame.c, frame.d) :
                Win16FontDialogMessage(frame.a, frame.b & 0xffff, frame.c, frame.d);
        ++t.host_resume_at;
        unsigned hwnd = frame.a;
        bool entered = hwnd != 0;
        if (frame.b == 1)
        {
            hwnd = ResumeHostCall();
            t.host_frames[frame_at].a = hwnd;
            t.host_frames[frame_at].b = 0;
            entered = FinishWin16ChoicePreparation(hwnd, frame.kind);
        }
        const unsigned result = entered ? RunModal(hwnd, true) : frame.kind == HostWin16Color ? 0 : 0xffffffffu;
        PopHostFrame();
        return frame.kind == HostWin16Color ? FinishWin16ColorDialog(hwnd, result) : FinishWin16FontDialog(hwnd, result);
    }
    case HostFileDialog:
    {
        ++t.host_resume_at;
        unsigned hwnd = frame.a;
        if (t.host_resume_at < t.host_frame_count && t.host_frames[t.host_resume_at].kind == HostDialogInit) {
            hwnd=ResumeHostCall();if(frame.b)FinishWin16FilePreparation(hwnd);
        }
        t.host_frames[frame_at].a = hwnd;t.host_frames[frame_at].d=0;
        const unsigned result = hwnd ? RunModal(hwnd, true) : 0;
        PopHostFrame();
        return frame.b ? FinishWin16FileDialog(hwnd, result, hwnd != 0) : FinishFileDialog(hwnd, result);
    }
    case HostDialogInit:
    {
        ++t.host_resume_at;
        const unsigned result = ResumeInner();
        PopHostFrame();
        return FinishDialogInit(frame.a, frame.b, frame.c != 0, result);
    }
    case HostCreateWindow:
        return ContinueCreateWindow(true);
    case HostDispatchPaint:
        return DispatchPaint(frame.a, frame.b, frame.c, true);
    case HostWin16Create:
    {
        ++t.host_resume_at;
        const unsigned hwnd = ResumeInner();
        PopHostFrame();
        if (hwnd && frame.a) Win16Send(hwnd, 0xc5, frame.a, 0);
        return hwnd;
    }
    case HostWin16Delivery:
    {
        ++t.host_resume_at;
        return FinishWin16Delivery(frame_at, ResumeInner());
    }
    case HostSentWait:
        return ResumeSentWait(frame.a, frame.b, frame.c, frame.d);
    case HostSentDelivery:
        return ResumeSentDelivery(frame.a, frame.b, frame.c, frame.d);
    case HostSendTimeout:
    {
        ++t.host_resume_at;
        const unsigned result = ResumeInner();
        const bool completed = !frame.b || t.sent_completed;
        PopHostFrame();
        return FinishSendTimeout(frame.a, result, completed);
    }
    case HostSendCopy:
    {
        ++t.host_resume_at;
        const unsigned result = ResumeInner();
        PopHostFrame();
        return FinishSendCopy(frame.a, frame.b, result, frame.c, frame.d);
    }
    case HostCrtThread:
    {
        ++t.host_resume_at;
        const unsigned result = ResumeInner();
        PopHostFrame();
        FinishCrtThread(result, frame.a != 0);
        return result;
    }
    default:
        t.host_resume = false;
        Fail("A thread of a saved state stood in a call that cannot be taken up");
        return 0;
    }
}
unsigned Application::RunModal(unsigned hwnd, bool recorded)
{
    auto *dialog = FindDialog(hwnd);
    if (!dialog)
        return 0xffffffff;
    // Out of a saved state: the loop was running already. Its record gives
    // back what it had in its hands, and everything up to the loop has
    // happened long ago.
    const unsigned thread_index = current_thread;
    bool resumed = false;
    unsigned owner = 0, focus = 0;
    bool disable = false, pushed = false;
    {
        auto &t = threads[thread_index];
        if (t.host_resume && t.host_resume_at < t.host_frame_count && t.host_frames[t.host_resume_at].kind == HostModal &&
            t.host_frames[t.host_resume_at].a == hwnd)
        {
            const auto &frame = t.host_frames[t.host_resume_at];
            owner = frame.b;
            focus = frame.c;
            disable = (frame.d & 1) != 0;
            resumed = pushed = true;
            if (++t.host_resume_at >= t.host_frame_count) t.host_resume = false;
            // The loop was not waiting but had handed a message out: that
            // comes back first (Application::ResumeInner).
            if (t.host_resume_level < t.host_resume_top || t.host_resume)
            {
                ResumeInner();
                dialog = FindDialog(hwnd);
                if (!dialog)
                {
                    PopHostFrame();
                    return 0xffffffff;
                }
            }
        }
    }
    if (!resumed)
    {
    // EVENT_SYSTEM_DIALOGSTART when the modal loop takes over.
    if (User::WinEventListening())
    {
        WinEventNotify(0x10, hwnd, 0, 0, false);
        dialog = FindDialog(hwnd);
        if (!dialog)
            return 0xffffffff;
    }
    if (log)
    {
        char trace[128];
        snprintf(trace, sizeof trace, "modal dialog begin hwnd=%u owner=%u", hwnd, dialog->owner);
        log(trace);
        // WM_INITDIALOG has run by now, so these are the final rectangles. A
        // property sheet grows and moves its frame there and shifts its
        // buttons, and a message box renumbers its sole OK button, so the
        // CreateWindow trace lines record neither the final corner nor the
        // final control id. Consumers that want to press a button read these.
        Gdi::RECT r{};
        if (User::GetWindowRect(hwnd, &r))
        {
            snprintf(trace, sizeof trace, "modal dialog rect hwnd=%u x=%d y=%d w=%d h=%d", hwnd, r.left,
                     r.top, r.right - r.left, r.bottom - r.top);
            log(trace);
        }
        for (unsigned child = User::GetWindow(hwnd, 5); child; child = User::GetWindow(child, 2))
            // The frame is still hidden here, so ask for the child's own
            // WS_VISIBLE: that is the bit a sheet clears for Apply and Help.
            if (User::IsButton(child) && (User::Style(child) & 0x10000000) && User::GetWindowRect(child, &r))
            {
                snprintf(trace, sizeof trace, "modal dialog button hwnd=%u id=%u x=%d y=%d w=%d h=%d", child,
                         User::ControlID(child), r.left, r.top, r.right - r.left, r.bottom - r.top);
                log(trace);
            }
    }
    dialog->modal = true;
    // A modal dialog ends mouse tracking in its owner's controls. Otherwise
    // a trackbar's repeat timer runs inside the nested message loop and can
    // reopen the very message box its scroll notification just displayed.
    const unsigned captured = User::CaptureOperation(0);
    unsigned capture_owner = captured;
    while (capture_owner && capture_owner != dialog->owner) capture_owner = User::Parent(capture_owner);
    if (captured && dialog->owner && capture_owner == dialog->owner)
    {
        Send(captured, 0x1f, 0, 0); // WM_CANCELMODE lets the control stop its timer.
        dialog = FindDialog(hwnd);
        if (!dialog) return 0xffffffff;
        if (User::CaptureOperation(0) == captured) User::CaptureOperation(2);
    }
    // USER drains messages posted by WM_INITDIALOG before showing the modal
    // frame. A dialog can finish in that interval without ever appearing.
    dialog->show_pending = !(User::Style(hwnd) & 0x10000000u);
    dialog = FindDialog(hwnd);
    if (!dialog)
        return 0xffffffff;
    owner = dialog->owner;
    focus = dialog->previous_focus;
    disable = owner && User::IsEnabled(owner);
    if (disable)
        { const unsigned enable[] = {owner, 0}; Api("EnableWindow", enable); }
    pushed = PushHostFrame(HostModal, hwnd, owner, focus, (disable ? 1u : 0u) | (recorded ? 2u : 0u));
    }
    while (!dialog->ended && state != Failed && state != Exited)
    {
        User::Message message = {};
        if (!ModalMessage(message))
        {
            if (dialog->ended || state == Failed || state == Exited) break;
            if (dialog->show_pending)
            {
                dialog->show_pending = false;
                ShowWindow(hwnd, 1);
                dialog = FindDialog(hwnd);
                if (!dialog) break;
                continue;
            }
            // Here a saved state can take the thread: the loop waits, and
            // nothing of the host lies between it and the wait.
            threads[thread_index].host_waiting = pushed;
            Suspend(Idle);
            threads[thread_index].host_waiting = false;
            dialog = FindDialog(hwnd);
            if (!dialog) break;
            continue;
        }
        MessageHook(3, 0, 1, message);
        if (dialog->ended || state == Failed || state == Exited)
            break;
        if (message.message == 0x12)
        {
            User::PostMessage(0, 0x12, message.wparam, 0);
            break;
        }
        // What the loop hands out is the last thing a turn does, so a saved
        // state can take the thread inside the procedure it reaches
        // (GiveTail).
        GiveTail(pushed ? TailFresh : TailNone);
        const bool taken = DialogMessage(hwnd, message);
        GiveTail(TailNone);
        if (!taken)
        {
            User::TranslateMessage(&message);
            GiveTail(pushed ? TailRecord : TailNone);
            Send(message.hwnd, message.message, message.wparam, message.lparam);
        }
        dialog = FindDialog(hwnd);
        if (!dialog) break;
    }
    if(pushed&&recorded&&dialog&&PropertySheetWindow(hwnd)){
        const unsigned at=threads[thread_index].host_frame_count-1;
        auto&frame=threads[thread_index].host_frames[at];frame.kind=HostPropertyModalFinish;
        frame.d=1;frame.data[0]=dialog->result;frame.data[1]=dialog->font;frame.data[2]=disable;
        GiveTail(TailNone);
        if(User::IsWindow(hwnd))Api("DestroyWindow",&hwnd);
        frame.d=2;return ResumePropertyWork(HostPropertyModalFinish,at);
    }
    if (pushed) PopHostFrame();
    if (!dialog)
        return 0xffffffff;
    unsigned result = dialog->result, font = dialog->font;
    if (User::IsWindow(hwnd))
    {
        unsigned args[] = {hwnd};
        Api("DestroyWindow", args);
    }
    *dialog = Dialog{};
    if (font && font != system && font != fixed)
    {
        unsigned args[] = {font};
        Api("DeleteObject", args);
    }
    if (disable)
        { const unsigned enable[] = {owner, 1}; Api("EnableWindow", enable); }
    if (User::IsWindow(focus))
        SetFocus(focus);
    else if (owner && User::IsWindow(owner))
        SetFocus(owner);
    // EVENT_SYSTEM_DIALOGEND as the loop is left.
    if (User::WinEventListening())
        WinEventNotify(0x11, hwnd, 0, 0, false);
    if (log)
    {
        char trace[96];
        snprintf(trace, sizeof trace, "modal dialog end result=%u", result);
        log(trace);
    }
    return result;
}
// WM_GETDLGCODE with the MSG of IsDialogMessage. USER supplies
// the actual MSG; the buffer lives across the callback, and what the control
// writes into it is the message from then on.
unsigned Application::DialogCode(unsigned hwnd, User::Message &message)
{
    const unsigned scratch = Allocate(sizeof message);
    if (!scratch) { Fail("Dialog key message allocation failed"); return 0; }
    Store(scratch, &message, sizeof message);
    const unsigned code = Send(hwnd, 0x87, message.wparam, scratch);
    if (state != Failed) Read(scratch, &message, sizeof message);
    Free(scratch);
    return code;
}
// USER: a control that has DLGC_HASSETSEL gets its whole text
// selected (EM_SETSEL 0, -1) before it takes the focus.
void Application::DialogSetFocus(unsigned hwnd)
{
    if (Send(hwnd, 0x87, 0, 0) & 8)
        Send(hwnd, 0xb1, 0, 0xffffffffu);
    if (User::IsWindow(hwnd) && state != Failed && state != Exited)
        SetFocus(hwnd);
}
// USER after the focus went from old to now: the default frame follows
// a push button that gets the focus and goes back to the button of
// DM_GETDEFID when the focus leaves the push buttons. Windows 98 takes
// BS_DEFPUSHBUTTON from every push button of the dialog first.
void Application::DialogDefaultButton(unsigned dialog, unsigned old, unsigned now)
{
    const auto alive = [&] { return state != Failed && state != Exited && User::IsWindow(dialog); };
    const auto code = [&](unsigned h) { return h && User::IsWindow(h) ? Send(h, 0x87, 0, 0) : 0u; };
    const auto clear = [&](unsigned from) {
        const unsigned start = from && !(User::ExtendedStyle(from) & 0x10000u)
                                   ? User::DialogChildControl(dialog, from)
                                   : User::DialogNextControl(dialog, 0, 3);
        unsigned control = start;
        for (unsigned n = 0; control && n < 1024 && alive(); ++n)
        {
            if (code(control) & 0x10)
                Send(control, 0xf4, 0, 1);
            control = User::DialogNextControl(dialog, control, 0);
            if (control == start) break;
        }
    };
    const auto default_id = [&] {
        const unsigned answer = Send(dialog, 0x400, 0, 0);
        return (answer >> 16) == 0x534b ? answer & 0xffff : 1u;
    };
    unsigned now_code = 0;
    if (now)
    {
        if (User::ExtendedStyle(now) & 0x10000u) return;
        now_code = code(now);
    }
    if (now == old)
    {
        if (!(now_code & 0x20)) return;
        const unsigned id = default_id();
        if (alive() && id != (User::ControlID(now) & 0xffff))
        {
            const unsigned button = User::GetDlgItem(dialog, id);
            if (button && (code(button) & 0x10)) clear(button);
        }
        if (alive() && User::IsWindow(now)) Send(now, 0xf4, 1, 1);
        return;
    }
    if ((old && (code(old) & 0x30)) || (now && (now_code & 0x30)))
        clear(now);
    if (!alive()) return;
    if (now_code & 0x20)
    {
        if (User::IsWindow(now)) Send(now, 0xf4, 1, 1);
        return;
    }
    const unsigned button = User::GetDlgItem(dialog, default_id());
    if (!button || !alive()) return;
    const unsigned button_code = code(button);
    if (!(button_code & 0x10) && (button_code & 0x20) && !(User::Style(button) & 0x08000000u))
        Send(button, 0xf4, 1, 1);
}
// USER: the next control whose text carries the mnemonic. The group
// of the control with the focus comes first (GetNextDlgGroupItem, at most 60
// steps), then every control of the dialog after it, the control itself
// last. A control that wants characters has no mnemonic, nor has a static
// control with SS_NOPREFIX. Windows 98 compares only the first '&' that is not
// "&&", after AnsiLower on both sides.
unsigned Application::DialogFindMnemonic(unsigned dialog, unsigned hwnd, unsigned character)
{
    const auto lower = [&](unsigned c) { return CharacterCaseApi("CharLowerA", &c) & 0xff; };
    const unsigned wanted = lower(character & 0xff);
    const auto matches = [&](unsigned control) {
        if (!control || !User::IsWindow(control)) return false;
        const unsigned code = Send(control, 0x87, 0, 0);
        if (state == Failed || state == Exited || !User::IsWindow(control)) return false;
        if (code & 0x80) return false;
        if ((code & 0x100) && (User::Style(control) & 0x80)) return false;
        char text[256]{};
        User::GetWindowText(control, text, sizeof text);
        const char *p = text;
        while (*p == ' ') ++p;
        while (*p)
        {
            if (lower((unsigned char)*p++) != '&') continue;
            const unsigned next = lower((unsigned char)*p);
            if (next == '&') { ++p; continue; }
            return next == wanted;
        }
        return false;
    };
    unsigned control = User::DialogChildControl(dialog, hwnd);
    for (unsigned n = 1; state != Failed && state != Exited; ++n)
    {
        control = User::NextDialogItem(dialog, control, false, true);
        if (!control || control == hwnd || n > 60) break;
        if (matches(control)) return control;
    }
    const unsigned start = User::DialogChildControl(dialog, hwnd);
    control = start;
    for (unsigned n = 0; n < 1024 && state != Failed && state != Exited; ++n)
    {
        control = User::DialogNextControl(dialog, control, 3);
        if (control != dialog && matches(control)) return control;
        if (control == start || !control) break;
    }
    return 0;
}
// USER: go to the control of a mnemonic. A static control hands on
// to the next tab stop; a disabled control is passed over; a push button is
// flashed and clicked without the focus (WM_COMMAND BN_CLICKED to its parent,
// answer 1); any other button gets the focus and is clicked when no other
// control has the mnemonic; everything else only gets the focus.
unsigned Application::DialogGotoMnemonic(unsigned dialog, unsigned hwnd, unsigned character)
{
    const bool recorded = TakeFresh();
    const auto alive = [&] { return state != Failed && state != Exited && User::IsWindow(dialog); };
    unsigned first = 0, control = hwnd;
    for (unsigned n = 0; n < 0x200 && alive(); ++n)
    {
        control = DialogFindMnemonic(dialog, control, character);
        if (!control || !alive()) return 0;
        unsigned code = Send(control, 0x87, 0, 0);
        if ((code & 0x100) && !(User::Style(control) & 0x08000000u))
        {
            control = User::NextDialogItem(dialog, control, false);
            if (!control) return 0;
            code = Send(control, 0x87, 0, 0);
        }
        if (!alive() || !User::IsWindow(control)) return 0;
        if (User::Style(control) & 0x08000000u)
        {
            if (control == first) return 0;
            if (!first) first = control;
            continue;
        }
        if (!(code & 0x2000))
        {
            DialogSetFocus(control);
            return control;
        }
        if (code & 0x30)
        {
            Send(control, 0xf3, 1, 0);
            if (User::IsWindow(control)) Send(control, 0xf3, 0, 0);
            const unsigned parent = User::Parent(control);
            if (parent && User::IsWindow(control))
            {
                if (recorded) GiveTail(TailRecord);
                Send(parent, 0x111, User::ControlID(control) & 0xffff, control);
            }
            return 1;
        }
        DialogSetFocus(control);
        if (alive() && User::IsWindow(control) && DialogFindMnemonic(dialog, control, character) == control)
            Send(control, 0xf5, 1, 0);
        return control;
    }
    return 0;
}
// USER (WM_SYSCHAR) and (WM_CHAR and the rest of WM_SYSCHAR).
bool Application::DialogCharacter(unsigned hwnd, User::Message &message)
{
    const bool recorded = TakeFresh();
    const bool system = message.message == 0x106;
    const auto beep = [&] { const unsigned type = 0; Api("MessageBeep", &type); };
    // hands the message to DefWindowProc of the dialog,
    // which makes WM_SYSCOMMAND SC_KEYMENU of it.
    const auto dialog_default = [&] { DefaultWindowMessage(hwnd, message.message, message.wparam, message.lparam); };
    if (system)
    {
        if (!User::GetFocus())
        {
            const unsigned menu_key = 0x12;
            if (!(Api("GetKeyState", &menu_key) & 0x8000u))
            {
                if (message.wparam == 13 && (User::Style(hwnd) & 0x20000000u)) dialog_default();
                else beep();
                return true;
            }
        }
        if (message.wparam == ' ') { dialog_default(); return true; }
    }
    if (message.hwnd == hwnd) return true;
    const unsigned code = DialogCode(message.hwnd, message);
    if (state == Failed || state == Exited || !User::IsWindow(hwnd)) return true;
    if (!system && (code & 0x84)) return false;
    if (message.wparam == 9 && (code & 2)) return false;
    if (message.wparam == ' ') return true;
    if (recorded) GiveTail(TailFresh);
    const unsigned found = DialogGotoMnemonic(hwnd, message.hwnd, message.wparam & 0xff);
    if (state == Failed || state == Exited || !User::IsWindow(hwnd)) return true;
    if (!found)
    {
        if (code & 4) return false;
        if (system) dialog_default();
        else beep();
        return true;
    }
    if (found != 1) DialogDefaultButton(hwnd, message.hwnd, found);
    return true;
}
bool Application::DialogMessage(unsigned hwnd, User::Message &message)
{
    // Out of the modal loop: the command of Enter and Escape is the last
    // thing the loop does with the message (GiveTail).
    const bool tail = TakeFresh();
    if (!User::IsWindow(hwnd))
        return false;
    if (FilterMessage(message, 0)) // MSGF_DIALOG; callbacks can alter or consume the message.
        return true;
    if (state == Failed || state == Exited || !User::IsWindow(hwnd))
        return true;
    unsigned root = message.hwnd;
    for (unsigned n = 0; root && root != hwnd && n < 192; ++n)
        root = User::Parent(root);
    if (root != hwnd)
        return false;
    if (message.message == 0x102 || message.message == 0x106)
    {
        if (tail) GiveTail(TailFresh);
        return DialogCharacter(hwnd, message);
    }
    if (message.message == 0x100)
    {
        // A property sheet takes the keys that change the page before the
        // dialog manager sees them (COMCTL32 Prsht_IsDialogMessage, the body
        // of PSM_ISDIALOGMESSAGE): Ctrl+Tab and Ctrl+Page Down show the next
        // page, Ctrl+Shift+Tab and Ctrl+Page Up the one before; a wizard has
        // no tabs and keeps the keys.
        if ((message.wparam == 9 || message.wparam == 0x21 || message.wparam == 0x22) &&
            (User::GetKeyState(0x11) & 0x8000) && FindDialog(hwnd) && FindDialog(hwnd)->procedure == 0x71000011)
        {
            const unsigned tab = User::GetDlgItem(hwnd, 0x3020);
            const unsigned count = tab && User::IsVisible(tab) ? Send(tab, 0x1304, 0, 0) : 0;
            if (count && count < 100)
            {
                const unsigned current = Send(tab, 0x130b, 0, 0);
                const bool back = message.wparam == 0x21 || (message.wparam == 9 && (User::GetKeyState(0x10) & 0x8000));
                Send(hwnd, 0x465, (current + (back ? count - 1 : 1)) % count, 0);
                return true;
            }
        }
        // The original dispatches requested keys itself; our modal loop does so
        // when this function returns false.
        const unsigned code = DialogCode(message.hwnd, message);
        if (state == Failed || state == Exited || !User::IsWindow(hwnd)) return true;
        if (code & 4) return false;
        if(message.wparam==0x70&&MessageBoxWindow(hwnd)){CallWindow(hwnd,0x53,0,0);return true;}
        if (message.wparam == 9)
        {
            if (code & 2) return false; // USER, DLGC_WANTTAB.
            // the next tab stop gets the focus, then the
            // default frame follows.
            const unsigned old = message.hwnd;
            unsigned next = User::NextDialogItem(hwnd, old, (User::Modifiers() & 1) != 0);
            if (next)
                DialogSetFocus(next);
            if (state == Failed || state == Exited || !User::IsWindow(hwnd)) return true;
            if (!User::IsWindow(next)) next = 0;
            DialogDefaultButton(hwnd, User::IsWindow(old) ? old : 0, next);
            return true;
        }
        if (message.wparam >= 0x25 && message.wparam <= 0x28)
        {
            if (code & 1) return false; // USER, DLGC_WANTARROWS.
            // the next control of the group. A push button
            // or any control but a static one gets the focus; a radio button
            // too, and it is clicked when the focus came from an automatic
            // radio button and it is not checked yet.
            const unsigned old = message.hwnd;
            const unsigned next = User::NextDialogItem(hwnd, old,
                                                       message.wparam == 0x25 || message.wparam == 0x26, true);
            if (!next)
                return false;
            const unsigned next_code = DialogCode(next, message);
            if (state == Failed || state == Exited || !User::IsWindow(hwnd) || !User::IsWindow(next)) return true;
            if (!(next_code & 0x70) && (next_code & 0x100))
                return true;
            DialogSetFocus(next);
            if (state == Failed || state == Exited || !User::IsWindow(hwnd)) return true;
            DialogDefaultButton(hwnd, old, next);
            if ((next_code & 0x70) == 0x40 && (User::Style(old) & 15) == 9 && User::IsWindow(next) &&
                !Send(next, 0xf0, 0, 0))
                Send(next, 0xf5, 1, 0);
            return true;
        }
        if (message.wparam == 13 || message.wparam == 27)
        {
            const unsigned def = message.wparam == 13 ? Send(hwnd, 0x400, 0, 0) : 0;
            if (state == Failed || state == Exited || !User::IsWindow(hwnd)) return true;
            unsigned id = message.wparam == 27 ? 2 : (def >> 16) == 0x534b ? def & 0xffffu : 1;
            // Enter presses the focus window only when that is a push button
            // (DLGC_DEFPUSHBUTTON or DLGC_UNDEFPUSHBUTTON); with a radio
            // button or a check box in the focus it is the default button's
            // (the hidden radio button of SYSDM's "Confirm Device Removal",

            if (message.wparam == 13 && User::IsButton(message.hwnd) && (code & 0x30))
                id = User::ControlID(message.hwnd);
            unsigned button = User::GetDlgItem(hwnd, id);
            if ((!button && message.wparam == 27) ||
                (User::IsEnabled(button) && (User::Style(button) & 0x10000000u)))
            {
                if (tail) GiveTail(TailRecord);
                CallWindow(hwnd, 0x111, id, button);
            }
            return true;
        }
    }
    return false;
}
unsigned Application::DialogProcedure(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    // In a tail position (GiveTail) the call keeps a record, and a saved
    // state can take the thread inside the procedure of the program: what
    // is left after it is done here again, with the answer the procedure
    // gives then.
    const unsigned tail = TakeTail();
    auto *dialog = FindDialog(hwnd);
    // DefDlgProc also serves classes derived from #32770 and windows made
    // directly with CreateWindow. They have DLGWINDOWEXTRA but no template.
    if (!dialog && User::IsWindow(hwnd) && User::ClassAccess(hwnd, -18, 0, 2) >= 30)
    {
        dialog = dialogs.Take();
        if (dialog)
        {
            *dialog = Dialog{};
            dialog->hwnd = hwnd;
            dialog->owner = User::Owner(hwnd);
            dialog->font = system;
            dialog->default_id = 0;
            User::WindowLong(hwnd, 4, dialog->procedure, false);
        }
    }
    if (!dialog) return DefaultWindowMessage(hwnd,message,wp,lp);
    bool resumed = false, pushed = false;
    {
        auto &t = threads[current_thread];
        if (t.host_resume && t.host_resume_at < t.host_frame_count &&
            t.host_frames[t.host_resume_at].kind == HostDialogProcedure && t.host_frames[t.host_resume_at].a == hwnd)
        {
            resumed = pushed = true;
            if (++t.host_resume_at >= t.host_frame_count) t.host_resume = false;
        }
        // A procedure of the port (a token) is no callback.
        else if ((tail == TailResult || tail == TailRecord) && dialog->procedure &&
                 ((dialog->procedure & 0xffff0000u) != 0x71000000u || Win16IsMark(dialog->procedure) ||
                  (dialog->procedure == 0x71000044 && Win16Process()) ||
                  (dialog->procedure == 0x71000013 && message == 0x111 && (wp & 65535) == 0x423) ||
                  Win16ChoiceDispatchResumable(hwnd, dialog->procedure, message, wp) ||
                  (dialog->procedure==0x71000043&&Win16FileDispatchResumable(hwnd,message)) ||
                  (dialog->procedure == 0x71000011 && PropertySheetRecordable(hwnd, message, wp, lp)) ||
                  (dialog->procedure == 0x71000041 && message == 0x111 && wp == 4008) ||
                  (dialog->procedure == CloseProgramDialogProcedure && message == 0x111 &&
                   ((wp & 65535) == 1 || ((wp & 65535) == 3 && (wp >> 16) == 2)))))
            pushed = PushHostFrame(HostDialogProcedure, hwnd, message, wp, lp);
    }
    struct Record { Application &app; bool pushed; ~Record() { if (pushed) app.PopHostFrame(); } } record{*this, pushed};
    struct E {
        Application &app; unsigned hwnd,message,wp,lp,procedure; bool resumed,pushed;
        // Before the procedure only; that has happened in a call taken up again.
        void Result(unsigned value) { if(!resumed)User::WindowLong(hwnd,0,value,true); }
        unsigned Result() { unsigned value=0;User::WindowLong(hwnd,0,value,false);return value; }
        bool HasProcedure() { return procedure!=0; }
        bool Alive() { return User::IsWindow(hwnd); }
        unsigned Call() {
            unsigned arguments[]={hwnd,message,wp,lp};
            if(pushed&&!resumed)app.GiveTail(TailRecord);
            const unsigned answer=
                   resumed?app.ResumeInner()
                 : procedure==0x71000010?app.FindProcedure(hwnd,message,wp,lp)
                 : procedure==0x71000011?app.PropertySheetProcedure(hwnd,message,wp,lp)
                 : procedure==0x71000012?app.FileDialogMessage(hwnd,message,wp,lp)
                 : procedure==0x71000043?app.Win16FileDialogMessage(hwnd,message,wp,lp)
                 : procedure==0x71000044?app.Win16PropertyPageMessage(hwnd,message,wp,lp)
                 : procedure==0x71000045?app.Win16ColorDialogMessage(hwnd,message,wp,lp)
                 : procedure==Application::Win16FontDialogToken?app.Win16FontDialogMessage(hwnd,message,wp,lp)
                 : procedure==0x71000049?app.Win16PrintDialogMessage(hwnd,message,wp,lp,false) // COMMDLG
                 : procedure==0x7100004a?app.Win16PrintDialogMessage(hwnd,message,wp,lp,true)  // COMMDLG
                 : procedure==0x71000058?app.FaultBoxMessage(hwnd,message,wp,lp)
                 : procedure==0x71000013?app.SerialUiMessage(hwnd,message,wp,lp)
                 : procedure==0x71000014?app.SerialUiAdvancedMessage(hwnd,message,wp,lp)
                 : procedure==0x71000041?app.PifPageMessage(hwnd,message,wp,lp)
                 : procedure==0x71000042?app.PifAdvancedMessage(hwnd,message,wp,lp)
                 : procedure==EndTaskDialogProcedure?app.EndTaskDialogMessage(hwnd,message,wp,lp)
                 : procedure==CloseProgramDialogProcedure?app.CloseProgramDialogMessage(hwnd,message,wp,lp)
                 : app.Call(procedure,arguments,4);
            // A 16-bit dialog procedure returns BOOL in AX only. Ignore the
            // upper DX word returned by the guest call; stale DX must not
            // turn an unhandled message into a handled one.
            return Win16IsMark(procedure) ? (answer & 0xffff) : answer;
        }
        unsigned Default() { return app.DialogDefault(hwnd,message,wp,lp); }
    } e{*this,hwnd,message,wp,lp,dialog->procedure,resumed,pushed};
    return Win98DialogDispatch::Procedure(e,message>=0x132&&message<=0x138?0x19:message);
}
unsigned Application::DialogDefault(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    unsigned box_result=0;if(MessageBoxMessage(hwnd,message,wp,lp,box_result))return box_result;
    if(message==0xc||message==0xd||message==0xe)return WindowTextDefault(hwnd,message,wp,lp);
    auto *dialog = FindDialog(hwnd);
    if (!dialog)
        return User::DefaultMessage(hwnd, message, wp, lp);
    if (message == 0x110)
        return 1;
    if (message == 6 || message == 7)
    {
        // USER reads the dialog task's queue focus. During a
        // foreground handoff the shared foreground record can still name
        // the departing queue's window; it is not this dialog's old control.
        const auto queue_focus = [&]() {
            const unsigned queue = User::WindowThread(hwnd);
            const unsigned focus = User::ThreadActivation(queue).focus;
            return focus && User::WindowThread(focus) == queue ? focus : 0u;
        };
        // USER saves a focused descendant on deactivation; 2A40
        // consumes that saved handle when focus returns. WM_SETFOCUS then
        // falls back to the first tab stop.
        if (message == 6 && !(wp & 0xffffu))
        {
            const unsigned focus = queue_focus();
            unsigned parent = User::Parent(focus);
            for (unsigned n = 0; parent && parent != hwnd && n < 192; ++n) parent = User::Parent(parent);
            if (parent == hwnd && !dialog->saved_focus) dialog->saved_focus = focus;
            return 0;
        }
        if (User::Style(hwnd) & 0x20000000u) return 0;
        unsigned focus = dialog->saved_focus;
        dialog->saved_focus = 0;
        if (User::IsWindow(focus))
        {
            DialogDefaultButton(hwnd, queue_focus(), focus);
            if (User::IsWindow(focus)) SetFocus(focus);
        }
        else if (message == 7 && !dialog->ended)
        {
            focus = User::NextDialogItem(hwnd, 0, false);
            if (focus) DialogSetFocus(focus);
        }
        return 0;
    }
    // USER: WM_GETFONT answers the font of the template ([dialog+72h]),
    // null without DS_SETFONT; WM_SETFONT is not DefDlgProc's.
    if (message == 0x31)
        return dialog->font == system ? 0 : dialog->font;
    // USER, WM_ERASEBKGND -> with CTLCOLOR_DLG (4).
    // The dialog procedure supplies the brush, not the dialog class brush.
    if (message == 0x14)
    {
        unsigned brush = CallWindow(hwnd, 0x136, wp, hwnd);
        if (!User::IsWindow(hwnd)) return 1;
        if (!brush) { unsigned color = 15; brush = Api("GetSysColorBrush", &color); }
        Gdi::RECT rect{};
        if (User::GetClientRect(hwnd, &rect) && Gdi::FillRect(wp, &rect, brush))
            User::DrawingChanged(wp);
        return 1;
    }
    if(message==0x112&&(wp&0xfff0)==0xf060)
        return CallWindow(hwnd,0x10,0,0);
    if (message == 0x10)
    {
        // EndDialog in the application's WM_CLOSE handler already chose the
        // result, even when that handler returned FALSE. Do not click Cancel
        // afterwards and overwrite it while the modal loop is unwinding.
        if (dialog->ended) return 0;
        CallWindow(hwnd,0x111,2,User::GetDlgItem(hwnd,2));
        return 0;
    }

    if (message == 0x28)
    {
        // WM_NEXTDLGCTL, USER. The old control is the focus of the
        // queue. With LOWORD(lParam) wParam is the control; otherwise the
        // next (wParam 0) or previous tab stop after the focus, or the first
        // one when nothing has the focus. A focus outside the dialog does
        // nothing. The new control gets the focus through
        // DlgSetFocus, and the default frame follows.
        unsigned old = User::GetFocus(), next = 0;
        if (lp & 0xffffu)
        {
            if (!old) old = hwnd;
            next = User::IsWindow(wp) ? wp : 0;
        }
        else if (!old)
        {
            next = User::NextDialogItem(hwnd, 0, false);
            old = hwnd;
        }
        else
        {
            unsigned parent = User::Parent(old);
            for (unsigned n = 0; parent && parent != hwnd && n < 192; ++n)
                parent = User::Parent(parent);
            if (parent != hwnd) return 0;
            next = User::NextDialogItem(hwnd, old, wp != 0);
        }
        if (next) DialogSetFocus(next);
        if (state == Failed || state == Exited || !FindDialog(hwnd)) return 0;
        DialogDefaultButton(hwnd, User::IsWindow(old) ? old : 0, User::IsWindow(next) ? next : 0);
        return 0;
    }
    if (message == 0x400)
        // USER rejects the saved ID once EndDialog set bit 1.
        return !dialog->ended && dialog->default_id ? 0x534b0000 | dialog->default_id : 0;
    if (message == 0x401)
    {
        // USER takes the successful no-op path for an ended dialog.
        if (dialog->ended) return 1;
        // USER's DM_SETDEFID moves the default frame as well: the old default
        // button, if it is one, becomes BS_PUSHBUTTON and a push button with
        // the new identifier BS_DEFPUSHBUTTON (DefDlgProc, DLGC_DEFPUSHBUTTON
        // 10h and DLGC_UNDEFPUSHBUTTON 20h). The wizard frame relies on it
        // when PSM_SETWIZBUTTONS names Next or Finish.
        const unsigned old_button = dialog->default_id ? User::GetDlgItem(hwnd, dialog->default_id) : 0;
        if (old_button && (CallWindow(old_button, 0x87, 0, 0) & 0x10) && User::IsWindow(hwnd))
            CallWindow(old_button, 0xf4, 0, 1);
        const unsigned new_button = (wp & 0xffff) ? User::GetDlgItem(hwnd, wp & 0xffff) : 0;
        if (new_button && User::IsWindow(new_button) && (CallWindow(new_button, 0x87, 0, 0) & 0x20))
            CallWindow(new_button, 0xf4, 1, 1);
        if (auto *current = FindDialog(hwnd))
            current->default_id = wp;
        return 1;
    }
    if (message == 0x402)
    {
        // DM_REPOSITION: a top-level dialog moves into the work area, the
        // screen without the taskbar; COMCTL32's property sheet sends it
        // after it has grown to its pages, which is what keeps the sheet of
        // an icon near the bottom of the screen visible.
        if (User::Style(hwnd) & 0x40000000u) return 0;
        const unsigned area = Allocate(16);
        if (!area) return 0;
        const unsigned query[] = {0x30, 0, area, 0}; // SPI_GETWORKAREA
        Gdi::RECT work{}, window{};
        if (Api("SystemParametersInfoA", query)) Read(area, &work, sizeof work);
        Free(area);
        if (work.right <= work.left || work.bottom <= work.top || !User::GetWindowRect(hwnd, &window)) return 0;
        int x = window.left, y = window.top;
        const int width = window.right - window.left, height = window.bottom - window.top;
        if (x + width > work.right) x = work.right - width;
        if (y + height > work.bottom) y = work.bottom - height;
        if (x < work.left) x = work.left;
        if (y < work.top) y = work.top;
        if (x != window.left || y != window.top)
        {
            const unsigned move[] = {hwnd, 0, (unsigned)x, (unsigned)y, 0, 0, 0x15}; // SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
            Api("SetWindowPos", move);
        }
        return 0;
    }
    return DefaultWindowMessage(hwnd, message, wp, lp);
}
} // namespace X86
