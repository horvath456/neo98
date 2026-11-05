// SPDX-License-Identifier: GPL-2.0-or-later
// PropertySheet, CreatePropertySheetPage and DestroyPropertySheetPage of the
// 16-bit COMMCTRL.DLL (ordinals 159, 160, 161), for a 16-bit caller such as
// SYSDM.CPL.
//
// COMMCTRL.DLL is ours by the rule of PLAN.md section 0. Its property sheet
// is 16-bit code of its own, not a thunk: Windows 98 checks the header (dwSize
// 1Eh to 22h, no flag in F000h or the high word, fewer than 100 pages),
// copies it, makes a page of every PROPSHEETPAGE with PSH_PROPSHEETPAGE,
// and Windows 98 builds the frame from its own template 3FCh with the dialog
// procedure and runs the modal loop. The frame, the tabs, the buttons
// and the notifications are the same as those of COMCTL32's sheet, which
// this port has natively (src/x86_property_sheet.cpp, the sheet the PIF
// pages already use for a 16-bit caller). So the 16-bit pages become pages
// of that sheet; what is 16-bit about them stays 16-bit:
//
// - the template comes out of the module of the page (or the far pointer
// of PSP_DLGINDIRECT) and is converted to the 32-bit form;
// - the dialog procedure is the 16-bit one; it gets WM_INITDIALOG with the
// far pointer of its own PROPSHEETPAGE, the copy CreatePropertySheetPage
// made, and WM_NOTIFY with a 16-bit NMHDR (hwndFrom, idFrom and
// code as words) followed by the lParam of PSHNOTIFY, in a block it can
// reach by a far pointer; every other message goes through the ordinary
// 32-to-16 translation of Win16Deliver.
//
// The page records: Windows 98 allocates dwSize bytes plus the title
// (PSP_USETITLE with a string) with GMEM_ZEROINIT, copies the structure,
// puts the title behind it and marks the title pointer with bit 15 of its
// selector word as "offset into this block", and counts pcRefParent up with
// PSP_USEREFPARENT. counts it down, calls the page callback with
// PSPCB_RELEASE for PSP_USECALLBACK and frees the block.
#include "win16_machine.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned Win16PageToken = 0x71000044;
// PROPSHEETPAGE of the 16-bit side.
constexpr unsigned PageSize = 0x00, PageFlags = 0x04, PageInstance = 0x08, PageTemplate = 0x0a,
                   PageTitle = 0x12, PageProcedure = 0x16, PageCallback = 0x1e, PageReference = 0x22;
constexpr unsigned PspDlgIndirect = 0x1, PspUseTitle = 0x8, PspHasHelp = 0x20, PspUseRefParent = 0x40,
                   PspUseCallback = 0x80;
// PROPSHEETHEADER of the 16-bit side.
constexpr unsigned SheetFlags = 0x04, SheetParent = 0x08, SheetInstance = 0x0a, SheetCaption = 0x10,
                   SheetPages = 0x14, SheetStart = 0x16, SheetArray = 0x1a;
constexpr unsigned PshPropTitle = 0x1, PshPropSheetPage = 0x8, PshWizard = 0x20, PshUsePStartPage = 0x40,
                   PshNoApplyNow = 0x80, PshUseCallback = 0x100, PshHasHelp = 0x200, PshModeless = 0x400;
} // namespace

// One page dialog of a 16-bit page: the window, the far pointer of the page
// record and the 16-bit procedure.
struct Application::Win16SheetPage
{
    unsigned hwnd = 0, page = 0, procedure = 0;
};
struct Application::Win16SheetState
{
    Application *app;
    Win16SheetState *next;
    unsigned memory = 0, pages[100] = {}, templates[100] = {}, made = 0;
    unsigned result = 0xffff, stage = 0, destroying_page = 0, added_base = 0;
    bool recorded = false;
    static Win16SheetState *head;
    explicit Win16SheetState(Application *a) : app(a), next(head) { head = this; }
    ~Win16SheetState()
    {
        auto **at = &head;
        while (*at && *at != this) at = &(*at)->next;
        if (*at) *at = next;
    }
};
Application::Win16SheetState *Application::Win16SheetState::head = nullptr;
namespace
{
constexpr unsigned SheetPageCapacity = 64;
}

unsigned Application::Win16CreatePropertyPage(unsigned far_page)
{
    const unsigned source = far_page >> 16 ? Win16Linear(far_page >> 16, far_page & 0xffff) : 0;
    if (!source)
        return 0;
    const unsigned size = Read32(source + PageSize), flags = Read32(source + PageFlags);
    if (size < 0x26 || size > 0x1000 || (flags & ~0xffu))
        return 0;
    const unsigned title = Read32(source + PageTitle);
    unsigned extra = 0;
    const char *text = nullptr;
    char copy[256] = {};
    if ((flags & PspUseTitle) && (title >> 16))
    {
        text = ReadString(Win16Linear(title >> 16, title & 0xffff));
        snprintf(copy, sizeof copy, "%s", text ? text : "");
        extra = unsigned(strlen(copy)) + 1;
    }
    const unsigned handle = Win16GlobalAlloc(0x40, size + extra);
    auto *block = handle ? Win16GlobalBlock(handle) : nullptr;
    if (!block || !block->memory)
        return 0;
    auto *bytes = new unsigned char[size];
    if (Read(source, bytes, size))
        Store(block->memory, bytes, size);
    delete[] bytes;
    if (extra)
    {
        Store(block->memory + size, copy, extra);
        Win16Word(block->memory + PageTitle, size & 0xffff);
        Win16Word(block->memory + PageTitle + 2, (size >> 16) | 0x8000);
    }
    if ((flags & PspUseRefParent) && Read32(block->memory + PageReference))
    {
        const unsigned count = Win16FarPointer(block->memory + PageReference);
        if (count)
            Win16Word(count, Win16Word(count) + 1);
    }
    return block->selector << 16;
}

bool Application::Win16DestroyPropertyPage(unsigned far_page)
{
    // This function can itself be COMMCTRL.161, as well as a recorded
    // release in the sheet's cleanup. TakeTail intentionally drops Fresh.
    const unsigned tail = threads[current_thread].tail;
    GiveTail(TailNone);
    const unsigned page = far_page >> 16 ? Win16Linear(far_page >> 16, far_page & 0xffff) : 0;
    if (!page)
        return false;
    const unsigned flags = Read32(page + PageFlags);
    if ((flags & PspUseRefParent) && Read32(page + PageReference))
    {
        const unsigned count = Win16FarPointer(page + PageReference);
        if (count)
            Win16Word(count, Win16Word(count) - 1);
    }
    if ((flags & PspUseCallback) && Read32(page + PageCallback))
    {
        const unsigned push[] = {0, 1, far_page >> 16, far_page & 0xffff}; // hwnd 0, PSPCB_RELEASE
        const bool kept = Win16Process() && (tail == TailFresh || tail == TailRecord) &&
                          PushHostFrame(HostWin16PageDestroy, far_page);
        if (kept) GiveTail(TailRecord);
        if (Win16Process())
            Win16CallPascal(Read32(page + PageCallback), push, 4, Win16Word(page + PageInstance));
        else
            Win16HostedCall(Read32(page + PageCallback), push, 4, Win16Word(page + PageInstance), nullptr, nullptr);
        if (kept) PopHostFrame();
    }
    Win16GlobalFree(far_page >> 16);
    return true;
}

// The title of a page, or the caption of a sheet: a string, a string of the
// page record itself (bit 15 of the selector word), or a string resource.
void Application::Win16SheetText(unsigned record, unsigned field, unsigned instance, char (&out)[256])
{
    out[0] = 0;
    const unsigned value = Read32(record + field);
    if (!value)
        return;
    if ((value >> 16) & 0x8000)
    {
        const char *text = ReadString(record + (value & 0x7fffffffu));
        snprintf(out, sizeof out, "%s", text ? text : "");
    }
    else if (value >> 16)
    {
        const char *text = ReadString(Win16Linear(value >> 16, value & 0xffff));
        snprintf(out, sizeof out, "%s", text ? text : "");
    }
    else
    {
        const unsigned buffer = Allocate(256);
        if (!buffer)
            return;
        Win16LoadStringInto(instance, value & 0xffff, buffer, 256);
        snprintf(out, sizeof out, "%s", ReadString(buffer));
        Free(buffer);
    }
}

// The template of a 16-bit page in the 32-bit form, in a block of its own.
unsigned Application::Win16PageTemplate(unsigned page)
{
    const unsigned flags = Read32(page + PageFlags), instance = Win16Word(page + PageInstance);
    const unsigned name = Read32(page + PageTemplate);
    unsigned char *source = nullptr;
    unsigned size = 0;
    unsigned handle = 0;
    if (flags & PspDlgIndirect)
    {
        const unsigned selector = name >> 16, offset = name & 0xffff;
        const unsigned at = selector ? Win16Linear(selector, offset) : 0;
        if (!at)
            return 0;
        const unsigned limit = Win16Limit(selector);
        size = limit >= offset ? limit - offset + 1 : 0;
        if (!size)
            return 0;
        source = new unsigned char[size];
        if (!Read(at, source, size))
        {
            delete[] source;
            return 0;
        }
        auto *converted = new unsigned char[4 * size + 64];
        unsigned length = 0;
        unsigned block = 0;
        if (ConvertDialogTemplate(source, size, converted, 4 * size + 64, length) && (block = Allocate(length)) != 0)
            Store(block, converted, length);
        delete[] converted;
        delete[] source;
        return block;
    }
    handle = Win16NativeTemplate(instance, name, 0);
    if (!handle)
        return 0;
    const unsigned locked = Api("GlobalLock", &handle);
    size = Api("GlobalSize", &handle);
    unsigned block = locked && size ? Allocate(size) : 0;
    if (block)
    {
        auto *bytes = new unsigned char[size];
        if (Read(locked, bytes, size))
            Store(block, bytes, size);
        delete[] bytes;
    }
    Api("GlobalUnlock", &handle);
    Api("GlobalFree", &handle);
    return block;
}

// A 16-bit page handle as a page of the native sheet: the record the native
// CreatePropertySheetPageA copies, with the converted template and the page
// procedure token, the way PropertySheet below builds its pages. The
// template and the 16-bit page stay until the 16-bit sheet ends.
unsigned Application::Win16NativePage(unsigned far_page)
{
    const unsigned page = far_page >> 16 ? Win16Linear(far_page >> 16, far_page & 0xffff) : 0;
    if (!page || win16_added_count >= sizeof win16_added_pages / sizeof win16_added_pages[0])
        return 0;
    const unsigned page_flags = Read32(page + PageFlags), page_instance = Win16Word(page + PageInstance);
    const unsigned form = Win16PageTemplate(page);
    const unsigned record_at = form ? Allocate(40 + 256) : 0;
    if (!record_at)
    {
        if (form)
            Free(form);
        return 0;
    }
    char title[256] = {};
    if (page_flags & PspUseTitle)
        Win16SheetText(page, PageTitle, page_instance, title);
    Store(record_at + 40, title, unsigned(strlen(title)) + 1);
    const unsigned record[10] = {40,
                                 PspDlgIndirect | (page_flags & (PspUseTitle | PspHasHelp)),
                                 page_instance,
                                 form,
                                 0,
                                 (page_flags & PspUseTitle) ? record_at + 40 : 0,
                                 Win16PageToken,
                                 far_page,
                                 0,
                                 0};
    Store(record_at, record, sizeof record);
    const unsigned handle = PropertyPageApi("CreatePropertySheetPageA", &record_at);
    Free(record_at);
    if (!handle)
    {
        Free(form);
        return 0;
    }
    win16_added_pages[win16_added_count] = far_page;
    win16_added_templates[win16_added_count++] = form;
    if (log)
    {
        char text[400];
        snprintf(text, sizeof text, "WIN16 COMMCTRL added page record=%08x flags=%04x template=%08x title=[%s]",
                 far_page, page_flags, form, title);
        log(text);
    }
    return handle;
}

// The template identifier of a 16-bit page in the native sheet: the page
// procedure is the token, its lParam the far pointer of the 16-bit record.
// PSN_SETACTIVE, PSN_WIZBACK and PSN_WIZNEXT answer with the name the page
// was made from in its own module, as COMMCTRL compares it; zero for a page
// that is not a 16-bit one.
unsigned Application::Win16PageTemplateName(unsigned page)
{
    if (Read32(page + 24) != Win16PageToken)
        return 0;
    const unsigned far_page = Read32(page + 28);
    const unsigned record = far_page >> 16 ? Win16Linear(far_page >> 16, far_page & 0xffff) : 0;
    return record ? Read32(record + PageTemplate) : 0;
}

// COMMCTRL.159 PropertySheet(lppsh).
unsigned Application::Win16PropertySheet(unsigned far_header)
{
    const bool gate = TakeFresh();
    const unsigned header = far_header >> 16 ? Win16Linear(far_header >> 16, far_header & 0xffff) : 0;
    if (!header)
        return 0xffff;
    const unsigned size = Read32(header), flags = Read32(header + SheetFlags);
    const unsigned count = Win16Word(header + SheetPages);
    if (size < 0x1e || size > 0x22 || (flags & 0xfffff000u) || count >= 100)
        return 0xffff;
    // The wizard (PSH_WIZARD) is the native wizard frame, template 1020 of
    // COMCTL32, with the same buttons and notifications.
    if (flags & (PshModeless | PshUseCallback))
    {
        char text[160];
        snprintf(text, sizeof text,
                 "Win16 COMMCTRL.159 PropertySheet with flags %04x: the modeless sheet and the sheet callback of "
                 "the 16-bit side are not built yet",
                 flags);
        Fail(text);
        return 0xffff;
    }
    const unsigned instance = Win16Word(header + SheetInstance);
    // The pages as far pointers to their records, made here for an array of
    // PROPSHEETPAGE the way makes them.
    auto *held = new Win16SheetState(this);
    held->added_base = win16_added_count;
    auto &pages = held->pages;
    auto &made = held->made;
    const unsigned array = Win16FarPointer(header + SheetArray);
    unsigned far_array = Read32(header + SheetArray);
    for (unsigned i = 0; i < count && array; ++i)
    {
        if (flags & PshPropSheetPage)
        {
            const unsigned at = Win16Linear(far_array >> 16, far_array & 0xffff);
            const unsigned page = Win16CreatePropertyPage(far_array);
            if (page)
                pages[made++] = page;
            far_array += Read32(at);
        }
        else if (const unsigned page = Read32(array + i * 4))
            pages[made++] = page;
    }
    char caption[256];
    Win16SheetText(header, SheetCaption, instance, caption);
    // The native sheet: header, pages, strings and templates in one block.
    const unsigned memory = Allocate(40 + 40 * (made ? made : 1) + 256 * (made + 1));
    held->memory = memory;
    auto &templates = held->templates;
    const bool kept = gate && Win16Process() && memory && PushHostFrame(HostWin16Sheet, memory);
    held->recorded = kept;
    unsigned result = 0xffff;
    if (memory)
    {
        const unsigned strings = memory + 40 + 40 * (made ? made : 1);
        Store(strings, caption, unsigned(strlen(caption)) + 1);
        for (unsigned i = 0; i < made; ++i)
        {
            const unsigned page = Win16Linear(pages[i] >> 16, pages[i] & 0xffff);
            const unsigned page_flags = Read32(page + PageFlags);
            const unsigned page_instance = Win16Word(page + PageInstance);
            templates[i] = Win16PageTemplate(page);
            char title[256] = {};
            if (page_flags & PspUseTitle)
                Win16SheetText(page, PageTitle, page_instance, title);
            const unsigned title_at = strings + 256 * (i + 1);
            Store(title_at, title, unsigned(strlen(title)) + 1);
            const unsigned record[10] = {40,
                                         PspDlgIndirect | (page_flags & (PspUseTitle | PspHasHelp)),
                                         page_instance,
                                         templates[i],
                                         0,
                                         (page_flags & PspUseTitle) ? title_at : 0,
                                         Win16PageToken,
                                         pages[i],
                                         0,
                                         0};
            Store(memory + 40 + 40 * i, record, sizeof record);
            if (log)
            {
                char text[400];
                snprintf(text, sizeof text, "WIN16 COMMCTRL page %u record=%08x flags=%04x template=%08x title=[%s]",
                         i, pages[i], page_flags, templates[i], title);
                log(text);
            }
        }
        const unsigned start = flags & PshUsePStartPage ? 0 : Win16Word(header + SheetStart);
        const unsigned sheet[10] = {40,
                                    PshPropSheetPage | (flags & (PshPropTitle | PshWizard | PshNoApplyNow | PshHasHelp)),
                                    Win16Word(header + SheetParent),
                                    instance,
                                    0,
                                    strings,
                                    made,
                                    start < made ? start : 0,
                                    memory + 40,
                                    0};
        Store(memory, sheet, sizeof sheet);
        const unsigned a[] = {memory};
        bool all = true;
        for (unsigned i = 0; i < made; ++i)
            all = all && templates[i];
        if (made && all)
        {
            if (kept) GiveTail(TailFresh);
            result = PropertySheetApi("PropertySheetA", a);
        }
        else if (log)
            log("WIN16 COMMCTRL PropertySheet: a page template did not load");
    }
    return FinishWin16PropertySheet(memory, result);
}
unsigned Application::FinishWin16PropertySheet(unsigned memory, unsigned result, bool resumed)
{
    auto *held = Win16SheetState::head;
    while (held && (held->app != this || held->memory != memory)) held = held->next;
    if (!held) return result & 0xffff;
    if (resumed && held->stage) held->destroying_page = 0; // the resumed HostWin16PageDestroy already freed it
    if (!held->stage)
    {
        held->result = result;
        held->stage = 1;
        for (auto &form : held->templates)
            if (form) { Free(form); form = 0; }
    }
    // The pages go with the sheet, the ones a page
    // added with PSM_ADDPAGE as well.
    while (held->made)
    {
        const unsigned page = held->pages[--held->made];
        held->pages[held->made] = 0;
        held->destroying_page = page;
        if (held->recorded) GiveTail(TailRecord);
        Win16DestroyPropertyPage(page);
        held->destroying_page = 0;
    }
    while (win16_added_count > held->added_base)
    {
        --win16_added_count;
        Free(win16_added_templates[win16_added_count]);
        win16_added_templates[win16_added_count] = 0;
        held->destroying_page = win16_added_pages[win16_added_count];
        win16_added_pages[win16_added_count] = 0;
        if (held->recorded) GiveTail(TailRecord);
        Win16DestroyPropertyPage(held->destroying_page);
        held->destroying_page = 0;
    }
    result = state == Failed || state == Exited ? 0xffff : held->result & 0xffff;
    const bool kept = held->recorded;
    Free(held->memory);
    delete held;
    if (kept) PopHostFrame();
    return result;
}
void Application::DiscardWin16PropertySheets()
{
    for (auto *held = Win16SheetState::head, *next = held; held; held = next)
    {
        next = held->next;
        if (held->app != this) continue;
        // No surviving native fiber can borrow this controller now.
        if (held->destroying_page) Win16GlobalFree(held->destroying_page >> 16);
        for (unsigned i = 0; i < held->made; ++i) Win16GlobalFree(held->pages[i] >> 16);
        for (auto form : held->templates) if (form) Free(form);
        Free(held->memory);
        delete held;
    }
}

// The dialog procedure of a 16-bit page inside the native sheet.
unsigned Application::Win16PropertyPageMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    const unsigned tail = TakeTail();
    if (!win16_sheet_pages)
        win16_sheet_pages = new Win16SheetPage[SheetPageCapacity];
    Win16SheetPage *record = nullptr;
    for (unsigned i = 0; i < SheetPageCapacity; ++i)
        if (win16_sheet_pages[i].hwnd && win16_sheet_pages[i].hwnd == hwnd)
            record = &win16_sheet_pages[i];
    if (message == 0x110)
    {
        // The native sheet hands its copy of the 32-bit page; lParam is the
        // far pointer of the 16-bit record.
        const unsigned page = Read32(lp + 0x1c);
        const unsigned flat = page >> 16 ? Win16Linear(page >> 16, page & 0xffff) : 0;
        if (!flat)
            return 0;
        for (unsigned i = 0; i < SheetPageCapacity; ++i)
            if (auto &slot = win16_sheet_pages[i]; !slot.hwnd || !User::IsWindow(slot.hwnd))
            {
                slot = {};
                record = &slot;
                break;
            }
        if (!record)
        {
            Fail("Win16 property page table is full");
            return 0;
        }
        record->hwnd = hwnd;
        record->page = page;
        const unsigned procedure = Read32(flat + PageProcedure);
        record->procedure = procedure ? Win16Mark(procedure) : 0;
        if (!record->procedure)
            return 0;
        const unsigned arguments[] = {hwnd, message, wp, page};
        const bool kept = tail == TailRecord && Win16Process() && PushHostFrame(HostWin16PageProcedure, hwnd, message);
        if (kept) GiveTail(TailRecord);
        const unsigned answer = Call(record->procedure, arguments, 4);
        if (kept) PopHostFrame();
        return answer & 0xffff;
    }
    if (!record || !record->procedure)
        return 0;
    // WM_NOTIFY takes the 16-bit shape in Win16Deliver: the header keeps the
    // places of the 32-bit one (COMMCTRL Windows 98 writes hwndFrom at +0,
    // idFrom at +4 and code at +8, PSHNOTIFY.lParam at +12; SYSDM's
    // wizard pages read the code at +8), the tree view's structures
    // are packed (src/win16_common_controls.cpp).
    const unsigned arguments[] = {hwnd, message, wp, lp};
    const bool kept = tail == TailRecord && Win16Process() && PushHostFrame(HostWin16PageProcedure, hwnd, message);
    if (kept) GiveTail(TailRecord);
    const unsigned answer = Call(record->procedure, arguments, 4) & 0xffff;
    if (kept) PopHostFrame();
    if (message == 0x82)
        *record = {};
    return answer;
}
unsigned Application::ResumeWin16PropertyPage(unsigned hwnd, unsigned message, unsigned result)
{
    if (message == 0x82 && win16_sheet_pages)
        for (unsigned i = 0; i < SheetPageCapacity; ++i)
            if (win16_sheet_pages[i].hwnd == hwnd) win16_sheet_pages[i] = {};
    PopHostFrame();
    return result & 0xffff;
}
} // namespace X86
