// SPDX-License-Identifier: GPL-2.0-or-later
#include "kernel.h"
#include "user_draw.h"
#include "win98_frame_control.h"
#include "jit_memory.h"
#include <circle/string.h>
#include <circle/util.h>
#include "../build/generated/win98_font.h"

#if AARCH != 64
#error This experiment must execute as AArch64.
#endif
static_assert(sizeof(void *) == 8, "AArch64 pointers required");
static_assert(sizeof(unsigned) == 4, "32-bit pixels required");

Kernel *Kernel::instance = nullptr;
static constexpr unsigned Gray = 0xffc0c0c0;
static constexpr unsigned White = 0xffffffff;
static constexpr unsigned Navy = 0xff000080;
static constexpr unsigned Teal = 0xff008080;
static constexpr unsigned Purple = 0xff604080;

Kernel::Kernel()
    : framebuffer(800, 600, 32), timer(&interrupts),
      logger(LogNotice, &timer), usb(&interrupts, &timer, TRUE)
{
    instance = this;
}

void Kernel::Log(const char *text)
{
    serial.Write(text, strlen(text));
    serial.Write("\n", 1);
}

bool Kernel::Initialize()
{
    if (!serial.Initialize(115200)) return false;
    logger.Initialize(&serial);
    Log("NEO98 BOOT arch=aarch64 pointer_bits=64");
    u64 level;
    asm volatile("mrs %0, CurrentEL" : "=r"(level));
    exception_level = (unsigned)(level >> 2);
    if (!interrupts.Initialize() || !timer.Initialize()) {
        Log("NEO98 FAIL interrupts/timer");
        return false;
    }
    if (!RasterSelfTest()) {
        Log("NEO98 FAIL raster clipping");
        return false;
    }
    Log("NEO98 SELFTEST raster-clipping=PASS");
    extern unsigned UnicornSelfTest();
    const unsigned cpu_failure = UnicornSelfTest();
    if (cpu_failure) {
        CString message; message.Format("NEO98 FAIL unicorn step=%u", cpu_failure); Log(message); return false;
    }
    Log("NEO98 SELFTEST unicorn=PASS host=aarch64 guest=x86 backend=tcg");
    extern unsigned UnicornOriginalUserTest();
    const unsigned original_cpu_failure = UnicornOriginalUserTest();
    if (original_cpu_failure) {
        CString message; message.Format("NEO98 FAIL unicorn-original-user32 case=%u", original_cpu_failure); Log(message); return false;
    }
    Log("NEO98 SELFTEST unicorn-original-user32=PASS functions=9 vectors=1908 pe_loader=native");
    const unsigned failed_vector = Win98::OriginalSelfTest();
    CString verification;
    if (failed_vector) {
        verification.Format("NEO98 FAIL original-user32 vector=%u", failed_vector);
        Log(verification);
        return false;
    }
    verification.Format("NEO98 SELFTEST original-user32=PASS functions=9 vectors=%u",
                        Win98::OriginalTestCount());
    Log(verification);
    const unsigned dib_failure = DibEng::OriginalSelfTest();
    if (dib_failure) {
        verification.Format("NEO98 FAIL original-dibeng vector=%u", dib_failure); Log(verification); return false;
    }
    verification.Format("NEO98 SELFTEST original-dibeng=PASS vectors=%u rops=256", DibEng::OriginalTestCount());
    Log(verification);
    const unsigned text_failure = DibText::OriginalSelfTest();
    if (text_failure) {
        verification.Format("NEO98 FAIL original-text vector=%u", text_failure); Log(verification); return false;
    }
    verification.Format("NEO98 SELFTEST original-text=PASS vectors=%u fonts=%u", DibText::OriginalTestCount(), DibText::FontCount());
    Log(verification);
    const unsigned user_failure=Win98Draw::OriginalSelfTest();
    if(user_failure){verification.Format("NEO98 FAIL original-user-draw vector=%u",user_failure);Log(verification);return false;}
    verification.Format("NEO98 SELFTEST original-user-draw=PASS vectors=%u",Win98Draw::OriginalTestCount());Log(verification);
    const unsigned frame_failure=Win98Frame::OriginalSelfTest();
    if(frame_failure){verification.Format("NEO98 FAIL original-frame-control vector=%u",frame_failure);Log(verification);return false;}
    verification.Format("NEO98 SELFTEST original-frame-control=PASS vectors=%u",Win98Frame::OriginalTestCount());Log(verification);
    const unsigned dialog_failure=Win98Dialog::OriginalSelfTest();
    if(dialog_failure){verification.Format("NEO98 FAIL original-dialog vector=%u",dialog_failure);Log(verification);return false;}
    verification.Format("NEO98 SELFTEST original-dialog=PASS vectors=%u",Win98Dialog::OriginalTestCount());Log(verification);
    const unsigned failed_replay = Winmine::OriginalSelfTest();
    if (failed_replay) {
        verification.Format("NEO98 FAIL original-winmine step=%u", failed_replay);
        Log(verification);
        return false;
    }
    verification.Format("NEO98 SELFTEST original-winmine=PASS steps=%u", Winmine::OriginalTestCount());
    Log(verification);
    extern unsigned RuntimeThreadSelfTest();
    const unsigned thread_failure=RuntimeThreadSelfTest();
    if(thread_failure){verification.Format("NEO98 FAIL native-threads code=%u",thread_failure);Log(verification);return false;}
    Log("NEO98 SELFTEST native-threads=PASS threads=3 worker_yields=160 events=PASS x87=PASS");
    extern unsigned RuntimeCommonSelfTest();
    const unsigned common_failure=RuntimeCommonSelfTest();
    {
        unsigned live = 0, peak = 0, flushes = 0; unsigned long code = 0;
        neo98_jit_statistics(&live, &peak, &code, &flushes);
        verification.Format("NEO98 JIT live=%u peak_slots=%u code_peak=%lu flushes=%u slot_bytes=%u",
                            live, peak, code, flushes, (unsigned)NEO98_JIT_SLOT_BYTES);
        Log(verification);
    }
    if(common_failure){verification.Format("NEO98 FAIL native-common-controls code=%u",common_failure);Log(verification);return false;}
    Log("NEO98 SELFTEST native-common-controls=PASS arrays=PASS x86-callbacks=PASS format-message=PASS "
        "tree=PASS image-lists=PASS list=PASS status=PASS registry=PASS timers=PASS strings=PASS "
        "printf=PASS files=PASS paths=PASS heaps=PASS kernel-helpers=PASS find-replace=PASS nls=PASS environment=PASS original-errors=PASS gdi-abi=PASS system-parameters=PASS paint-protocol=PASS string-helpers=PASS listbox=PASS shell-hooks=PASS windows=PASS atoms=PASS local-heap-atoms=PASS global-atoms=PASS window-properties=PASS nls-composite=PASS nls-decompose=PASS key-state=PASS activation=PASS focus=PASS mouse_focus=PASS crt-control=PASS error-mode=PASS critical-sections=PASS dynamic-tls=PASS crt-memory=PASS dll-onexit=PASS named-mutexes=PASS process-onexit=PASS virtual-memory=PASS");
    extern unsigned RuntimeVirtualSelfTest();
    const unsigned virtual_failure = RuntimeVirtualSelfTest();
    if (virtual_failure) { verification.Format("NEO98 FAIL native-page-protection code=%u", virtual_failure); Log(verification); return false; }
    Log("NEO98 SELFTEST native-page-protection=PASS x86-write=PASS api-write=PASS decommit=PASS noaccess-execution=PASS");
    extern unsigned RuntimeModuleSelfTest();
    const unsigned module_failure = RuntimeModuleSelfTest();
    if (module_failure) { verification.Format("NEO98 FAIL native-modules code=%u", module_failure); Log(verification); return false; }
    Log("NEO98 SELFTEST native-modules=PASS relocation=PASS dependencies=PASS dllmain=PASS exports=PASS reloads=20 loader-lock=PASS dll-resources=PASS");
    // Runs last: the earlier contract programs require the first process and
    // thread identities of a boot, which two further applications would consume.
    extern unsigned RuntimeMultiEngineSelfTest(unsigned &);
    unsigned concurrent_slots = 0;
    const unsigned engine_failure = RuntimeMultiEngineSelfTest(concurrent_slots);
    if (engine_failure) {
        verification.Format("NEO98 FAIL native-multi-engine code=%u", engine_failure); Log(verification); return false;
    }
    verification.Format("NEO98 SELFTEST native-multi-engine=PASS applications=2 engines=%u slots=%u slot_bytes=%u",
                        concurrent_slots, (unsigned)NEO98_JIT_SLOTS, (unsigned)NEO98_JIT_SLOT_BYTES);
    Log(verification);
    game.Configure(8, 8, 10, 0x98);
    // Mailbox pixel order 0 = BGR, matching little-endian 0xAARRGGBB words.
    // QEMU's initial order differs from Circle's assumed framebuffer layout.
    CBcmPropertyTags properties;
    TPropertyTagSimple pixel_order = {};
    if (!properties.GetTag(0x00048006, &pixel_order, sizeof(pixel_order), 4) ||
        pixel_order.nValue != 0) {
        Log("NEO98 FAIL framebuffer pixel order");
        return false;
    }
    if (!framebuffer.Initialize() || framebuffer.GetDepth() != 32 ||
        framebuffer.GetWidth() != 800 || framebuffer.GetHeight() != 600) {
        Log("NEO98 FAIL framebuffer");
        return false;
    }
    display = {reinterpret_cast<unsigned *>((uintptr)framebuffer.GetBuffer()),
               800, 600, (int)framebuffer.GetPitch() / 4};
    // Draw into cached ARM RAM, then copy only completed, changed rectangles.
    surface = {new unsigned[800 * 600], 800, 600, 800};
    if (!surface.pixels) { Log("NEO98 FAIL drawing buffer allocation"); return false; }
    application_mode = strcmp(options.GetAppOptionString("app", "notepad"), "lab")!=0;
    Log("NEO98 FRAMEBUFFER width=800 height=600 depth=32");
    // USB comes up before the application, because the startup loop already
    // routes keyboard and mouse reports: an application that keeps running
    // without idling has to be clickable while it starts. See
    //
    if (!usb.Initialize()) {
        Log("NEO98 FAIL USB initialization");
        return false;
    }
    if (application_mode) {
        if (!InitializeApplication()) { Log("NEO98 FAIL application startup"); return false; }
    } else {
        if (!InitializeGdi()) { Log("NEO98 FAIL GDI initialization"); return false; }
        Paint();
    }
    if(application_mode){CString ready;ready.Format("NEO98 READY app=%s input=USB-keyboard-and-mouse",options.GetAppOptionString("app","notepad"));Log(ready);}
    else Log("NEO98 READY keys=space,s,g,n,1,2,3,d,m,q mouse=left,right,middle");
    return true;
}

void Kernel::Text(int x, int y, const char *text, unsigned color)
{
    Gdi::SetTextColor(screen_dc, Gdi::RGB(color >> 16, color >> 8, color));
    Gdi::SetBkMode(screen_dc, Gdi::TRANSPARENT);
    Gdi::TextOutA(screen_dc, x, y, text, strlen(text));
}

void Kernel::Button(Rect rect, const char *label)
{
    FillRect(surface, rect, Gray);
    DrawEdge(surface, rect, false);
    Text(rect.left + 12, rect.top + (rect.bottom - rect.top - (int)FontHeight) / 2, label);
}

void Kernel::Status()
{
    FillRect(surface, {126, 252, 660, 270}, White);
    CString status;
    status.Format("Boot time: %u seconds     Counter: %u", timer.GetUptime(), counter);
    Text(127, 254, status);
}

void Kernel::Present(Rect rect)
{
    const Rect bounds = {0, 0, surface.width, surface.height};
    if (!Win98::IntersectRect(&rect, &rect, &bounds)) return;
    DibEng::Bitmap from = {reinterpret_cast<unsigned char *>(surface.pixels), 800, 600, surface.stride * 4, 32};
    DibEng::Bitmap to = {reinterpret_cast<unsigned char *>(display.pixels), 800, 600, display.stride * 4, 32};
    DibEng::BitBlt(to, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                   &from, rect.left, rect.top, 0x00cc0020, {0});
    asm volatile("dsb sy" ::: "memory");
}

void Kernel::Paint()
{
    if (gdi_view) { PaintGdi(); DrawCursor(); return; }
    if (mines_view) { PaintMines(); DrawCursor(); return; }
    const unsigned started = CTimer::GetClockTicks();
    unsigned pixels = 0;
    const char *kind = full_dirty ? "full" : "partial";
    if (full_dirty) {
        FillRect(surface, {0, 0, 800, 600}, palette ? Purple : Teal);
        Text(28, 24, "NEO98 / original Windows components", White);
        Text(28, 47, "Raspberry Pi 3 - Circle - AArch64", White);

        FillRect(surface, {94, 104, 714, 492}, 0xff004848);
        FillRect(surface, {86, 96, 706, 484}, Gray);
        DrawEdge(surface, {86, 96, 706, 484}, false);
        FillRect(surface, {91, 101, 701, 130}, Navy);
        Text(101, 109, "Windows 98 / ARM porting lab", White);
        Text(114, 153, "Windows 98 geometry behavior on ARM64.");
        Text(114, 179, "MS Sans Serif loaded from your Windows 98 installation.");
        FillRect(surface, {114, 217, 678, 319}, White);
        DrawEdge(surface, {114, 217, 678, 319}, true);
        CString status;
        status.Format("Architecture: AArch64     Exception level: EL%u", exception_level);
        Text(127, 229, status);
        Status();
        status.Format("USER32: %u original x86 cases passed on ARM64.", Win98::OriginalTestCount());
        Text(127, 279, status, 0xff006000);
        Button({114, 342, 310, 380}, "[Space] Count");
        Button({324, 342, 500, 380}, "[C] Colour");
        Button({514, 342, 678, 380}, "[R] Reset");
        Text(114, 409, "Click the QEMU window, then use the keyboard.");
        Text(114, 435, "Native geometry + original font. Windows applications are the next step.");

        FillRect(surface, {0, 562, 800, 600}, Gray);
        FillRect(surface, {0, 562, 800, 564}, White);
        Button({6, 568, 111, 595}, "NEO98");
        Text(130, 575, running ? "Windows 98 component test" : "Halted");
        Text(620, 575, "[Q] Halt");
        Present({0, 0, 800, 600});
        pixels = 800 * 600;
    } else {
        if (status_dirty) {
            Status();
            Present({126, 252, 660, 270});
            pixels += 534 * 18;
        }
        if (taskbar_dirty) {
            FillRect(surface, {128, 573, 400, 590}, Gray);
            Text(130, 575, running ? "Windows 98 component test" : "Halted");
            Present({128, 573, 400, 590});
            pixels += 272 * 17;
        }
    }
    full_dirty = status_dirty = taskbar_dirty = false;
    const unsigned elapsed = CTimer::GetClockTicks() - started;
    CString frame;
    frame.Format("NEO98 FRAME kind=%s pixels=%u elapsed_us=%u counter=%u palette=%u",
                 kind, pixels, elapsed, counter, palette);
    Log(frame);
    DrawCursor();
}

void Kernel::Command(char key)
{
    if (editor_view && gdi_view) { const char text[] = {key, 0}; EditorCommand(text, 0); return; }
    if (key == 'e' || key == 'E') {
        gdi_view = editor_view = true; mines_view = false; gdi_drag = -1;
        GdiDamage({0, 0, 800, 600}); Log("NEO98 VIEW editor"); return;
    }
    if (key == 'g' || key == 'G') {
        gdi_view = true; editor_view = false; mines_view = false; GdiDamage({0, 0, 800, 600});
        Log("NEO98 VIEW gdi"); return;
    }
    if (key == 'd' || key == 'D') {
        game.Preview(-2, -2); game.chord = 0;
        mine_capture = face_pressed = menu_open = false;
        gdi_view = false; gdi_drag = -1; mines_view = false; full_dirty = true;
        Log("NEO98 VIEW diagnostics");
        return;
    }
    if (key == 'm' || key == 'M') {
        gdi_view = false; gdi_drag = -1; mines_view = true; mine_dirty = mine_full = true;
        Log("NEO98 VIEW minesweeper");
        return;
    }
    if (gdi_view) { GdiCommand(key); return; }
    if (mines_view) { MineCommand(key); return; }
    const char *action;
    switch (key) {
    case ' ': ++counter; status_dirty = true; action = "count"; break;
    case 'c': case 'C': palette ^= 1; full_dirty = true; action = "colour"; break;
    case 'r': case 'R':
        full_dirty |= palette != 0;
        counter = 0; palette = 0; status_dirty = true; action = "reset"; break;
    case 'q': case 'Q': running = false; taskbar_dirty = true; action = "halt"; break;
    default: return;
    }
    CString line;
    line.Format("NEO98 EVENT action=%s counter=%u palette=%u", action, counter, palette);
    Log(line);
}

void Kernel::KeyPressed(const char *text)
{
    const unsigned next = (instance->key_write + 1) % 64;
    if (!*text || next == instance->key_read) return;
    auto &event = instance->key_reports[instance->key_write];
    unsigned i = 0; for (; i < 7 && text[i]; ++i) event.text[i] = text[i];
    event.text[i] = 0; event.modifiers = instance->key_modifiers; event.raw = false;
    event.sequence = ++instance->input_sequence;
    asm volatile("dmb sy" ::: "memory");
    instance->key_write = next;
}

void Kernel::KeyStatus(unsigned char modifiers, const unsigned char keys[6])
{
    instance->key_modifiers = modifiers;
    if (instance->application_mode)
    {
        const auto next=(instance->key_write+1)%64;
        if (next==instance->key_read) return;
        auto &event=instance->key_reports[instance->key_write];
        event={};event.raw=true;event.modifiers=modifiers;event.sequence=++instance->input_sequence;
        for(unsigned i=0;i<6;++i)event.keys[i]=keys[i];
        asm volatile("dmb sy" ::: "memory");
        instance->key_write=next;
        return;
    }
    // Circle's shift tables map these navigation keys to KeyNone. Preserve
    // them for selection without changing the pinned keyboard dependency.
    if (modifiers & 0x22) for (unsigned i = 0; i < 6; ++i) {
        const unsigned code = keys[i];
        bool previous = false;
        for (auto old : instance->previous_raw_keys) previous |= old == code;
        if (previous || code < 0x4a || code > 0x52) continue;
        static const char *sequences[] = {"\x1b[1~", "\x1b[5~", "\x1b[3~", "\x1b[4~", "\x1b[6~", "\x1b[C", "\x1b[D", "\x1b[B", "\x1b[A"};
        const char *sequence = sequences[code - 0x4a];
        if ((modifiers & 0x11) && code == 0x4a) sequence = "\x1b[1;5H";
        if ((modifiers & 0x11) && code == 0x4d) sequence = "\x1b[1;5F";
        KeyPressed(sequence);
    }
    for (unsigned i = 0; i < 6; ++i) instance->previous_raw_keys[i] = keys[i];
}

void Kernel::KeyboardRemoved(CDevice *, void *)
{
    instance->keyboard = nullptr;
    const unsigned char released[6]={};
    KeyStatus(0,released);
    instance->application_leds = ~0u;
    instance->key_modifiers = 0;
    for (auto &key : instance->previous_raw_keys) key = 0;
}

void Kernel::ServiceInput()
{
    usb.UpdatePlugAndPlay();
    if (!keyboard) {
        keyboard = static_cast<CUSBKeyboardDevice *>(devices.GetDevice("ukbd1", FALSE));
        if (keyboard) {
            keyboard->RegisterKeyPressedHandler(KeyPressed);
            keyboard->RegisterKeyStatusHandlerRaw(KeyStatus, !application_mode);
            keyboard->RegisterRemovedHandler(KeyboardRemoved);
            Log("NEO98 INPUT usb-keyboard=ready");
            CString line; line.Format("NEO98 INPUT keymap=%s", options.GetKeyMap()); Log(line);
        }
    }
    if (keyboard && !application_mode) keyboard->UpdateLEDs();
    if (!mouse) {
        mouse = static_cast<CMouseDevice *>(devices.GetDevice("mouse1", FALSE));
        if (mouse) {
            mouse->RegisterStatusHandler(MouseStatus);
            mouse->RegisterRemovedHandler(MouseRemoved);
            Log("NEO98 INPUT usb-mouse=ready");
        }
    }
    while (mouse_read != mouse_write || key_read != key_write)
    {
        asm volatile("msr daifset, #2" ::: "memory");
        const bool take_mouse = mouse_read != mouse_write &&
            (key_read == key_write || int(mouse_reports[mouse_read].sequence-key_reports[key_read].sequence)<0);
        if (take_mouse)
        {
            const auto report=mouse_reports[mouse_read];mouse_read=(mouse_read+1)%32;
            asm volatile("msr daifclr, #2" ::: "memory");
            if(application_mode)ApplicationMouse(report.buttons,report.dx,report.dy,report.wheel);
            else MineInput(report.buttons,report.dx,report.dy,report.wheel);
        }
        else
        {
            const auto event=key_reports[key_read];key_read=(key_read+1)%64;
            asm volatile("msr daifclr, #2" ::: "memory");
            if(application_mode)
            {
                if(event.raw)ApplicationRawKeys(event.modifiers,event.keys);
                else ApplicationKey(event.text,event.modifiers);
            }
            else if(editor_view && gdi_view)EditorCommand(event.text,event.modifiers);
            else Command(event.text[0]);
        }
    }
    if(application_mode)ApplicationKeyRepeat();
    char serial_key;
    if (serial.Read(&serial_key, 1) == 1) {
        if (application_mode) { const char text[] = {serial_key, 0}; ApplicationKey(text, 0); }
        else Command(serial_key);
    }
    if (input_seconds != timer.GetUptime()) {
        input_seconds = timer.GetUptime();
        if (gdi_view) { }
        else if (mines_view) { game.Tick(); mine_dirty = true; }
        else status_dirty = true;
        CString line;
        unsigned live = 0, peak = 0, flushes = 0; unsigned long code = 0;
        neo98_jit_statistics(&live, &peak, &code, &flushes);
        line.Format("NEO98 TICK uptime=%u jit_slots=%u/%u peak_slots=%u code_peak=%lu flushes=%u",
                    input_seconds, live, (unsigned)NEO98_JIT_SLOTS, peak, code, flushes);
        Log(line);
    }
}

void Kernel::Run()
{
    while (running) {
        ServiceInput();
        if (application_mode) PumpApplication();
        else if (gdi_view ? gdi_dirty : mines_view ? mine_dirty : (full_dirty || status_dirty || taskbar_dirty)) Paint();
        timer.MsDelay(5);
    }
    Log("NEO98 HALTED");
}
