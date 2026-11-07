// SPDX-License-Identifier: GPL-2.0-or-later
// The DOS program as a process of its own loader: the third entry side
// beside the PE and NE loaders. CreateProcess, WinExec and ShellExecute
// arrive here through Application::Start with an MZ file that carries no PE
// or NE header, a.COM, a.BAT or a.PIF. The process has a process
// identity, an exit code and a waitable handle like any other; its one thread
// runs no x86 code but a native loop that owns the window of the class `tty`,
// the way WINOLDAP's task owns the MS-DOS window under Win98. The process has
// guest memory but no processor until it loads an x86 module
// (AttachProcessor). The machine behind the window is the provider of
// src/dos_vm.h; without one the loader refuses the program.
// What Win98 does (WINOA386.MOD of the local tree, strings "tty", "Finished",
// "%s - %s"): the window title is the PIF title while the program runs, the
// program's own name is appended when the prompt starts another program,
// "Finished - " is put in front when the program has ended and the PIF asks
// to keep the window. Closing the window ends the machine; while a program
// still runs it asks first, as WINOLDAP does.
#include "x86_app.h"
#include "x86_address_space.h"
#include "jit_memory.h"
#include "dos_vm.h"
#include "x86_state_dos_ui.h"
#include "win98_pifmgr.h"
#include "win98_ne_image.h"
#include "dos_vm_truetype.h"
#include "sound.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
// The 3dfx Voodoo of the machine is drawn by the GPU backend, which only a
// hosted build has (src/dos_voodoo.h); the kernel build knows no card.
#if __has_include(<vector>)
#include "dos_voodoo.h"
#include "user.h"
#define NEO98_DOS_VOODOO 1
#endif

namespace X86
{
namespace
{
// The native window procedure token of the `tty` class, dispatched by
// Application::CallWindow like the other native tokens.
constexpr unsigned TtyProcedure = 0x71000040;
constexpr unsigned WindowStyle = 0x00cb0000; // WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
constexpr unsigned TextWidth = 640, TextHeight = 400;
constexpr unsigned long long FrameWait = 15000, IdleWait = 100000;
// A program that reads the linear frame buffer of its 3dfx Voodoo stands
// still in every such read until this loop has drawn what it drew: the loop
// looks for the card's frames this often while such reads keep coming.
constexpr unsigned long long VoodooReadWait = 1000, VoodooReadSpan = 1000000;
// The toolbar of the MS-DOS window: WINOLDAP shows it when the WIN group of
// the PIF says so, a strip above the DOS screen with the font size box and
// seven flat buttons. The button images are bitmap 4 of WINOA386.MOD,
// 128x16 in 16 colours, eight pictures of 16x16: Mark, Copy, Paste, Font,
// Properties, Full screen, Background, and one for the exclusive mode. The
// strip is 28 pixels high, the buttons 23x22 like a flat toolbar button of
// COMCTL32, the box 62x21, a gap of 8 between the groups.
constexpr int ToolbarHeight = 28, ButtonWidth = 23, ButtonHeight = 22, ComboLeft = 4, ComboWidth = 62, ComboHeight = 21,
              ButtonTop = 3, ButtonsLeft = ComboLeft + ComboWidth + 8, GroupGap = 8;
struct ToolbarButton { const char *name; unsigned image; };
constexpr ToolbarButton ToolbarButtons[7] = {{"Mark", 0}, {"Copy", 1}, {"Paste", 2}, {"Full screen", 5},
                                             {"Properties", 4}, {"Background", 6}, {"Font", 3}};
constexpr unsigned ToolbarImageWidth = 128, ToolbarImageHeight = 16;

const char *Leaf(const char *path)
{
    const char *leaf = path;
    for (const char *p = path; *p; ++p)
        if (*p == '\\' || *p == '/' || *p == ':') leaf = p + 1;
    return leaf;
}
// Case-insensitive comparisons of DOS names.
unsigned Fold(unsigned c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
bool SameText(const char *a, const char *b)
{
    for (; *a || *b; ++a, ++b)
        if (Fold((unsigned char)*a) != Fold((unsigned char)*b)) return false;
    return true;
}
bool SamePrefix(const char *a, const char *b, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
    {
        if (Fold((unsigned char)a[i]) != Fold((unsigned char)b[i])) return false;
        if (!a[i]) return true;
    }
    return true;
}
bool EndsWith(const char *text, const char *suffix)
{
    const unsigned n = strlen(text), m = strlen(suffix);
    return n >= m && SameText(text + n - m, suffix);
}
void Directory(const char *path, char (&out)[260])
{
    snprintf(out, sizeof out, "%s", path);
    char *slash = strrchr(out, '\\');
    if (!slash) { out[0] = 0; return; }
    // Keep the root's backslash: "D:\" stays "D:\".
    if (slash == out + 2 && out[1] == ':') slash[1] = 0;
    else *slash = 0;
}
// Whether a name is a plain 8.3 name DOS takes as it is.
bool ShortName(const char *name)
{
    unsigned base = 0, ext = 0;
    bool dot = false;
    for (const char *p = name; *p; ++p)
    {
        const unsigned char c = (unsigned char)*p;
        if (c == '.') { if (dot) return false; dot = true; continue; }
        if (c <= ' ' || c > 126 || strchr("\"*+,/:;<=>?[\\]|", c)) return false;
        if (dot) ++ext; else ++base;
    }
    return base && base <= 8 && ext <= 3;
}
} // namespace
bool DosVmEndsWith(const char *text, const char *suffix) { return EndsWith(text, suffix); }

// What the loop keeps for one DOS process.
struct Application::DosVmState
{
    DosVm::Launch launch;
    DosVm::Session *session = nullptr;
    unsigned window = 0;
    char title[80] = {};          // the PIF title, or the program's name
    char shown_title[120] = {};   // what the caption says right now
    char running[9] = {};         // the program DOS runs, as the provider reports it
    bool finished = false, closed = false;
    unsigned exit_code = 0;
    // The picture of the machine as the window shows it: in a window the
    // frame scaled by a whole factor, on the full screen stretched to the
    // screen. `raw` keeps the machine's own pixels so a mode switch of the
    // window can scale them again.
    unsigned *pixels = nullptr, *raw = nullptr;
    unsigned width = 0, height = 0, scale = 1, seen = 0;
    unsigned raw_width = 0, raw_height = 0;
    // A picture the host wants shown until the machine draws its next: the
    // one a saved state was made with. Taken by the
    // next turn of the service.
    unsigned *shown = nullptr;
    unsigned shown_width = 0, shown_height = 0;
    // The full screen the host wants for the window, as the state had it:
    // -1 none, 0 a window, 1 the full screen.
    int wanted_fullscreen = -1;
    unsigned client_width = TextWidth, client_height = TextHeight;
    // Full screen, Alt+Enter: the window loses its frame and covers the
    // screen; the rectangle and the style of the window come back with it.
    bool fullscreen = false;
    Gdi::RECT windowed_rect = {};
    unsigned windowed_style = WindowStyle;
    // The keys the machine holds down, by scancode and extended bit, so the
    // window can release them when it loses the keyboard.
    unsigned char keys_down[2][128] = {};
    unsigned keys_down_vk[2][128] = {};
    // The mouse: the DOS mouse counts steps, the window sees positions.
    bool mouse_seen = false;
    int mouse_x = 0, mouse_y = 0;
    // The pointer's place on the DOS picture, 0 to 65535 of its width and
    // height; with the exclusive mouse it moves by the steps.
    unsigned place_x = 32768, place_y = 32768;
    // The icon file and index of the PIF; PIFMGR.DLL 0 is the MS-DOS icon.
    char icon_file[81] = {};
    unsigned icon_index = 0;
    unsigned pif_flags = 0; // flPrgInit of PROPPRG: NOPIF, DEFAULTPIF
    // A console program DOS waits for: its process handle until it ends.
    unsigned waiting_process = 0;
    // The toolbar: shown per the WIN group of the PIF, its images from
    // WINOA386.MOD, 128x16 XRGB composed on the button face colour.
    bool toolbar = false;
    unsigned *toolbar_images = nullptr;
    int Strip() const { return toolbar && !fullscreen && toolbar_images ? ToolbarHeight : 0; }
    // Mark: the pointer selects a block of text cells, drawn inverted;
    // Copy puts the block on the clipboard. Background: the machine runs
    // while the window is behind another (TSK_BACKGROUND of the PIF).
    bool marking = false, selecting = false, selected = false, background = true, paused = false;
    int sel_x0 = 0, sel_y0 = 0, sel_x1 = 0, sel_y1 = 0; // cells
    unsigned text_columns = 0, text_rows = 0;
    unsigned sound = 0; // the mixer source of the machine's sound
    unsigned long long sound_submitted = 0; // bytes given to it so far
    // The file the property sheet of the window opens: the PIF or the program.
    char props_path[260] = {};
    // The Misc page, applied by the window while the program runs: the
    // question on close (TSK_NOWARNTERMINATE clear), Alt+Enter kept by
    // Windows (KBD_NOALTENTER clear), QuickEdit (MSE_WINDOWENABLE clear:
    // the mouse marks text instead of reaching DOS) and the exclusive mouse
    // (MSE_EXCLUSIVE: the window captures the pointer on a click).
    bool warn_close = true, alt_enter = true, quickedit = false, exclusive = false, captured = false;
    // The text mode in a window, the way WINOLDAP draws it: the character
    // cells in a raster size of the WOAFont (FNT 2.0 resources), the size of
    // the Font page (FNT group: flags, cx at +8, cy at +0xa) or, for Auto,
    // the largest one whose window fits the screen.
    unsigned char *font = nullptr;
    unsigned font_bytes = 0;
    bool font_tried = false;
    struct Face { unsigned width, height, offset, first, last; } faces[16] = {};
    unsigned face_count = 0;
    unsigned fnt_flags = 0x1c, fnt_width = 0, fnt_height = 0;
    bool text_picture = false; // the window shows the cells, not the machine's pixels
    bool zoomed = false;       // the window is maximized (WM_SIZE)
    // A TrueType size of the Font page: Lucida Console of the tree.
    DosVmTrueType::Face *truetype = nullptr;
    bool truetype_tried = false;
    // The shortcut key of the Program page, in the form WM_SETHOTKEY takes.
    unsigned hotkey = 0;
    // The size box of the toolbar: a COMBOBOX child of the window, owner
    // drawn like the size list of the Font page, shown with the strip.
    unsigned font_box = 0;
    bool font_box_shown = false;
    // The window's property sheet is open (its modal loop runs).
    bool sheet_open = false;
    unsigned cursor_phase = 0;
    // On the full screen the display is the VM's, as on Win98: no Windows
    // pointer, only the one the program or the DOS mouse driver draws. In a
    // window the Windows pointer stays, except over a Voodoo picture.
    bool OwnPointer() const
    {
#ifdef NEO98_DOS_VOODOO
        if (voodoo_shown) return !finished;
#endif
        return fullscreen && !finished;
    }
#ifdef NEO98_DOS_VOODOO
    // The 3dfx Voodoo of the machine, once the program has switched it on;
    // while its output is on, the picture of the window is the card's.
    DosVoodoo::Renderer *voodoo = nullptr;
    bool voodoo_shown = false, voodoo_overlay = false;
    unsigned long long voodoo_time = 0;   // microseconds spent drawing, for the log
    unsigned long long voodoo_read = 0;   // when the program last read the linear frame buffer
    ~DosVmState()
    {
        if (voodoo_overlay) User::ClearOverlay();
        delete voodoo;
        delete[] pixels; delete[] raw; delete[] toolbar_images; delete[] font; delete truetype;
    }
#else
    ~DosVmState() { delete[] pixels; delete[] raw; delete[] toolbar_images; delete[] font; delete truetype; }
#endif
};
// The display as it is now: a program before the DOS box may have left
// another mode than the 800x600 of the start (the reference system's launcher runs on
// a larger desktop), and a full screen box has to cover that one.
struct ScreenExtent
{
    bool wide;
    operator unsigned() const
    {
        int width = 800, height = 600, depth = 0;
        User::ScreenMode(width, height, depth);
        return (unsigned)(wide ? (width > 0 ? width : 800) : (height > 0 ? height : 600));
    }
};
constexpr ScreenExtent ScreenWidth{true}, ScreenHeight{false};

bool Application::DosVmProcess() const { return dos_vm != nullptr; }

// The DOS spelling of a guest path: every component that is not a plain 8.3
// name is replaced by the alias the file store keeps for it, which is the
// name the DOS side of the same directory shows as well.
bool Application::DosVmPath(const char *guest, char (&out)[260])
{
    char full[260];
    if (files.FullPath(guest, full)) return false;
    unsigned n = 0;
    out[0] = 0;
    char prefix[260] = {};
    const char *p = full;
    // Drive letter and colon.
    if (!(p[0] && p[1] == ':')) return false;
    prefix[0] = out[0] = (char)(p[0] & 0x5f);
    prefix[1] = out[1] = ':';
    n = 2;
    p += 2;
    while (*p)
    {
        while (*p == '\\') ++p;
        if (!*p) break;
        const char *end = p;
        while (*end && *end != '\\') ++end;
        char component[256];
        const unsigned length = (unsigned)(end - p) < sizeof component - 1 ? (unsigned)(end - p) : sizeof component - 1;
        memcpy(component, p, length);
        component[length] = 0;
        snprintf(prefix + strlen(prefix), sizeof prefix - strlen(prefix), "\\%s", component);
        const char *spelling = component;
        if (!ShortName(component))
        {
            unsigned node = 0;
            if (files.store.Lookup(prefix, node) || !files.store.Get(node)->alias[0]) return false;
            spelling = files.store.Get(node)->alias;
        }
        if (n + strlen(spelling) + 2 >= sizeof out) return false;
        out[n++] = '\\';
        strcpy(out + n, spelling);
        n += strlen(spelling);
        p = end;
    }
    if (n == 2) { out[2] = '\\'; out[3] = 0; }
    return true;
}

const char *Application::DosSettingsPath(unsigned window)
{
    auto *app = static_cast<Application *>(User::ProcessContextOf(User::WindowProcess(window)));
    return app && app->dos_vm ? app->dos_vm->props_path : nullptr;
}

bool Application::StartDosVm(const unsigned char *font, unsigned font_length, const char *name, const char *command)
{
    const auto *provider = DosVm::Current();
    if (!provider)
    {
        Fail("No DOS machine provider is installed");
        return false;
    }
    snprintf(program_name, sizeof program_name, "%s", name);
    state = Running;
    font_bytes = font;
    font_size = font_length;
    if (unsigned registry_error = Registry::SystemInitializationError())
    {
        last_error = registry_error;
        Fail("Installed registry configuration could not be loaded");
        return false;
    }
    Files::SetClock(&Application::FileClock);
    char path[260];
    if (strchr(name, '\\') || strchr(name, '/') || strchr(name, ':'))
    {
        if (files.FullPath(name, path)) { Fail("Invalid executable path"); return false; }
    }
    else snprintf(path, sizeof path, "C:\\WINDOWS\\%s", name);
    dos_vm = new DosVmState;
    if (!dos_vm) { Fail("DOS process state allocation failed"); return false; }
    auto &d = *dos_vm;
    // The settings come from PIFMGR, the way WINOLDAP reads them before it
    // builds the MS-DOS window: the PIF itself, the PIF beside the program,
    // WINDOWS\PIF, _DEFAULT.PIF, or the defaults of the module. PROPPRG
    // carries title, command line, working directory, close on exit and the
    // icon; VID the full screen bit, MEM the memory, TSK the idle sensitivity.
    char program[260], directory[260], parameters[300] = {};
    char pif_directory[260];
    snprintf(d.props_path, sizeof d.props_path, "%s", path);
    Directory(path, pif_directory);
    {
        auto *record = new Win98PifMgr::Record;
        if (!record || !PifMgrOpenPath(path, *record))
        {
            delete record;
            last_error = 2;
            Fail("PIFMGR refused the program");
            return false;
        }
        unsigned char prg[Win98PifMgr::PrgBytes], mem[16], vid[12], tsk[14], win[38];
        if (!Win98PifMgr::GetPrg(*record, prg, sizeof prg, 0)) { delete record; Fail("PIFMGR answered no program group"); return false; }
        Win98PifMgr::GetMem(*record, mem, sizeof mem);
        Win98PifMgr::Get(*record, Win98PifMgr::GroupVid, vid, sizeof vid, 0);
        Win98PifMgr::Get(*record, Win98PifMgr::GroupTsk, tsk, sizeof tsk, 0);
        Win98PifMgr::Get(*record, Win98PifMgr::GroupWin, win, sizeof win, 0);
        d.toolbar = (Win98PifMgr::Detail::Word(win) & 2) != 0; // WIN_TOOLBAR
        d.pif_flags = Win98PifMgr::Detail::Word(prg + Win98PifMgr::PrgFlagsInit);
        d.hotkey = Win98PifMgr::Detail::Word(prg + Win98PifMgr::PrgHotKey);
        DosVmLiveSettings(*record);
        snprintf(d.title, sizeof d.title, "%s", reinterpret_cast<const char *>(prg + Win98PifMgr::PrgTitle));
        snprintf(d.icon_file, sizeof d.icon_file, "%s", reinterpret_cast<const char *>(prg + Win98PifMgr::PrgIconFile));
        d.icon_index = Win98PifMgr::Detail::Word(prg + Win98PifMgr::PrgIconIndex);
        d.launch.close_on_exit = (Win98PifMgr::Detail::Word(prg + Win98PifMgr::PrgFlags) & Win98PifMgr::PrgCloseOnExit) != 0;
        // In a machine that ends with its program the window goes with the
        // program, unless the program has a PIF of its own that says
        // otherwise: _DEFAULT.PIF and the defaults of PIFMGR are nobody's
        // choice for this program (src/dos_vm.h).
        if (DosVm::EndsWithProgram() && !(record->flags & Win98PifMgr::RecordFileNamed)) d.launch.close_on_exit = true;
        d.launch.fullscreen = (Win98PifMgr::Detail::Word(vid) & 0x0100) != 0;
        const unsigned xms_maximum = Win98PifMgr::Detail::Word(mem + 0xe), ems_maximum = Win98PifMgr::Detail::Word(mem + 0xa);
        d.launch.xms = xms_maximum != 0;
        d.launch.ems = d.launch.ems && ems_maximum != 0;
        if (xms_maximum != 0xffff && xms_maximum)
        {
            unsigned mb = (xms_maximum + 1023) / 1024 + 1;
            d.launch.memory_mb = mb < 4 ? 4 : mb > 64 ? 64 : mb;
        }
        DosVmMachineSettings(*record, prg, mem, vid);
        delete record;
        // The command line: the program, then the parameters, the program
        // quoted when its path has a space (PIFMGR ).
        const char *line = reinterpret_cast<const char *>(prg + Win98PifMgr::PrgCmdLine);
        unsigned i = 0, n = 0;
        if (line[0] == '"') { for (i = 1; line[i] && line[i] != '"'; ++i) if (n + 1 < sizeof program) program[n++] = line[i]; if (line[i] == '"') ++i; }
        else for (; line[i] && line[i] != ' '; ++i) if (n + 1 < sizeof program) program[n++] = line[i];
        program[n] = 0;
        while (line[i] == ' ') ++i;
        snprintf(parameters, sizeof parameters, "%s", line + i);
        snprintf(directory, sizeof directory, "%s", reinterpret_cast<const char *>(prg + Win98PifMgr::PrgWorkDir));
        if (directory[0] == '"') { unsigned k = 0; for (const char *q = directory + 1; *q && *q != '"'; ++q) directory[k++] = *q; directory[k] = 0; }
        if (!program[0]) { last_error = 2; Fail("The PIF names no program"); return false; }
        // A relative program is relative to the PIF. A path that does not
        // exist is reported missing, as Win98 does; a PIF that still names
        // the drive of another machine has to be corrected.
        char full[260];
        unsigned node = 0;
        char relative[260];
        if (!(strchr(program, ':') || program[0] == '\\')) snprintf(relative, sizeof relative, "%s\\%s", pif_directory, program);
        else snprintf(relative, sizeof relative, "%s", program);
        // A program started by its own name runs itself; the PIF beside it
        // lends only its settings, the way WINOLDAP runs the file
        // CreateProcess named. Only a started PIF names the program (
        // LOEWE.EXE did not start because LOEWE.PIF names C:\GAMES\LOEWE of
        // the old machine).
        const bool started_pif = EndsWith(path, ".PIF");
        bool pif_program_found = !(files.FullPath(relative, full) || files.store.Lookup(full, node));
        if (!started_pif && !(pif_program_found && SameText(full, path)))
        {
            snprintf(full, sizeof full, "%s", path);
            pif_program_found = true;
            parameters[0] = 0;
            if (command && *command)
            {
                const char *p = command;
                if (*p == '"') { ++p; while (*p && *p != '"') ++p; if (*p) ++p; }
                else while (*p && *p != ' ' && *p != '\t') ++p;
                while (*p == ' ' || *p == '\t') ++p;
                snprintf(parameters, sizeof parameters, "%s", p);
            }
            if (!directory[0]) snprintf(directory, sizeof directory, "%s", pif_directory);
        }
        if (!pif_program_found)
        {
            last_error = 2;
            char text[400];
            snprintf(text, sizeof text, "The PIF's program %s was not found", program);
            Fail(text);
            return false;
        }
        snprintf(program, sizeof program, "%s", full);
        if (!directory[0]) Directory(program, directory);
        else if (!strcmp(directory, ".\\") || !strcmp(directory, ".")) snprintf(directory, sizeof directory, "%s", pif_directory);
        // A program without a PIF keeps the parameters of its command line.
        if ((d.pif_flags & Win98PifMgr::PrgInitNoPif) && command && *command)
        {
            const char *p = command;
            if (*p == '"') { ++p; while (*p && *p != '"') ++p; if (*p) ++p; }
            else while (*p && *p != ' ' && *p != '\t') ++p;
            while (*p == ' ' || *p == '\t') ++p;
            snprintf(parameters, sizeof parameters, "%s", p);
        }
    }
    {
        char length_check[260];
        if (files.FullPath(directory, length_check) == 0) snprintf(directory, sizeof directory, "%s", length_check);
        else Directory(program, directory);
    }
    // The DOS spellings. The working directory is the PIF's when it exists,
    // otherwise the program's own.
    char dos_program[260], dos_directory[260], program_directory[260];
    if (!DosVmPath(program, dos_program)) { last_error = 2; Fail("The DOS program's path has no DOS spelling"); return false; }
    if (!DosVmPath(directory, dos_directory)) Directory(dos_program, dos_directory);
    Directory(dos_program, program_directory);
    snprintf(d.launch.directory, sizeof d.launch.directory, "%s", dos_directory);
    const char *invoke = SameText(program_directory, dos_directory) ? Leaf(dos_program) : dos_program;
    if (parameters[0]) snprintf(d.launch.command, sizeof d.launch.command, "%s %s", invoke, parameters);
    else snprintf(d.launch.command, sizeof d.launch.command, "%s", invoke);
    // COMMAND.COM uses the DOS machine's shell. Only a bare invocation is
    // interactive: dropping /C would leave CRT _popen waiting forever.
    if (SameText(Leaf(dos_program), "COMMAND.COM"))
    {
        if(parameters[0]) snprintf(d.launch.command,sizeof d.launch.command,"Z:\\COMMAND.COM %s",parameters);
        else d.launch.command[0]=0;
    }
    // The DOS keyboard layout follows the layout of the session, by the
    // language of its layout handle; DOSBox's names for them.
    {
        static const struct { unsigned language; const char *layout; } layouts[] = {
            {0x407, "gr"}, {0x409, "us"}, {0x809, "uk"}, {0x40c, "fr"}, {0x80c, "be"}, {0x410, "it"}, {0x40a, "sp"},
            {0x413, "nl"}, {0x807, "sg"}, {0x41d, "sv"}, {0x414, "no"}, {0x406, "dk"}, {0x40b, "su"}, {0x816, "po"},
            {0x416, "br"}, {0x405, "cz"}, {0x415, "pl"}, {0x40e, "hu"}, {0x419, "ru"}};
        const unsigned language = keyboard_layout_state.entries[0].layout & 0xffff;
        for (const auto &entry : layouts)
            if (entry.language == language) snprintf(d.launch.keyboard_layout, sizeof d.launch.keyboard_layout, "%s", entry.layout);
    }
    if (!ConfigureMemory()) return false;
    // The process runs no x86 code: its thread is the native loop of
    // RunDosVm, the DOS program runs in the machine. It still needs guest
    // memory, because USER, GDI and the kernel of the port keep the records
    // of a process there, so it gets an address space without a processor
    // (CpuBackend::None, src/x86_address_space.h). The fixed stack, the gate
    // region, the LDT and the page of GDT, TEB and PEB are reserved as
    // Application::Start reserves them, so the heap search cannot take them
    // and a processor finds them free when the process gets one
    // (AttachProcessor below).
    if (!Check(CreateCpu(CpuBackend::None, cpu, "dos-process"))) return false;
    if (!Map(0x0fe00000, 0x200000, 3) || !Map(GateBase, GateRegionBytes, 3) || !Map(Win16LdtBase, Win16LdtBytes, 3) ||
        !Map(SystemPageBase, SystemPageBytes, 3) || !MapProcessHeap())
        return false;
    JoinSession();
    // The TEB and the process records are memory the native kernel reads
    // for this process as for any other.
    const unsigned teb = MainTeb;
    Write32(teb, 0xffffffff);
    Write32(teb + 4, 0x10000000);
    Write32(teb + 8, 0x0fe00000);
    Write32(teb + 0x18, teb);
    Write32(teb + 0x1c, 0xffff0001);
    Write32(teb + 0x2c, teb + 0x88);
    threads[0].id = AllocateThreadIdentity();
    if (!threads[0].id) { Fail("Global thread identity capacity exhausted"); return false; }
    Write32(teb + 0x20, process_id);
    Write32(teb + 0x24, threads[0].id);
    threads[0].app = this;
    threads[0].keyboard_layout = keyboard_layout_state.entries[0].layout;
    User::SetKeyboardLanguage(keyboard_layout_state.entries[0].layout);
    InitializeThreadControl(0);
    if (!InitializeProcessDatabase()) { Fail("Process database allocation failed"); return false; }
    Write32(teb + 0x30, process_database);
    modules[0].kind = 5;
    modules[0].handle = 0;
    modules[0].bound = true;
    snprintf(modules[0].path, sizeof modules[0].path, "%s", path);
    command_line = String(command ? command : path);
    crt_data = Allocate(64);
    if (crt_data) Write32(crt_data, command_line);
    if (!InitializeProcessEnvironment()) { Fail("Process environment allocation failed"); return false; }
    if (!InitializeArguments() || !InitializeNls() || !InitializeThreadCrt()) return false;
#ifdef NEO98_RUNTIME_CONTENT
    // USER creates its first MS Sans Serif font before the stock objects.
    // Release hosts must have read the installed catalogue by then.
    LoadInstalledFonts();
#endif
    if (!User::Initialize()) { Fail("Native USER initialization failed"); return false; }
    if (!User::AttachProcess(process_id, this)) { Fail("Desktop process capacity exhausted"); return false; }
    user_initialized = true;
    User::BindTask(process_id, threads[0].id);
    if (!InitializeUserProviders(0x400)) return false; // WINOLDAP is a 4.0 module
    if (!InstallStockFonts()) return false;
    const unsigned colors[] = {0xffffff, 0xc0c0c0, 0x808080, 0x404040, 0};
    for (unsigned i = 0; i < 5; ++i) stock[i] = Gdi::CreateSolidBrush(colors[i]);
    stock[15] = Gdi::DefaultPalette();
    stock[5] = Gdi::CreateNullBrush();
    stock[6] = Gdi::CreatePen(0, 0, 0xffffff);
    stock[7] = Gdi::CreatePen(0, 0, 0);
    stock[8] = Gdi::CreatePen(5, 0, 0);
    LoadDesktopWallpaper();
    // The scheduler saves and restores the context of every thread; the
    // address space answers with an empty one.
    auto &thread = threads[0];
    thread.fiber = NewThreadFiber();
    if (!thread.fiber || !Check(cpu->NewContext(thread.context)) || !Check(cpu->Save(thread.context)) ||
        !thread.fiber->Start([](void *p) { static_cast<Application *>(p)->RunDosVm(); }, this))
    {
        Fail("Execution fiber allocation failed");
        return false;
    }
    if (log)
    {
        char text[700];
        snprintf(text, sizeof text, "DOSVM program %s command=\"%s\" directory=\"%s\" title=\"%s\" close=%u fullscreen=%u xms=%u ems=%u memory=%u",
                 path, d.launch.command, d.launch.directory, d.title, d.launch.close_on_exit ? 1u : 0u,
                 d.launch.fullscreen ? 1u : 0u, d.launch.xms ? 1u : 0u, d.launch.ems ? 1u : 0u, d.launch.memory_mb);
        log(text);
    }
    return true;
}

// The processor of the DOS process. The MS-DOS window is a Windows task
// under Win98 (WINOLDAP), and the property sheet its Properties and Font
// buttons open is COMCTL32's (PifPropertySheet, src/x86_pif_pages.cpp):
// loading COMCTL32 runs its DllMain, and its tab control and trackbars run
// as x86 code. The first x86 module the process loads therefore brings the
// processor and the x86 surroundings of a PE process without an image, the
// ones Application::Start builds: the gate region with its trap bytes and
// hooks, GDT, LDT, segment registers and the fixed stack. The engine is
// Unicorn, as for Win16: the pages of the address space go over by copy and
// the caller bytes (session blocks, DIB bits) by MapHost at any host
// address, which FEXCore can share only at the offset of its own 16-KiB
// pages, one source per host page; reservations of
// the shared arena, where DIB bits lie, are exact to 4 KiB. A process of another kind without a processor does
// not exist; asking for one there is a fault of the port.
bool Application::AttachProcessor()
{
    if (HasProcessor()) return true;
    if (!dos_vm || !cpu) { Fail("x86 module in a process without a processor"); return false; }
#if !defined(__APPLE__) && !defined(__linux__)
    // Unicorn aborts when its code-cache allocation fails, so refuse early.
    if (!neo98_jit_free_slots()) { Fail("No free translation cache slot for another x86 engine"); return false; }
#endif
    Cpu *space = cpu, *processor = nullptr;
    const auto backend = ProcessCpuBackend() == CpuBackend::Native ? CpuBackend::Native : CpuBackend::Unicorn;
    if (!Check(CreateCpu(backend, processor, "dos-process-x86-module"))) return false;
    // The CPUID answer comes from a page of its own before the pages of the
    // process arrive, as in Application::Start.
    cpu = processor;
    if (!InitializeCpuDescription() || !InitializeFpu() || !Check(CopyAddressSpace(*space, *processor)))
    {
        cpu = space;
        delete processor;
        return false;
    }
    for (auto &t : threads)
        if (t.context) { space->FreeContext(t.context); t.context = nullptr; }
    delete space;
    constexpr unsigned Gates = GateBase, Exit = 0x7000ffe0; // the gate region and exit gate of x86_app.cpp
    {
        static unsigned char trap[65536];
        memset(trap, 0xf4, sizeof trap);
        for (unsigned offset = 0; offset < GateRegionBytes; offset += sizeof trap) Store(Gates + offset, trap, sizeof trap);
        Protect(Gates, GateRegionBytes, 5);
    }
    const unsigned long long teb = MainTeb;
    const unsigned long long descriptors[] = {
        0, 0x00cf9a000000ffffull, 0x00cf92000000ffffull,
        0xfffull | ((teb & 0xffffff) << 16) | (0x93ull << 40) | (0x40ull << 48) | ((teb >> 24) << 56),
        (unsigned long long)(Win16LdtBytes - 1) | ((unsigned long long)(Win16LdtBase & 0xffffff) << 16) |
            (0x82ull << 40) | ((unsigned long long)(Win16LdtBase >> 24) << 56)};
    Store(SystemPageBase, descriptors, sizeof descriptors);
    DescriptorTable gdt;
    gdt.base = SystemPageBase;
    gdt.limit = (3 + ThreadCapacity) * 8 - 1;
    Check(cpu->SetWide(Reg::Gdtr, &gdt));
    DescriptorTable ldt;
    ldt.base = Win16LdtBase;
    ldt.limit = Win16LdtBytes - 1;
    Check(cpu->SetWide(Reg::Ldtr, &ldt));
    Register(Reg::Cr0, 1);
    Register(Reg::Cs, 8);
    const int segments[] = {Reg::Ds, Reg::Es, Reg::Ss, Reg::Gs};
    for (int reg : segments) Register(reg, 16);
    Register(Reg::Fs, 24);
    Check(cpu->SetGateHook(Gates, Gates + GateRegionBytes - 1, Gate, this));
    Check(cpu->SetFaultHook(MemoryFaultHook, this));
    AddSliceHook();
    Check(cpu->SetInterruptHook(GuestInterrupt, this));
    Check(cpu->SetPortHooks(GuestPortInput, GuestPortOutput, this));
    Register(Reg::Esp, 0x0fffe000);
    Write32(0x0fffe000, Exit);
    Register(Reg::Eflags, 2);
    // Every thread starts from this state; the DOS process has one.
    for (auto &t : threads)
        if (t.fiber && (!Check(cpu->NewContext(t.context)) || !Check(cpu->Save(t.context)))) return false;
    if (log)
    {
        char text[120];
        snprintf(text, sizeof text, "DOSVM processor attached process=%u", process_id);
        log(text);
    }
    return state != Failed;
}

// The caption: "<title>", "<title> - <PROGRAM>" while the prompt runs a
// program of its own, "Finished - <title>" after the end.
void Application::DosVmTitle()
{
    auto &d = *dos_vm;
    char text[120];
    if (d.finished) snprintf(text, sizeof text, "Finished - %s", d.title);
    else if (d.running[0] && strcmp(d.running, "DOSBOX") && strcmp(d.running, "NEO98END") &&
             (!d.launch.command[0] || !SamePrefix(d.running, Leaf(d.launch.command), strlen(d.running))))
        snprintf(text, sizeof text, "%s - %s", d.title, d.running);
    else snprintf(text, sizeof text, "%s", d.title);
    if (!strcmp(text, d.shown_title) || !d.window) return;
    snprintf(d.shown_title, sizeof d.shown_title, "%s", text);
    User::SetWindowText(d.window, text);
}

// The button images: bitmap 4 of WINOA386.MOD, RLE4 with a 16 colour
// palette, composed on the button face colour into 128x16 XRGB pixels.
// Without the module, or without the bitmap, the window has no toolbar.
bool Application::DosVmLoadToolbar()
{
    auto &d = *dos_vm;
    if (!d.toolbar || d.toolbar_images) return d.toolbar_images != nullptr;
    unsigned handle = 0, status = 0, size = 0, done = 0;
    if (files.Open("C:\\WINDOWS\\SYSTEM\\WINOA386.MOD", 1, 3, 3, 0x80, handle, status)) return false;
    unsigned char *module = nullptr;
    bool ok = !files.Size(handle, size) && size >= 64 && size <= (1u << 20);
    if (ok) { module = new unsigned char[size]; ok = module && !files.Read(handle, module, size, done) && done == size; }
    files.Close(handle);
    unsigned offset = 0, bytes = 0, error = 0;
    Win98Ne::Image image;
    if (ok) ok = image.Parse(module, size, error) && image.ResourceAt(0x8002, 0x8004, nullptr, offset, bytes) && bytes >= 40 + 64;
    if (ok)
    {
        const unsigned char *b = module + offset;
        const unsigned header = b[0] | b[1] << 8, width = b[4] | b[5] << 8, height = b[8] | b[9] << 8,
                       bits = b[14] | b[15] << 8, compression = b[16] | b[17] << 8;
        ok = header == 40 && width == ToolbarImageWidth && height == ToolbarImageHeight && bits == 4 && compression == 2;
        if (ok)
        {
            unsigned palette[16];
            for (unsigned i = 0; i < 16; ++i) palette[i] = b[40 + 4 * i] | b[41 + 4 * i] << 8 | b[42 + 4 * i] << 16;
            d.toolbar_images = new unsigned[ToolbarImageWidth * ToolbarImageHeight];
            for (unsigned i = 0; i < ToolbarImageWidth * ToolbarImageHeight; ++i) d.toolbar_images[i] = 0xc0c0c0;
            const unsigned char *p = b + 40 + 64, *end = module + offset + bytes;
            unsigned x = 0, y = 0;
            auto put = [&](unsigned v) {
                if (x < width && y < height && v != 8) d.toolbar_images[(height - 1 - y) * width + x] = palette[v];
                ++x;
            };
            while (p + 1 < end && y < height)
            {
                const unsigned n = p[0], c = p[1];
                p += 2;
                if (n)
                    for (unsigned k = 0; k < n; ++k) put(k & 1 ? c & 15 : c >> 4);
                else if (c == 0) { x = 0; ++y; }
                else if (c == 1) break;
                else if (c == 2) { if (p + 1 < end) { x += p[0]; y += p[1]; } p += 2; }
                else
                {
                    for (unsigned k = 0; k < c && p + k / 2 < end; ++k) put(k & 1 ? p[k / 2] & 15 : p[k / 2] >> 4);
                    p += (c + 1) / 2;
                    if ((c + 1) / 2 & 1) ++p;
                }
            }
        }
    }
    if (log)
    {
        char text[160];
        snprintf(text, sizeof text, "DOSVM toolbar images=%u module=%u resource=%u bytes=%u", d.toolbar_images ? 1u : 0u, size, offset, bytes);
        log(text);
    }
    delete[] module;
    return d.toolbar_images != nullptr;
}

// The window: created through the same path a program's CreateWindowExA
// takes, so USER sees an ordinary top-level window of this thread.
bool Application::DosVmCreateWindow()
{
    auto &d = *dos_vm;
    DosVmLoadToolbar();
    if(!InitializeImageResources()) {Fail("DOS window cursor resources could not be initialized");return false;}
    unsigned fields[12] = {48};
    if (!User::GetNativeClassInfo(0, "tty", fields) && !User::RegisterClass("tty", TtyProcedure, 0, 0, 0))
    {
        Fail("The tty window class could not be registered");
        return false;
    }
    Gdi::RECT frame = {0, 0, (int)d.client_width, (int)d.client_height + d.Strip()};
    User::AdjustWindowRect(&frame, WindowStyle, 0, 0);
    const unsigned klass = String("tty"), title = String(d.title);
    if (!klass || !title) { Fail("DOS window strings could not be allocated"); return false; }
    snprintf(d.shown_title, sizeof d.shown_title, "%s", d.title);
    // A second MS-DOS window cascades below the ones already open, a caption
    // height down and to the right, the way new windows cascade under Win98;
    // the first one takes the default place.
    unsigned x = 0x80000000u, y = 0x80000000u;
    {
        unsigned nodes[192], count = 0, open = 0;
        if (User::WindowSnapshot(0, nodes, 192, count))
            for (unsigned i = 0; i < count; ++i)
            {
                char klass_name[16] = {};
                User::GetClassName(nodes[i], klass_name, sizeof klass_name);
                if (!strcmp(klass_name, "tty") && User::IsVisible(nodes[i])) ++open;
            }
        if (open)
        {
            x = 64 + 22 * (open % 8);
            y = 55 + 22 * (open % 8);
        }
    }
    const unsigned arguments[] = {0, klass, title, WindowStyle, x, y,
                                  (unsigned)(frame.right - frame.left), (unsigned)(frame.bottom - frame.top), 0, 0, 0, 0};
    d.window = CreateWindow(arguments);
    Free(klass);
    Free(title);
    if (!d.window) { Fail("The MS-DOS window could not be created"); return false; }
    // The icon the shell shows for the PIF: PIFMGR.DLL at the PIF's index,
    // index 0 the MS-DOS icon, through the same extraction the shell uses.
    // ExtractIconEx packs the small size into the high word.
    {
        const char *file = d.icon_file[0] ? d.icon_file : "C:\\WINDOWS\\SYSTEM\\PIFMGR.DLL";
        unsigned large = 0, small = 0;
        ShellExtractIconPair(file, d.icon_index, (16u << 16) | 32u, (16u << 16) | 32u, 0, large, small);
        if (large) User::SetWindowIcon(d.window, true, large);
        if (small) User::SetWindowIcon(d.window, false, small);
    }
    // The shortcut key of the Program page, as the shell sets it on the
    // window of a program started from a PIF or a link.
    if (d.hotkey) Send(d.window, 0x32, d.hotkey, 0); // WM_SETHOTKEY
    // Run of the Program page: flPrgInit 0x1 minimized. Maximized is the
    // largest window, which for the MS-DOS window is the one it has.
    ShowWindow(d.window, (d.pif_flags & 1) && !d.launch.fullscreen ? 7 : 5);
    NoteDosWindow(d.window, d.Strip());
    // Under a host that keeps DOS windows the PIF's "full screen" starts in
    // the window; Alt+Enter takes the whole display from there.
    if (d.launch.fullscreen && HostKeepsDosWindows())
    {
        if (log) log("DOSVM fullscreen=0 (the host keeps DOS sessions in their windows)");
    }
    else if (d.launch.fullscreen) DosVmFullscreen(true);
    return true;
}

// LUCON.TTF of the tree for a TrueType size of the Font page.
bool Application::DosVmLoadTrueType()
{
    auto &d = *dos_vm;
    if (d.truetype_tried) return d.truetype && d.truetype->Ready();
    d.truetype_tried = true;
    d.truetype = new DosVmTrueType::Face;
    const bool ready = d.truetype && d.truetype->Open("LUCON.TTF");
    if (log) log(ready ? "DOSVM truetype LUCON.TTF ready=1" : "DOSVM truetype LUCON.TTF ready=0");
    return ready;
}

// The raster font of DOS windows: WOAFont in [386Enh] of SYSTEM.INI, as VMM
// and WINOLDAP take it. Setup writes the file of the code page there,
// DOSAPP.FON for 437 (the ISO installation) and APP850.FON for 850 (a German
// installation); a bare name lies in WINDOWS\FONTS. Without the key Win98
// falls back to DOSAPP.FON. The port had APP850.FON written in, so the ISO
// tree offered no raster size at all.
bool Application::DosVmOpenWoaFont(unsigned &handle, char *found, unsigned found_capacity)
{
    char name[128] = {};
    MciProfile("386Enh", "woafont", name, sizeof name, true);
    if (!name[0]) snprintf(name, sizeof name, "%s", "DOSAPP.FON");
    char path[192];
    unsigned status = 0;
    if (strchr(name, '\\') || strchr(name, ':')) snprintf(path, sizeof path, "%s", name);
    else snprintf(path, sizeof path, "C:\\WINDOWS\\FONTS\\%s", name);
    bool opened = !files.Open(path, 1, 3, 3, 0x80, handle, status);
    if (!opened)
    {
        snprintf(path, sizeof path, "C:\\WINDOWS\\SYSTEM\\%s", name);
        opened = !files.Open(path, 1, 3, 3, 0x80, handle, status);
    }
    if (opened && found && found_capacity) snprintf(found, found_capacity, "%s", path);
    return opened;
}

// The WOAFont of the tree: its raster sizes, the FNT 2.0 resources 1 to 16
// (cell width at +0x56, height at +0x58, first and last character at +0x5f
// and +0x60, the glyph table of width and offset words at +0x76).
bool Application::DosVmLoadFont()
{
    auto &d = *dos_vm;
    if (d.font_tried) return d.face_count != 0;
    d.font_tried = true;
    unsigned handle = 0, size = 0, done = 0;
    if (!DosVmOpenWoaFont(handle)) return false;
    if (!files.Size(handle, size) && size > 64 && size < (1u << 20))
    {
        d.font = new unsigned char[size];
        if (d.font && (files.Read(handle, d.font, size, done) || done != size)) { delete[] d.font; d.font = nullptr; }
    }
    files.Close(handle);
    if (!d.font) return false;
    d.font_bytes = size;
    Win98Ne::Image image;
    unsigned error = 0;
    if (!image.Parse(d.font, size, error)) return false;
    for (unsigned id = 1; id <= 16 && d.face_count < 16; ++id)
    {
        unsigned offset = 0, bytes = 0;
        if (!image.ResourceAt(0x8008, 0x8000 | id, nullptr, offset, bytes) || bytes < 0x76 || offset + bytes > size) continue;
        const unsigned char *f = d.font + offset;
        if ((f[0] | f[1] << 8) != 0x200) continue;
        auto &face = d.faces[d.face_count++];
        face = {(unsigned)(f[0x56] | f[0x57] << 8), (unsigned)(f[0x58] | f[0x59] << 8), offset, f[0x5f], f[0x60]};
    }
    return d.face_count != 0;
}

// The window's picture of a text mode: every cell in the face's glyph,
// foreground and background from the attribute in the 16 colours of the
// text mode (bit 7 blinks, so the background has eight), and the cursor as
// its scan lines scaled to the cell, blinking twice a second.
bool Application::DosVmTextPicture()
{
    auto &d = *dos_vm;
    const auto *provider = DosVm::Current();
    if (!provider || !provider->ScreenText || !d.session) return false;
    static unsigned short cells[80 * 60];
    unsigned columns = 0, rows = 0;
    if (!provider->ScreenText(provider->owner, d.session, columns, rows, cells, 80 * 60) || !columns || !rows) return false;
    // The face: the size of the FNT group, the nearest raster height for a
    // TrueType size, or for Auto the largest size whose window fits the
    // work area of the 800x600 screen.
    // A TrueType size (FNT 0x800 without Auto): the cell size of the group,
    // the glyphs of Lucida Console.
    if (!(d.fnt_flags & 0x10) && (d.fnt_flags & 0x800) && d.fnt_width && d.fnt_height && DosVmLoadTrueType())
        return DosVmTextCells(cells, columns, rows, nullptr, d.fnt_width, d.fnt_height);
    if (!DosVmLoadFont()) return false;
    const unsigned face = DosVmFace(columns, rows, DosVmZoomed());
    if (face >= d.face_count) return false;
    return DosVmTextCells(cells, columns, rows, &d.faces[face], d.faces[face].width, d.faces[face].height);
}

// The window is maximized. WINOLDAP's maximized window is not the size of
// the screen: it is as large as the text in its font, in the upper left
// corner of the screen with its frame outside, and the caption shows
// Restore. Only the font "Auto" changes with it, to the largest that fits
// the screen that way.
bool Application::DosVmZoomed() const
{
    return dos_vm && dos_vm->window && !dos_vm->fullscreen && (User::Style(dos_vm->window) & 0x01000000u);
}

// The raster face of the text mode, an index into the faces of the state
// (face_count when there is none): the size of the FNT group or the nearest
// height, or for Auto the largest size whose window fits the work area of
// the screen; a maximized window has its frame outside the screen and only
// the caption and the strip above the text.
unsigned Application::DosVmFace(unsigned columns, unsigned rows, bool zoomed)
{
    auto &d = *dos_vm;
    const DosVmState::Face *face = nullptr;
    if (!(d.fnt_flags & 0x10) && d.fnt_width && d.fnt_height)
    {
        for (unsigned i = 0; i < d.face_count; ++i)
        {
            const auto &candidate = d.faces[i];
            if (candidate.width == d.fnt_width && candidate.height == d.fnt_height) { face = &candidate; break; }
            const int miss = (int)candidate.height - (int)d.fnt_height;
            const int best_miss = face ? (int)face->height - (int)d.fnt_height : 1 << 20;
            if ((miss < 0 ? -miss : miss) < (best_miss < 0 ? -best_miss : best_miss)) face = &candidate;
        }
    }
    else
    {
        unsigned cells[16][2] = {};
        for (unsigned i = 0; i < d.face_count && i < 16; ++i) cells[i][0] = d.faces[i].width, cells[i][1] = d.faces[i].height;
        const unsigned found = DosVmAutoCell(cells, d.face_count, columns, rows, d.Strip(), zoomed);
        if (found < d.face_count) face = &d.faces[found];
    }
    return face ? (unsigned)(face - d.faces) : d.face_count;
}

// The size "Auto" takes: the largest raster cell (width first, then height)
// whose window of columns x rows cells, with its frame and the toolbar strip,
// fits the work area of the screen (the taskbar takes 28 pixels). A
// maximized window has its frame outside the screen. Answers the index into
// `cells`, or `count` when none fits.
unsigned Application::DosVmAutoCell(const unsigned (*cells)[2], unsigned count, unsigned columns, unsigned rows, int strip,
                                    bool zoomed)
{
    Gdi::RECT frame = {0, 0, 0, strip};
    User::AdjustWindowRect(&frame, WindowStyle, 0, 0);
    // The two borders of one direction; a maximized window has them
    // outside the screen.
    const int borders = zoomed ? frame.right - frame.left : 0;
    const int room_x = (int)ScreenWidth - (frame.right - frame.left) + borders;
    const int room_y = (int)ScreenHeight - 28 - (frame.bottom - frame.top) + borders;
    unsigned best = count;
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned w = cells[i][0], h = cells[i][1];
        if ((int)(columns * w) > room_x || (int)(rows * h) > room_y) continue;
        if (best == count || w > cells[best][0] || (w == cells[best][0] && h > cells[best][1])) best = i;
    }
    return best;
}

// The cell "Auto" stands for on the Font page: in the process of a running
// MS-DOS window the one its text takes now, otherwise the one an 80 x 25
// window with or without the toolbar would take.
bool Application::DosVmAutoFor(const unsigned (*cells)[2], unsigned count, bool toolbar, unsigned &width, unsigned &height)
{
    unsigned columns = 80, rows = 25;
    int strip = toolbar ? ToolbarHeight : 0;
    bool zoomed = false;
    if (dos_vm && dos_vm->window && User::IsWindow(dos_vm->window))
    {
        if (dos_vm->text_columns && dos_vm->text_rows) columns = dos_vm->text_columns, rows = dos_vm->text_rows;
        strip = dos_vm->Strip();
        zoomed = DosVmZoomed();
    }
    const unsigned found = DosVmAutoCell(cells, count, columns, rows, strip, zoomed);
    if (found >= count) return false;
    width = cells[found][0];
    height = cells[found][1];
    return true;
}

// The size of the picture the window shows, as it is or as it would be
// maximized: the cells of the text mode in their font, or the pixels of a
// graphics mode as they are.
bool Application::DosVmPictureSize(bool zoomed, unsigned &width, unsigned &height)
{
    auto &d = *dos_vm;
    if (d.text_columns && d.text_rows)
    {
        if (!(d.fnt_flags & 0x10) && (d.fnt_flags & 0x800) && d.fnt_width && d.fnt_height && DosVmLoadTrueType())
        {
            width = d.text_columns * d.fnt_width;
            height = d.text_rows * d.fnt_height;
            return true;
        }
        if (DosVmLoadFont())
        {
            const unsigned face = DosVmFace(d.text_columns, d.text_rows, zoomed);
            if (face < d.face_count)
            {
                width = d.text_columns * d.faces[face].width;
                height = d.text_rows * d.faces[face].height;
                return true;
            }
        }
    }
    width = d.width;
    height = d.height;
    return width && height;
}

// The cells into the window's picture, in `face` of APP850.FON or, without
// one, in the TrueType face of the state.
bool Application::DosVmTextCells(const unsigned short *cells, unsigned columns, unsigned rows, const void *raster,
                                 unsigned fw, unsigned fh)
{
    auto &d = *dos_vm;
    const auto *provider = DosVm::Current();
    const auto *face = static_cast<const DosVmState::Face *>(raster);
    static const unsigned palette[16] = {0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
                                         0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff};
    const unsigned width = columns * fw, height = rows * fh;
    if (width != d.width || height != d.height || d.scale != 1 || !d.pixels)
    {
        delete[] d.pixels;
        d.pixels = new unsigned[(size_t)width * height];
        d.width = width;
        d.height = height;
        d.scale = 1;
    }
    const unsigned char *f = face ? d.font + face->offset : nullptr;
    const unsigned column_bytes = (fw + 7) / 8;
    for (unsigned row = 0; row < rows; ++row)
        for (unsigned column = 0; column < columns; ++column)
        {
            const unsigned cell = cells[row * columns + column], character = cell & 0xff, attribute = cell >> 8;
            const unsigned fg = palette[attribute & 15], bg = palette[(attribute >> 4) & 7];
            unsigned glyph = 0;
            const unsigned char *ink = face ? nullptr : d.truetype->Glyph(character, fw, fh);
            if (face && character >= face->first && character <= face->last)
            {
                const unsigned at = 0x76 + (character - face->first) * 4;
                if (face->offset + at + 4 <= d.font_bytes) glyph = f[at + 2] | f[at + 3] << 8;
            }
            unsigned *out = d.pixels + (size_t)row * fh * width + column * fw;
            for (unsigned y = 0; y < fh; ++y, out += width)
                for (unsigned x = 0; x < fw; ++x)
                {
                    bool on = ink && ink[y * fw + x];
                    if (face && glyph)
                    {
                        const unsigned byte = face->offset + glyph + (x / 8) * fh + y;
                        on = byte < d.font_bytes && x / 8 < column_bytes && (d.font[byte] >> (7 - x % 8)) & 1;
                    }
                    out[x] = on ? fg : bg;
                }
        }
    unsigned column = 0, row = 0, start = 0, end = 0, lines = 0;
    if (provider->TextCursor && provider->TextCursor(provider->owner, d.session, column, row, start, end, lines) && lines &&
        !(start & 0x20) && start <= end && column < columns && row < rows && !d.cursor_phase)
    {
        const unsigned cell = cells[row * columns + column];
        const unsigned colour = palette[(cell >> 8) & 15];
        unsigned top = (start & 0x1f) * fh / lines, bottom = ((end & 0x1f) + 1) * fh / lines;
        if (bottom > fh) bottom = fh;
        if (top >= bottom) top = bottom ? bottom - 1 : 0;
        for (unsigned y = top; y < bottom; ++y)
            for (unsigned x = 0; x < fw; ++x) d.pixels[(size_t)(row * fh + y) * width + column * fw + x] = colour;
    }
    return true;
}

// The picture for the window from the machine's pixels: a whole factor in a
// window (320x200 twice, 640x400 and 640x480 as they are), the whole screen
// when the window covers it. Win98 switches the display mode for a full
// screen DOS box; here the screen keeps its 800x600 and the picture is
// stretched to it, nearest neighbour, which keeps the 4:3 of every DOS mode.
void Application::DosVmScale()
{
    auto &d = *dos_vm;
    if (!d.raw) return;
    unsigned width, height, scale;
    d.text_picture = !d.fullscreen && DosVmTextPicture();
    if (d.text_picture) { width = d.width; height = d.height; scale = d.scale; }
    else if (d.fullscreen)
    {
        width = ScreenWidth; height = ScreenHeight; scale = 0;
        // The mode changed under a full screen box: its window follows.
        if (d.window && (width != d.width || height != d.height)) User::MoveWindow(d.window, 0, 0, (int)width, (int)height);
    }
    else
    {
        scale = d.raw_width <= 400 && d.raw_height <= 300 ? 2 : 1;
        width = d.raw_width * scale;
        height = d.raw_height * scale;
    }
    if (!d.text_picture && (width != d.width || height != d.height || d.scale != scale))
    {
        delete[] d.pixels;
        d.pixels = new unsigned[(size_t)width * height];
        d.width = width;
        d.height = height;
        d.scale = scale;
    }
    if (d.text_picture) {}
    else if (scale)
        for (unsigned y = 0; y < d.raw_height; ++y)
        {
            const unsigned *row = d.raw + (size_t)y * d.raw_width;
            for (unsigned s = 0; s < scale; ++s)
            {
                unsigned *out = d.pixels + (size_t)(y * scale + s) * width;
                if (scale == 1) memcpy(out, row, (size_t)d.raw_width * 4);
                else for (unsigned x = 0; x < d.raw_width; ++x) out[x * 2] = out[x * 2 + 1] = row[x];
            }
        }
    else
        for (unsigned y = 0; y < height; ++y)
        {
            const unsigned *row = d.raw + (size_t)(y * d.raw_height / height) * d.raw_width;
            unsigned *out = d.pixels + (size_t)y * width;
            for (unsigned x = 0; x < width; ++x) out[x] = row[x * d.raw_width / width];
        }
    if (d.selected && d.text_columns && d.text_rows)
    {
        // The marked block, inverted, in cells of the text mode.
        const int cw = (int)(width / d.text_columns), ch = (int)(height / d.text_rows);
        const int x0 = (d.sel_x0 < d.sel_x1 ? d.sel_x0 : d.sel_x1) * cw, x1 = ((d.sel_x0 < d.sel_x1 ? d.sel_x1 : d.sel_x0) + 1) * cw;
        const int y0 = (d.sel_y0 < d.sel_y1 ? d.sel_y0 : d.sel_y1) * ch, y1 = ((d.sel_y0 < d.sel_y1 ? d.sel_y1 : d.sel_y0) + 1) * ch;
        unsigned inverted = 0;
        for (int y = y0; y < y1 && y < (int)height; ++y)
            for (int x = x0; x < x1 && x < (int)width; ++x) { d.pixels[(size_t)y * width + x] ^= 0xffffff; ++inverted; }
        if (log && trace_calls) { char text[120]; snprintf(text, sizeof text, "DOSVM mark inverted=%u rect=%d,%d,%d,%d", inverted, x0, y0, x1, y1); log(text); }
    }
    if (d.window && !d.fullscreen && (width != d.client_width || height != d.client_height))
    {
        d.client_width = width;
        d.client_height = height;
        Gdi::RECT frame_rect = {0, 0, (int)width, (int)height + d.Strip()}, window = {};
        User::AdjustWindowRect(&frame_rect, User::Style(d.window), 0, User::ExtendedStyle(d.window));
        User::GetWindowRect(d.window, &window);
        User::MoveWindow(d.window, window.left, window.top, frame_rect.right - frame_rect.left,
                         frame_rect.bottom - frame_rect.top);
    }
    DosVmFontBox();
    if (d.window) User::InvalidateRect(d.window, nullptr, false);
}

// The Misc page as the window applies it, at the start and after every
// Apply: always suspend (TSK_BACKGROUND 0x2 clear), the question on close
// (TSK_NOWARNTERMINATE 0x10), Alt+Enter left to the program
// (KBD_NOALTENTER 0x100), QuickEdit (MSE_WINDOWENABLE 0x1 clear) and the
// exclusive mouse (MSE_EXCLUSIVE 0x2). The other shortcut keys of the page,
// Alt+Tab, Ctrl+Esc and the rest, are the desktop's to take; the window
// never sees them either way.
void Application::DosVmLiveSettings(Win98PifMgr::Record &record)
{
    auto &d = *dos_vm;
    unsigned char tsk[14] = {}, kbd[18] = {}, mse[4] = {};
    Win98PifMgr::Get(record, Win98PifMgr::GroupTsk, tsk, sizeof tsk, 0);
    Win98PifMgr::Get(record, Win98PifMgr::GroupKbd, kbd, sizeof kbd, 0);
    Win98PifMgr::Get(record, Win98PifMgr::GroupMse, mse, sizeof mse, 0);
    const unsigned task = Win98PifMgr::Detail::Word(tsk), keys = Win98PifMgr::Detail::Word(kbd),
                   mouse = Win98PifMgr::Detail::Word(mse);
    d.background = (task & 0x2) != 0;
    d.warn_close = !(task & 0x10);
    d.alt_enter = !(keys & 0x100);
    d.quickedit = !(mouse & 0x1);
    d.exclusive = (mouse & 0x2) != 0;
    d.launch.fast_paste = (keys & 0x1) != 0;
    d.launch.idle_sensitivity = Win98PifMgr::Detail::Word(tsk + 12);
    unsigned char fnt[78] = {};
    if (Win98PifMgr::Get(record, Win98PifMgr::GroupFnt, fnt, sizeof fnt, 0))
    {
        d.fnt_flags = Win98PifMgr::Detail::Word(fnt);
        d.fnt_width = Win98PifMgr::Detail::Word(fnt + 8);
        d.fnt_height = Win98PifMgr::Detail::Word(fnt + 0xa);
    }
    if (d.captured && !d.exclusive) DosVmReleaseMouse();
    if (log)
    {
        char text[200];
        snprintf(text, sizeof text, "DOSVM misc tsk=%04x kbd=%04x mse=%04x background=%u warn=%u altenter=%u quickedit=%u exclusive=%u font=%04x,%ux%u",
                 task, keys, mouse, d.background ? 1u : 0u, d.warn_close ? 1u : 0u, d.alt_enter ? 1u : 0u,
                 d.quickedit ? 1u : 0u, d.exclusive ? 1u : 0u, d.fnt_flags, d.fnt_width, d.fnt_height);
        log(text);
    }
}

namespace
{
// The lines of a section of text, CR LF or LF, without empty lines and
// REM lines; each goes to `line`.
template <class F> void SectionLines(const unsigned char *bytes, unsigned length, F line)
{
    unsigned at = 0;
    while (at < length && bytes[at])
    {
        unsigned end = at;
        while (end < length && bytes[end] && bytes[end] != '\r' && bytes[end] != '\n') ++end;
        char text[260];
        const unsigned n = end - at < sizeof text - 1 ? end - at : sizeof text - 1;
        memcpy(text, bytes + at, n);
        text[n] = 0;
        const char *p = text;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p && !(SamePrefix(p, "REM", 3) && (p[3] == ' ' || !p[3])) && *p != ';') line(p);
        at = end;
        while (at < length && (bytes[at] == '\r' || bytes[at] == '\n')) ++at;
    }
}
} // namespace

// What the machine gets from the PIF at its start, beside title, program,
// directory, close on exit, full screen, icon and XMS/EMS: the batch file
// (ENV+4), conventional memory (MEM+6), the initial size in rows (VID+0xa),
// MS-DOS mode (flPrgInit 0x8) with its CONFIG.SYS and AUTOEXEC.BAT
// sections, and NEO98's own section with the options of the machine.
void Application::DosVmMachineSettings(Win98PifMgr::Record &record, const unsigned char *prg, const unsigned char *mem,
                                       const unsigned char *vid)
{
    auto &d = *dos_vm;
    auto &l = d.launch;
    unsigned char env[0x58] = {};
    Win98PifMgr::Get(record, Win98PifMgr::GroupEnv, env, sizeof env, 0);
    unsigned used = 0;
    auto add = [&](const char *line) {
        const unsigned n = (unsigned)strlen(line);
        if (used + n + 2 >= sizeof l.prelude) return;
        memcpy(l.prelude + used, line, n);
        used += n;
        l.prelude[used++] = '\n';
        l.prelude[used] = 0;
    };
    // Conventional memory below 640 KB (Memory page): LOADFIX without a
    // program allocates the difference at the bottom of memory and keeps it
    // for the session, so the program loads above it and its own return
    // code stays the one DOS reports.
    // A number on the page is written as minimum and maximum alike; Auto is 0 and 640, and the default of the module, 128 and
    // 512, is no limit either.
    const unsigned low = Win98PifMgr::Detail::Word(mem + 6), low_minimum = Win98PifMgr::Detail::Word(mem + 4);
    if (low == low_minimum && low && low < 640)
    {
        char line[40];
        snprintf(line, sizeof line, "LOADFIX -%u > NUL", 640 - low);
        add(line);
    }
    // The batch file of the Program page runs before the program.
    char batch[0x51] = {};
    memcpy(batch, env + 4, 0x50);
    char *b = batch;
    while (*b == ' ') ++b;
    if (*b)
    {
        char line[0x60];
        snprintf(line, sizeof line, "call %s", b);
        add(line);
    }
    const unsigned rows = Win98PifMgr::Detail::Word(vid + 0xa);
    l.lines = rows == 43 || rows == 50 ? rows : 0;
    // MS-DOS mode: Win98 leaves Windows and boots DOS with the sections as
    // CONFIG.SYS and AUTOEXEC.BAT. The machine is DOS already; it takes the
    // AUTOEXEC.BAT lines before the program, EMM386's NOEMS from CONFIG.SYS,
    // runs full screen and closes at the end, when Win98 would come back.
    if (Win98PifMgr::Detail::Word(prg + Win98PifMgr::PrgFlagsInit) & 0x8)
    {
        l.fullscreen = true;
        l.close_on_exit = true;
        unsigned char text[4096];
        unsigned length = Win98PifMgr::GetNamed(record, Win98PifMgr::ConfigSection, text, sizeof text - 1);
        text[length < sizeof text ? length : sizeof text - 1] = 0;
        SectionLines(text, length, [&](const char *line) {
            char upper[260];
            unsigned i = 0;
            for (; line[i] && i + 1 < sizeof upper; ++i) upper[i] = line[i] >= 'a' && line[i] <= 'z' ? (char)(line[i] - 32) : line[i];
            upper[i] = 0;
            if (strstr(upper, "EMM386") && strstr(upper, "NOEMS")) l.ems = false;
        });
        length = Win98PifMgr::GetNamed(record, Win98PifMgr::AutoexecSection, text, sizeof text - 1);
        text[length < sizeof text ? length : sizeof text - 1] = 0;
        SectionLines(text, length, [&](const char *line) {
            // Windows itself and the drivers the machine has built in stay out.
            if (SamePrefix(line, "WIN", 3) && (line[3] == ' ' || !line[3])) return;
            add(line);
        });
    }
    // The options of the machine: "name=value" lines, CR LF in the file.
    unsigned char options[sizeof l.options];
    const unsigned length = Win98PifMgr::GetNamed(record, Win98PifMgr::OptionSection, options, sizeof options - 1);
    unsigned n = 0;
    for (unsigned i = 0; i < length && options[i] && n + 1 < sizeof l.options; ++i)
        if (options[i] != '\r') l.options[n++] = (char)options[i];
    l.options[n] = 0;
}

// After a page of the property sheet applied its groups: the toolbar (WIN)
// and "always suspend" (TSK) take effect in the running window, as they do
// under WINOLDAP; memory, the machine and the sound options wait for the
// next start of the program.
void Application::DosVmReloadSettings()
{
    auto &d = *dos_vm;
    auto *record = new Win98PifMgr::Record;
    if (!record) return;
    if (d.props_path[0] && PifMgrOpenPath(d.props_path, *record))
    {
        unsigned char win[38];
        Win98PifMgr::Get(*record, Win98PifMgr::GroupWin, win, sizeof win, 0);
        const bool toolbar = (Win98PifMgr::Detail::Word(win) & 2) != 0;
        const unsigned font_before = d.fnt_flags | d.fnt_width << 16 | d.fnt_height << 24;
        DosVmLiveSettings(*record);
        // The Font page chose another size: the window draws with it now,
        // and the size box shows it.
        if ((d.fnt_flags | d.fnt_width << 16 | d.fnt_height << 24) != font_before)
        {
            DosVmFillFontBox();
            d.client_width = d.client_height = 0;
            DosVmScale();
        }
        unsigned char prg[Win98PifMgr::PrgBytes];
        if (Win98PifMgr::GetPrg(*record, prg, sizeof prg, 0))
        {
            const unsigned hotkey = Win98PifMgr::Detail::Word(prg + Win98PifMgr::PrgHotKey);
            if (hotkey != d.hotkey && d.window) Send(d.window, 0x32, hotkey, 0);
            d.hotkey = hotkey;
        }
        if (toolbar != d.toolbar)
        {
            d.toolbar = toolbar;
            if (toolbar) DosVmLoadToolbar();
            d.client_width = d.client_height = 0; // the frame fits the strip again
            DosVmScale();
        }
        if (log)
        {
            char text[120];
            snprintf(text, sizeof text, "DOSVM settings reloaded toolbar=%u background=%u warn=%u altenter=%u quickedit=%u exclusive=%u",
                     d.toolbar ? 1u : 0u, d.background ? 1u : 0u, d.warn_close ? 1u : 0u, d.alt_enter ? 1u : 0u,
                     d.quickedit ? 1u : 0u, d.exclusive ? 1u : 0u);
            log(text);
        }
    }
    delete record;
}

// Alt+Enter, and the PIF's "full screen": the window drops its frame and
// covers the screen, and back. Win98 asks the display driver for the DOS
// mode instead; the desktop is not visible either way.
void Application::DosVmFullscreen(bool on)
{
    auto &d = *dos_vm;
    if (!d.window || d.fullscreen == on) return;
    if (on && HostKeepsDosWindows())
    {
        // The window stays a window; the host shows its picture alone over
        // the display, and the same keys bring the window back.
        AskHostDosFullscreen(d.window, d.Strip());
        if (log) log("DOSVM full screen asked of the host");
        return;
    }
    d.fullscreen = on;
    if (on)
    {
        User::GetWindowRect(d.window, &d.windowed_rect);
        d.windowed_style = User::Style(d.window);
        User::SetStyle(d.window, (d.windowed_style & ~0x00cf0000u) | 0x80000000u); // WS_POPUP without frame
        User::MoveWindow(d.window, 0, 0, (int)ScreenWidth, (int)ScreenHeight);
        // Win98 gives the whole display to the VM; nothing of the desktop,
        // not even the taskbar that is always on top, shows. The
        // window goes above the topmost windows while it has the screen.
        User::OrderWindow(d.window, ~0u); // HWND_TOPMOST
    }
    else
    {
        User::OrderWindow(d.window, ~1u); // HWND_NOTOPMOST
        User::SetStyle(d.window, d.windowed_style);
        User::MoveWindow(d.window, d.windowed_rect.left, d.windowed_rect.top,
                         d.windowed_rect.right - d.windowed_rect.left, d.windowed_rect.bottom - d.windowed_rect.top);
        d.client_width = d.client_height = 0; // the next picture fits the window again
    }
    DosVmScale();
    // The pointer goes with the display and comes back with the window.
    if (on) { const unsigned cursor[1] = {0}; Api("SetCursor", cursor); }
    else DosVmRestoreCursor();
    if (log)
    {
        char text[80];
        snprintf(text, sizeof text, "DOSVM fullscreen=%u", on ? 1u : 0u);
        log(text);
    }
}

// A Windows program DOS was asked to run: the child of this process, started
// the way WINOLDAP starts it, with the DOS current directory. The DOS side
// has already gone on; Win98's prompt does not wait either.
void Application::DosVmRunWindowsProgram(const char *program, const char *arguments, const char *directory, bool wait)
{
    char windows_program[260], windows_directory[260], line[600];
    if (!DosVmWindowsPath(program, windows_program)) snprintf(windows_program, sizeof windows_program, "%s", program);
    if (!directory[0] || !DosVmWindowsPath(directory, windows_directory)) snprintf(windows_directory, sizeof windows_directory, "%s", directory);
    if (arguments[0]) snprintf(line, sizeof line, "\"%s\" %s", windows_program, arguments);
    else snprintf(line, sizeof line, "\"%s\"", windows_program);
    const unsigned text = String(line), dir = windows_directory[0] ? String(windows_directory) : 0;
    const unsigned startup = Allocate(68), information = Allocate(16);
    unsigned result = 0;
    if (text && startup && information)
    {
        unsigned char zero[68] = {};
        Store(startup, zero, sizeof zero);
        Write32(startup, 68);
        Store(information, zero, 16);
        const unsigned a[] = {0, text, 0, 0, 0, 0, 0, dir, startup, information};
        result = ProcessApi("CreateProcessA", a);
        if (result)
        {
            const unsigned process[] = {Read32(information)}, thread[] = {Read32(information + 4)};
            ThreadApi("CloseHandle", thread);
            // A console program is waited for: the loop watches the handle.
            if (wait && dos_vm && !dos_vm->waiting_process) dos_vm->waiting_process = process[0];
            else ThreadApi("CloseHandle", process);
        }
    }
    if (wait && !result && dos_vm && dos_vm->session)
    {
        // Win98 reports a program it cannot run; the DOS side gets an error level.
        const auto *provider = DosVm::Current();
        if (provider && provider->WindowsProgramDone) provider->WindowsProgramDone(provider->owner, dos_vm->session, 1);
    }
    if (log)
    {
        char message[800];
        snprintf(message, sizeof message, "DOSVM windows program %s result=%u error=%u", line, result, last_error);
        log(message);
    }
    if (text) Free(text);
    if (dir) Free(dir);
    if (startup) Free(startup);
    if (information) Free(information);
}

// A new picture of the machine: keep its pixels and show them.
void Application::DosVmFrame(const DosVm::Frame &frame)
{
    auto &d = *dos_vm;
    if (frame.width != d.raw_width || frame.height != d.raw_height || !d.raw)
    {
        delete[] d.raw;
        d.raw = new unsigned[(size_t)frame.width * frame.height];
        d.raw_width = frame.width;
        d.raw_height = frame.height;
    }
    memcpy(d.raw, frame.pixels, (size_t)frame.width * frame.height * 4);
    d.seen = frame.serial;
    DosVmScale();
}

#ifdef NEO98_DOS_VOODOO
// The 3dfx Voodoo of the machine: every frame the card drew since the last
// turn goes through the GPU backend, and while the card's output is on, its
// picture is the window's. In a window the card is drawn at the resolution
// of the program. When the window has the whole display (the full screen of
// Win98, or the host shows it alone), the card is drawn at the largest whole
// multiple that fits the host's output, and that picture goes to the host as
// it is (User::SetOverlay), the way the Glide driver of the Windows side
// shows its front buffer; NEO98_VOODOO_SCALE names the multiple instead.
void Application::DosVmVoodoo()
{
    auto &d = *dos_vm;
    const auto *provider = DosVm::Current();
    if (!d.launch.voodoo_host || !provider->VoodooFrame || !gpu_backend) return;
    const bool whole_display = d.fullscreen || (d.window && HostZoomedWindow() == d.window);
    DosVoodooFrame frame;
    bool changed = false;
    bool inactive_status = false;
    // A machine far ahead is caught up over the next turns; the window's
    // messages are served in between.
    for (unsigned n = 0; n < 4 && provider->VoodooFrame(provider->owner, d.session, frame); ++n)
    {
        // A frame the program cut with a read of the linear frame buffer:
        // its thread goes on when the frame is done with, drawn or not.
        struct Drawn
        {
            const DosVm::Provider *provider;
            DosVm::Session *session;
            bool wanted;
            ~Drawn() { if (wanted && provider->VoodooDrawn) provider->VoodooDrawn(provider->owner, session); }
        } drawn = {provider, d.session, frame.read_mask != 0};
        if (frame.read_mask) d.voodoo_read = Now();
        if (!d.voodoo)
        {
            if (!frame.active) { inactive_status = true; continue; }
            d.voodoo = new DosVoodoo::Renderer(*gpu_backend, process_id | 0x20000000u);
        }
        unsigned scale = 1;
        if (const char *text = getenv("NEO98_VOODOO_SCALE")) scale = unsigned(atoi(text));
        else if (whole_display && frame.width && frame.height)
        {
            int host_width = 0, host_height = 0;
            User::HostOutput(host_width, host_height);
            const unsigned fit_x = unsigned(host_width > 0 ? host_width : 0) / frame.width;
            const unsigned fit_y = unsigned(host_height > 0 ? host_height : 0) / frame.height;
            scale = fit_x < fit_y ? fit_x : fit_y;
        }
        DosVoodooReadback *readback = provider->VoodooReadback ? provider->VoodooReadback(provider->owner, d.session) : nullptr;
        const bool was_active = d.voodoo->Active();
        const unsigned long long before = Now();
        if (d.voodoo->Draw(frame, scale, readback)) changed = true;
        d.voodoo_time += Now() - before;
        if (log && was_active != d.voodoo->Active())
        {
            // The card's output goes on and off: the size it was set to, and
            // at the end what was drawn and what a frame cost the host.
            char text[200];
            const unsigned long long frames = d.voodoo->Frames();
            snprintf(text, sizeof text, "DOSVM voodoo output=%u size=%ux%u scale=%u frames=%llu triangles=%llu draw_us=%llu reads=%llu",
                     d.voodoo->Active() ? 1u : 0u, frame.width, frame.height, scale < 1 ? 1u : scale > 8 ? 8u : scale, frames,
                     d.voodoo->Triangles(), frames ? d.voodoo_time / frames : 0, d.voodoo->Reads());
            log(text);
        }
    }
    if (!d.voodoo)
    {
        // A restored UI may still show its saved card picture while the
        // restored core already gives the monitor to VGA. Its first status
        // is authoritative even though no GPU renderer needed rebuilding.
        if (inactive_status && d.voodoo_shown)
        {
            d.voodoo_shown = false;
            d.seen = ~d.seen;
            DosVmRestoreCursor();
        }
        return;
    }
    // The picture as it was drawn is the host's only while the window has
    // the display; it goes the moment the window is a window again, whether
    // the card drew since or not, or the host would keep showing it.
    const bool overlay = d.voodoo->Active() && whole_display && d.voodoo->FullWidth() != d.voodoo->Width();
    if (d.voodoo_overlay && !overlay) User::ClearOverlay();
    if (changed && d.voodoo->Active())
    {
        DosVm::Frame picture;
        picture.pixels = d.voodoo->Picture();
        picture.width = d.voodoo->Width();
        picture.height = d.voodoo->Height();
        picture.serial = d.seen;
        d.voodoo_shown = true;
        DosVmFrame(picture);
    }
    else if (!d.voodoo->Active() && d.voodoo_shown)
    {
        // The VGA has the monitor again: its newest picture, whatever its
        // number.
        d.voodoo_shown = false;
        d.seen = ~d.seen;
        DosVmRestoreCursor();
    }
    if (overlay && (changed || !d.voodoo_overlay))
        User::SetOverlay(d.voodoo->Full(), int(d.voodoo->FullWidth()), int(d.voodoo->FullHeight()));
    d.voodoo_overlay = overlay;
}
#endif

// The output can return to VGA without another WM_SETCURSOR: USER caches
// the client hit. Only restore a pointer this process left hidden.
void Application::DosVmRestoreCursor()
{
    if (!dos_vm || User::PointerOwner() != this || User::PointerShape()) return;
    Gdi::POINT point = {};
    User::CursorPosition(&point);
    const unsigned hwnd = dos_vm->window;
    DefaultSetCursor(hwnd, hwnd, User::FrameHitTest(hwnd, point.x, point.y) | (0x0200u << 16));
}

// The end of the machine, from the program's end or from the window.
void Application::DosVmExit(unsigned code)
{
    if (!dos_vm) { threads[0].done = true; return; }
    auto &d = *dos_vm;
#ifdef NEO98_DOS_VOODOO
    if (log && d.voodoo && d.voodoo->Frames() && d.voodoo_time)
    {
        char text[160];
        snprintf(text, sizeof text, "DOSVM voodoo end frames=%llu triangles=%llu draw_us=%llu reads=%llu", d.voodoo->Frames(),
                 d.voodoo->Triangles(), d.voodoo_time / d.voodoo->Frames(), d.voodoo->Reads());
        log(text);
        d.voodoo_time = 0;   // said once
    }
#endif
    if (d.session)
    {
        const auto *provider = DosVm::Current();
        if (provider) provider->Stop(provider->owner, d.session);
        d.session = nullptr;
    }
    if (d.sound) { Sound::Shared().Close(d.sound); d.sound = 0; }
    if (d.window && User::IsWindow(d.window)) CbtDestroyWindow(d.window, false);
    d.window = 0;
    if (state != Failed && state != Exited)
    {
        exit_code = code;
        state = Exited;
        FreePipes();
    }
    threads[0].result = code;
    threads[0].done = true;
}

// The picture the DOS window shows, in the pixels of the machine (the VGA's,
// or the 3dfx Voodoo's while it has the monitor), for the picture of a saved
// state; and a picture for the window to show until the machine draws again.
bool Application::DosVmPicture(const unsigned *&pixels, unsigned &width, unsigned &height) const
{
    if (!dos_vm || !dos_vm->raw || !dos_vm->raw_width || !dos_vm->raw_height) return false;
    pixels = dos_vm->raw;
    width = dos_vm->raw_width;
    height = dos_vm->raw_height;
    return true;
}
bool Application::DosVmFullscreenNow() const { return dos_vm && dos_vm->fullscreen; }
void Application::DosVmWantFullscreen(bool on)
{
    if (dos_vm && dos_vm->fullscreen != on) dos_vm->wanted_fullscreen = on ? 1 : 0;
}
void Application::DosVmShowPicture(const unsigned *pixels, unsigned width, unsigned height)
{
    if (!dos_vm || !pixels || !width || !height) return;
    delete[] dos_vm->shown;
    dos_vm->shown = new unsigned[(size_t)width * height];
    memcpy(dos_vm->shown, pixels, (size_t)width * height * 4);
    dos_vm->shown_width = width;
    dos_vm->shown_height = height;
}

void Application::FreeDosVm()
{
    if (!dos_vm) return;
    delete[] dos_vm->shown;
    dos_vm->shown = nullptr;
    if (dos_vm->session)
    {
        const auto *provider = DosVm::Current();
        if (provider) provider->Stop(provider->owner, dos_vm->session);
        dos_vm->session = nullptr;
    }
    if (dos_vm->sound) { Sound::Shared().Close(dos_vm->sound); dos_vm->sound = 0; }
    delete dos_vm;
    dos_vm = nullptr;
}

// The machine's side of one turn of the loop: the newest picture, the
// program's name for the title, the text geometry and the cursor, the
// background rule, the Windows programs DOS asked for, the sound into the
// mixer and the end. The loop calls it every turn; while the window's
// property sheet runs its own modal loop, the window's timer calls it with
// `modal` set, which leaves the end to the loop. False when the
// process has ended.
bool Application::DosVmService(bool modal)
{
    auto &d = *dos_vm;
    const auto *provider = DosVm::Current();
    if (!d.session || !provider) return true;
    if (provider->Microphone) provider->Microphone(provider->owner, d.session, Now());
    DosVm::Frame frame;
#ifdef NEO98_DOS_VOODOO
    DosVmVoodoo();
    if (!d.voodoo_shown)
#endif
    if (provider->NewFrame(provider->owner, d.session, d.seen, frame)) DosVmFrame(frame);
    if (d.wanted_fullscreen >= 0)
    {
        const bool on = d.wanted_fullscreen != 0;
        d.wanted_fullscreen = -1;
        DosVmFullscreen(on);
    }
    if (d.shown)
    {
        // The picture of a state that was just put in: the window shows it
        // while the machine stands.
        frame.pixels = d.shown;
        frame.width = d.shown_width;
        frame.height = d.shown_height;
        frame.serial = d.seen;
        DosVmFrame(frame);
        delete[] d.shown;
        d.shown = nullptr;
    }
    char running[9];
    provider->Program(provider->owner, d.session, running, sizeof running);
    if (strcmp(running, d.running)) { snprintf(d.running, sizeof d.running, "%s", running); DosVmTitle(); }
    if (provider->ScreenText)
    {
        static unsigned short cells[80 * 50];
        unsigned columns = 0, rows = 0;
        // Only the geometry is wanted here; a graphics mode answers false.
        if (!provider->ScreenText(provider->owner, d.session, columns, rows, cells, 80 * 50)) columns = rows = 0;
        if (columns != d.text_columns || rows != d.text_rows)
        {
            // A new text geometry, also the first one: the picture may have
            // come before the machine reported its cells, and a screen that
            // does not change brings no new picture, so it is built again here.
            d.text_columns = columns;
            d.text_rows = rows;
            if (d.raw) DosVmScale();
        }
        // The cursor of the text picture blinks on its own clock.
        if (d.text_picture)
        {
            const unsigned phase = (unsigned)((Now() / 266000) & 1);
            if (phase != d.cursor_phase) { d.cursor_phase = phase; DosVmScale(); }
        }
    }
    if (provider->Pause)
    {
        const bool pause = !d.background && !d.finished && User::GetForegroundWindow() != d.window;
        if (pause != d.paused) { d.paused = pause; provider->Pause(provider->owner, d.session, pause); }
    }
    char program[260], arguments[128], directory[260];
    bool wait = false;
    while (provider->WindowsProgram && d.session &&
           provider->WindowsProgram(provider->owner, d.session, program, sizeof program, arguments, sizeof arguments,
                                    directory, sizeof directory, wait))
        DosVmRunWindowsProgram(program, arguments, directory, wait);
    if (d.waiting_process && d.session)
    {
        const unsigned out = Allocate(4);
        unsigned code = 259;
        if (out)
        {
            const unsigned arguments2[] = {d.waiting_process, out};
            if (ProcessApi("GetExitCodeProcess", arguments2)) code = Read32(out);
            Free(out);
        }
        if (code != 259)
        {
            const unsigned close[] = {d.waiting_process};
            ThreadApi("CloseHandle", close);
            d.waiting_process = 0;
            if (provider->WindowsProgramDone) provider->WindowsProgramDone(provider->owner, d.session, code);
            if (log)
            {
                char text[120];
                snprintf(text, sizeof text, "DOSVM windows program done code=%u", code);
                log(text);
            }
        }
    }
    if (d.sound)
    {
        short samples[2048 * 2];
        unsigned got;
        while ((got = provider->Audio(provider->owner, d.session, samples, 2048)) != 0)
        {
            // This is live DOS audio, not an application's waveOut queue.
            // A slow output clock or a burst after a guest scheduling stall
            // must not leave old sound playing behind the current picture.
            // Bound both handoffs (the provider has the same 50 ms limit).
            const auto played = Sound::Shared().Played(d.sound);
            const auto queued = d.sound_submitted > played ? d.sound_submitted - played : 0;
            if (queued + got * 4 > DosVm::AudioQueueFrames * 4)
            {
                if (log && getenv("NEO98_TRACE_AUDIO"))
                {
                    char text[120];
                    snprintf(text, sizeof text, "DOSVM audio catch-up queued=%llu incoming=%u", queued / 4, got);
                    log(text);
                }
            }
            if (Sound::Shared().SubmitLive(d.sound, (const unsigned char *)samples, got * 4,
                                          1, DosVm::AudioQueueFrames)) break;
            d.sound_submitted += got * 4;
        }
        unsigned tag;
        while ((tag = Sound::Shared().Complete(d.sound)) != 0) {}
    }
    unsigned code = 0;
    bool crashed = false;
    char message[256] = {};
    if (!modal && !d.finished && provider->Finished(provider->owner, d.session, code, crashed, message, sizeof message))
    {
        d.finished = true;
        d.exit_code = code;
        if (log)
        {
            char text[400];
            snprintf(text, sizeof text, "DOSVM finished code=%u crashed=%u close=%u %s", code, crashed ? 1u : 0u,
                     d.launch.close_on_exit ? 1u : 0u, message);
            log(text);
        }
        if (crashed || code)
        {
            char error[320];
            snprintf(error, sizeof error, "%s (DOS exit code %u)", message[0] ? message : "DOS program failed", code);
            ReportDosFailure(d.props_path, error);
        }
        if (d.launch.close_on_exit || crashed) { DosVmExit(code); return false; }
        // Win98 keeps the window with the last screen and "Finished -";
        // a program that ended in the full screen gives the display back to
        // the desktop, where its window waits.
        if (d.fullscreen) DosVmFullscreen(false);
        // The last screen stays in the window; the pointer over it is Windows' again.
        DosVmRestoreCursor();
        provider->Stop(provider->owner, d.session);
        d.session = nullptr;
        DosVmTitle();
    }
    return true;
}

// Frames of sound of the DOS machines that lie in the mixer and are not
// played yet: with what the machines still hold themselves, the delay of
// their sound. The line of a pause prints it.
unsigned Application::DosVmSoundQueued()
{
    unsigned long long frames = 0;
    for (auto *process : session_processes)
        if (process && process->dos_vm && process->dos_vm->sound)
        {
            const unsigned long long played = Sound::Shared().Played(process->dos_vm->sound);
            if (process->dos_vm->sound_submitted > played) frames += (process->dos_vm->sound_submitted - played) / 4;
        }
    return (unsigned)frames;
}

// The one thread of the DOS process: the window's message loop, the pictures
// of the machine into the window, its sound into the mixer, and the end.
void Application::RunDosVm()
{
    auto &d = *dos_vm;
    const auto *provider = DosVm::Current();
    if (!DosVmCreateWindow()) { ReportDosFailure(d.props_path, Error()); DosVmExit(0xc0000142u); return; }
    char error[256] = {};
#ifdef NEO98_DOS_VOODOO
    // With a GPU backend that draws the pixel pipeline of the card, the
    // machine hands over what its 3dfx Voodoo draws; without one it draws
    // the card itself, in software. NEO98_VOODOO_SOFTWARE asks for that too.
    d.launch.voodoo_host = gpu_backend && gpu_backend->Glide() && provider->VoodooFrame && !getenv("NEO98_VOODOO_SOFTWARE");
#endif
    d.session = provider->Start(provider->owner, d.launch, error, sizeof error);
    if (log)
    {
        char text[120];
        snprintf(text, sizeof text, "DOSVM session process=%u window=%u session=%p", process_id, d.window, (void *)d.session);
        log(text);
    }
    if (!d.session)
    {
        char text[320];
        snprintf(text, sizeof text, "The DOS machine refused the program: %s", error);
        Fail(text);
        ReportDosFailure(d.props_path, text);
        DosVmExit(0xc0000142u);
        return;
    }
    Sound::Format format;
    format.rate = DosVm::AudioRate;
    format.channels = 2;
    format.bits = 16;
    d.sound = Sound::Shared().Open(format);
    RunDosVmLoop();
}
bool Application::SavedDosUiThread(unsigned index) const
{
    if (index || !dos_vm || !dos_vm->window || !User::IsWindow(dos_vm->window)) return false;
    const auto &t = threads[0];
    return t.host_frame_count && t.host_frames[0].kind == HostDosUiRoot && t.host_frames[0].d == 1 &&
           t.host_frames[0].a == dos_vm->window;
}
bool Application::DosUiStateReady(char *why, size_t bytes, bool require_session) const
{
    const char *error = nullptr;
    if (!dos_vm || !SavedDosUiThread(0)) error = "the native DOS UI root is not initialized";
    else if (require_session && !dos_vm->session) error = "the DOS provider is not attached";
    else if (dos_vm->finished || dos_vm->closed || dos_vm->waiting_process) error = "the DOS UI is ending or owns a Windows child";
    else if (!cpu || (cpu->Backend() != CpuBackend::None && cpu->Backend() != CpuBackend::Unicorn && cpu->Backend() != CpuBackend::Native)) error = "the DOS UI uses an unsupported processor";
#ifdef NEO98_DOS_VOODOO
    else if (dos_vm->voodoo && !dos_vm->voodoo->Healthy()) error = "the DOS UI Voodoo renderer failed to draw its buffers";
    else if (dos_vm->voodoo_overlay) error = "the DOS UI Voodoo renderer overlay is outside this provider";
#endif
    if (!error)
        for (unsigned i = 1; i < 16; ++i)
            if (threads[i].fiber && !threads[i].fiber->Finished()) { error = "the DOS UI has another native thread"; break; }
    if (!error)
        for (unsigned i = 1; i < threads[0].host_frame_count; ++i)
            if (!MachineState::DosUiInnerFrameSupported(threads[0].host_frames[i].kind, threads[0].host_frames[i].d))
            { if (getenv("NEO98_STATE_PROBE")) fprintf(stderr,"NEO98 DOS UI frame kind=%u phase=%u\n",threads[0].host_frames[i].kind,threads[0].host_frames[i].d);
              error = "the DOS UI has an unsupported inner host continuation"; break; }
    if (!error)
    {
        const auto &t = threads[0];
        for (unsigned i = 1; i < t.host_frame_count; ++i)
            if (t.host_frames[i].kind == 61 || (t.host_frames[i].kind == 40 && t.host_frames[i].d == 2))
            {
                const unsigned api = t.host_frames[i].kind == 61 ? i - 1 : i;
                if (api + 3 >= t.host_frame_count ||
                    !MachineState::DosUiCollectedSheet(t.host_frames[api], t.host_frames[api+1],
                                                      t.host_frames[api+2], t.host_frames[api+3]) ||
                    !PropertyWorkRecorded(0, api+2, HostPropertySheet, 1))
                { error = "the DOS UI has an incomplete PIF collection continuation"; break; }
            }
    }
    if (why && bytes) snprintf(why, bytes, "%s", error ? error : "");
    return !error;
}
void Application::DrainDosUiGraphics()
{
#ifdef NEO98_DOS_VOODOO
    if (!dos_vm || !dos_vm->session || !gpu_backend) return;
    const auto *provider = DosVm::Current();
    if (!provider || !dos_vm->launch.voodoo_host) return;
    // While USER and guest fibers stand at the captured call boundary, the
    // core may still ask for the GPU buffers of its current frame. Update
    // only this derived renderer, never FontBox/Scale or window callbacks.
    const unsigned scale = dos_vm->voodoo ? dos_vm->voodoo->Scale() : 1;
    DosVoodoo::Drain(*provider, dos_vm->session, *gpu_backend, process_id | 0x20000000u,
                           dos_vm->voodoo, scale);
#endif
}
bool Application::ExportDosLaunch(DosVm::Launch &launch) const
{
    if (!dos_vm) return false;
    launch = dos_vm->launch;
    return true;
}
bool Application::BindRestoredDosVm(DosVm::Session *session, const DosVm::Launch &launch)
{
    if (!session || !dos_vm || dos_vm->session || !SavedDosUiThread(0)) return false;
    const auto &saved = dos_vm->launch;
    if (strcmp(saved.command, launch.command) || strcmp(saved.directory, launch.directory) ||
        saved.memory_mb != launch.memory_mb || saved.xms != launch.xms || saved.ems != launch.ems ||
        saved.fullscreen != launch.fullscreen || saved.close_on_exit != launch.close_on_exit ||
        saved.idle_sensitivity != launch.idle_sensitivity || strcmp(saved.keyboard_layout, launch.keyboard_layout) ||
        strcmp(saved.prelude, launch.prelude) || saved.lines != launch.lines || saved.fast_paste != launch.fast_paste ||
        strcmp(saved.options, launch.options) || saved.voodoo_host != launch.voodoo_host) return false;
    dos_vm->session = session;
#ifdef NEO98_DOS_VOODOO
    // The restored GPU owner starts with the core's first restore packet.
    dos_vm->voodoo_time = 0;
    dos_vm->voodoo_read = 0;
#endif
    return true;
}
void Application::ResumeDosVm()
{
    auto &t = threads[0];
    t.host_resume = true; t.host_resume_at = 1; t.host_resume_top = depth; t.host_resume_level = 0; t.tail = TailNone;
    if (t.host_frame_count > 1 || depth) ResumeInner();
    PopHostFrame();
    t.host_resume = false;
    if (state != Failed && state != Exited && dos_vm && dos_vm->session) RunDosVmLoop();
}
void Application::RunDosVmLoop()
{
    auto &d = *dos_vm;
    const bool kept = !current_thread && PushHostFrame(HostDosUiRoot, d.window, 0, 0, 1);
    struct Root { Application &app; bool kept; ~Root() { if (kept) app.PopHostFrame(); } } root{*this, kept};
    input_idle = true;
    unsigned dispatched = 0;
    while (state != Failed && state != Exited && !threads[0].done)
    {
        ++native_turns;
        ProcessSentMessages();
        for (unsigned n = 0; n < 64 && state != Failed && state != Exited; ++n)
        {
            if (current_thread == InputThread() && InputPending()) DrainInput();
            User::Message message = {};
            if (!User::PeekMessage(&message, 0, 0, 0, true, true)) break;
            if (message.message == 0x12) break; // WM_QUIT: nobody posts it here, but a hook could
            if (log && trace_calls && dispatched < 64)
            {
                char text[120];
                snprintf(text, sizeof text, "DOSVM message hwnd=%u message=%04x wp=%x lp=%x", message.hwnd, message.message, message.wparam, message.lparam);
                log(text);
                ++dispatched;
            }
            if (User::IsWindow(message.hwnd))
            {
                if (kept) GiveTail(TailRecord);
                CallWindow(message.hwnd, message.message, message.wparam, message.lparam);
            }
        }
        if (state == Failed || state == Exited) break;
        if (d.closed) { DosVmExit(d.finished ? d.exit_code : 0); break; }
        if (d.session && !DosVmService(false)) break;
        // The wait of the loop is MsgWaitForMultipleObjects with no objects,
        // QS_ALLINPUT and a deadline (block code 7): the scheduler wakes a
        // GetMessage wait (block code 1) for messages only, never at its
        // deadline, and the machine's next picture and its end arrive
        // without any message.
        auto &thread = threads[0];
        thread.block = 7;
        thread.multiwait = Thread::Multiwait{};
        thread.multiwait.message = true;
        thread.multiwait.mask = 0x04ff; // QS_ALLINPUT
        unsigned long long wait = d.session ? FrameWait : IdleWait;
#ifdef NEO98_DOS_VOODOO
        if (d.session && d.voodoo_read && Now() - d.voodoo_read < VoodooReadSpan) wait = VoodooReadWait;
#endif
        thread.deadline = Now() + wait;
        Suspend(Idle);
    }
    // The destructor frees the state before it resumes this fiber to unwind.
    if (dos_vm && (state == Failed || state == Exited)) DosVmExit(threads[0].done ? threads[0].result : exit_code);
    threads[0].done = true;
}

// The size box of the toolbar, the COMBOBOX WINOLDAP puts at the left of
// the strip: Auto, the raster sizes of APP850.FON and the TrueType sizes
// of Lucida Console, as the FNT group allows them (4 raster, 8 TrueType),
// in the owner drawn form of the Font page (a TT mark before a TrueType
// size). It exists while the strip shows; a choice takes effect at once and
// goes into the PIF (DosVmChooseFont).
void Application::DosVmFontBox()
{
    auto &d = *dos_vm;
    const bool want = d.window && d.toolbar && !d.fullscreen && d.toolbar_images;
    if (want && !d.font_box)
    {
        // CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP | WS_CHILD
        const unsigned klass = String("COMBOBOX"), text = String("");
        if (!klass || !text) { if (klass) Free(klass); if (text) Free(text); return; }
        const unsigned arguments[] = {0, klass, text, 0x40210213u, (unsigned)ComboLeft, (unsigned)((ToolbarHeight - ComboHeight) / 2),
                                      (unsigned)ComboWidth, 200, d.window, DosVmFontBoxId, image.Base(), 0};
        d.font_box = CreateWindow(arguments);
        Free(klass);
        Free(text);
        if (!d.font_box) return;
        const unsigned font[] = {d.font_box, 0x30, stock[13], 0}; // WM_SETFONT
        Api("SendMessageA", font);
        // The open list is wider than the box, so "10 x 18" with its mark fits.
        const unsigned width[] = {d.font_box, 0x160, 84, 0}; // CB_SETDROPPEDWIDTH
        Api("SendMessageA", width);
        DosVmFillFontBox();
    }
    if (d.font_box && want != d.font_box_shown)
    {
        ShowWindow(d.font_box, want ? 5 : 0);
        d.font_box_shown = want;
    }
}

void Application::DosVmFillFontBox()
{
    auto &d = *dos_vm;
    if (!d.font_box) return;
    auto send = [&](unsigned message, unsigned wp, unsigned lp) {
        const unsigned a[] = {d.font_box, message, wp, lp};
        return Api("SendMessageA", a);
    };
    send(0x14b, 0, 0); // CB_RESETCONTENT
    auto add = [&](unsigned data) {
        char text[24];
        if (data == 0xffff) snprintf(text, sizeof text, "Auto");
        else snprintf(text, sizeof text, "%2u x %2u", data & 0xff, (data >> 8) & 0x7f);
        const unsigned s = String(text);
        if (!s) return;
        const unsigned at = send(0x143, 0, s); // CB_ADDSTRING
        Free(s);
        if (at < 0x8000) send(0x151, at, data); // CB_SETITEMDATA
    };
    add(0xffff);
    if ((d.fnt_flags & 4) && DosVmLoadFont())
        for (unsigned i = 0; i < d.face_count; ++i) add(d.faces[i].width | d.faces[i].height << 8);
    if (d.fnt_flags & 8)
        for (unsigned h : DosVmTrueType::Heights) add(DosVmTrueType::WidthFor(h) | h << 8 | 0x8000);
    // The size of the group, or Auto.
    unsigned selected = 0;
    if (!(d.fnt_flags & 0x10))
    {
        const unsigned want = d.fnt_width | d.fnt_height << 8 | ((d.fnt_flags & 0x800) ? 0x8000u : 0);
        const unsigned count = send(0x146, 0, 0); // CB_GETCOUNT
        for (unsigned i = 0; i < count && i < 64; ++i)
            if (send(0x150, i, 0) == want) { selected = i; break; } // CB_GETITEMDATA
    }
    send(0x14e, selected, 0); // CB_SETCURSEL
}

// A size of the box: the window draws with it at once, the PIF keeps it
// (FNT: 0x10 Auto, 0x800 TrueType, the cell at +8 and +0xa), and the
// keyboard goes back to the DOS window.
void Application::DosVmChooseFont(unsigned data)
{
    auto &d = *dos_vm;
    if (data == 0xffff) d.fnt_flags |= 0x10;
    else
    {
        d.fnt_flags &= ~0x810u;
        if (data & 0x8000) d.fnt_flags |= 0x800;
        d.fnt_width = data & 0xff;
        d.fnt_height = (data >> 8) & 0x7f;
    }
    if (const unsigned props = d.props_path[0] ? PifMgrOpenHandle(d.props_path) : 0)
    {
        unsigned char fnt[78] = {};
        if (PifMgrGroup(props, Win98PifMgr::GroupFnt, fnt, sizeof fnt))
        {
            fnt[0] = (unsigned char)d.fnt_flags;
            fnt[1] = (unsigned char)(d.fnt_flags >> 8);
            if (data != 0xffff)
            {
                fnt[8] = (unsigned char)d.fnt_width; fnt[9] = 0;
                fnt[0xa] = (unsigned char)d.fnt_height; fnt[0xb] = 0;
            }
            if (PifMgrSetGroup(props, Win98PifMgr::GroupFnt, fnt, sizeof fnt)) PifMgrFlushHandle(props);
        }
        PifMgrCloseHandle(props);
    }
    if (log)
    {
        char text[100];
        snprintf(text, sizeof text, "DOSVM font box flags=%04x cell=%ux%u", d.fnt_flags, d.fnt_width, d.fnt_height);
        log(text);
    }
    d.client_width = d.client_height = 0;
    DosVmScale();
    Api("SetFocus", &d.window);
}

// The strip: button face, the font size box with "Auto" and its arrow, the
// seven flat buttons in two groups. Flat, as COMCTL32 draws a toolbar with
// TBSTYLE_FLAT: a button shows its frame only under the pointer, which this
// port does not track.
void Application::DosVmPaintToolbar(unsigned dc, int width)
{
    auto &d = *dos_vm;
    const Gdi::RECT strip = {0, 0, width, ToolbarHeight};
    Gdi::FillRect(dc, &strip, stock[1]);
    auto line = [&](int x, int y, int w, int h, unsigned brush) {
        const Gdi::RECT r = {x, y, x + w, y + h};
        Gdi::FillRect(dc, &r, brush);
    };
    // The size box is a child window of its own (DosVmFontBox).
    // The buttons: their images out of the strip of WINOA386.MOD.
    DibEng::Bitmap bitmap{reinterpret_cast<unsigned char *>(d.toolbar_images), (int)ToolbarImageWidth, (int)ToolbarImageHeight,
                          (int)ToolbarImageWidth * 4, 32};
    if (auto source = Gdi::CreateSurfaceDC(bitmap))
    {
        int x = ButtonsLeft;
        for (unsigned i = 0; i < 7; ++i)
        {
            if (i == 3)
            {
                line(x + 3, ButtonTop + 2, 1, ButtonHeight - 4, stock[2]);
                line(x + 4, ButtonTop + 2, 1, ButtonHeight - 4, stock[0]);
                x += GroupGap;
            }
            // A pressed button, Mark while marking and Background while on,
            // shows the sunken frame of a checked flat button.
            const bool pressed = (i == 0 && d.marking) || (i == 5 && d.background);
            if (pressed)
            {
                line(x, ButtonTop, ButtonWidth, 1, stock[2]); line(x, ButtonTop, 1, ButtonHeight, stock[2]);
                line(x, ButtonTop + ButtonHeight - 1, ButtonWidth, 1, stock[0]); line(x + ButtonWidth - 1, ButtonTop, 1, ButtonHeight, stock[0]);
            }
            Gdi::BitBlt(dc, x + (ButtonWidth - 16) / 2 + pressed, ButtonTop + (ButtonHeight - 16) / 2 + pressed, 16, 16, source,
                        (int)(ToolbarButtons[i].image * 16), 0, 0x00cc0020);
            x += ButtonWidth;
        }
        Gdi::DeleteDC(source);
    }
    User::DrawingChanged(dc);
}

// The clipboard of the session through the same entries a program uses,
// with a text of the DOS screen, or the text it holds for Paste.
bool Application::DosVmClipboardSet(unsigned hwnd, const char *text)
{
    const unsigned n = (unsigned)strlen(text) + 1;
    const unsigned open[] = {hwnd};
    if (!Api("OpenClipboard", open)) return false;
    Api("EmptyClipboard", open);
    const unsigned alloc[] = {0x2042, n};
    const unsigned handle = Api("GlobalAlloc", alloc);
    const unsigned pointer = handle ? Api("GlobalLock", &handle) : 0;
    bool ok = pointer && Store(pointer, text, n);
    if (pointer) Api("GlobalUnlock", &handle);
    if (ok)
    {
        const unsigned set[] = {7, handle}; // CF_OEMTEXT, as WINOLDAP publishes DOS screen bytes.
        ok = Api("SetClipboardData", set) != 0;
    }
    Api("CloseClipboard", open);
    if (!ok && handle) Api("GlobalFree", &handle);
    return ok;
}
bool Application::DosVmClipboardGet(unsigned hwnd, char *out, unsigned capacity)
{
    out[0] = 0;
    const unsigned open[] = {hwnd};
    if (!Api("OpenClipboard", open)) return false;
    const unsigned format[] = {7}; // USER converts CF_TEXT/Unicode to the OEM code page.
    const unsigned handle = Api("GetClipboardData", format);
    if (handle)
    {
        const unsigned lock[] = {handle};
        const unsigned pointer = Api("GlobalLock", lock);
        snprintf(out, capacity, "%s", ReadString(pointer ? pointer : handle));
        Api("GlobalUnlock", lock);
    }
    Api("CloseClipboard", open);
    return handle != 0;
}

// Copy of the toolbar, and Enter or a right click with QuickEdit: the
// marked block of the text screen goes to the clipboard, lines without
// their trailing blanks.
void Application::DosVmCopySelection()
{
    auto &d = *dos_vm;
    unsigned short cells[80 * 50];
    unsigned columns = 0, rows = 0;
    const auto *provider = DosVm::Current();
    if (d.selected && provider && provider->ScreenText &&
        provider->ScreenText(provider->owner, d.session, columns, rows, cells, 80 * 50) && columns && rows)
    {
        char text[80 * 50 + 200];
        unsigned n = 0;
        const int x0 = d.sel_x0 < d.sel_x1 ? d.sel_x0 : d.sel_x1, x1 = d.sel_x0 < d.sel_x1 ? d.sel_x1 : d.sel_x0;
        const int y0 = d.sel_y0 < d.sel_y1 ? d.sel_y0 : d.sel_y1, y1 = d.sel_y0 < d.sel_y1 ? d.sel_y1 : d.sel_y0;
        for (int row = y0; row <= y1 && row < (int)rows; ++row)
        {
            unsigned start = n;
            for (int column = x0; column <= x1 && column < (int)columns; ++column)
            {
                const unsigned c = cells[row * columns + column] & 0xff;
                text[n++] = c ? (char)c : ' ';
            }
            while (n > start && text[n - 1] == ' ') --n;
            if (row < y1) { text[n++] = '\r'; text[n++] = '\n'; }
        }
        text[n] = 0;
        DosVmClipboardSet(d.window, text);
    }
    d.selected = false;
    d.marking = false;
    DosVmScale();
}

// The exclusive mouse of the Misc page: the pointer is hidden and held in the
// client area, every movement goes to DOS.
void Application::DosVmCaptureMouse()
{
    auto &d = *dos_vm;
    if (!d.window || d.captured) return;
    const unsigned rect = Allocate(16);
    if (!rect) return;
    const unsigned client[] = {d.window, rect};
    Api("GetClientRect", client);
    const unsigned map[] = {d.window, 0, rect, 2};
    Api("MapWindowPoints", map);
    Api("ClipCursor", &rect);
    Free(rect);
    const unsigned hide[] = {0};
    Api("ShowCursor", hide);
    Api("SetCapture", &d.window);
    d.captured = true;
    d.mouse_seen = false;
}
void Application::DosVmReleaseMouse()
{
    auto &d = *dos_vm;
    if (!d.captured) return;
    d.captured = false;
    const unsigned none[] = {0};
    Api("ClipCursor", none);
    const unsigned show[] = {1};
    Api("ShowCursor", show);
    Api("ReleaseCapture", none);
}

// A click in the strip: the seven buttons of WINOLDAP. Mark starts the
// selection of a text block with the pointer, Copy puts the block on the
// clipboard, Paste types the clipboard's text into the machine, Full screen
// switches, Background lets the machine run behind other windows or not.
// Properties opens the property sheet of the program with the pages of
// PIFMGR (src/x86_pif_pages.cpp), Font the same sheet on its Font page, the
// way WINOLDAP calls EDITPROPERTIES (PIFMGR ) with a start page.
void Application::DosVmToolbarClick(int x, int y)
{
    const unsigned tail = TakeTail();
    auto &d = *dos_vm;
    if (y < ButtonTop || y >= ButtonTop + ButtonHeight) return;
    int left = ButtonsLeft;
    for (unsigned i = 0; i < 7; ++i)
    {
        if (i == 3) left += GroupGap;
        if (x >= left && x < left + ButtonWidth)
        {
            if (log)
            {
                char text[80];
                snprintf(text, sizeof text, "DOSVM toolbar button=%s", ToolbarButtons[i].name);
                log(text);
            }
            switch (i)
            {
            case 0: // Mark
                d.marking = !d.marking;
                d.selected = false;
                d.selecting = false;
                break;
            case 1: // Copy
                DosVmCopySelection();
                break;
            case 2: // Paste
            {
                char text[4096];
                const auto *provider = DosVm::Current();
                if (DosVmClipboardGet(d.window, text, sizeof text) && text[0] && provider && provider->PasteText && d.session)
                    provider->PasteText(provider->owner, d.session, text);
                break;
            }
            case 3: DosVmFullscreen(true); break;
            case 4: // Properties
            case 6: // Font
            {
                // The sheet runs a modal loop of its own; the window's timer
                // keeps the machine's pictures and sound going meanwhile.
                const unsigned window = d.window;
                const unsigned start_timer[] = {window, DosVmServiceTimer, 15, 0};
                exact_timer_request = true;
                Api("SetTimer", start_timer);
                d.sheet_open = true;
                const bool kept = (tail == TailResult || tail == TailRecord) && PushHostFrame(HostDosToolbar, window);
                if (kept) GiveTail(TailFresh);
                PifPropertySheet(d.props_path, window, i == 6 ? 1 : 0, d.title);
                FinishDosVmToolbar(window);
                if (kept) PopHostFrame();
            }
                break;
            case 5: d.background = !d.background; break;
            default: break;
            }
            // A modal sheet can return while destruction unwinds this fiber
            // after FreeDosVm. Reacquire the owner before touching its window.
            if (dos_vm && dos_vm->window) User::InvalidateRect(dos_vm->window, nullptr, false);
            return;
        }
        left += ButtonWidth;
    }
}

// The path a DOS program hands over, in the spelling of the Windows side: a
// component the store does not know by that name is an 8.3 alias of a long
// name, and the alias of a directory entry names it.
bool Application::DosVmWindowsPath(const char *dos, char (&out)[260])
{
    if (!(dos[0] && dos[1] == ':')) return false;
    snprintf(out, sizeof out, "%c:", dos[0]);
    const char *p = dos + 2;
    while (*p)
    {
        while (*p == '\\') ++p;
        if (!*p) break;
        const char *end = p;
        while (*end && *end != '\\') ++end;
        char component[256];
        const unsigned length = (unsigned)(end - p) < sizeof component - 1 ? (unsigned)(end - p) : sizeof component - 1;
        memcpy(component, p, length);
        component[length] = 0;
        char candidate[260];
        snprintf(candidate, sizeof candidate, "%s\\%s", out, component);
        unsigned node = 0;
        const char *spelling = component;
        if (files.store.Lookup(candidate, node))
        {
            char parent_path[260];
            snprintf(parent_path, sizeof parent_path, "%s\\", out);
            unsigned parent = 0;
            if (!files.store.Lookup(parent_path, parent))
                for (unsigned index = 0, child = 0; !files.store.Enumerate(parent, index, child); ++index)
                {
                    const auto *entry = files.store.Get(child);
                    if (entry && entry->alias[0] && SameText(entry->alias, component)) { spelling = entry->name; break; }
                }
        }
        if (strlen(out) + strlen(spelling) + 2 >= sizeof out) return false;
        snprintf(candidate, sizeof candidate, "%s\\%s", out, spelling);
        snprintf(out, sizeof out, "%s", candidate);
        p = end;
    }
    if (strlen(out) == 2) snprintf(out, sizeof out, "%c:\\", dos[0]);
    return true;
}

// Both the toolbar and its window procedure have work after the sheet.
void Application::FinishDosVmToolbar(unsigned window)
{
    if (dos_vm)
    {
        dos_vm->sheet_open = false;
        dos_vm->mouse_seen = false;
        if (dos_vm->window) User::InvalidateRect(dos_vm->window, nullptr, false);
    }
    const unsigned stop_timer[] = {window, DosVmServiceTimer};
    Api("KillTimer", stop_timer);
}

// The window procedure of the `tty` class.
unsigned Application::DosVmWindowMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    const unsigned tail = TakeTail();
    auto *d = dos_vm;
    switch (message)
    {
    case 0x0020: // WM_SETCURSOR: a graphics mode has its own pointer.
        if(d && d->OwnPointer() && (lp & 0xffff)==1) {
            Gdi::POINT point = {};User::CursorPosition(&point);
            User::ScreenToClient(hwnd, &point);
            if(point.y>=d->Strip()) {const unsigned cursor[1]={0};Api("SetCursor",cursor);return 1;}
        }
        return DefaultWindowMessage(hwnd,message,wp,lp);

    case 0x8000 + 0x98: // Host request for the properties of this DOS window.
        if (d) { GiveTail(tail); DosVmToolbarClick(ButtonsLeft + 4 * ButtonWidth + GroupGap + 1, ButtonTop + 1); }
        return 0;
    case 0x0400 + 0x0292:
        // A page of the property sheet applied a group (PIFMGR posts the
        // group to the window that opened the sheet): what can change while
        // the program runs changes now, the rest with the next start.
        if (d) DosVmReloadSettings();
        return 0;
    case 0x000f: // WM_PAINT
    {
        User::Paint paint = {};
        const unsigned dc = User::BeginPaint(hwnd, paint);
        if (log && trace_calls)
        {
            char text[160];
            snprintf(text, sizeof text, "DOSVM paint hwnd=%u dc=%u pixels=%u size=%ux%u", hwnd, dc, d && d->pixels ? 1u : 0u,
                     d ? d->width : 0u, d ? d->height : 0u);
            log(text);
        }
        if (!dc) return 0;
        Gdi::RECT client = {};
        User::GetClientRect(hwnd, &client);
        const int strip = d ? d->Strip() : 0;
        if (strip) DosVmPaintToolbar(dc, client.right);
        if (d && d->pixels)
        {
            DibEng::Bitmap bitmap{reinterpret_cast<unsigned char *>(d->pixels), (int)d->width, (int)d->height,
                                  (int)d->width * 4, 32};
            if (auto source = Gdi::CreateSurfaceDC(bitmap))
            {
                const bool drawn = Gdi::BitBlt(dc, 0, strip, (int)d->width, (int)d->height, source, 0, 0, 0x00cc0020);
                if (log && trace_calls && !drawn) log("DOSVM paint BitBlt refused");
                Gdi::DeleteDC(source);
                // Direct drawing into the window's surface: tell the
                // composition that these pixels changed.
                User::DrawingChanged(dc);
            }
            if ((int)d->width < client.right) Gdi::PatBlt(dc, (int)d->width, strip, client.right - (int)d->width, client.bottom - strip, 0x00000042);
            if ((int)d->height + strip < client.bottom) Gdi::PatBlt(dc, 0, (int)d->height + strip, client.right, client.bottom - (int)d->height - strip, 0x00000042);
        }
        else Gdi::PatBlt(dc, 0, strip, client.right, client.bottom - strip, 0x00000042);
        User::EndPaint(hwnd, paint);
        return 0;
    }
    case 0x0014: // WM_ERASEBKGND: the picture covers the client area
        return 1;
    case 0x0024: // WM_GETMINMAXINFO: maximized, the window is as large as
                 // its picture (DosVmZoomed), not as the screen.
    {
        const unsigned result = DefaultWindowMessage(hwnd, message, wp, lp);
        unsigned width = 0, height = 0;
        if (d && d->window == hwnd && !d->fullscreen && lp && DosVmPictureSize(true, width, height))
        {
            Gdi::RECT frame = {0, 0, (int)width, (int)height + d->Strip()};
            User::AdjustWindowRect(&frame, User::Style(hwnd), 0, User::ExtendedStyle(hwnd));
            const int size[2] = {frame.right - frame.left, frame.bottom - frame.top};
            Store(lp + 8, size, sizeof size); // ptMaxSize
        }
        return result;
    }
    case 0x0005: // WM_SIZE: maximized or restored, the font Auto follows and
                 // the frame fits the picture again.
        if (d && d->window == hwnd && !d->fullscreen && (wp == 0 || wp == 2) && d->zoomed != (wp == 2))
        {
            d->zoomed = wp == 2;
            d->client_width = d->client_height = 0;
            if (d->raw) DosVmScale();
            if (log)
            {
                char text[100];
                snprintf(text, sizeof text, "DOSVM maximized=%u client=%ux%u", d->zoomed ? 1u : 0u, d->client_width, d->client_height);
                log(text);
            }
        }
        return DefaultWindowMessage(hwnd, message, wp, lp);
    case 0x0100: case 0x0101: case 0x0104: case 0x0105: // WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP
    {
        const bool down = message == 0x0100 || message == 0x0104;
        const unsigned scan = (lp >> 16) & 0xff;
        const bool extended = (lp >> 24) & 1;
        // Alt+Enter switches between the window and the full screen, as
        // WINOLDAP does; the key does not reach DOS.
        // Without "Alt+Enter" reserved on the Misc page the keys go to DOS.
        if (d && d->alt_enter && wp == 0x0d && (lp & (1u << 29)))
        {
            if (message == 0x0104) DosVmFullscreen(!d->fullscreen);
            if (message == 0x0104 || message == 0x0105) return 0;
        }
        // QuickEdit: Enter copies a marked block, the way the console does.
        if (d && d->quickedit && d->selected && wp == 0x0d && message == 0x0100)
        {
            DosVmCopySelection();
            return 0;
        }
        if (d && d->session)
        {
            const auto *provider = DosVm::Current();
            if (provider) provider->Key(provider->owner, d->session, wp, scan, extended, down);
            if (scan < 128) { d->keys_down[extended][scan] = down; d->keys_down_vk[extended][scan] = wp; }
        }
        return 0;
    }
    case 0x0200: case 0x0201: case 0x0202: case 0x0204: case 0x0205: case 0x0207: case 0x0208:
    {
        // WM_MOUSEMOVE and the button messages: the DOS mouse takes steps in
        // the pixels of its own mode, so the window's movement is divided by
        // the scale it shows the picture at. In a window Win98 maps the
        // pointer to the DOS screen the same way, the DOS cursor follows the
        // Windows pointer over the client area.
        if (!d || !d->session) return 0;
        const int x = (short)(lp & 0xffff), y = (short)(lp >> 16);
        // The toolbar and the game both hit HTCLIENT. USER's hit-test cache
        // cannot distinguish them, so update the shape on client movement.
        if(d->OwnPointer()) {
            if(y>=d->Strip()) {const unsigned cursor[1]={0};Api("SetCursor",cursor);}
            else DefaultWindowMessage(hwnd,0x0020,hwnd,1u|(message<<16));
        }
        if (y < d->Strip())
        {
            if (message == 0x0201) { GiveTail(tail); DosVmToolbarClick(x, y); }
            if (dos_vm) dos_vm->mouse_seen = false;
            return 0;
        }
        // QuickEdit: a right click copies a marked block.
        if (d->quickedit && message == 0x0204 && d->selected) { DosVmCopySelection(); return 0; }
        if ((d->marking || d->quickedit) && d->text_columns && d->text_rows && d->width && d->height)
        {
            // Mark, and QuickEdit of the Misc page: the pointer selects
            // cells of the text screen instead of moving the DOS mouse.
            const int strip = d->Strip();
            const int cw = (int)(d->width / d->text_columns), ch = (int)(d->height / d->text_rows);
            int cx = (x / (cw ? cw : 1)), cy = ((y - strip) / (ch ? ch : 1));
            if (cx < 0) cx = 0; if (cy < 0) cy = 0;
            if (cx >= (int)d->text_columns) cx = (int)d->text_columns - 1;
            if (cy >= (int)d->text_rows) cy = (int)d->text_rows - 1;
            if (message == 0x0201)
            {
                d->selecting = true; d->selected = true; d->sel_x0 = d->sel_x1 = cx; d->sel_y0 = d->sel_y1 = cy; DosVmScale();
                if (log) { char text[80]; snprintf(text, sizeof text, "DOSVM mark start cell=%d,%d picture=%ux%u", cx, cy, d->width, d->height); log(text); }
            }
            else if (message == 0x0200 && d->selecting) { d->sel_x1 = cx; d->sel_y1 = cy; DosVmScale(); }
            else if (message == 0x0202) { d->selecting = false; d->sel_x1 = cx; d->sel_y1 = cy; DosVmScale(); }
            return 0;
        }
        // Exclusive mode: the first click takes the pointer into the window
        // until the window loses the focus.
        if (d->exclusive && !d->captured && message == 0x0201) { DosVmCaptureMouse(); return 0; }
        int dx = 0, dy = 0;
        if (d->mouse_seen)
        {
            if (d->scale) { dx = (x - d->mouse_x) / (int)d->scale; dy = (y - d->mouse_y) / (int)d->scale; }
            else { dx = (x - d->mouse_x) * (int)d->raw_width / (int)ScreenWidth; dy = (y - d->mouse_y) * (int)d->raw_height / (int)ScreenHeight; }
        }
        // Keep the remainder of the division for the next step, so slow
        // movement still arrives.
        if (dx || dy || !d->mouse_seen) { d->mouse_x = x; d->mouse_y = y; }
        d->mouse_seen = true;
        const unsigned buttons = (wp & 1 ? 1u : 0u) | (wp & 2 ? 2u : 0u) | (wp & 0x10 ? 4u : 0u);
        // The place: where the Windows pointer is over the picture, as
        // WINOLDAP and VMD give it to the DOS mouse driver of a windowed VM. The exclusive mouse has no Windows pointer; its place
        // moves by the steps.
        if (d->width && d->height)
        {
            auto clamp = [](long long v) { return (unsigned)(v < 0 ? 0 : v > 65535 ? 65535 : v); };
            if (d->captured && d->raw_width && d->raw_height)
            {
                d->place_x = clamp((long long)d->place_x + (long long)dx * 65536 / (int)d->raw_width);
                d->place_y = clamp((long long)d->place_y + (long long)dy * 65536 / (int)d->raw_height);
            }
            else
            {
                d->place_x = clamp(((long long)x * 2 + 1) * 32768 / (int)d->width);
                d->place_y = clamp(((long long)(y - d->Strip()) * 2 + 1) * 32768 / (int)d->height);
            }
        }
        const auto *provider = DosVm::Current();
        if (provider) provider->Mouse(provider->owner, d->session, dx, dy, d->place_x, d->place_y, buttons);
        return 0;
    }
    case 0x0010: // WM_CLOSE
        // While a program runs, WINOLDAP asks first: strings 276 and 2324 of
        // WINOA386.MOD, a Yes/No box with the window's title. At the bare
        // prompt, or after the program has ended, the window just closes.
        // "Warn if still active" of the Misc page off: no question.
        if (d && d->session && d->warn_close && !d->finished && d->running[0] && strcmp(d->running, "DOSBOX"))
        {
            char title[120];
            User::GetWindowText(hwnd, title, sizeof title);
            const unsigned answer = MessageBox(hwnd,
                "Windows cannot shut down this program automatically.  It is recommended that you exit the program "
                "with its quit or exit command.\n\nDo you wish to terminate this program now and lose any unsaved "
                "information in the program?", title, 0x34); // MB_YESNO | MB_ICONEXCLAMATION
            if (answer != 6) return 0;
        }
        CbtDestroyWindow(hwnd, false);
        return 0;
    case 0x0111: // WM_COMMAND: CBN_SELCHANGE of the size box
        if (d && (wp & 0xffff) == DosVmFontBoxId && (wp >> 16) == 1 && d->font_box)
        {
            const unsigned get[] = {d->font_box, 0x147, 0, 0}; // CB_GETCURSEL
            const unsigned at = Api("SendMessageA", get);
            const unsigned data_args[] = {d->font_box, 0x150, at, 0}; // CB_GETITEMDATA
            if (at < 0x8000) DosVmChooseFont(Api("SendMessageA", data_args) & 0xffff);
            return 0;
        }
        return DefaultWindowMessage(hwnd, message, wp, lp);
    case 0x002b: // WM_DRAWITEM of the size box
        if (d && lp && Read32(lp + 4) == DosVmFontBoxId) return PifFontDrawItem(lp, true);
        return DefaultWindowMessage(hwnd, message, wp, lp);
    case 0x002c: // WM_MEASUREITEM of the size box
        if (d && lp && Read32(lp + 4) == DosVmFontBoxId) { Write32(lp + 0x10, 16); return 1; }
        return DefaultWindowMessage(hwnd, message, wp, lp);
    case 0x0113: // WM_TIMER: the service turn while the property sheet is open
        if (wp == DosVmServiceTimer)
        {
            if (d && d->sheet_open) DosVmService(true);
            return 0;
        }
        return DefaultWindowMessage(hwnd, message, wp, lp);
    case 0x0006: // WM_ACTIVATE: a full screen in the background leaves the topmost band
        if (d && d->fullscreen) User::OrderWindow(hwnd, (wp & 0xffff) ? ~0u : ~1u);
        return DefaultWindowMessage(hwnd, message, wp, lp);
    case 0x0008: // WM_KILLFOCUS: an exclusive mouse lets go
        if (d && d->captured) DosVmReleaseMouse();
        // The keys held down go up: their releases reach the window that
        // has the keyboard now, and the machine would keep them pressed.
        if (d && d->session)
            if (const auto *provider = DosVm::Current())
                for (unsigned e = 0; e < 2; ++e)
                    for (unsigned sc = 0; sc < 128; ++sc)
                        if (d->keys_down[e][sc])
                        {
                            provider->Key(provider->owner, d->session, d->keys_down_vk[e][sc], sc, e != 0, false);
                            d->keys_down[e][sc] = 0;
                        }
        return DefaultWindowMessage(hwnd, message, wp, lp);
    case 0x0002: // WM_DESTROY
        if (d && d->captured) DosVmReleaseMouse();
        if (d && d->window == hwnd) { d->closed = true; d->window = 0; }
        return DefaultWindowMessage(hwnd, message, wp, lp);
    default:
        return DefaultWindowMessage(hwnd, message, wp, lp);
    }
}
} // namespace X86
namespace DosVm
{
bool IsProgram(const char *name, const void *bytes, unsigned length)
{
    if (name && (X86::DosVmEndsWith(name, ".COM") || X86::DosVmEndsWith(name, ".BAT") || X86::DosVmEndsWith(name, ".PIF"))) return true;
    const auto *data = static_cast<const unsigned char *>(bytes);
    if (data && length >= Win98PifMgr::HeaderBytes + Win98PifMgr::SectionHeader &&
        Win98PifMgr::Detail::SectionName(data + Win98PifMgr::HeaderBytes, "MICROSOFT PIFEX")) return true;
    // An MZ file whose new-executable pointer names no PE or NE header: a
    // plain DOS program, also a DOS4GW one with an LE/LX image behind the stub.
    if (!data || length < 0x40 || data[0] != 'M' || data[1] != 'Z') return false;
    const unsigned relocation_table = data[0x18] | (unsigned)data[0x19] << 8;
    const unsigned header = data[0x3c] | (unsigned)data[0x3d] << 8 | (unsigned)data[0x3e] << 16 | (unsigned)data[0x3f] << 24;
    if (relocation_table < 0x40) return true; // a header this short has no e_lfanew
    if (header + 4 > length) return true;
    if (data[header] == 'P' && data[header + 1] == 'E' && !data[header + 2] && !data[header + 3]) return false;
    if (data[header] == 'N' && data[header + 1] == 'E') return false;
    return true;
}
} // namespace DosVm
