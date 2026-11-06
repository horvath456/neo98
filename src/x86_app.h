// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "fiber.h"
#include "x86_state.h"
#include "api_name.h"
#include "win98_selector_table.h"
#include "pe32.h"
#include "registry.h"
#include <new>
#include "files.h"
#include "user.h"
#include "win98_caret.h"
#include "win_image.h"
#include "win98_find.h"
#include "win98_shell.h"
#include "win98_critical.h"
#include "virtual_memory.h"
#include "execute_pages.h"
#include "win98_session_arena.h"
#include "win98_hooks.h"
#include "win98_kernel_misc.h"
#include "win98_keyboard_layout.h"
#include "win98_keyboard_translate.h"
#include "win98_accelerators.h"
#include "win98_semaphore.h"
#include "win98_thread_control.h"
#include "win98_pointer_probe.h"
#include "win98_default_locale.h"
#include "win98_crt_multibyte.h"
#include "win98_system_locale.h"
#include "win98_path_family.h"
#include "win98_shell_dragdrop.h"
#include "win98_shell_pif.h"
#include "win98_ne_image.h"
#include "win98_version_info.h"
#include "crash_report.h"
#include "x86_cpu.h"
namespace PcVga { struct Device; }
namespace Gpu { class Backend; struct Surface; }
namespace MachineState { struct Machine; }
namespace OpenGL { class Context; struct Picture; }
namespace Win98MessageBox {struct Parameters;struct Buttons;}
namespace Sound {struct Format;}
namespace DosVm
{
struct Frame;
struct Launch;
struct Session;
}
namespace Win98PifMgr
{
struct Record;
}
namespace Win98DisplayReposition { struct Rect; }
struct AH_Status; // src/host_devices_abi.h
namespace X86
{
class Application final : private Pe32::AddressSpace
{
    // The saved state of a machine reads and binds every part of a process
    // (src/x86_state_machine.cpp).
    friend struct MachineState::Machine;
  public:
    // Where the guest code and the record arena of critical sections lie in
    // every process (src/x86_critical.cpp).
    static constexpr unsigned CriticalCodeBase = 0xc8000000u, CriticalRecordBase = 0xc8010000u,
                              CriticalRecordBytes = 0x4000000u, CriticalChunkBytes = 0x10000u;
    // Thread records of one process. Win98 has no such limit; sixteen ran
    // out under Internet Explorer 5's setup, whose CRYPT32 keeps a waiting
    // thread for every open certificate store and whose INSENG never closes
    // the handles of its finished install threads, and a finished thread
    // keeps its record while a handle names it. The first sixteen
    // stacks lie in the port's reservation at 20000000h, the others in the
    // private arena (Application::CreateThread). A saved state carries the
    // first sixteen; a process that uses more is not saved.
    static constexpr unsigned ThreadCapacity = 128, FixedStackSlots = 16;
    // Kernel handles of one process. Win98 grows the handle table of a
    // process as needed; 128 ran out under the IE 5 setup, which keeps an
    // event pair and a thread handle for every CRYPT32 watcher.
    static constexpr unsigned KernelHandleCapacity = 512, KernelGenerationMask = 0x3fff;
    // Records of the processes of the session, live or ended. An ended
    // process keeps its record (exit code and identity) while handles to it
    // are open; installers leave many of them open, and sixteen ran out under
    // the IE 5 setup of Office XP. Live processes stay bound by the
    // sixteen process contexts of USER.
    static constexpr unsigned ChildCapacity = 64;
    // Whether the thread with this index of this process has run out. A wait
    // on a thread handle another process duplicated asks the owning process
    // this; see src/x86_multiwait.cpp.
    bool ThreadDone(unsigned index) const;
    // Win98 keeps the modules of a process in a linked list of MODREFs and sets
    // no upper bound. NEO98 keeps a table, so the bound is ours; it has to be
    // high enough for the largest graph a Win98 program really opens. The shell
    // with the web view of the Control Panel holds 32 modules before OLE32
    // loads the class of the folder, which is where the old table of 32 ran out.
    static constexpr unsigned ModuleCapacity = 128;
    // The dependency edges of one module, one bit per table slot.
    struct ModuleSet
    {
        static constexpr unsigned Words = (ModuleCapacity + 31) / 32;
        unsigned words[Words] = {};
        bool Has(unsigned i) const { return i < ModuleCapacity && (words[i >> 5] >> (i & 31) & 1u); }
        void Add(unsigned i) { if (i < ModuleCapacity) words[i >> 5] |= 1u << (i & 31); }
        void Remove(unsigned i) { if (i < ModuleCapacity) words[i >> 5] &= ~(1u << (i & 31)); }
        void Merge(const ModuleSet &other) { for (unsigned w = 0; w < Words; ++w) words[w] |= other.words[w]; }
        bool operator==(const ModuleSet &other) const
        {
            for (unsigned w = 0; w < Words; ++w) if (words[w] != other.words[w]) return false;
            return true;
        }
    };
    enum State
    {
        Running,
        Idle,
        Exited,
        Failed
    };
    // Native configuration: UTC = local + bias minutes. Both profiles default to UTC.
    void ConfigureTimeZoneBias(int minutes);
    // The local time the guest clock starts from, SYSTEMTIME order.
    static void StartGuestClock(const unsigned short fields[8]);
    static bool guest_clock_frozen;
    bool ConfigureKeyboardLayout(const char *map); // Installed physical provider: DE or US, before Start.
    // VkKeyScan of the installed layout: shift state (1 SHIFT, 2 CTRL, 4 ALT)
    // over the virtual key, FFFFh when the layout has no key for it.
    unsigned KeyScan(unsigned character);
    using Logger = void (*)(const char *);
    explicit Application(Logger logger);
    ~Application();
    // The registry change observer of src/x86_advapi_registry.cpp reaches the
    // process that registered the notification through this.
    void SignalRegistryNotification(unsigned event);
    // A medium change of one drive while the machine runs, after the host has
    // mounted the new provider for the letter (Volume::Mount, CdAudio::Insert):
    // the file store forgets the old medium, MSCDEX reports the change once,
    // and every top-level window gets WM_DEVICECHANGE with
    // DBT_DEVICEREMOVECOMPLETE for the medium that left and DBT_DEVICEARRIVAL
    // for the one that came, as CONFIGMG broadcasts them on Win98. Explorer
    // refreshes and starts AUTORUN.INF from there. src/x86_device_change.cpp,
    // `announce` false mounts without the broadcast: the folder of a
    // program the host was asked to start is a drive for that program, and
    // the shell would open a window on a drive that arrives.
    bool ChangeMedium(char letter, bool announce = true);
    bool Start(const void *exe, unsigned length, const unsigned char *font, unsigned font_length,
               const char *program_name = "NOTEPAD.EXE", const char *command = nullptr);
    // Play the user's SystemStart sound asynchronously after logon and before
    // launching the SYSTEM.INI shell, without a default fallback sound. The
    // host calls this once per machine start.
    void PlayStartupSound();
    const unsigned *ConsolePixels(); // Native host view of the active text buffer.
    unsigned long long ConsoleFrameHash() const;
    // A CLI host may copy console stream writes to its terminal. The bytes
    // retain the guest's console code page, currently CP437.
    using ConsoleOutput = void (*)(const unsigned char *, unsigned, bool, unsigned);
    static void SetConsoleOutput(ConsoleOutput);
    unsigned ConsoleInputSpace() const;
    unsigned ConsoleCodePage();
    void EndConsoleInput();
    State Pump(unsigned slices = 32);
    static void PumpChildren(unsigned slices = 8);
    static unsigned LiveChildren();
    // Called for every child process right after its image started; a host
    // runner uses it to install its watches there too.
    static inline void (*child_started)(Application &) = nullptr;
    // Every live process of the session parks in its message wait. A launcher
    // that started another program and left is out of the picture; what
    // decides is the last process still running.
    static bool SessionWaitingForInput();
    // How many records of the session-wide named-object namespace still have
    // a handle on them. A test uses it to see that a session leaves none.
    static unsigned LiveKernelObjects();
    // Ending a session: every child still running is terminated the way
    // KERNEL32!TerminateProcess terminates it, then reaped by PumpChildren.
    // A shell leaves the autostart programs of the Run keys running, so a
    // host runner needs this to finish. Returns how many were terminated.
    static unsigned TerminateChildren(unsigned code);
    void TerminateSelf(unsigned code); // The body of TerminateProcess.
    static unsigned ChildRecords();
    // Children that ended in Fail. `contains` selects one class of error,
    // `first` takes the text of the first match. A shell test uses this to see
    // that no autostart program died silently; the shell itself never learns
    // about it, Win98 only writes a box.
    static unsigned FailedChildren(const char *contains = nullptr, const char **first = nullptr);
    // The n-th recorded failure, "program: reason", or nullptr past the end.
    static const char *FailedChild(unsigned index);
    State Status() const
    {
        return state;
    }
    bool WaitingForInput() const
    {
        // Block 1 is the GetMessage style wait. Block 7 is the multiwait of
        // MsgWaitForMultipleObjects; with a wake mask and no deadline it waits
        // for input exactly like the first one, which is how a shell parks
        // while its children run. Anything with a deadline is still working.
        if (state != Idle)
            return false;
        // A thread that waits for the network waits for input from outside
        // too, and a page on its way is not an idle browser.
        if (NetworkPending())
            return false;
        if (threads[0].block == 1)
            return true;
        return threads[0].block == 7 && threads[0].multiwait.message && !threads[0].deadline;
    }
    const char *Error() const
    {
        return error;
    }
    bool CursorVisible() const { return !(cursor_count & 0x80000000u); }
    unsigned ApiCalls() const
    {
        return api_calls;
    }
    // Pictures the display driver put on the screen: flips, and blits into
    // the primary surface (a game in a window). A frame counter for tests.
    unsigned HalPresents() const { return hal_presents; }
    // Progress the host can see from outside: API calls, plus the turns of a
    // DOS machine's loop, which runs DOSBox natively and calls no API while a
    // DOS program has the keyboard, and the turns of a modal loop of USER
    // (ModalMessage). The calls of a 16-bit program count as well: its gates
    // are thunk transitions and no API calls, and without them a Win16
    // program that polls PeekMessage in a loop of its own (the title picture
    // of Shanghai II) was reported as hanging.
    unsigned Progress() const
    {
        return api_calls + native_turns + thunk_transitions;
    }
    unsigned Callbacks() const
    {
        return callbacks;
    }
    unsigned MainWindow() const
    {
        return main_window;
    }
    unsigned ExitCode() const
    {
        return exit_code;
    }
    // Read-only compositor inspection; does not enter an x86 callback from the host.
    bool InspectListBox(unsigned hwnd, unsigned &count, unsigned &selection) const;
    unsigned LiveThreads() const;
    unsigned ThreadsCreated() const
    {
        return threads_created;
    }
    unsigned ThreadId() const;
    unsigned ProcessId() const {return process_id;}
    void Key(unsigned key, unsigned character, bool shift, bool control);
    void KeyUp(unsigned key, bool shift = false, bool control = false);
    // The key Key pressed for the key its caller named, for KeyUp.
    unsigned typed_named_key = 0, typed_key = 0;
    void RawKey(unsigned key, unsigned character, bool down, unsigned scan = 0, bool extended = false);
    void KeyboardModifiers(unsigned usb_mask);
    void Mouse(int x, int y, bool down, bool up);
    void MouseButtons(int x, int y, unsigned buttons, const Gdi::POINT *motion = nullptr);
    // Win98 moves the real pointer when a program calls SetCursorPos, and the
    // mouse driver measures the next movement from the new place. A host that
    // only feeds absolute window coordinates has to move its own pointer along,
    // or a program that centres the cursor after every movement reads a
    // distance from the centre instead of a step. The provider is the
    // host's; src/ stays free of any window system.
    static void SetHostCursorProvider(void *context, void (*move)(void *, int, int));
    // The screen the host shows the desktop on, in place of the size the
    // registry names : the display driver starts in it and
    // comes back to it, and the registry never stores it. Zero and zero give
    // the registry's size back. With `apply` a running session changes mode
    // the next time one of its threads asks for messages, the way
    // ChangeDisplaySettings of another process would reach it.
    static void SetHostDisplay(int width, int height, bool apply);
    static void HostDisplay(int &width, int &height);
    static bool HostDisplayPending();
    // A window of the guest the user picked on the Mac (Mission Control, the
    // window menu): it comes to the foreground on the same kind of thread.
    static void RequestForeground(unsigned hwnd);
    static void RequestWindowSize(unsigned hwnd, int width, int height);
    static void RequestCtrlAltDelete();
    static bool HostRequestPending();
    // The host's own application lost or got the activation: a window that
    // is not the guest's took the foreground, the way a window of another
    // program does under Win98, or gave it back.
    static void HostActivation(bool active);
    // The work area is the whole monitor whatever the shell sets, for a host
    // that does not show the shell's taskbar; off, the shell's own work area
    // comes back. Applied like the other requests of the host.
    static void SetHostWorkArea(bool whole);
    // The host shows every window of the guest as a window of its own (the
    // seamless view): a DOS session keeps its window where Win98 would give
    // it the whole display (the PIF's "full screen", Alt+Enter). The desktop
    // of such a host is not a screen a DOS mode could take.
    static void SetHostKeepsDosWindows(bool keep);
    static bool HostKeepsDosWindows();
    // Under such a host Alt+Enter and the toolbar's "Full screen" of a DOS
    // window ask the host to show that window alone over the whole display,
    // and to leave that again: the host takes the request here. `strip` is
    // the height of the toolbar above the machine's picture, which the full
    // screen leaves out, as Win98 shows only the DOS screen.
    static void AskHostDosFullscreen(unsigned window, int strip);
    static bool TakeHostDosFullscreen(unsigned &window, int &strip);
    // The toolbar strip of a DOS window the host knows from such a request
    // or from the session's start; -1 for any other window.
    static void NoteDosWindow(unsigned window, int strip);
    static int DosWindowStrip(unsigned window);
    // The window the host shows alone over the display right now, or 0. A
    // DOS window whose 3dfx Voodoo has the monitor draws the card at the
    // resolution of the host's output then (src/x86_dos_vm.cpp).
    static void SetHostZoomedWindow(unsigned window);
    static unsigned HostZoomedWindow();
    static bool HostWorkWhole();
    static bool HostWorkAsked(const void *rect, void *out = nullptr);
    bool serving_host_work = false;
    // The pointer the guest shows, for a host that shows it with a cursor of
    // its own instead of USER's drawn one : the OEM
    // identifier when it is one of USER's cursors (IDC_ARROW...), zero for
    // a program's own; its handle; and its picture as ARGB rows with the hot
    // spot (alpha 255 where the cursor covers, an inverting pixel black).
    // False when the guest shows no pointer.
    static bool HostPointer(unsigned &identifier, unsigned &handle, int &width, int &height, int &hot_x, int &hot_y,
                            unsigned *argb, unsigned capacity);
    // The program a window of the guest belongs to, for a host that shows
    // every program with an entry of its own (the Dock):
    // the process, its program name, and the icon of the window as ARGB rows
    // (WM_GETICON's large icon, the class icon, then the small ones; alpha
    // 255 where the icon covers). Width zero when the window has none.
    static bool HostWindowProgram(unsigned hwnd, unsigned &process, char *program, unsigned program_size, int &width,
                                  int &height, unsigned *argb, unsigned capacity);
    // The guest path of the program file behind a window ("C:\\SPIELE\\SPIEL.EXE"):
    // the host offers a game machine for the program in front. False for a window without a process here.
    static bool HostWindowProgramPath(unsigned hwnd, char *path, unsigned path_size);
    // Original desktop ListView icons/labels, sampled on its process's thread.
    static const unsigned char *HostDesktopIconMask(unsigned now, int width, int height);
    static void MoveHostCursor(int x, int y);
    void MouseWheel(int steps);
    void MouseScroll(int units, int sideways, unsigned flags);
    static int WheelLines(unsigned hwnd, int delta);
    // Every native tree view of this process, a line per shown row, for tests.
    void DumpTrees(void (*line)(const char *));
    void DumpLists(void (*line)(const char *));
    void DumpListBoxes(void (*line)(const char *));
    unsigned Send(unsigned hwnd, unsigned message, unsigned wp = 0, unsigned lp = 0);
    unsigned ScrollBarMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp);
    unsigned SendFromNative(unsigned hwnd,unsigned message,unsigned wp,unsigned lp);
    unsigned SendCrossProcess(unsigned hwnd,unsigned message,unsigned wp,unsigned lp,unsigned timeout=0,unsigned flags=0,bool *completed=nullptr);
    unsigned SendCrossProcessRaw(Application*other,unsigned hwnd,unsigned message,unsigned wp,unsigned lp,unsigned timeout=0,unsigned flags=0,bool *completed=nullptr,bool activation=false);
    unsigned QueueForegroundActivation(unsigned hwnd, unsigned flags);
    // SwitchForeground flag of ShowWindow (USER): change the foreground
    // queue without activating another window of that queue yet. Distinct
    // from the activation codes (2, and 3 for a mouse click) passed through.
    static constexpr unsigned ForegroundQueueOnly = 0x100;
    // Pending sends whose target window, thread, or process disappears.
    void CancelSendsToWindow(unsigned hwnd);
    void CancelSendsOfThread(unsigned index);
    void CancelSendLifetimes();
    unsigned SendDispatch(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);

  private:
    struct ProcessLaunchEnvironment;
    unsigned ProcessApi(ApiName, const unsigned *);
    struct LegacyProcessEnvironment;
    unsigned LegacyProcessApi(ApiName, const unsigned *);
    unsigned LaunchChild(const unsigned *, const char *, const char *);
    Win98PathFamily::State *ProcessFileState();
    static void ReferenceChild(unsigned);
    static void ReleaseChild(unsigned);
    static unsigned ChildProcessIdentity(unsigned);
    // Every child the session ever started, including the ones whose record
    // is already gone because they finished and their handles closed.
    static unsigned children_started;
public:
    static unsigned ChildrenStarted();
    // The state of a waitable process object, for a runner that names what a
    // stalled thread waits for.
    static bool ChildSignaled(unsigned, bool);
    // A process handle of a foreign process (OpenProcess on a process this
    // Application did not start). Win98 signals such a handle when the
    // process ends, so the holder can wait on it; DDHELP.EXE does exactly
    // that for every DirectDraw client.
    static bool ForeignProcessSignaled(unsigned identity);
private:
    unsigned FindProcessIdentity(unsigned identity, bool &live);
    static unsigned ChildExitCode(unsigned, bool);
    void FreeChildHandles();
    void OrphanChildren();
    unsigned char *owned_executable=nullptr, *owned_font=nullptr;
    char *initial_environment=nullptr;
    unsigned initial_environment_size=0;
    unsigned startup_info[17]={68,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0};
    const char *initial_title=nullptr;
    User::InputPacket input_packets[256];
    unsigned input_head = 0, input_count = 0;
    void EnqueueInput(const User::InputPacket &);
    void DrainInput();
    bool InputPending() const;
    int MatchingInput(unsigned hwnd, unsigned minimum, unsigned maximum) const;
    void RoutePacket(unsigned offset);
    unsigned InputThread();
    unsigned input_thread = 0;
    void MouseFocus(unsigned hwnd, bool focus = true);
    unsigned MouseActivate(unsigned hwnd, unsigned hit, unsigned message);
    struct HookEnvironment;
    struct HookSlot
    {
        unsigned handle = 0;
        Win98Hooks::Record record;
    } hooks[128];
    unsigned hook_serial = 0, hook_heads[129][14]{}, hook_owners[129]{}, current_hooks[17]{};
    // Hooks for all threads of the session (SetWindowsHookEx with thread 0):
    // the session keeps module and procedure, each process whose code is of
    // the kind of that module loads it and carries an entry in its own
    // global chain (src/x86_hooks.cpp).
    struct SessionHookMirror { unsigned session = 0, local = 0; } session_hook_mirrors[16];
    unsigned session_hooks_seen = 0;
    bool session_hooks_busy = false;
    void SyncSessionHooks();
    void RemoveSessionHooks();
    struct CbtDestroyEnvironment;
    unsigned CbtDestroyWindow(unsigned,bool);
    void DestroyThreadWindows(unsigned);
    bool CbtPresent();
    bool CbtEvent(unsigned,unsigned,unsigned);
    bool CbtActivate(unsigned,unsigned,unsigned);
    bool CbtCreate(unsigned,unsigned);
    unsigned ScrollApi(ApiName,const unsigned*);
    unsigned HookApi(ApiName, const unsigned *);
    int CurrentHookType() const;
    unsigned MessageHook(int type, int code, unsigned wp, unsigned message);
    unsigned MessageHook(int type, int code, unsigned wp, User::Message &);
    bool InputHook(const User::Message &, bool removed);
    void CallWndProcHook(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    bool dispatching_posted = false;
    bool FilterMessage(User::Message &, int code);
    void RemoveThreadHooks(unsigned thread);
    unsigned process_id=0;
    static unsigned AllocateThreadIdentity();
    VirtualMemory::Space virtual_memory;
    struct MemoryBackend;
    bool ConfigureMemory();
    bool MapSystemBios();
    // The native allocation arena of Allocate. Its address is chosen after
    // the image is mapped.
    static constexpr unsigned HeapArenaBytes = 0x2000000;
    // The arena grows behind its end while the address space there is free,
    // the way the process heap of Win98 grows, up to this limit.
    static constexpr unsigned HeapArenaLimit = 0x10000000;
    // heap_end closes the block next_alloc bumps through; heap_total counts
    // every block the arena has mapped.
    unsigned heap_base = 0, heap_end = 0, heap_total = HeapArenaBytes;
    bool MapProcessHeap();
    bool GrowHeap(unsigned required, bool relocate = true);
    // AddFontResource: installs a font file of the guest, answers its faces.
    unsigned AddFontResourceFile(const char *path);
    // The free space of the arena: every free block plus what it can still
    // grow by. `largest` is the biggest single block a request could get.
    void HeapFree(unsigned &total, unsigned &largest) const;
    bool InitializeCpuDescription();
    unsigned InstanceVersion(unsigned instance);
    void DescribeProcessor(const unsigned *vendor);
    unsigned cpu_signature = 0;
    unsigned ProcessMemoryApi(ApiName, const unsigned *);
    unsigned MemoryStatusApi(const unsigned *);
    // The guest's _GetDemandPageInfo report (14 doublewords) and DPMI 0500h,
    // Get Free Memory Information, the 30h bytes of the answer as twelve
    // doublewords, both built from it (src/x86_memory_status.inc,

    bool GuestDemandInfo(unsigned *info);
    bool DpmiFreeMemoryInfo(unsigned *info);
    unsigned loader_writes = 0;
    unsigned MemoryApi(ApiName, const unsigned *);
    struct MappingSystem;
    MappingSystem *mapping_system = nullptr;
    unsigned MappingApi(ApiName, const unsigned *);
    void ReferenceMapping(unsigned);
    void ReleaseMappingReference(unsigned);
    void FreeMappings();
    unsigned ReserveMappingPages(unsigned, unsigned, unsigned);
    unsigned CommitMappingPages(unsigned, unsigned, unsigned);
    unsigned ReleaseMappingPages(unsigned);
    bool Unmap(unsigned, unsigned);
    // The gate for software interrupts of the 32-bit guest, in
    // src/x86_interrupts.cpp.
    static void GuestInterrupt(Cpu &, unsigned number, void *context);
    // Port input of the guest, src/x86_app.cpp. The refresh rate is the one the
    // display driver of this port reports for its modes.
    PcVga::Device *vga = nullptr;
    int vga_width = 0, vga_height = 0, vga_depth = 0;
    bool EnsureVga();
    bool BindVga();
    bool VgaInterrupt();
    void EndVga();
    bool VgaScanout();
    static unsigned GuestPortInput(Cpu &, unsigned port, unsigned size, void *context);
    static void GuestPortOutput(Cpu &, unsigned port, unsigned size, unsigned value, void *context);
    static constexpr unsigned RefreshHertz = 60, RetraceMicroseconds = 1200;
    // What GetWindowThreadProcessId answers for the desktop window (src/x86_windows.cpp).
    static constexpr unsigned DesktopOwnerThread = 0xfffe0001u, DesktopOwnerProcess = 0xfffe0000u;
    unsigned NtServiceCall(unsigned service, unsigned arguments);
    unsigned PowerCapabilities(unsigned input, unsigned input_bytes, unsigned output,
                               unsigned output_bytes);
    unsigned PowerPolicy(unsigned level, unsigned input, unsigned input_bytes, unsigned output,
                         unsigned output_bytes);
    // The session-wide arena: one set of host bytes at one guest address in
    // every process of the session. Implemented in src/x86_session_arena.cpp.
    struct SessionBlock;
    static SessionBlock session_blocks[96];
    static Win98SessionArena::Plan session_plan;
    static Application *session_processes[Win98SessionArena::HolderCapacity];
    void JoinSession();
    void LeaveSession();
    void PublishSessionBlock(SessionBlock &);
    static SessionBlock *FindSessionBlock(unsigned address);
    static SessionBlock *FindSessionSection(unsigned address);
    static SessionBlock *FindSessionName(unsigned kind, const char *name);
    struct SessionBackend;
    SessionBlock *CreateSessionBlock(unsigned kind, unsigned address, unsigned bytes, const char *name);
    // A linear range of the session window and the oldest heap HeapDestroy
    // gave back, handed out again when the window or the records run out

    unsigned ReserveSessionRange(unsigned bytes);
    static bool RecycleRetiredSessionHeap();
    static unsigned session_retire_clock;
    unsigned char *SystemArena(const char *name, unsigned address, unsigned bytes);
    unsigned CommitSessionPages(SessionBlock &, unsigned page, unsigned count, unsigned protection = 0x60000);
    unsigned DecommitSessionPages(SessionBlock &, unsigned page, unsigned count);
    bool AttachSessionBlock(SessionBlock &, bool loaded_section = false);
    void ReleaseSessionBlock(SessionBlock &);
    bool HoldsSessionBlock(const SessionBlock &) const;
    void ShareImageSections(unsigned module);
    // The address a module of the session got when its preferred base was
    // taken, so it lies at the same place in every process.
    static unsigned SessionModuleBase(const char *leaf);
    static void RecordSessionModuleBase(const char *leaf, unsigned base, unsigned bytes);
    void DropSessionBlocksIn(unsigned base, unsigned size);
    void ReleaseSessionSectionsIn(unsigned base, unsigned size);
    void FreeSessionBlocks();
    unsigned process_database = 0;
    bool InitializeProcessDatabase();
    unsigned ReserveSessionHeap(unsigned pages);
    unsigned AdoptSessionHeap(unsigned handle);
    bool SessionHeapAddress(unsigned address) const;
    unsigned CommitSessionHeap(unsigned page, unsigned count);
    unsigned DecommitSessionHeap(unsigned page, unsigned count);
    unsigned CommitSessionView(unsigned page, unsigned count);
    void RetireSessionHeap(unsigned base);
    unsigned ReserveSessionView(const char *name, unsigned bytes);
    bool IsSharedVirtual(unsigned address);
    unsigned ReserveSharedVirtual(unsigned page, unsigned pages, unsigned flags);
    unsigned CommitSharedVirtual(unsigned page, unsigned pages, unsigned flags);
    unsigned DecommitSharedVirtual(unsigned page, unsigned pages);
    unsigned ReleaseSharedVirtual(unsigned base);
    unsigned ProtectSharedVirtual(unsigned address, unsigned bytes, unsigned flags);
    unsigned char *SessionBytes(unsigned address, unsigned bytes);
    static unsigned GuestProtection(unsigned);
    // What Unicorn gets for a range the guest sees with GuestProtection: the
    // execute right only where code lives or has lived. Marks the range
    // executable when the caller asked for it.
    unsigned HostProtection(unsigned base, unsigned size, unsigned permissions);
    // Off, every readable page gets the execute right at once, the way this
    // port worked until 21 September 2026. The host runner offers the switch so
    // one binary can measure both ways (tools/probe_application.cpp,
    // --execute-everything); nothing inside the guest can reach it.
    static bool execute_on_demand;
    // Puts the execute right back on the pages of the range that already had
    // it, after a map or a protect handed Unicorn the reduced value.
    void KeepExecutable(unsigned base, unsigned size, unsigned permissions);
    // The repair of a fetch from a page the guest may read: gives that page
    // the execute right and answers whether the run may go on.
    bool GrantExecute();
    // The tokens of the native window procedures (0x71000001..0x71000dff; the
    // handlers and thread entries of the port lie above). A fetch from one is
    // a direct call of a system class procedure, which Win98 allows:
    // turns it into CallWindowProcA and answers whether the run may go on.
    static constexpr unsigned NativeProcedureBase = 0x71000000, NativeProcedureEnd = 0x71000e00;
    bool EnterNativeProcedure();
    bool EnterExceptionGuard();
    static constexpr unsigned ThreadStartHandler = 0x71000e30;
    void InstallThreadStartHandler(unsigned teb, unsigned frame);
    // A range that holds code from the start, inside a range already mapped.
    void MarkGuestCode(unsigned base, unsigned size);
    ExecutePages execute_pages;
    bool Map(unsigned, unsigned, unsigned) override;
    bool Store(unsigned, const void *, Pe32::Size) override;
    bool Protect(unsigned, unsigned, unsigned) override;
    bool ReserveImage(unsigned, unsigned) override;
    unsigned ImageDataPageSize() const override;
    bool RelaxImageData(unsigned, unsigned) override;
    // Guest memory for the handlers. Inside one page the engine lets the
    // host touch in place (Cpu::page_read, Cpu::page_write) the bytes are
    // read and written there. Relaxed read-only PE pages use Store's original
    // rights check for native API outputs. Everything else takes ReadChecked
    // or Store as well.
    bool Read(unsigned a, void *p, unsigned n)
    {
        if (const unsigned char *readable = cpu->page_read)
            if (n && n <= 4096 - (a & 4095) && readable[a >> 12])
            {
                __builtin_memcpy(p, cpu->memory + a, n);
                return true;
            }
        return ReadChecked(a, p, n);
    }
    bool ReadChecked(unsigned, void *, unsigned);
    bool Fetch(unsigned a, void *p, unsigned n) override { return Read(a, p, n); }
    unsigned Read32(unsigned a)
    {
        unsigned v = 0;
        Read(a, &v, 4);
        return v;
    }
    void Write32(unsigned a, unsigned v)
    {
        if (const unsigned char *writable = cpu->page_write)
            if ((a & 4095) <= 4092 && writable[a >> 12] && !write_watch_count && !cpu->ImageDataReadOnly(a))
            {
                __builtin_memcpy(cpu->memory + a, &v, 4);
                return;
            }
        Store(a, &v, 4);
    }
    // A register by its number in X86::Reg (src/x86_cpu.h). The general
    // registers and EIP are read in place where the engine hands them out.
    unsigned Register(int r)
    {
        if (cpu && cpu->gpr && (unsigned)r < 8)
            return (unsigned)cpu->gpr[r];
        if (r == Reg::Eip && cpu && cpu->eip)
            return (unsigned)*cpu->eip;
        return EngineRegister(r);
    }
    void Register(int r, unsigned v)
    {
        if (cpu && cpu->gpr && (unsigned)r < 8)
            cpu->gpr[r] = v;
        else
            EngineRegister(r, v);
    }
    unsigned EngineRegister(int);
    void EngineRegister(int, unsigned);
    unsigned Allocate(unsigned);
    unsigned AllocationSize(unsigned, bool local = false);
    bool Free(unsigned);
    unsigned String(const char *);
    const char *ReadString(unsigned);
    unsigned PutString(unsigned, const char *, unsigned capacity = 0xffffffff);
    // The result of an engine call: zero is success, anything else ends the
    // process with the engine's text and the place of the fault.
    bool Check(unsigned code) { return code ? CheckFailed(code) : true; }
    bool CheckFailed(unsigned);
    void Fail(const char *);
    // The guest memory fault the engine reported last, so that a failure names
    // the address it stumbled over and not only the error class. Filled by
    // MemoryFaultHook, read by Check.
    struct MemoryFault
    {
        unsigned address = 0, size = 0, type = 0;
        bool valid = false;
    } memory_fault;
    static bool MemoryFaultHook(Cpu &, unsigned kind, unsigned address, unsigned size, void *context);
    // A page fault, a breakpoint or a single step of the guest goes down its
    // FS:[0] chain first; only an exception nobody handles ends the run.
    // src/x86_fault_exceptions.inc.
    bool GuestFault(unsigned code);
    bool Arm64GuestFault(unsigned code);
    bool DispatchGuestException(unsigned code, unsigned trap, unsigned parameters, unsigned first,
                                unsigned second, const char *described);
    struct FaultRepeat
    {
        unsigned address = 0, fault = 0, count = 0;
    } fault_repeats;
    // A software interrupt is noticed inside the emulator and delivered after
    // it has stopped, because delivery runs guest code.
    struct PendingException
    {
        unsigned code = 0;
        bool valid = false;
    } pending_exception;
    // Writes " at <address>..." with the native page state behind it.
    unsigned DescribeMemoryFault(char *, unsigned);
    static bool Resolve(void *, const char *, const char *, unsigned, unsigned *);
    bool NativeProcedure(const char *, const char *, unsigned, unsigned *, bool allow_missing);
    bool NativeModule(const char *) const;
    // Win98 module search over the mounted tree; src/x86_modules.cpp.
    bool KnownModule(const char *leaf);
    unsigned SearchModuleFile(const char *name, char (&path)[260], char *searched, unsigned searched_size);
    struct Module
    {
        unsigned kind = 0, handle = 0, references = 0, order = 0;
        ModuleSet dependencies;
        bool bound = false, initializing = false, attached = false, unloading = false, no_thread_calls = false;
        unsigned win16_alias = 0; // Stable native Win16 alias, retired with this module.
        bool gdi_cares = false; // KERNEL32 ordinal 23, GDIReallyCares
        char path[260] = {};
        Pe32::Image *pe = nullptr;
        unsigned char *file = nullptr;
        // Bytes of the guest PE header page at handle, zero when there is none. Only a native provider carries one; a real image is its own.
        unsigned header_bytes = 0;
    } modules[ModuleCapacity];
    // Slots at and above this mark are free. Every scan of the table stops
    // here, so a table with room for 128 costs what its occupancy costs.
    unsigned module_watermark = 1;
    // Static TLS (PE data directory 9). One record per module whose directory
    // the loader honoured; LoadLibrary never adds one.
    struct StaticTlsModule
    {
        unsigned module = 0, index = 0, raw = 0, raw_size = 0, zero_fill = 0, index_address = 0;
    } static_tls[32];
    unsigned static_tls_count = 0;
    bool RegisterStaticTls(unsigned module);
    bool InitializeStaticTls();
    bool StartThreadStaticTls(unsigned thread);
    void EndThreadStaticTls(unsigned thread);
    unsigned AllocateTlsSlot();
    unsigned ProcessHeapBlock(unsigned bytes, unsigned flags = 0x40);
    void FreeProcessHeapBlock(unsigned address);
    unsigned binding_module = 0, module_order = 0, loader_owner = 0, loader_depth = 0;
    unsigned next_module_alias = 0x8007;
    bool collecting_modules = false, modules_terminated = false;
    struct LibraryExEnvironment;
    unsigned ModuleApi(ApiName, const unsigned *);
    unsigned NullModuleIndex();
    unsigned ResourceApi(ApiName, const unsigned *);
    bool ResourceName(unsigned, Pe32::Image::ResourceKey &, unsigned short (&)[260]);
    bool ResourceNameW(unsigned value, Pe32::Image::ResourceKey &key, unsigned short *wide);
    unsigned AnsiWide(unsigned);
    unsigned AnsiNarrow(unsigned);
    struct ResourceHandle
    {
        unsigned id = 0, module = 0, entry = 0, address = 0, size = 0;
        bool loaded = false, accelerator = false;
    } resource_handles[1024];
    unsigned resource_serial = 1;
    // LOAD_LIBRARY_AS_DATAFILE views, KERNEL32. The file bytes
    // are mapped read-only, nothing is relocated, bound or attached, and the
    // handle carries bit 0. src/x86_datafile.cpp.
    static constexpr unsigned DatafileCapacity = 16;
    struct DatafileModule
    {
        unsigned handle = 0, base = 0, size = 0;
        Pe32::Image *pe = nullptr;
        unsigned char *file = nullptr;
        char path[260] = {};
        // A view the program mapped itself and named with bit 0 set: the
        // view stays the program's (src/x86_datafile.cpp).
        bool borrowed = false;
    } datafiles[DatafileCapacity];
    DatafileModule *AdoptViewAsDatafile(unsigned handle);
    void ForgetBorrowedDatafile(unsigned base);
    unsigned MappedViewBytes(unsigned address);
    // True when the range lies in a mapped module image. Win98 cannot change
    // the page attributes of a view.
    bool ImageRightsFixed(unsigned address, unsigned size) const;
    bool RepairResourceWrite(unsigned address);
    unsigned LoadDatafileModule(const char *name);
    bool FreeDatafileModule(unsigned handle);
    DatafileModule *DatafileAt(unsigned handle);
    // One resource module: the parsed image plus the guest address of a
    // resource. A loaded image answers base + RVA, a datafile view answers
    // view + file offset.
    struct ResourceModule
    {
        const Pe32::Image *pe = nullptr;
        unsigned view = 0, key = 0;
        bool datafile = false;
        unsigned Address(unsigned rva) const;
    };
    bool ResolveResourceModule(unsigned handle, ResourceModule &);
    unsigned FindModule(const char *);
    unsigned ModuleIndex(unsigned, bool allow_unloading = false);
    // A dynamic load has no importing module, including after slot 32 fills.
    unsigned LoadModule(const char *, unsigned parent = ModuleCapacity);
    unsigned ModuleProcedure(unsigned, const char *, unsigned, unsigned recursion = 0, bool imports = false);
    bool InitializeModule(unsigned);
    bool LockLoader();
    void UnlockLoader();
    void CollectModules();
    void DeleteModule(unsigned);
    // src/x86_module_image.cpp: the readable PE header page of a native
    // service module, with an export directory over the existing gates.
    bool CreateModuleHeader(unsigned);
    void ReleaseModuleHeader(unsigned);
    unsigned ModuleOrder(unsigned (&indices)[ModuleCapacity], unsigned (&orders)[ModuleCapacity], bool threads, bool reverse);
    void NotifyModules(unsigned reason);
    void TerminateModules();
    bool AvailableImageAddress(unsigned base, unsigned size);
    static void Gate(Cpu &, unsigned address, void *context);
    // One Step runs at most this many translated blocks.
    static constexpr unsigned SliceBlocks = 50000;
    unsigned slice_blocks = 0;
    // With the budget of the host engine, Slice runs only when the budget is
    // used up, and slice_armed is the number of blocks that budget covered.
    // Without it (another engine build) Slice is a block hook as before.
    bool budget_slices = false;
    unsigned slice_armed = 1;
    bool AddSliceHook();
    void ArmSlice();
    static void Slice(Cpu &, unsigned address, void *context);
    bool Dispatch();
    // The gate of an import on the positive list carries its own return
    // instruction, and this serves the call without leaving uc_emu_start. It
    // answers false for every call it does not take, and then the gate hook
    // stops the run the way it always did.
    bool FastGate(unsigned gate);
    // Off, every gate keeps the ordinary stop. The host runner offers the
    // switch so one binary can measure both ways
    // (tools/probe_application.cpp, --no-fast-gates).
    static bool fast_gates;
    // Every other gate under an engine that can run host code inside its
    // translated code (FEXCore): the hook serves the call right there, the
    // way the Run loop would after a stop, and stops the engine only where
    // the Run loop has to act (thread end, a used-up turn, a failure). A
    // handler may still wait, switch threads or call back into the guest;
    // the engine keeps its frame across the yield (Cpu::DetachFrame).
    // NEO98_DIRECT_GATES=0 goes back to the stop for every call.
    static bool direct_gates;
    void DirectGate(unsigned address);
    // DirectGate's Service failed: the Run loop ends as after its own.
    bool direct_break = false;
    // Win98 gives a running thread a quantum (Step); see there.
    static constexpr unsigned long long TurnMicroseconds = 4000;
    // The measuring mode of the import census (tools/probe_application.cpp
    // --census). A call of an export the port does not
    // implement then writes a line and returns the answer of an unimplemented
    // Win98 export instead of stopping the run, so one start of a program shows
    // every such call and not only the first. Never on in an ordinary run.
    static bool export_census;
    // Set by the interactive host only (tools/neo98_host.cpp): a call of an export
    // without a body is logged once and answered like an unimplemented Win98
    // export instead of ending the process. Tests and the matrix keep the stop.
    static bool tolerate_missing_exports;
    // Module plus offset of a guest address, the text of the crash report.
    void DescribeCodeAddress(unsigned value, char *out, unsigned capacity);
    // The address of a data export of a native provider, or zero. The block
    // lies in the module's header page.
    unsigned ContractDataAddress(const char *dll, const char *name, unsigned ordinal);
    // Raised while CreateModuleHeader mints the addresses of an export
    // directory. Every import record made under it belongs to the table and
    // not to the program.
    unsigned building_module_header = 0;
    // Enter/LeaveCriticalSection on a section this call neither has to wait
    // for nor has to wake anybody from. False sends the call the ordinary way.
    bool CriticalUncontended(ApiName name, unsigned handle);
    // The gate of EnterCriticalSection and LeaveCriticalSection jumps into
    // guest code that takes the uncontended case itself (src/x86_critical.cpp). Its second half is the ordinary entry of the
    // same import, which that code takes for every other case.
    unsigned GateEntry(unsigned address);
    bool MapCriticalChunk(unsigned address);
    unsigned critical_chunks_mapped[CriticalRecordBytes / CriticalChunkBytes / 32] = {};
    bool critical_code_mapped = false;
    bool trivial_code_mapped = false;
    unsigned long long fast_gate_calls = 0, slow_gate_calls = 0, fast_gate_withdrawn = 0, guest_gate_calls = 0;
    // Set while a handler runs inside the gate hook. A fiber switch there
    // would leave the engine in the middle of a translated block, so Suspend
    // stops the run with a diagnosis instead.
    bool in_fast_gate = false;
    void Run();
    void RunEnd();
    // The call a thread out of a saved state stood in, taken up again.
    bool StateResume();
    // A thread out of a saved state that stood inside callbacks: every
    // frame of the host between the levels of the guest is entered again
    // (src/x86_state_resume.cpp).
    bool ResumeThread();
    bool ResumeGuest(unsigned level);
    unsigned ResumeCall(unsigned level);
    unsigned ResumeInner();
    bool FinishGate(unsigned gate, unsigned value);
    static bool TailGate(const char *dll, const char *name, unsigned id, bool win16);
    void Win16GateReturn(unsigned sp, unsigned cs, unsigned ip, unsigned words, unsigned result);
    // The tail position: a gate or a recorded frame says that the next
    // callback is the last thing it does (GiveTail), every frame on the way
    // takes the word and hands it on only where the same holds for itself,
    // and Call keeps it in its mark.
    enum Tail : unsigned
    {
        TailNone,
        TailFresh,   // a gate whose handler may say so
        TailResult,  // the gate answers what the callback answers
        TailZero,    // the gate answers zero
        TailAgain,   // the gate is made again (a wait that delivered a sent message)
        TailRecord,  // a recorded frame goes on by itself
    };
    unsigned TakeTail()
    {
        auto &t = threads[current_thread];
        const unsigned tail = t.tail;
        t.tail = 0;
        return tail >= TailResult ? tail : 0;
    }
    // Whether the handler that asks is the one the gate called, with
    // nothing of the guest run in between.
    bool TakeFresh()
    {
        auto &t = threads[current_thread];
        const unsigned tail = t.tail;
        t.tail = 0;
        return tail == TailFresh;
    }
    void GiveTail(unsigned tail) { threads[current_thread].tail = tail; }
    // What a handler of a gate says before its last call.
    void TailOfGate(unsigned tail)
    {
        auto &t = threads[current_thread];
        t.tail = t.tail == TailFresh ? tail : 0;
    }
    // A saved state is wanted (MachineState::WantSavePoint): every thread
    // stops where it comes back to the loop of its routine, between two
    // calls of the system and with no callback open, which is a place a
    // thread can be saved at. Without it a program that spends its time
    // inside window procedures is rarely found at such a place.
    static bool state_point_wanted;
    bool Step();
    bool Service();
    void Suspend(State);
    unsigned Api(ApiName, const unsigned *);
    bool OuterCallMark(unsigned esp) const;
    unsigned Printf(unsigned output, unsigned format, unsigned arguments);
    unsigned FormatMessage(const unsigned *);
    // The RT_MESSAGETABLE text of one mapped module, for
    // FORMAT_MESSAGE_FROM_HMODULE; false when the module has no such message.
    bool ModuleMessage(unsigned module, unsigned identifier, unsigned language, char *out,
                       unsigned capacity);
    unsigned process_environment = 0;
    struct ProcessEnvironment;
    bool InitializeProcessEnvironment();
    void FreeProcessEnvironment();
    unsigned EnvironmentApi(ApiName, const unsigned *);
    unsigned ExpandProcessEnvironment(const char *source, char *output, unsigned capacity);
    unsigned FindProcessEnvironment(const char *name);
    unsigned char *common_dialog_bytes = nullptr;
    unsigned common_dialog_size = 0;
    Pe32::Image *common_dialog_image = nullptr;
    bool common_dialog_read = false, common_dialog_valid = false;
    const unsigned char *CommonResource(unsigned type, unsigned id, unsigned &size);
    unsigned common_dialog_tls = 0; // Private TLS slot encoded as index+1.
    bool InitializeCommonDialog();
    void FreeCommonDialog();
    unsigned CommonDialogError();
    void CommonDialogError(unsigned);
    struct PrintEnvironment;
    unsigned PrintDialogApi(ApiName,const unsigned*);
    struct SpecialFolderState;
    struct SpecialFolderEnvironment;
    unsigned SpecialFolderApi(ApiName,const unsigned*);
    void FreeSpecialFolders();
    struct CabinetState;
    struct CabinetEnvironment;
    unsigned CabinetApi(unsigned,const unsigned*);
    void FreeCabinet();
    struct ShellControlState;
    struct ShellControlEnvironment;
    unsigned ShellControlApi(ApiName,const unsigned*);
    void FreeShellControl();
    struct DragDropEnvironment;
    unsigned ShellDragDropApi(unsigned, const unsigned *);
    Win98ShellDragDrop::State drag_drop;
    struct ShellEnvironment;
    struct ShellIconEnvironment;
    unsigned ShellIconApi(ApiName, const unsigned *);
    Win98Shell::State shell_state;
    unsigned shell_subscription = 0, shell_serial = 0;
    unsigned RegisterMessage(const char *);
    unsigned ShellWindow();
    unsigned SetShellWindow(unsigned);
    unsigned RegisterShell(unsigned hwnd, unsigned flags);
    void ShellEvent(unsigned code, unsigned hwnd, unsigned parameter);
    struct AtomTable;
    AtomTable *atoms = nullptr;
    unsigned AtomApi(ApiName, const unsigned *);
    AtomTable &SystemAtoms(bool registration);
    unsigned GlobalAtomSearch(unsigned name, bool add);
    unsigned GlobalAtomDelete(unsigned atom);
    unsigned GlobalAtomText(const char *);
    unsigned GlobalAtomName(unsigned atom, char *, unsigned capacity);
    unsigned GlobalAtomApi(ApiName, const unsigned *);
    unsigned RegisterAtom(unsigned name);
    void FreeAtoms();
    struct DdeState;
    DdeState *dde = nullptr;
    DdeState &Dde();
    void FreeDde();
    unsigned DdeApi(ApiName, const unsigned *);
    void *DdeInstance(unsigned id); // DdeState::Instance*, the type is private to x86_dde.cpp
    unsigned DdeCallback(unsigned instance, unsigned type, unsigned format, unsigned conv,
                         unsigned hsz1, unsigned hsz2, unsigned data, unsigned d1, unsigned d2);
    unsigned DdeBroadcastInitiate(unsigned client_window, unsigned service, unsigned topic);
    unsigned DdeMatchServer(unsigned client_index, unsigned service, unsigned from);
    unsigned DdeOpen(unsigned client_index, unsigned server_index, unsigned service, unsigned topic);
    void *DdeConversation(unsigned handle); // DdeState::Conversation*, private to x86_dde.cpp
    unsigned DdeMakeData(unsigned owner, unsigned source, unsigned size, unsigned offset,
                         unsigned item, unsigned format, unsigned flags);
    struct PropertyEnvironment;
    PropertyEnvironment *properties = nullptr;
    unsigned PropertyApi(ApiName, const unsigned *);
    Application *PropertyOwner(unsigned hwnd);
    void DestroyProperties(unsigned hwnd);
    void FreeProperties();
    unsigned ShellCopyApi(ApiName, const unsigned *);
    unsigned ShellNumberApi(ApiName, const unsigned *);
    unsigned ShellCompareApi(ApiName, const unsigned *);
    unsigned PathApi(ApiName, const unsigned *);
    unsigned ShellRegistryOrdinalApi(unsigned, const unsigned *);
    struct ShellRegistryOrdinalEnvironment;
    unsigned ShellPolicyApi(unsigned, const unsigned *);
    struct ShellPolicyEnvironment;
    struct ShellRegistryEnvironment;
    unsigned ShellRegistryApi(ApiName, const unsigned *);
    struct KernelPathEnvironment;
    KernelPathEnvironment *kernel_paths = nullptr;
    unsigned KernelPathApi(ApiName, const unsigned *);
    void FreeKernelPaths();
    void KernelPathLock(bool acquire, unsigned id);
    struct Toolbar { unsigned hwnd=0,object=0,active=0,strip_images=0; bool destroy_pending=false,layout_pending=false,accessibility_pending=false; } toolbars[64];
    // One shared measurement DC for every toolbar. A per-toolbar compatible DC
    // costs five GDI handles out of the 255 the eight-bit handle index allows,
    // so it ran the object table dry long before this pool. The DC selects a
    // caller font only while measuring and drops it again afterwards.
    unsigned toolbar_font_dc=0;
    unsigned ToolbarFontDC(unsigned font);
    void ToolbarFontRelease();
    struct ToolbarMemory;
    struct TabEnvironment;
    struct TooltipEnvironment;
    unsigned TooltipMessage(unsigned,unsigned,unsigned,unsigned);
    void RelayTooltipInput(unsigned,unsigned,unsigned,unsigned);
    void DrawTooltip(unsigned,unsigned,const Gdi::RECT&);
    struct UpDownEnvironment;
    unsigned UpDownMessage(unsigned,unsigned,unsigned,unsigned);
    unsigned UpDownBuddyMessage(unsigned,unsigned,unsigned,unsigned);
    void DrawUpDown(unsigned,unsigned,const Gdi::RECT&);
    unsigned TrackbarMessage(unsigned,unsigned,unsigned,unsigned);
    void DrawTrackbar(unsigned,unsigned,const Gdi::RECT&);
    unsigned SerialUiAdvanced(unsigned,unsigned,unsigned);
    unsigned SerialUiAdvancedMessage(unsigned,unsigned,unsigned,unsigned);
    unsigned TabMessage(unsigned,unsigned,unsigned,unsigned);
    void LayoutTab(unsigned);
    void DrawTab(unsigned,unsigned,const Gdi::RECT&);
    unsigned ToolbarMessage(unsigned,unsigned,unsigned,unsigned);
    void LayoutToolbar(Toolbar&);
    void AutosizeToolbar(Toolbar&);
    void DrawToolbar(unsigned,Gdi::HDC,const Gdi::RECT&);
    void ReleaseToolbar(Toolbar&);
    void FreeToolbars();
    struct CommonMemory;
    unsigned CommonControlApi(unsigned,const unsigned *);
    unsigned PropertyPageApi(ApiName, const unsigned *);
    struct PropertySheetEnvironment;
    unsigned PropertySheetApi(ApiName, const unsigned *);
    unsigned ShowPropertySheet(unsigned);
    bool PropertySheetMessage(unsigned,unsigned,unsigned,unsigned,unsigned&);
    bool PropertySheetWindow(unsigned);
    bool ModalMessage(User::Message &message, bool remove = true);
    unsigned PropertySheetProcedure(unsigned,unsigned,unsigned,unsigned);
    unsigned CommonInitApi(ApiName name,const unsigned *args);
    bool UnsupportedCommonProcedure(unsigned procedure);
    unsigned CommonMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp,unsigned procedure=0);
    unsigned CommonControlKind(unsigned hwnd,unsigned procedure=0);
    void DrawCommon(unsigned hwnd,Gdi::HDC,const Gdi::RECT &,unsigned kind=0);
    struct Tree;
    Tree *trees[8]={};
    Tree *FindTree(unsigned,bool create=false);
    unsigned TreeNotify(Tree &,unsigned code,unsigned action,unsigned old_item,unsigned new_item);
    unsigned SelectTree(Tree &,unsigned item,unsigned action=0);
    unsigned ExpandTree(Tree &,unsigned item,unsigned action);
    void TreeScroll(Tree &,unsigned show=0);
    void FreeCommon();
    struct List;
    List *lists[8] = {};
    List *FindList(unsigned, bool create = false);
    unsigned ListMessage(unsigned, unsigned, unsigned, unsigned);
    void DrawList(unsigned, Gdi::HDC, const Gdi::RECT &);
    void FreeLists();
    struct ComboBox;
    // Win98 has no count here; property sheets with many pages (a DOS
    // program has nine) need more than a dialog of their own.
    ComboBox *combo_boxes[128]={};
    ComboBox *FindComboBox(unsigned,bool=false);
    unsigned ComboBoxMessage(unsigned,unsigned,unsigned,unsigned);
    bool ComboListMouse(unsigned,unsigned,unsigned,unsigned);
    bool ComboEditKey(unsigned,unsigned,unsigned,unsigned);
    bool ComboListNotify(unsigned,unsigned);
    unsigned ComboOfList(unsigned list);
    void DrawComboBox(unsigned,Gdi::HDC,const Gdi::RECT&);
    void FreeComboBoxes();
    struct ListBox;
    ListBox *list_boxes[64] = {};
    ListBox *FindListBox(unsigned, bool create = false);
    unsigned ListBoxMessage(unsigned, unsigned, unsigned, unsigned);
    void DrawListBox(unsigned, Gdi::HDC, const Gdi::RECT &, unsigned item = ~0u, unsigned action = 1, bool focus = true);
    void FreeListBoxes();
    unsigned ListNotify(List &, unsigned code, unsigned index, unsigned old_state, unsigned new_state);
    void ListText(List &, unsigned index, unsigned column, char *text, unsigned capacity);
    struct StatusBar
    {
        unsigned hwnd = 0, count = 1, height = 20, font = 0;
        int parts[8] = {-1};
        unsigned flags[9] = {};
        char text[9][256] = {};
        bool simple = false;
    } status_bars[8];
    unsigned StatusMessage(unsigned, unsigned, unsigned, unsigned);
    void DrawStatus(unsigned, Gdi::HDC, const Gdi::RECT &);
    unsigned ImageApi(ApiName,const unsigned *);
    unsigned CreateResourceBitmap(const unsigned char *,unsigned);
    unsigned LookupResourceIcon(const unsigned char *,unsigned,bool,int,int,unsigned);
    unsigned CreateResourceIcon(const unsigned char *,unsigned,bool,unsigned,int,int,unsigned);
    struct ImageResources;
    ImageResources *image_resources = nullptr;
    bool InitializeImageResources();
    void FreeImageResources();
    bool AllocateDibLease(unsigned, Gdi::DibLease &);
    // The bits of GDI's DIB sections belong to the session (src/x86_gdi.cpp).
    struct DibBackend;
    bool AllocateSessionDib(unsigned, Gdi::DibLease &);
    bool HoldSessionDib(unsigned index);
    bool QuerySessionDib(unsigned, VirtualMemory::Info &);
    bool MapSessionDib(unsigned address);
    void DropSessionDibs(bool all, bool unmap);
    static void ReleaseDibLease(void *,const Gdi::DibLease &);
    static void ReleaseDibView(void *,const Gdi::DibLease &);
    unsigned PublicGdiObject(unsigned) const;
    unsigned GdiApi(ApiName, const unsigned *);
    unsigned SelectMappedTextFont(unsigned dc, unsigned &temporary);
    unsigned CoordinatesApi(ApiName,const unsigned*);
    unsigned MonitorsApi(ApiName,const unsigned*);
    unsigned WindowTextApi(ApiName,const unsigned*);
    unsigned WindowTextDefault(unsigned,unsigned,unsigned,unsigned);
    struct WindowTextMemory;
    unsigned FontCreateApi(ApiName, const unsigned *);
    // src/x86_font_catalog.cpp: the fonts of the installation and the stock fonts.
    void LoadInstalledFonts();
    bool InstallStockFonts();
    unsigned FontCharsetApi(ApiName, const unsigned *);
    unsigned WideTextApi(ApiName,const unsigned*);
    unsigned FontWidthApi(ApiName, const unsigned *);
    unsigned FontQueryApi(ApiName, const unsigned *);
    unsigned ScrollDcApi(ApiName, const unsigned *);
    unsigned CensusImportApi(ApiName, const unsigned *);
    bool ScrollRegionCopy(unsigned dc, const Gdi::RECT &destination, unsigned region,
                          int logical_dx, int logical_dy);
    unsigned SystemParameters(const unsigned *);
    unsigned FontSmoothingParameters(const unsigned *);
    void LoadUserStartSettings();
    unsigned MouseButtonSwapParameter(const unsigned *);
    unsigned ScreenSaveParameters(const unsigned *);
    void NoteHardwareInput();
    void ScreenSaverTick();
    unsigned StartScreenSaver();
    // WinHelpA starts WINHLP32 with the command on its line, src/x86_winhelp.cpp.
    unsigned WinHelpApi(const unsigned *);
    // A file opened from outside the machine, src/x86_host_open.cpp.
    static void RequestOpen(const char *guest_path);
    static unsigned OpenRequests();
    void RequestDosSettings(const char *guest_path, unsigned owner = 0);
    static const char *DosSettingsPath(unsigned window);
    using DosFailureHook = void (*)(void *, const char *, const char *);
    static void SetDosFailureHook(void *context, DosFailureHook hook);
    static void ReportDosFailure(const char *path, const char *error);
    void ServeOpenRequests(bool recorded = false);
    bool HasDosSettingsRequests() const;
    static bool DosSettingsStateOpen();
    bool DosUiStateReady(char *why, size_t bytes, bool require_session = true) const;
    bool ExportDosLaunch(DosVm::Launch &launch) const;
    void DrainDosUiGraphics();
    bool BindRestoredDosVm(DosVm::Session *session, const DosVm::Launch &launch);
    unsigned SystemParametersWrite(const unsigned *);
    bool WriteSystemProfile(const char *section, const char *key, const char *value);
    // USER HWND_BROADCAST. Mode is the original delivery selector.
    unsigned BroadcastMessage(unsigned mode, unsigned message, unsigned wp, unsigned lp);
    unsigned WindowApi(ApiName, const unsigned *);
    struct ParentEnvironment;
    unsigned ParentApi(ApiName,const unsigned*);
    unsigned ParentPosition(unsigned,unsigned,int,int,int,int,unsigned);
    unsigned PaintApi(ApiName, const unsigned *);
    void RedrawImmediate(unsigned hwnd, unsigned kind, unsigned parameter, bool descendant = false);
    // src/x86_user_regions.cpp: the window DC and the update region exports.
    unsigned WindowDeviceContext(unsigned hwnd, unsigned region, unsigned flags);
    Gdi::POINT WindowClientOrigin(unsigned hwnd, bool frame);
    unsigned UpdateRegionApi(ApiName, const unsigned *);
    // src/x86_user32_imports.cpp: the remaining imported USER32 exports.
    unsigned UserOemStringApi(ApiName, const unsigned *);
    unsigned User32ImportApi(ApiName, const unsigned *);
    // src/x86_user_arrange.cpp: the icon grid, tiling, cascading and the rest.
    static constexpr unsigned HiddenPopupCapacity = 64;
    unsigned hidden_popups[HiddenPopupCapacity] = {};
    // The fields of the "SysFWStat" property USER keeps per window:
    // active is bit 8000h, origin bit 1000h, running bit 4000h, and flags the
    // stored low bits 000Bh. count is the high word.
    struct FlashState { unsigned hwnd = 0; bool active = false, origin = false, running = false;
                        unsigned flags = 0, count = 0; };
    // FLASHW_CAPTION, FLASHW_TRAY, FLASHW_TIMER and FLASHW_TIMERNOFG, the bits
    // Windows 98 keeps, and the ones Windows 98 writes back into the property.
    static constexpr unsigned FlashCaption = 1, FlashTray = 2, FlashTimer = 4;
    static constexpr unsigned FlashTimerNoForeground = 8;
    static constexpr unsigned FlashPublicBits = 0x0f, FlashStoredBits = 0x0b;
    // The timer id Windows 98 passes and the native stand-in for the USER-owned
    // timer procedure, which no guest address may collide with.
    static constexpr unsigned FlashTimerId = 0xfff5, FlashTimerProcedure = 0x71000e40;
    FlashState flash_states[HiddenPopupCapacity] = {};
    unsigned system_thread_flags = 0, network_capabilities = 0;
    unsigned ArrangeIcons(unsigned parent);
    bool ArrangeCandidate(unsigned hwnd, unsigned how, unsigned &flags);
    unsigned ArrangeWindows(bool cascade, const unsigned *);
    bool FlashWindowState(unsigned hwnd, bool write, bool value);
    FlashState *FlashRecord(unsigned hwnd, bool create);
    void FlashStop(FlashState &);
    unsigned FlashWindowEx(unsigned info);
    void FlashTick(unsigned hwnd);
    unsigned ArrangeApi(ApiName, const unsigned *);
    // src/x86_window_exports.cpp: the USER32 window exports the import rounds
    // left out, from AnimateWindow to the MDI, dialog and property thunks.
    unsigned AnimateWindow(unsigned hwnd, unsigned time, unsigned flags);
    // src/x86_dlgdir.cpp: USER behind the two DlgDirSelectEx entries.
    bool DialogDirectorySelection(unsigned list, char *out, unsigned capacity);
    // src/x86_properties.cpp: USER behind EnumPropsA and EnumPropsExA.
    unsigned EnumProperties(unsigned hwnd, unsigned callback, unsigned parameter, bool extended);
    void SetSystemColour(unsigned index, unsigned colour);
    // USER keeps the saved pair in its local heap and hands back a
    // near pointer; NEO98 keeps them here and hands back a marked token.
    static constexpr unsigned SystemColourCookie = 0x53434f00;  // 'SCO'
    struct SystemColourSave { bool used = false; unsigned cookie = 0, count = 0,
                              colours[32] = {}, brushes[32] = {}; };
    SystemColourSave system_colour_saves[4] = {};
    unsigned WindowExportApi(ApiName, const unsigned *);
    // src/x86_window_icon.cpp: the WM_QUERYDRAGICON step of USER.
    unsigned QueryDragIcon(unsigned hwnd);
    // src/x86_window_info.cpp: the nine USER32 information queries of USER
    // segment 28, with two accessors in the control families they ask.
    unsigned FrameBorderUnits(unsigned style, unsigned extended);
    unsigned WindowInfoApi(ApiName, const unsigned *);
    bool ComboBoxView(unsigned hwnd, unsigned *out);
    unsigned ListBoxItemsPerColumn(unsigned hwnd);
    // src/x86_mdi.cpp: the MDICLIENT class, DefFrameProc, DefMDIChildProc and
    // TranslateMDISysAccel, all of USER.EXE segment 17.
    static constexpr unsigned MdiClientProcedure = 0x71000005;
    // The DMGFrame class of DDEML, src/x86_dde.cpp.
    static constexpr unsigned DdeFrameProcedure = 0x71000050;
    unsigned DdeFrameMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    static constexpr unsigned MdiClientCapacity = 8;
    struct MdiClient {
        unsigned hwnd = 0, menu = 0, first_id = 0, active = 0, maximized = 0, listed = 0;
        // The word at 6Ch of USER's client: children created so far.
        unsigned created = 0;
        unsigned decorated_menu = 0, decorated_child = 0, decorated_system = 0;
        unsigned removing_child = 0;
        bool separator = false, composing = false, composing_menu = false;
        char frame_title[128] = {};
    };
    MdiClient mdi_clients[MdiClientCapacity] = {};
    MdiClient *MdiRecord(unsigned hwnd, bool create);
    unsigned MdiChildren(unsigned client, unsigned *out, unsigned capacity);
    void MdiRefreshMenu(MdiClient &);
    void MdiComposeTitle(MdiClient &);
    void MdiComposeMenu(MdiClient &);
    void MdiActivate(MdiClient &, unsigned child);
    void MdiChildGeometry(unsigned hwnd, unsigned *create);
    unsigned MdiClientMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    unsigned DefFrameMessage(unsigned frame, unsigned client, unsigned message, unsigned wp, unsigned lp);
    unsigned DefMdiChildMessage(unsigned child, unsigned message, unsigned wp, unsigned lp);
    unsigned MdiApi(ApiName, const unsigned *);
    // src/x86_dlgdir.cpp: DlgDirListA and the LB_DIR / CB_DIR enumeration.
    unsigned ListBoxDirectory(unsigned hwnd, unsigned attributes, unsigned pattern);
    unsigned DlgDirApi(ApiName, const unsigned *);
    // src/x86_user_draw_state.cpp: DrawState, GrayString and the tabbed text.
    bool DrawStateBlit(unsigned hdc, unsigned brush, unsigned mono, int x, int y, int cx, int cy);
    unsigned HalftoneBrush(unsigned &pattern);
    unsigned DrawStateApi(ApiName, const unsigned *);
    unsigned TabbedTextApi(ApiName, const unsigned *);
    // src/x86_user_session.cpp: ExitWindowsEx and the registration entries.
    unsigned SessionBroadcast(unsigned message, unsigned wp, unsigned lp, bool query);
    // EndTask and the confirmation dialog it loads out of the original USER.EXE.
    static constexpr unsigned EndTaskDialogProcedure = 0x71000015;
    static constexpr unsigned CloseProgramDialogProcedure = 0x71000016;
    static constexpr unsigned CloseProgramThread = 0x71000f40;
    unsigned close_program_thread_id = 0;
    void StartCloseProgramRequest();
    void CloseProgramDialog();
    unsigned CloseProgramDialogMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    bool ConvertDialogTemplate(const unsigned char *source, unsigned size, unsigned char *out,
                               unsigned capacity, unsigned &length);
    unsigned EndTaskDialog(unsigned owner);
    unsigned EndTaskDialogMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    bool TaskAlive(unsigned task);
    bool TaskEnding(unsigned task);
    bool TerminateTask(unsigned task);
    unsigned UserSessionApi(ApiName, const unsigned *);
    // USER: WM_QUERYENDSESSION, WM_ENDSESSION and ExitKernel(code).
    unsigned EndSession(unsigned flags, unsigned code);
    // The display mode: the screen surface of the compositor, the list
    // EnumDisplaySettings walks and the change ChangeDisplaySettings applies.
    //
    void InstallScreenLease();
    bool DisplayModeOf(unsigned devmode, int &width, int &height, int &depth);
    unsigned ApplyDisplayMode(int width, int height, int depth, unsigned flags);
    // USER: the flags CDS_TEST, CDS_UPDATEREGISTRY and CDS_NORESET
    // around the change, and a null DEVMODE for the registry mode.
    unsigned ChangeDisplaySettings(unsigned devmode, unsigned flags);
    // HKCC\Display\Settings, "Resolution" and "BitsPerPixel" (USER
    // writes them, Windows 98 reads them). Without usable values: 800x600x32.
    void RegistryDisplayMode(int &width, int &height, int &depth);
    void StoredDisplayMode(int &width, int &height, int &depth);
    bool WriteRegistryDisplayMode(int width, int height, int depth);
    // The display comes up in the registry mode once per desktop session.
    void LoadDisplayStartMode();
    void ServeHostRequests();
    // The first icon of the program file, for the host when no window of the
    // program carries one (Dock): HostWindowProgram asks, the next
    // ServeHostRequests of the process loads it, as the shell does for the
    // file's icon (SHELL32 ExtractIcon, index 0).
    bool host_icon_wanted = false, host_icon_tried = false;
    unsigned host_program_icon = 0, host_icon_asked = 0;
    void ServeHostDesktopIcons();
    unsigned char host_desktop_pending = 0; // 0 idle, 1 requested, 2 sampling (no nested snapshot)
    unsigned host_desktop_time = 0, host_desktop_bytes = 0;
    int host_desktop_width = 0, host_desktop_height = 0;
    unsigned char *host_desktop_mask = nullptr;
    // USER after a mode change, src/x86_user_session.cpp.
    void RepositionTopLevel(const Win98DisplayReposition::Rect &old_monitor, const Win98DisplayReposition::Rect &old_work,
                            bool full_screen);
    // src/x86_gdi_imports.cpp: the remaining imported GDI32 exports.
    unsigned GdiImportsApi(ApiName, const unsigned *);
    unsigned GdiPathsApi(ApiName, const unsigned *);
    // src/x86_ddraw_driver.cpp: the DirectDraw bottom edge of the display
    // driver, the DCI escape DCICOMMAND and the device \\.\DDRAW.VXD.
    //
    static unsigned DriverEscapeSupport(unsigned escape, bool known);
    unsigned DciEscapeApi(unsigned hdc, unsigned in_bytes, unsigned in, unsigned out_bytes, unsigned out);
    // The DCI primary surface. DIBENG keeps one static DCISURFACEINFO per
    // driver and counts how often it handed it out; this is the same, one
    // guest block per process.
    unsigned dci_surface = 0, dci_surfaces = 0;
    // The selector DCIMAN's copies keep in dwReserved2 for the surface above,
    // and the guest dword DCICreatePrimary32 lets the driver answer into
    // (src/x86_dciman16.cpp).
    unsigned dci_selector = 0, dci_surface_out = 0;
    // The Win16 driver's structure uses a screen selector and far callbacks;
    // keep it separate from DCIMAN32's flat-address description.
    unsigned dci_win16_surface = 0, dci_win16_selector = 0, dci_screen_selector = 0;
    // Word-sized tokens for DCs returned through DDRAW16.
    unsigned ddraw16_word_handles[32] = {};
    // HEL device contexts and pixel staging buffers for DirectDraw surfaces.
    // See src/win98_ddraw16_surface.h.
    struct DDrawSurfaceDc
    {
        unsigned dc = 0, guest = 0, bytes = 0, depth = 0;
        unsigned char *host = nullptr;
        int width = 0, height = 0, stride = 0;
        // shared means host points directly to screen pixels. Guest and host
        // use the same storage, so no copy-in/copy-out is needed.
        bool used = false, cached = false, wide = false, shared = false;
    } ddraw_surface_dcs[8];
    unsigned ddraw_surface_busy = 0;
    unsigned DirectDrawSurfaceDc(unsigned descriptor, unsigned palette, unsigned reference);
    unsigned DirectDrawReleaseSurfaceDc(unsigned dc);
    unsigned DciCreatePrimarySurface(unsigned out);
    unsigned DirectDraw16CreatePrimary(unsigned hdc, unsigned copy);
    unsigned DirectDraw16CopySurfaceInfo(unsigned source, unsigned copy);
    unsigned DirectDraw16SurfaceOriginal(unsigned surface);
    unsigned DirectDraw16SurfaceHasSelector(unsigned hdc);
    unsigned DirectDraw16WordHandle(unsigned handle);
    unsigned DirectDraw16NativeHandle(unsigned token);
    void DirectDraw16ReleaseHandle(unsigned token);
    unsigned DciBeginAccess(unsigned surface, int x, int y, int dx, int dy);
    bool dci_frame_access = false;
    unsigned long long dci_frame_time = 0;   // one clock for the whole display
    void PaceFrame();                        // src/x86_ddraw_driver.cpp
    unsigned DciEndAccess(unsigned surface);
    unsigned DciDestroySurface(unsigned surface);
    unsigned DirectDrawVxdControl(const unsigned *);
    void DriverTrace(const char *what, unsigned value);
    // src/x86_wsock2.cpp: the native WSOCK2.VXD, the socket layer under
    // MSAFD.DLL, on the sockets of the host (src/net_host.h).
    unsigned Wsock2VxdControl(const unsigned *);
    unsigned Wsock2Dispatch(const unsigned *);
    bool Wsock2Available() const;
    void Wsock2CloseDevice();
    // src/x86_dinput_vxd.cpp: the native DINPUT.VXD for the mouse.
    struct DinputVxd
    {
        unsigned sequence = 0, state = 0, keyboard_state = 0;
        unsigned format[20] = {};
        bool formatted = false;
        unsigned acquired = 0, exclusive = 0;
        unsigned keyboard_acquired = 0, keyboard_exclusive = 0;
        unsigned instances[16] = {};
        unsigned count = 0;
    } dinput_vxd;
    unsigned DinputVxdControl(const unsigned *);
    bool DinputInstance(unsigned) const;
    void DinputDropInstance(unsigned);
    void FreeDinputVxd();
    static void DinputCloseEvent(unsigned ring0);
    // src/x86_dsound_vxd.cpp: the native DSOUND.VXD and the DirectSound driver
    // of the mixer behind it.
    unsigned dsound_vxd_opens = 0, dsound_vxd_version = 0;
    bool dsound_vxd_third_mute = false, dsound_vxd_mapped = false;
    // The devnode of the sound device: the DirectSound driver names it in its
    // description and the wave device answers DRV_QUERYDEVNODE with it, which
    // is how DSOUND.DLL finds the wave device of a driver.
    unsigned DsoundDevNode();
    unsigned DsoundVxdVersion();
    bool DsoundVxdOpen();
    unsigned DsoundVxdControl(const unsigned *);
    void DsoundVxdTrace(unsigned code, unsigned in, unsigned bytes, unsigned result);
    bool DsoundVxdFormat(unsigned address, Sound::Format &format);
    void FreeDsoundVxd();
    static bool DsoundVxdClocked();
    void DsoundVxdServe();
    void DsoundVxdNotify(void *source, unsigned position, bool stopped);
    static void DsoundVxdSetEvent(unsigned ring0);
    static bool DsoundVxdMixing();
    void DinputMouse(int dx, int dy, int dz, unsigned buttons);
    void DinputKey(unsigned scan, bool down);
    void DinputEvent(unsigned instance, unsigned offset, unsigned value, unsigned sequence, unsigned time);
    static bool DinputKeyboard(unsigned scan, bool down);
    static bool DinputPointer(int x, int y, unsigned buttons, const Gdi::POINT *motion);
    static bool DinputMouseExclusive();
    static bool DinputWheel(int steps);
    static void DinputWarp(int x, int y);
    static void DinputForget();
    static unsigned DosVmSoundQueued();
    bool dinput_warping = false;
    void FreeWsock2();
    void TickNetwork();
    bool NetworkPending() const;
    bool ConsoleText(unsigned row, char *out, unsigned size) const;
    unsigned Wsock2Control(unsigned block, unsigned size);
    unsigned Wsock2Echo(unsigned in, unsigned in_length, unsigned out, unsigned capacity, unsigned block);
    bool Wsock2ProtocolTemplate(unsigned index, unsigned char *out);
    unsigned VdhcpRequest(unsigned buffer, unsigned size, bool found, unsigned address, unsigned mask, unsigned gateway);
    bool VdhcpAvailable() const;
    unsigned VdhcpVxdControl(const unsigned *);
    bool VipAvailable() const;
    unsigned VipVxdControl(const unsigned *);
    unsigned long long network_tick = 0;
    friend struct Wsock2;
    // src/x86_mixer.cpp: the mixer device of the sound driver. The lines and
    // the controls are in src/win98_mixer.h.
    static constexpr unsigned MixerRecordCapacity = 8;
    unsigned mixer_records[MixerRecordCapacity] = {};
    unsigned mixer_record_count = 0;
    unsigned MixerApi(ApiName, const unsigned *);
    bool MixerObjectDevice(unsigned object, unsigned &device);
    void MixerName(unsigned at, const char *text, unsigned bytes);
    unsigned MixerControlPair(const void *record);
    void MixerSetControlPair(const void *record, unsigned value);
    void MixerControlChanged(unsigned control_id);
    void MixerWriteLine(unsigned at, const void *record);
    void MixerWriteControl(unsigned at, const void *record);
    // src/x86_ddhal.cpp: the DirectDraw HAL and the Direct3D HAL of the display
    // driver on a host rasterizer. Without a backend the driver keeps refusing
    // the DDHAL commands and DirectDraw stays on its emulation path.
    //
    static Gpu::Backend *gpu_backend;
    static void SetGpuBackend(Gpu::Backend *);
    static bool DirectDrawHalAvailable();
    struct DirectDrawHal;
    struct HalVertexShader;   // a declared vertex shader of the DirectX 8 driver model
    struct HalElement;        // one element of a vertex layout
    struct HalDeclaration9;   // a vertex declaration of the DirectX 9 driver model
    struct HalSurface;    // one surface DDRAW named through CreateSurfaceEx
    struct HalContext;    // one Direct3D context
    struct HalPlane;      // a surface as a blit reads or writes it
    DirectDrawHal *ddhal = nullptr;
    // src/x86_opengl.cpp: the installable client driver of the display
    // driver, OpenGL on the same rasterizer.
    struct OpenGLState;
    friend class OpenGLGuest;
    OpenGLState *opengl = nullptr;
    unsigned MappedViewCommit(unsigned address, unsigned bytes);   // src/x86_mapping.cpp
    static const char *OpenGLIcdModule();
    static unsigned OpenGLEntry(const char *name);
    static unsigned OpenGLArgumentCount(const char *name);   // stack words, or ~0u
    static bool OpenGLAvailable();
    static bool OpenGLEscapeSupported(unsigned escape);
    unsigned OpenGLEscape(unsigned in_bytes, unsigned in, unsigned out_bytes, unsigned out);
    unsigned OpenGLApi(unsigned entry, const unsigned *args);
    OpenGL::Context *OpenGLContext(unsigned dhglrc);
    void OpenGLPresent(unsigned hwnd, const OpenGL::Picture &picture);
    void OpenGLRelease();
    void OpenGLStateSync();   // before a saved state: src/x86_opengl.cpp
    // src/x86_glide.cpp: GLIDE2X.DLL, the Glide 2 driver of a 3dfx card, on
    // the same rasterizer.
    struct GlideState;
    friend class GlideHost;
    GlideState *glide = nullptr;
    static const char *GlideModule();
    static unsigned GlideEntry(const char *name);
    static unsigned GlideArgumentCount(const char *name);   // stack words, or ~0u
    static const char *GlideExportName(unsigned ordinal);
    static bool GlideAvailable();
    static bool GlideFastEntry(unsigned ordinal);
    unsigned GlideApi(unsigned ordinal, const unsigned *args);
    void GlideRelease();
    void GlideStateSync();   // before a saved state: src/x86_glide.cpp
    bool GlideRegistryString(const char *value, char *out, unsigned capacity);
    // src/x86_glide_page.inc: GLIDEPAG.DLL, the page "Glide" of the display
    // properties, a shell extension of DESK.CPL. Its entries take the route
    // of the driver with numbers from 0x1000.
    static const char *GlidePageModule();
    static unsigned GlidePageEntry(const char *name);
    static unsigned GlidePageArgumentCount(const char *name);   // stack words, or ~0u
    unsigned GlidePageApi(unsigned entry, const unsigned *args);
    unsigned GlidePageTables();
    bool GlidePageStore(int scale, bool smooth);
    HalSurface *HalSurfaceByLcl(unsigned lcl);
    HalSurface *HalSurfaceByHandle(unsigned handle, bool buffer = false, unsigned local = 0);
    bool HalBufferBytes(unsigned lcl, unsigned &address, unsigned &size);
    unsigned HalMipLevel(unsigned lcl, unsigned level);
    unsigned HalVolumeSlices(unsigned lcl);
    void HalVolumeBlt(unsigned dest_handle, unsigned source_handle, const int *point, const int *box);
    bool HalBlt9(unsigned source, const int *source_rect, unsigned source_level, unsigned dest, const int *dest_rect,
                 unsigned dest_level, bool fill, unsigned color);
    void HalCreatePixelShader(HalContext &, unsigned handle, const unsigned char *code, unsigned bytes);
    void HalDeletePixelShader(HalContext &, unsigned handle);
    void HalSetPixelShader(HalContext &, unsigned handle);
    void HalCreateVertexShader(HalContext &, unsigned handle, const unsigned char *declaration, unsigned declaration_bytes,
                               const unsigned char *code, unsigned code_bytes);
    void HalDeleteVertexShader(HalContext &, unsigned handle);
    void HalSetVertexShader(HalContext &, unsigned handle);
    const HalVertexShader *HalVertexShaderOf(HalContext &);
    const HalVertexShader *HalVertexShaderOf(HalContext &, unsigned handle);
    unsigned HalElementsOf(const HalVertexShader &, HalElement *elements);
    bool HalGatherInputs(HalContext &, const HalElement *elements, unsigned count, unsigned first, unsigned vertices,
                         unsigned vertex_base, unsigned &slots);
    bool HalPackDeclaration(const HalElement *elements, unsigned count, bool pretransformed, unsigned vertices, unsigned slots,
                            unsigned &fvf, unsigned &stride);
    void HalCreateDeclaration9(HalContext &, unsigned handle, const unsigned char *data, unsigned count);
    void HalDeleteDeclaration9(HalContext &, unsigned handle);
    const HalDeclaration9 *HalDeclaration9Of(HalContext &);
    unsigned HalMapFunction(const HalDeclaration9 &, const HalVertexShader &function, HalElement *layout);
    unsigned HalMapFixedFunction(const HalDeclaration9 &, HalElement *layout, bool &pretransformed);
    HalContext *HalContextOf(unsigned handle);
    bool HalBeginBatch(HalContext &);
    void HalBindTextures(HalContext &);
    void HalRecord(HalContext &, unsigned key);
    void HalCubeFaces(unsigned lcl, unsigned stage, Gpu::Surface &view);
    void HalSetRenderState(HalContext &, unsigned state, unsigned value, unsigned rstates);
    void HalSetStageState(HalContext &, unsigned stage, unsigned state, unsigned value);
    void HalStateSet(HalContext &, unsigned operation, unsigned handle, unsigned rstates);
    unsigned DirectDrawHalCommand(unsigned command, unsigned parameter, unsigned out_bytes, unsigned out);
    unsigned DirectDrawHalInfo(unsigned buffer);
    unsigned DirectDrawHalFns(unsigned buffer);
    unsigned DisplayDriverApi(ApiName, const unsigned *);
    void FreeDirectDrawHal();
    // NEO98DX.DLL, the DirectDraw and DirectSound driver below the ARM64
    // DDRAW.DLL and DSOUND.DLL: src/x86_dx_driver.cpp, src/x86_dx_sound.cpp,
    //
    struct DxDriver;
    struct DxSurface;
    struct DxSound;
    struct DxSoundVoice;
    struct DxSoundMemory;
    DxDriver *dx_driver = nullptr;
    DxSound *dx_sound = nullptr;
    static const char *DxDriverModule();
    unsigned DxDriverApi(ApiName, const unsigned *);
    unsigned DxSoundApi(ApiName, const unsigned *);
    void FreeDxDriver();
    void FreeDxSound();
    DxSurface *DxSurfaceOf(unsigned handle);
    bool DxSurfaceView(unsigned handle, Gpu::Surface &view);
    void DxCubeFaces(unsigned handle, Gpu::Surface *faces, Gpu::Surface &view);
    void DxSync(const unsigned char *bits, bool will_write);
    bool DxPlane(const void *abi_plane, bool write, void *blit_plane, DxSurface **surface, unsigned &result);
    bool DxAllocateSurfaceMemory(DxSurface &);
    void DxReleaseSurfaceMemory(DxSurface &);
    bool DxBindPrimary(DxSurface &);
    unsigned DxWindowClip(unsigned window, void **rects);
    unsigned DxCreateSurface(unsigned address, unsigned bytes);
    unsigned DxDestroySurface(unsigned handle);
    unsigned DxAttach(unsigned handle, unsigned attached, bool attach);
    unsigned DxLock(unsigned address, unsigned bytes);
    unsigned DxUnlock(unsigned handle);
    unsigned DxBlt(unsigned address, unsigned bytes);
    unsigned DxFlip(unsigned list, unsigned count, unsigned flags);
    unsigned DxIsLost(unsigned handle);
    unsigned DxRestore(unsigned handle, unsigned address, unsigned bytes);
    unsigned DxSetPalette(unsigned handle, unsigned start, unsigned count, unsigned colors, unsigned flags);
    unsigned DxSetColorKey(unsigned handle, unsigned flags, unsigned low, unsigned high);
    unsigned DxGetDC(unsigned address, unsigned bytes);
    unsigned DxReleaseDC(unsigned dc);
    unsigned DxGetInfo(unsigned address, unsigned bytes);
    unsigned DxGetMode(unsigned index, unsigned address, unsigned bytes);
    unsigned DxSetMode(unsigned width, unsigned height, unsigned bits);
    unsigned DxRestoreMode();
    unsigned DxSetExclusive(unsigned window, bool enter);
    unsigned DxWaitForVerticalBlank(unsigned flags, unsigned in_blank_address);
    unsigned DxGetScanLine(unsigned address);
    unsigned DxGammaRamp(unsigned address, bool set);
    unsigned DxGetClipList(unsigned window, unsigned data, unsigned bytes_address);
    // Direct3D for ARM64 processes: the contexts of the rasterizer below the
    // Direct3D 7 of the SDK, src/x86_dx_d3d.cpp.
    struct DxD3d;
    struct DxD3dContext;
    DxD3d *dx_d3d = nullptr;
    void FreeDxD3d();
    DxD3dContext *DxD3dContextOf(unsigned handle);
    bool DxD3dBegin(DxD3dContext &);
    void DxD3dBindTextures(DxD3dContext &);
    unsigned DxD3dGetCaps(unsigned address, unsigned bytes);
    unsigned DxD3dCreateContext(unsigned target, unsigned depth, unsigned out);
    unsigned DxD3dDestroyContext(unsigned handle);
    unsigned DxD3dRun(unsigned handle, unsigned commands, unsigned bytes);
    DxSoundVoice *DxSoundBuffer(unsigned handle);
    void DxSoundSettle(DxSoundVoice &);
    void DxSoundServe();
    void DxSoundVolume(DxSoundVoice &, bool force = false);
    void DxSoundNotify(DxSoundVoice &, unsigned position, bool stopped);
    void DxSoundRelease(DxSoundVoice &, bool unmap);
    unsigned DxSoundCreate(unsigned address, unsigned bytes);
    unsigned DxSoundDuplicate(unsigned handle, unsigned out);
    unsigned HalGate(const char *name);
    unsigned char *HalHostPointer(unsigned flat, unsigned bytes);
    bool HalCreateDriverObject();
    bool HalRefreshInfo();
    void HalRegisterSurface(unsigned lcl, unsigned local, unsigned depth, bool evict = true);
    struct HalKeep { unsigned handle, keep; };
    unsigned HalChainHandles(unsigned lcl, unsigned depth, HalKeep *list, unsigned count, unsigned capacity);
    void HalForgetSurface(unsigned lcl);
    unsigned HalRegisterShadow(unsigned lcl, unsigned local, unsigned depth);
    HalSurface *HalShadowByHandle(unsigned handle);
    void HalEvictHandle(unsigned handle, unsigned keep);
    void HalEvictHandles(const HalKeep *list, unsigned count);
    bool HalTransientSurface(unsigned lcl);
    bool HalSurfaceView(unsigned lcl, Gpu::Surface &view);
    unsigned HalFlip(unsigned data);
    unsigned HalCanCreateSurface(unsigned data);
    unsigned HalBlt(unsigned data);
    void HalReleaseAll();
    void HalSync(const unsigned char *bits, bool will_write);
    void HalSyncAll();   // before a saved state: src/x86_ddhal.cpp
    void HalShowIfVisible(HalContext &);
    void HalTextureBlt(unsigned dest_handle, unsigned source_handle, const int *point, const int *rect);
    bool HalPlaneOf(unsigned lcl, HalPlane &plane);
    bool HalCopyBlocks(const HalPlane &d, const HalPlane &s, const int *rect, int x, int y);
    unsigned HalCreateSurface(unsigned data);
    unsigned HalLock(unsigned data);
    unsigned HalUnlock(unsigned data);
    unsigned HalWaitForVerticalBlank(unsigned data);
    unsigned HalSetMode(unsigned data);
    unsigned HalGetDriverInfo(unsigned data);
    unsigned HalDriverInfo2(unsigned out, unsigned expected);
    unsigned HalContextCreate(unsigned data);
    unsigned HalDrawPrimitives2(unsigned data);
    unsigned HalClear2(unsigned data);
    // src/x86_gdi_device.cpp: Escape, the print document group and the
    // deferred device families.
    unsigned ExpectedWindowsVersion();
    unsigned GdiEscapeApi(ApiName, const unsigned *);
    unsigned GdiDocumentApi(ApiName, const unsigned *);
    unsigned GdiPixelFormatApi(ApiName, const unsigned *);
    unsigned GdiGammaApi(ApiName, const unsigned *);
    unsigned GdiIcmApi(ApiName, const unsigned *);
    // src/x86_gdi_fonts.cpp: the remaining font exports.
    unsigned GdiTextExtentExApi(ApiName, const unsigned *);
    unsigned GdiMapperFlagsApi(ApiName, const unsigned *);
    unsigned GdiCharacterPlacementApi(ApiName, const unsigned *);
    unsigned GdiOutlineQueryApi(ApiName, const unsigned *);
    unsigned abort_procedures = 0; // GDI DC+D0h, the SetAbortProc callback
    bool PathRecord(ApiName, const unsigned *, unsigned &);
    // src/x86_accelerators.cpp: the accelerator table store and its exports.
    static bool DestroyCreatedAcceleratorTable(unsigned handle);
    bool AcceleratorTableEntries(unsigned handle, const Win98Accelerators::Entry *&, unsigned &);
    unsigned AcceleratorResource(unsigned handle, unsigned &size);
    unsigned AcceleratorApi(ApiName, const unsigned *);
    // src/x86_keyboard_translate.cpp: the KEYBOARD.DRV translation exports.
    const Win98KeyboardTranslate::Layout *KeyboardTranslationLayout(unsigned hkl);
    unsigned KeyboardTranslateApi(ApiName, const unsigned *);
    // src/x86_gdi_metafile.cpp: the recording metafile DC, the WMF record
    // format and playback through the same native GDI calls.
    struct MetafileSlot; struct MetafileState;
    MetafileState *metafiles = nullptr;
    MetafileState &Metafiles();
    MetafileSlot *MetafileEntry(unsigned handle); MetafileSlot *RecordingMetafile(unsigned dc);
    unsigned MetafileHandleOf(const MetafileSlot *) const;
    unsigned MetafileApi(ApiName, const unsigned *);
    bool MetafileRecord(ApiName, const unsigned *, unsigned &);
    bool MetafileIcon(unsigned, int, int, const WinImage::Image &, unsigned, unsigned &);
    bool MetafileWrite(MetafileSlot &, unsigned, const unsigned short *, unsigned);
    unsigned MetafileSelect(MetafileSlot &, unsigned, unsigned);
    bool MetafileCreateRecord(MetafileSlot &, unsigned, unsigned);
    unsigned MetafilePoly(MetafileSlot &, ApiName, const unsigned *);
    unsigned MetafilePolyPolygon(MetafileSlot &, const unsigned *);
    unsigned MetafileText(MetafileSlot &, ApiName, const unsigned *);
    void MetafileHeader(unsigned char *, const MetafileSlot &, unsigned, unsigned);
    unsigned MetafileClose(MetafileSlot &);
    void MetafileRelease(MetafileSlot &);
    void MetafileUnsupported(ApiName, bool enhanced);
    bool MetafileStore(const char *, const unsigned char *, unsigned);
    unsigned char *MetafileLoad(unsigned, unsigned &);
    unsigned MetafileAdopt(const unsigned char *, unsigned);
    unsigned MetafilePlay(unsigned, MetafileSlot &);
    unsigned MetafilePlayRecord(unsigned, unsigned, unsigned, const unsigned char *, unsigned);
    unsigned MetafileCreateObject(unsigned, const unsigned char *, unsigned, unsigned, unsigned);
    unsigned MetafilePlayPoly(unsigned, unsigned, const unsigned char *, unsigned);
    unsigned MetafilePlayText(unsigned, unsigned, const unsigned char *, unsigned);
    unsigned EnhancedApi(ApiName, const unsigned *);
    bool EnhancedRecord(MetafileSlot &, ApiName, const unsigned *, unsigned &);
    bool EnhancedWrite(MetafileSlot &, unsigned, const unsigned char *, unsigned);
    void EnhancedBounds(MetafileSlot &, int, int, int, int);
    unsigned EnhancedSelect(MetafileSlot &, unsigned, unsigned);
    unsigned EnhancedPoly(MetafileSlot &, ApiName, const unsigned *);
    unsigned EnhancedText(MetafileSlot &, ApiName, const unsigned *);
    unsigned EnhancedClose(MetafileSlot &);
    unsigned EnhancedAdopt(const unsigned char *, unsigned);
    unsigned MetafileKindOf(unsigned dc);
    unsigned MetafileObjectType(unsigned handle);
    bool EnhancedRecordRaw(unsigned dc, unsigned type, const unsigned char *payload, unsigned bytes);
    void EnhancedBoundsRaw(unsigned dc, int left, int top, int right, int bottom);
    unsigned EnhancedOpenFile(unsigned path);
    // src/x86_gdi32_gates.cpp: the GDI.EXE gates of phase 2 of branch dlls.
    unsigned Gdi32GateApi(ApiName, const unsigned *);
    unsigned GdiDcKind(unsigned dc);
    void GdiAccumulate(ApiName, const unsigned *);
    unsigned EnhancedPlay(unsigned, MetafileSlot &, unsigned, unsigned, unsigned);
    unsigned EnhancedPlayRecord(unsigned, unsigned, unsigned, const unsigned char *, unsigned);
    void FreeMetafiles();
    // src/x86_dde_params.cpp: the DDE message lParam pool.
    unsigned DdeParameterApi(ApiName, const unsigned *);
    unsigned RedrawWindowRegion(unsigned hwnd, unsigned region, unsigned flags);
    unsigned DefaultPaint(unsigned hwnd);
    unsigned DispatchPaint(unsigned hwnd, unsigned wp, unsigned lp, bool resumed = false);
    // The shared body of BeginPaint: User::BeginPaint plus the WM_ERASEBKGND
    // callback of USER. Both the 32-bit entry and the Win16 gate of
    // USER.39 call it and only convert their arguments.
    unsigned BeginPaintWindow(unsigned hwnd, User::Paint &paint);
    unsigned EraseBackground(unsigned hwnd, unsigned dc);
    unsigned OwnGdi(unsigned handle, unsigned kind);
    bool OwnsGdiObject(unsigned handle) const;
    unsigned GdiOwnerToken(unsigned handle);
    unsigned LcMapStringApi(const unsigned *);
    unsigned SearchPathApi(const unsigned *);
    unsigned OpenProcessApi(const unsigned *);
    unsigned Win16ModuleApi(unsigned ordinal, const unsigned *);
    // Dispatch identity of one export of a loaded Win16 catalogue module, the
    // value ordinal 36 answers with. The flat thunk connector writes these
    // into the token table of a Route::Native16 block.
    unsigned Win16CatalogTarget(unsigned handle, unsigned ordinal);
    // The six implemented 16-bit VER.DLL routines behind catalogue ordinals
    // 6 to 11; src/x86_version16.cpp, decisions in src/win98_version_info.h.
    // `sixteen`: the caller is 16-bit code, not a stub of VERSION.DLL.
    unsigned VersionInfo16(unsigned ordinal, unsigned stack, bool sixteen = false);
    // VerFindFile works its shared directory out once per loaded VER.DLL; in
    // NEO98 that is once per process.
    Win98VersionInfo::FindState version_find;
    // The three DirectX-era catalogue modules of PLAN.md step 8, each with its
    // own provider file and implementation header. Words answers the number
    // of 16-bit argument words the generated 32-bit stub pushes and ~0u for an
    // ordinal this port does not carry.
    static unsigned DirectDraw16Words(unsigned ordinal);
    unsigned DirectDraw16(unsigned ordinal, unsigned stack);
    static unsigned DciMan16Words(unsigned ordinal);
    unsigned DciMan16(unsigned ordinal, unsigned stack);
    // The visible region of a window's client area in screen coordinates, a
    // new region the caller deletes (0 without a window): the client area cut
    // to the screen, less every visible top-level window above it. DDRAW16's
    // InquireVisRgn and DCIMAN's window watch use it (src/x86_ddraw16.cpp).
    unsigned WindowVisibleRegion(unsigned hwnd);
    // The window watches of DCIMAN.DLL ( WinWatchOpen): the window, the
    // "changed" word at +12h, the rectangle of the last clip list (+14h) and
    // the cached RGNDATA (+20h). `seen` is what this port has instead of the
    // WH_CALLWNDPROC hook: the region last handed out, so a change
    // of the visible region sets `changed` (src/x86_dciman16.cpp).
    struct DciWatch
    {
        bool used = false, changed = false;
        unsigned hwnd = 0;
        int rect[4] = {};
        unsigned char *cache = nullptr, *seen = nullptr;
        unsigned cache_bytes = 0, seen_bytes = 0;
    } dci_watches[16];
    void FreeDciWatch(DciWatch &);
    bool DciWatchRegion(unsigned hwnd, unsigned char *&data, unsigned &bytes);
    static unsigned DiskApi16Words(unsigned ordinal);
    unsigned DiskApi16(unsigned ordinal, unsigned stack);
    // DMUSIC16.DLL, the 16-bit half of the DirectMusic port driver.
    // src/x86_dmusic16.cpp.
    static unsigned DMusic16Words(unsigned ordinal);
    unsigned DMusic16(unsigned ordinal, unsigned stack);
    unsigned DMusic16Answer(unsigned ordinal, unsigned stack);
    static unsigned Tsp3216Words(unsigned ordinal);
    static unsigned Inet16Words(unsigned ordinal);
    unsigned Inet16(unsigned ordinal, unsigned stack);
    unsigned Tsp3216(unsigned ordinal, unsigned stack);
    void FreeDMusic16Ports();
    void DMusic16Play(void *device, bool flush);
    void DMusic16Release(void *device, bool all);
    void PumpDMusic16();
    bool DMusic16Active();
    unsigned win16_module_requests = 0;
    struct NativeWin16Module { unsigned handle=0, references=0, memory=0, segment=0; } native_win16_modules[12];
    static unsigned CatalogArgumentWords(unsigned slot, unsigned ordinal);
    bool NativeWin16Dispatch(unsigned,unsigned,unsigned,unsigned&,unsigned&);

    // The Win16 loader of PLAN.md step 9, src/ne_loader.cpp: an NE image runs
    // as the original 16-bit code it is, behind real descriptors of a real
    // LDT, and reaches KERNEL, USER and GDI through gates of the gate region.
    struct Import; // defined with the gate table below
    struct Win16Block;
    struct Win16Module;
    struct Win16Machine;
    struct Win16Frame;
    struct Win16Owner { Win16Machine *state = nullptr; ~Win16Owner(); } win16;
    bool StartWin16(const void *, unsigned, const unsigned char *, unsigned, const char *, const char *);
    // A DOS program is a process of the DOS machine of src/dos_vm.h; its one
    // thread runs RunDosVm natively and owns the `tty` window. src/x86_dos_vm.cpp.
    struct DosVmState;
    DosVmState *dos_vm = nullptr;
    bool StartDosVm(const unsigned char *, unsigned, const char *, const char *);
    void RunDosVm();
    bool DosVmCreateWindow();
    void DosVmFrame(const DosVm::Frame &);
    void DosVmVoodoo();
    void DosVmRestoreCursor();
    void DosVmScale();
    void DosVmFullscreen(bool on);
    bool DosVmLoadToolbar();
    void DosVmPaintToolbar(unsigned dc, int width);
    void DosVmToolbarClick(int x, int y);
    void DosVmReloadSettings();
    void DosVmCopySelection();
    void DosVmFontBox();
    void DosVmFillFontBox();
    void DosVmChooseFont(unsigned data);
    static constexpr unsigned DosVmFontBoxId = 0x5001;
    bool DosVmService(bool modal);
    static constexpr unsigned DosVmServiceTimer = 0x444f;
    bool DosVmLoadFont();
    bool DosVmOpenWoaFont(unsigned &handle, char *found = nullptr, unsigned found_capacity = 0);
    bool DosVmLoadTrueType();
    bool DosVmTextPicture();
    bool DosVmZoomed() const;
    unsigned DosVmFace(unsigned columns,unsigned rows,bool zoomed);
    static unsigned DosVmAutoCell(const unsigned (*cells)[2], unsigned count, unsigned columns, unsigned rows, int strip,
                                  bool zoomed);
    bool DosVmAutoFor(const unsigned (*cells)[2], unsigned count, bool toolbar, unsigned &width, unsigned &height);
    bool DosVmPictureSize(bool zoomed,unsigned &width,unsigned &height);
    bool DosVmTextCells(const unsigned short *cells, unsigned columns, unsigned rows, const void *raster, unsigned fw,
                        unsigned fh);
    void DosVmCaptureMouse();
    void DosVmReleaseMouse();
    // The PIF groups the window applies itself (Misc page) and the ones the
    // machine gets at its start; src/x86_dos_vm.cpp.
    void DosVmLiveSettings(Win98PifMgr::Record &record);
    void DosVmMachineSettings(Win98PifMgr::Record &record, const unsigned char *prg, const unsigned char *mem,
                              const unsigned char *vid);
    bool DosVmClipboardSet(unsigned hwnd, const char *text);
    bool DosVmClipboardGet(unsigned hwnd, char *out, unsigned capacity);
    bool DosVmWindowsPath(const char *dos, char (&out)[260]);
    void DosVmRunWindowsProgram(const char *program, const char *arguments, const char *directory, bool wait);
    void DosVmTitle();
    void DosVmExit(unsigned code);
    bool DosVmPath(const char *guest, char (&out)[260]);
    unsigned DosVmWindowMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    void FreeDosVm();
    bool DosVmProcess() const;
    // The picture the DOS window shows, in the machine's pixels, and one for
    // it to show until the machine draws again.
    bool DosVmPicture(const unsigned *&pixels, unsigned &width, unsigned &height) const;
    void DosVmShowPicture(const unsigned *pixels, unsigned width, unsigned height);
    // Whether the DOS window has the full screen (Alt+Enter), and the wish
    // of a loaded state for it.
    bool DosVmFullscreenNow() const;
    void DosVmWantFullscreen(bool on);
    bool Win16Machinery();
    bool Win16Task(Win16Module &, const char *command);
    bool Win16Environment(Win16Module &);
    bool Win16EnterRingThree(unsigned code, unsigned ip, unsigned stack, unsigned sp);
    bool Win16FlatEnter(CpuContext *&saved, unsigned &previous_top);
    // The flat level-zero state of a Win16 task before it entered level
    // three; Win16FlatEnter starts the 32-bit side of a thunk from it.
    CpuContext *win16_flat_template = nullptr;
    void Win16FlatLeave(CpuContext *saved, unsigned previous_top);
    // A 16-bit callback (a window procedure, a hook, an enumeration) that a
    // thread of a Win16 task reaches while it runs flat code inside a generic
    // thunk (a wait in DllMain that serves paints) runs on the task's 16-bit
    // stack below the frame of that thunk, the way the thunk layer of Win98
    // switches back to the SS:SP it saved on the way to 32 bits. The scope
    // puts the innermost 16-bit state of Win16FlatEnter in place and brings
    // the flat state back at its end.
    struct Win16TaskStack
    {
        Application &app;
        CpuContext *flat = nullptr;
        explicit Win16TaskStack(Application &a);
        ~Win16TaskStack();
    };
    bool Win16Descriptor(unsigned selector);
    unsigned Win16Segment(unsigned base, unsigned limit, bool code, bool big);
    unsigned Win16Linear(unsigned selector, unsigned offset) const;
    unsigned Win16Limit(unsigned selector) const;
    void Win16DescribeAddress(char *out, unsigned capacity, unsigned selector, unsigned offset);
    unsigned Win16Word(unsigned linear);
    void Win16Word(unsigned linear, unsigned value);
    unsigned Win16Gate(const char *module, const char *name, unsigned ordinal);
    unsigned Win16GatePointer(unsigned gate);
    Win16Module *Win16Find(const char *name);
    unsigned Win16ReadFile(const char *path, unsigned char *&bytes, unsigned &length);
    bool Win16KernelConstant(unsigned ordinal, unsigned &value);
    static unsigned Win16RealModeParagraph(unsigned ordinal, const char *symbol);
    unsigned Win16RealModeSelector(unsigned paragraph);
    unsigned Win16RealModeImage();
    bool Win16SetupxRealModeCall();
    // DOS's country tables in the first megabyte (src/ne_loader.cpp).
    enum : unsigned { DosCollateAddress = 0x0600, DosUpperAddress = 0x0710, DosCaseMapAddress = 0x07a0 };
    bool Win16DosCountry();
    bool Win16DosCaseMap(unsigned segment, unsigned offset, unsigned &ax);
    unsigned Win16FindLibrary(const char *module, char (&full)[260], unsigned char *&bytes,
                              unsigned &length);
    Win16Module *Win16Place(const char *path, unsigned char *bytes, unsigned length, bool library,
                            unsigned &error);
    bool Win16ModuleDatabase(Win16Module &);
    bool Win16LoadSegments(Win16Module &, unsigned &error);
    void Win16PatchEntries(Win16Module &);
    bool Win16Relocate(Win16Module &, unsigned number, unsigned &error);
    unsigned Win16Ordinal(Win16Module &, const char *symbol);
    unsigned Win16Import(const char *module, const char *symbol, unsigned ordinal, unsigned &error);
    bool Win16Call(Import &, const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16KernelCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16DosCall(unsigned &result);
    // DPMI 0300h for INT 21h: the DOS call with the registers of a real mode
    // call structure (src/ne_loader.cpp); false where DOS has no answer here.
    bool Win16RealModeDos(unsigned structure);
    bool Win16DosFind(bool first, unsigned dta, unsigned &error);
    bool Win16DosVolumeLabel(unsigned pattern, unsigned dta, unsigned &error);
    bool Win16DosLongName(unsigned ax, unsigned &result, unsigned flags);
    bool Win16DosFileControlFind(bool first, unsigned &result, unsigned flags);
    static void Win16Interrupt(Cpu &, unsigned number, void *context);
    void Win16Clear(unsigned address, unsigned bytes);
    unsigned Win16GlobalAlloc(unsigned flags, unsigned size);
    bool Win16GlobalMap(Win16Block &, unsigned memory, unsigned size);
    Win16Block *Win16GlobalBlock(unsigned handle);
    bool Win16GlobalFree(unsigned handle);

  public:
    // Missing runtime DLL name, including extension, for the parent to
    // inspect after startup failure. PE startup sets missing_module_error to
    // ERROR_DLL_NOT_FOUND (1157). The 16-bit path only records
    // win16_not_found.
    char missing_module[128] = {};
    unsigned missing_module_error = 0;
    // Full startup diagnostic for a missing export rather than a missing
    // module.
    char missing_text[320] = {};
    // Whether Start is binding startup imports. Only failures in this phase
    // produce a startup dialog.
    bool binding_process_image = false;
    // A missing startup file is a normal load error, not a crash. Fail must
    // not generate a crash report for it.
    bool load_failure = false;
    // Most recently missing 16-bit module filename. KERNEL32 ordinal 91
    // (PK16FNF) exposes this session-wide value to SHELL32.
    static char win16_not_found[128];

  private:
    // Win16 metafiles (src/win16_metafile.cpp). HMETAFILE is a global-heap
    // block containing the image.
    bool Win16MetafileCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned words);
    bool Win16MetafileValid(unsigned handle);
    unsigned Win16MetafileNative(unsigned handle);
    unsigned Win16MetafileWrap(unsigned native);
    void Win16MetafileRelease(unsigned native);
    bool Win16LocalInit(unsigned selector, unsigned start, unsigned end);
    bool Win16LocalCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16FileCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    // The USER and GDI gates of the second part, src/win16_user.cpp. A window
    // procedure of the 16-bit side is a far pointer behind a mark above
    // Win16Mark, which is the range native USER already treats as guest
    // code.
    static constexpr unsigned Win16MarkBase = 0x71010000;
    // The Win98 base of KERNEL32 and the return address of a process entry
    // inside its header page (src/x86_module_image.cpp).
    static constexpr unsigned Win98KernelBase = 0xbff70000, ProcessReturnOffset = 0x40,
                              ProcessExitGate = 0x7000ffe0;
    static constexpr unsigned Win16ThunkOrdinal = 0x10000;
    unsigned Win16GateSelector();
    unsigned Win16Scratch();
    // True while a 16-bit gate runs, so a provider that would put a
    // structure on the caller's stack knows that stack is segmented.
    bool InWin16Gate() const { return win16_gate_depth != 0; }
    unsigned win16_gate_depth = 0;
    // The SHELL, KEYBOARD and WIN87EM gates, src/win16_shell.cpp.
    bool Win16KeyboardCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16ShellCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    unsigned Win16Shell32(const char *name, unsigned ordinal, const unsigned *a, unsigned count, bool &found);
    bool Win16SoundCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16WaveOutGetDevCaps(const Win16Frame &, unsigned &result);
    bool Win16WaveInGetDevCaps(const Win16Frame &, unsigned &result);
    bool Win16WaveOutCall(unsigned, const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16WaveOutNotify(unsigned handle, unsigned message, unsigned parameter);
    bool Win16WaveInNotify(unsigned handle, unsigned message, unsigned parameter);
    unsigned Win16CallbackCheck(unsigned kind, unsigned callback);
    unsigned Win16TaskThread(unsigned task);
    void Win16PostCallback(unsigned kind, unsigned callback, unsigned message, unsigned word, unsigned parameter);
    void Win16WaveOutCallback(unsigned callback, unsigned word, unsigned instance, unsigned message, unsigned first);
    void Win16FreeWaveOut();
    bool Win16CommonControlModule();
    // DPA_Create to DPA_Search of COMMCTRL, 328 to 339 (src/win16_dpa.inc).
    bool Win16DpaCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    unsigned Win16DpaAlloc(unsigned bytes);
    bool Win16DpaFree(unsigned far_pointer);
    unsigned Win16DpaReAlloc(unsigned far_pointer, unsigned bytes);
    // A 16-bit Pascal routine called from inside a gate; `push` in push order.
    // `ds` ~0u: the task's data segment in DS and AX.
    unsigned Win16CallPascal(unsigned far_pointer, const unsigned *push, unsigned count, unsigned ds = 0);
    // Copy MMSOUND threshold status to the word returned by
    // GetThresholdEvent. Pump calls this because note callbacks run on the
    // mixer thread.
    void Win16SoundServe();
    void Win16SoundRelease();
    unsigned Win16SoundEvent();
    bool Win16MathCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16MathClear();
    bool Win16MathPending(unsigned flags, unsigned control);
    bool Win16CommDlgCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    // of WIN87EM.DLL loads this control word after `fninit`.
    static constexpr unsigned Win87ControlWord = 0x1332;
    void Win16ReadRect(unsigned at, Gdi::RECT &rect);
    void Win16WriteRect(unsigned at, const Gdi::RECT &rect);
    unsigned Win16TaskCount();
    unsigned Win16EditHandle(unsigned hwnd, unsigned message, unsigned wparam);
    // The local heap operations a gate can ask for on the task's DGROUP.
    enum : unsigned
    {
        Win16LocalAllocate = 0,
        Win16LocalReAllocate,
        Win16LocalFree,
        Win16LocalLock,
        Win16LocalUnlock,
        Win16LocalFlags,
        Win16LocalHandle,
        Win16LocalSizeOf,
        Win16LocalLargestFree,
        Win16LocalHeapSize
    };
    unsigned Win16LocalOperation(unsigned kind, unsigned a, unsigned b = 0, unsigned c = 0);
    bool Win16Printf(const Win16Frame &frame, unsigned &result, unsigned &words);
    bool Win16Printf(const Win16Frame &frame, unsigned &result, unsigned &words, bool list);
    // The word a 16-bit program names a file with, and the way back.
    unsigned Win16FileWord(unsigned native);
    unsigned Win16FileNative(unsigned word);
    void Win16FileRelease(unsigned word);
    bool Win16StandardHandle(unsigned word);
    // The staging ring for a structure a 16-bit caller owns.
    static constexpr unsigned Win16StagingSlots = 8, Win16StagingSlotBytes = 64;
    unsigned Win16Staging(unsigned bytes);
    unsigned Win16WidenStructure(unsigned structure, unsigned far_pointer);
    unsigned Win16Answer(unsigned message16, unsigned result);
    struct Win16ControlTransfer
    {
        unsigned target = 0, buffer = 0, count = 0, kind = 0;
    };
    unsigned Win16ControlAnswer(Win16ControlTransfer &, unsigned result);
    unsigned Win16Mark(unsigned far_pointer);
    unsigned Win16MarkPointer(unsigned mark) const;
    unsigned Win16NativePointer(unsigned procedure);
    unsigned Win16Procedure(unsigned far_pointer);
    unsigned Win16ProcedurePointer(unsigned procedure);
    unsigned Win16Invoke(unsigned far_pointer, unsigned hwnd, unsigned message, unsigned wparam,
                         unsigned lparam);
    // A callback into 32-bit code while a hosted 16-bit call is on the CPU
    // runs on the flat context the call was entered from.
    unsigned Win16HostedStack();
    bool Win16HostedFlat(unsigned &low);
    void Win16HostedResume(unsigned low);
    // `record` is the 264-byte record of the shared enumeration
    // (src/x86_font_query.cpp); the 16-bit callback gets ENUMLOGFONT and
    // NEWTEXTMETRIC in their 16-bit forms.
    unsigned Win16FontCallback(unsigned far_pointer, const unsigned char *record, unsigned font_type,
                               unsigned parameter);
    unsigned Win16HookCallback(unsigned procedure, unsigned code, unsigned wparam, unsigned lparam);
    unsigned Win16Far(unsigned flat);
    void Win16NoteFarAlias(unsigned flat, unsigned far_pointer);
    unsigned Win16RememberFar(unsigned far_pointer);
    unsigned Win16FarOf(unsigned flat);
    static unsigned Win16StructureBytes(unsigned structure);
    bool Win16StructureIn(unsigned structure, unsigned out, unsigned source);
    void Win16StructureOut(unsigned structure, unsigned staged, unsigned target);
    bool Win16ToThirtyTwo(unsigned hwnd, unsigned &message, unsigned &wparam, unsigned &lparam,
                         Win16ControlTransfer *transfer = nullptr);
    unsigned Win16Send(unsigned hwnd, unsigned message, unsigned wparam, unsigned lparam);
    void Win16ReadMessage(unsigned at, User::Message &, Win16ControlTransfer *transfer = nullptr);
    // A control message whose far pointer names an array or a rectangle: it
    // needs a native copy for the time of the delivery.
    bool Win16NeedsTransfer(unsigned hwnd, unsigned message, unsigned lparam);
    void Win16WriteMessage(unsigned at, const User::Message &);
    static unsigned Win16UserWords(unsigned ordinal, const char *&name);
    static unsigned Win16GdiWords(unsigned ordinal, const char *&name);
    bool Win16UserCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16GdiCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    // The ordinals of the pass over the whole export tables :
    // src/win16_user_more.inc and src/win16_gdi_more.inc. `handled` is false
    // for an ordinal neither knows.
    bool Win16UserMore(unsigned ordinal, const Win16Frame &, unsigned words, unsigned &result, bool &handled);
    bool Win16GdiMore(unsigned ordinal, const Win16Frame &, unsigned words, unsigned &result, bool &handled);
    unsigned Win16RegisterClassAt(unsigned at, unsigned small_icon);
    unsigned Win16ClassInfoInto(unsigned instance, unsigned klass, unsigned out);
    unsigned Win16MenuFromTemplate(const unsigned char *source, unsigned bytes);
    bool Win16GetMessage(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16GetObject(const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16GetClassInfo(const Win16Frame &, unsigned &result, unsigned &words);
    Win16Module *Win16ModuleOf(unsigned instance);
    bool Win16OwnsInstance(unsigned instance);
    bool Win16WidenMenuTemplate(const unsigned char *data, unsigned length, unsigned &position, unsigned level,
                                unsigned char *out, unsigned limit, unsigned &written);
    bool Win16MultimediaCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words);
    unsigned Win16SystemOrdinal(const char *module, const char *name);
    bool Win16CreateToolbarEx(const Win16Frame &frame, unsigned &result);
    bool Win16CommonControlCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words);
    // The property sheet of the 16-bit COMMCTRL, src/win16_property_sheet.cpp.
    struct Win16SheetPage;
    struct Win16SheetState;
    Win16SheetPage *win16_sheet_pages = nullptr;
    unsigned Win16CreatePropertyPage(unsigned far_page);
    // Pages a 16-bit page added to the running sheet with PSM_ADDPAGE, and
    // their converted templates; they go when the 16-bit sheet ends.
    unsigned Win16NativePage(unsigned far_page);
    unsigned Win16PageTemplateName(unsigned page);
    // src/win16_common_controls.cpp: the image lists and the tree view of the
    // 16-bit COMMCTRL.
    unsigned win16_image_lists[32] = {};
    // Bit 31 and the delta of the WM_MOUSEWHEEL a 16-bit procedure is in.
    unsigned win16_wheel = 0;
    // The delta of the last WM_MOUSEWHEEL written into a MSG of 16-bit code.
    unsigned win16_wheel_window = 0, win16_wheel_place = 0, win16_wheel_queued = 0;
    unsigned Win16ImageListNative(unsigned word);
    unsigned Win16ImageListWord(unsigned native);
    bool Win16ImageListCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words);
    void Win16TreeItemIn(unsigned source, unsigned target);
    void Win16TreeItemOut(unsigned source, unsigned target, unsigned text16);
    bool Win16TreeSend(unsigned hwnd, unsigned message, unsigned wparam, unsigned lparam, unsigned &result);
    void Win16ListItemIn(unsigned source, unsigned target);
    void Win16ListItemOut(unsigned source, unsigned target, unsigned text16);
    bool Win16ListSend(unsigned hwnd, unsigned message, unsigned wparam, unsigned lparam, unsigned &result);
    unsigned Win16NotifyBytes(unsigned lparam);
    void Win16NotifyTo16(unsigned lparam, unsigned at, unsigned far_at);
    void Win16NotifyBack(unsigned lparam, unsigned at, unsigned far_at);
    // src/win16_config_manager.cpp: the 16-bit API of CONFIGMG.
    struct ConfigTree;
    bool ConfigTreeBuild(ConfigTree &tree);
    void ConfigDeviceId(const ConfigTree &tree, unsigned index, char (&out)[200]);
    bool Win16ConfigManagerCall(unsigned &result, unsigned &words,unsigned function32=~0u,unsigned arguments32=0);
    unsigned ConfigManagerControl(const unsigned *args);
    unsigned BatteryControl(const unsigned *args);
    unsigned win16_added_pages[32] = {}, win16_added_templates[32] = {}, win16_added_count = 0;
    bool Win16DestroyPropertyPage(unsigned far_page);
    unsigned Win16PropertySheet(unsigned far_header);
    unsigned FinishWin16PropertySheet(unsigned memory, unsigned result, bool resumed = false);
    void DiscardWin16PropertySheets();
    unsigned ResumeWin16PropertyPage(unsigned hwnd, unsigned message, unsigned result);
    unsigned Win16PropertyPageMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    void Win16SheetText(unsigned record, unsigned field, unsigned instance, char (&out)[256]);
    unsigned Win16PageTemplate(unsigned page);
    unsigned Win16FarPointer(unsigned at);
    bool Win16FileDialog(const Win16Frame &frame, bool save, unsigned &result);
    // The Win 3.1 file dialog of the 16-bit COMMDLG (src/win16_file_dialog.inc).
    struct Win16OpenDialog;
    unsigned Win16FileDialogMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    // ChooseColor of the 16-bit COMMDLG, src/win16_choose_color.inc.
    struct Win16ControllerPhase;
    struct Win16ColorDialog;
    bool Win16ChooseColor(const Win16Frame &frame, unsigned &result);
    unsigned Win16ColorDialogMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    unsigned Win16ColorBoxMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    // ChooseFont of the 16-bit COMMDLG, src/win16_choose_font.inc.
    // Its enumerations call the shared font provider with the token as the
    // procedure; the provider hands the 32-bit record to
    // Win16FontEnumCallback (src/x86_font_query.cpp).
    struct Win16FontDialog;
    static constexpr unsigned Win16FontDialogToken = 0x71000047, Win16FontEnumToken = 0x71000048;
    bool Win16ChooseFont(const Win16Frame &frame, unsigned &result);
    unsigned Win16FontDialogMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    unsigned Win16FontEnumCallback(unsigned record, unsigned type);
    // PrintDlg of the 16-bit COMMDLG, src/win16_print_dialog.inc.
    struct Win16PrintDialogs;
    unsigned Win16PrintDialogMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp, bool setup);
    unsigned Win16PrintFieldMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp, bool list);
    // ENUMLOGFONTEX.elfScript: string 1000 + charset of GDI.EXE.
    void GdiScriptName(unsigned charset, char *out, unsigned capacity);
    struct CommDlgImages &CommDlgResources();
    bool Win16PrintDialog(const Win16Frame &frame, unsigned &result);
    // FindText and ReplaceText of COMMDLG : the modeless
    // dialog works on a 32-bit FINDREPLACE, the caller keeps its 16-bit one,
    // and each notification copies the flags back and names the 16-bit one
    // with its far pointer.
    struct Win16FindShadow
    {
        unsigned wide = 0, narrow = 0, template_flags = 0, hwnd = 0;
    };
    Win16FindShadow win16_find[4];
    bool Win16FindDialog(const Win16Frame &frame, bool replace, unsigned &result);
    unsigned Win16FindNarrow(unsigned wide);
    unsigned Win16FindTemplate(unsigned narrow, unsigned flags);
    unsigned Win16ToNativeGlobal(unsigned handle16);
    unsigned Win16FromNativeGlobal(unsigned handle, unsigned into = 0);
    unsigned Win16NativeTemplate(unsigned instance, unsigned far_name, unsigned handle16);
    void Win16CommonDialogError(unsigned value);
    Win16Block *Win16GlobalSlot();
    void Win16RegisterSegment(unsigned selector, unsigned memory, unsigned size);
    unsigned Win16LoadMenu(unsigned instance, unsigned id, const char *label);
    unsigned Win16LoadBitmap(unsigned instance, unsigned id, const char *label, unsigned flags = 0);
    bool Win16SetClipboardData(const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16GetClipboardData(const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16BeginPaint(const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16EndPaint(const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16RegisterClass(const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16CreateWindow(const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16CreateWindow(const Win16Frame &, unsigned &result, unsigned &words, bool extended);
    bool Win16LoadString(const Win16Frame &, unsigned &result, unsigned &words);
    unsigned Win16LoadStringInto(unsigned instance, unsigned id, unsigned buffer, unsigned capacity);
    bool Win16FormatMessage(const Win16Frame &, unsigned &result);
    unsigned Win16FormatWords(unsigned out, unsigned format, unsigned arguments, const Win16Frame *frame);
    unsigned Win16LoadImage(unsigned instance, bool cursor, unsigned id, const char *label, unsigned cx = 0,
                            unsigned cy = 0);
    unsigned Win16ImageAlias(unsigned native, unsigned instance, bool cursor, unsigned id, const char *label);
    unsigned Win16ImageNative(unsigned word) const;
    unsigned Win16ImageWord(unsigned native) const;
    unsigned Win16DestroyImage(unsigned word);
    unsigned Win16LoadAccelerators(unsigned instance, unsigned id, const char *label);
    unsigned Win16Resource(Win16Module &, unsigned type, unsigned name, const char *label,
                           unsigned &bytes);
    void Win16ResourceName(const Win16Frame &, unsigned index, unsigned &id, char *label,
                           unsigned capacity);
    unsigned Win16Dialog(unsigned instance, unsigned type_id, const char *label, unsigned owner,
                         unsigned procedure, unsigned parameter, bool modal);
    unsigned Win16DialogInstance(const Win16Module &);
    unsigned Win16DosDirectory(bool create, unsigned source);
    unsigned Win16DialogFrom(const unsigned char *bytes, unsigned size, unsigned dialog_instance,
                             unsigned owner, unsigned procedure, unsigned parameter, bool modal);
    unsigned Win16DialogIndirect(unsigned instance, unsigned address, unsigned size, unsigned owner,
                                 unsigned procedure, unsigned parameter, bool modal);
    bool Win16EnterLibrary(Win16Module &);
    bool Win16LibraryEntryPoint(Win16Module &);
    // FreeLibrary: KRNL386, the WEP call and the removal of
    // DelModule.
    bool Win16FreeLibrary(unsigned word);
    bool Win16CallExit(Win16Module &);
    bool Win16EnterLibraries(Win16Module &);
    bool Win16EnterLibraries();

  public:
    // A message on its way into a 16-bit window procedure. Application::Call
    // routes a mark here.
    unsigned Win16Deliver(unsigned procedure, unsigned hwnd, unsigned message, unsigned wparam,
                          unsigned lparam);
    static bool Win16IsMark(unsigned procedure);

  private:

  public:
    // True while this process runs a 16-bit program, for a host that wants to
    // say so, and the entry the loader route of src/x86_modules.cpp uses.
    bool Win16Process() const;
    // Original 16-bit code called from native code of this process: a printer
    // driver's DDI, its ExtDeviceMode, the LibMain of a hosted library.
    // src/x86_win16_hosted.cpp. `push` holds the argument
    // words in the order a Pascal caller pushes them; the answer is DX:AX.
    struct Win16HostedRegisters { unsigned ax = 0, bx = 0, cx = 0, dx = 0, si = 0, di = 0, es = 0; };
    bool Win16HostedReady();
    bool Win16HostedEngine() const;
    unsigned Win16HostedCall(unsigned far_pointer, const unsigned *push, unsigned count, unsigned ds,
                             const Win16HostedRegisters *registers = nullptr, bool *returned = nullptr,
                             unsigned *removed = nullptr);
    struct Win16HostedLoadFrame;
    struct HostedRecord;
    unsigned Win16HostedLibrary(const char *path, unsigned &error, Win16HostedLoadFrame *owner = nullptr);
    unsigned ResumeWin16HostedLoad(unsigned key);
    unsigned ContinueWin16HostedLibrary(Win16HostedLoadFrame &frame, bool resumed);
    bool ContinueWin16HostedEnter(Win16HostedLoadFrame &frame, unsigned module, bool resumed);
    unsigned ResumeWin16HostedLibrary(unsigned key);
    unsigned ResumeWin16HostedEnter(unsigned key, unsigned module);
    unsigned ResumeWin16HostedCall(unsigned frame);
    void FinishWin16HostedCall(unsigned frame, bool returned);
    void DropWin16HostedContext(CpuContext *context);
    void DiscardSavedWin16HostedLoads();
    bool Win16HostedWorkRecorded(unsigned key, unsigned kind, unsigned module, unsigned phase) const;
    bool Win16HostedEnter(Win16Module &module, unsigned &error);
    bool Win16HostedArea(unsigned &stack, unsigned &sp, unsigned &low);
    void Win16HostedSetLow(unsigned low);
    // USER's installable drivers of the 16-bit side, the drivers hosted in
    // this process. src/win16_driver.cpp.
    struct Win16Driver
    {
        unsigned module = 0, procedure = 0, driver_id = 0;
        char name[128] = {};
        // NEO98CAP.DRV, the native capture driver (src/win16_capture.cpp):
        // no module, and the VfW channel the entry opened.
        bool camera = false;
        unsigned channel = 0;
    };
    struct Win16Capture
    {
        unsigned width = 320, height = 240, users = 0, camera = 0;
        bool streaming = false, running = false;
        unsigned usec = 66667, callback = 0, instance = 0, flags = 0, video = 0, start = 0, frames = 0, error = 0;
        unsigned queue[32] = {}, queued = 0;
    };
    Win16Capture win16_capture;
    unsigned Win16CaptureFar(unsigned far_pointer);
    bool Win16CaptureFill(unsigned header, unsigned time);
    void Win16CaptureNotify(unsigned message, unsigned far_header);
    void Win16CaptureServe();
    void Win16CaptureCamera(bool on);
    unsigned Win16CaptureMessage(Win16Driver &entry, unsigned message, unsigned first, unsigned second);
    Win16Driver win16_drivers[16];
    Win16Driver *Win16DriverOf(unsigned handle);
    unsigned Win16OpenDriver(const char *name, const char *section, unsigned second);
    unsigned Win16SendDriverMessage(unsigned handle, unsigned message, unsigned first, unsigned second);
    unsigned Win16CloseDriver(unsigned handle, unsigned first, unsigned second, bool close);
    bool Win16DriverCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words);
    // USER.200 to 215, the communication functions (src/win16_comm.inc).
    bool Win16CommCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words);
    unsigned Win16CommEventWord(unsigned cid, bool far_pointer);
    // MMSYSTEM's mmio of the 16-bit side: word handles over WINMM's.
    // src/win16_mmio.cpp.
    unsigned win16_mmio[32] = {};
    // The words GDI.403 keeps at +0Eh of an object (src/win16_user.cpp).
    struct GdiObjectWord { unsigned handle = 0, value = 0; } gdi_object_words[32];
    // The DIB sections GDI.489 made and the selector over the bits of each.
    struct Win16DibSection { unsigned handle = 0, selector = 0; } win16_dib_sections[32];
    unsigned Win16MmioNative(unsigned handle);
    bool Win16MmioCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words);
    // MMSYSTEM's mmThread family (src/win16_mmthread.cpp).
    unsigned Win16MmThreadBlock(unsigned handle);
    bool Win16MmThreadCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words);
    void RunWin16MmThread();
    // Flat thunks into and out of hosted 16-bit modules, and the KERNEL
    // entries of their 16-bit side. src/x86_thunk_hosted.cpp.
    unsigned Win16HostedThunkData(const char *dll16, const char *name, unsigned &module);
    bool FlatThunkHostedCall(unsigned token, unsigned arguments, unsigned bytes, unsigned &result, unsigned &words);
    bool Win16ThunkConnect16(const Win16Frame &frame, unsigned &result);
    bool Win16SlThunk(const Win16Frame &frame, unsigned &result, unsigned &words);
    unsigned Win16HostedProcedure(unsigned module, unsigned ordinal);
    unsigned Win16HostedLoad(const char *path);
    void Win16HostedUnwind(unsigned first);
    bool Win16HostedModule(unsigned handle);
    unsigned Win16HostedOrdinal(unsigned handle, const char *name);
    unsigned Win16HostedInstance(unsigned module);
    void Win16HostedExit(Win16Module &module);
    bool Win16VersionCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words);
    bool Win16ToolhelpCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words);
    // TOOLHELP's notifications and interrupt callbacks.
    void Win16ToolhelpNotify(unsigned id, unsigned data);
    void Win16ToolhelpLoadSegment(const Win16Module &module, unsigned number);
    void Win16ToolhelpStartDll(const Win16Module &module, unsigned entry);
    bool Win16ToolhelpFault(unsigned number);
    bool Win16ToolhelpExit(unsigned code);
    bool Win16FaultStop(unsigned number);
    // MSCDEX for 16-bit programs (src/x86_mscdex.cpp).
    bool MscdexInterrupt(unsigned &ax, unsigned &bx, unsigned &cx, bool &carry);
    bool MscdexRealMode(unsigned call_structure);
    void MscdexRequest(char letter, unsigned header);
    // True while 16-bit code is on the CPU of the current thread.
    bool Win16Code();
    bool Win16InstructionSlice();
    // The printer half of GDI.EXE: a printer driver loaded into this process
    // and its DDI; src/x86_print_driver.cpp.
    struct PrintBlock;
    struct PrintDriver;
    struct PrintDc;
    struct PrintHost;
    PrintHost *print_host = nullptr;
    PrintBlock PrintAllocate(unsigned bytes);
    void PrintRelease(PrintBlock &);
    bool PrintRegistryString(const char *path, const char *value, char *out, unsigned capacity);
    PrintDriver *PrintLoadDriver(const char *name, unsigned &error);
    unsigned PrintCall(PrintDriver &, unsigned ordinal, const unsigned *push, unsigned count, bool &ok);
    PrintDc *PrintFind(unsigned hdc);
    bool PrinterDC(unsigned hdc);
    unsigned PrinterCreateDC(unsigned driver, unsigned device, unsigned output, unsigned mode, bool information);
    bool PrinterDeleteDC(unsigned hdc);
    int PrintControlPair(PrintDc &, unsigned function, unsigned (&pair)[2]);
    static int PrintEscapePair(void *context, unsigned number, unsigned (&pair)[2]);
    void FreePrintHost();
    // The print segment of GDI.EXE behind the gates a printer driver calls;
    // src/win16_print.cpp.
    struct PrintRegistry;
    bool Win16PrintCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned words);
    int PrinterDefaults(unsigned printer, unsigned data, unsigned size_or_kind);
    int PrinterStoreDefaults(unsigned printer, unsigned data, unsigned size);
    unsigned PrinterGetData(unsigned printer, unsigned profile_far, unsigned profile, unsigned type, unsigned data,
                            unsigned size, unsigned needed);
    unsigned PrinterSetData(unsigned printer, unsigned profile_far, unsigned profile, unsigned type, unsigned data,
                            unsigned size);
    unsigned PrinterSpoolGetData(unsigned printer, unsigned profile, unsigned type, unsigned data, unsigned size,
                                 unsigned needed);
    // The spool half: the client of GDI32 that talks to SPOOL32, and the job
    // API of GDI.EXE; src/x86_print_spool.cpp.
    struct SpoolClient;
    struct PrintJob;
    struct PrintJobs;
    SpoolClient *spool = nullptr;
    PrintJobs *print_jobs = nullptr;
    unsigned SpoolShared(unsigned bytes);
    void SpoolSharedFree(unsigned block);
    bool SpoolConnect(bool start);
    unsigned SpoolRecord();
    bool SpoolInProcess();
    unsigned SpoolExport(const char *name);
    unsigned SpoolWait(unsigned handle, bool win16_process);
    bool SpoolRequest(unsigned id, const unsigned *arguments, unsigned count);
    unsigned SpoolCopyIn(unsigned pointer, unsigned bytes, unsigned &owned);
    unsigned SpoolStringIn(unsigned text, unsigned &owned);
    bool SpoolOpenPrinter(unsigned name, unsigned handle_out);
    unsigned SpoolStartDoc(unsigned printer, unsigned level, unsigned info);
    bool SpoolPrinterCall(unsigned id, const char *export_name, unsigned printer);
    bool SpoolWritePrinter(unsigned printer, unsigned data, unsigned bytes, unsigned written);
    bool SpoolAbortPrinter(unsigned printer);
    bool SpoolCancelJob(unsigned printer, unsigned job_id);
    PrintJob *PrintJobOf(unsigned handle);
    bool PrintQueryAbort(unsigned hdc);
    unsigned PrintClassifyPort(const char *port);
    int PrintOpenJob(unsigned output, unsigned title, unsigned hdc);
    int PrintStartSpoolPage(unsigned handle);
    bool PrintFlushJob(PrintJob &job);
    int PrintWriteSpool(unsigned handle, unsigned data, unsigned bytes);
    int PrintEndSpoolPage(unsigned handle);
    int PrintDeleteJob(unsigned handle);
    int PrintCloseJob(unsigned handle);
    unsigned PrintAbortProcedure(PrintDc &dc);
    void FreePrintSpool();
    // The document of a printer DC; src/x86_print_document.cpp.
    unsigned PrintMapFlat(unsigned flat);
    void PrintUnmapFar(unsigned far);
    int PrintControl(PrintDc &, unsigned escape, unsigned in_far, unsigned out_far);
    unsigned PrintDocumentMode(PrintDc &, unsigned fwtype);
    int PrinterStartDoc(PrintDc &, unsigned docinfo);
    int PrinterStartPage(PrintDc &);
    int PrinterEndPage(PrintDc &);
    int PrinterEndDoc(PrintDc &);
    int PrinterAbortDoc(PrintDc &);
    unsigned PrinterDocumentApi(ApiName name, const unsigned *a);
    unsigned PrinterEscape(unsigned hdc, unsigned escape, unsigned in_bytes, unsigned in, unsigned out);
    // Drawing over the driver; src/x86_print_draw.cpp.
    bool PrintWork(PrintDc &);
    unsigned PrintPhysicalColor(PrintDc &, unsigned rgb);
    void PrintDrawMode(PrintDc &);
    int PrintDeviceSize(unsigned hdc, int size, bool vertical);
    unsigned PrintRealize(PrintDc &, int style, unsigned in_far, unsigned out_far, unsigned xform);
    bool PrintRealizePen(PrintDc &);
    bool PrintRealizeBrush(PrintDc &);
    bool PrintRealizeFont(PrintDc &);
    bool PrintPrepare(PrintDc &, bool pen, bool brush, bool font);
    unsigned PrintClipRects(PrintDc &, Gdi::RECT *rects, unsigned capacity);
    bool PrintOutput(PrintDc &, unsigned style, const Gdi::POINT *points, unsigned count, bool pen, bool brush);
    bool PrintDevicePoints(unsigned hdc, unsigned points, unsigned count, Gdi::POINT *out);
    unsigned PrintExtTextOut(PrintDc &, int x, int y, unsigned clip_far, unsigned count, bool measure,
                             unsigned widths_far, unsigned opaque_far, unsigned options);
    bool PrintTextExtent(PrintDc &, const char *text, unsigned count, int &cx, int &cy);
    bool PrintTextMetrics(PrintDc &, Gdi::TEXTMETRICA &);
    bool PrinterDrawApi(ApiName name, const unsigned *a, unsigned &result);
    void PrintReleaseObjects(PrintDc &);
    bool PrinterTextOut(PrintDc &, int x, int y, unsigned options, const Gdi::RECT *rect, const char *text,
                        unsigned count, const int *dx);
    bool PrinterTextMetricsLogical(PrintDc &, Gdi::TEXTMETRICA &);
    bool PrinterTextExtentLogical(PrintDc &, const char *text, unsigned count, Gdi::SIZE &);
    bool PrinterCharWidths(PrintDc &, unsigned first, unsigned last, int *widths);
    bool PrinterPatBlt(PrintDc &, int x, int y, int width, int height, unsigned rop);
    bool PrinterPolyPolygon(PrintDc &, unsigned points, unsigned counts, unsigned polygons, unsigned &result);
    static bool PrintHookTextOut(void *, unsigned, int, int, unsigned, const Gdi::RECT *, const char *, unsigned,
                                 const int *);
    static bool PrintHookMetrics(void *, unsigned, Gdi::TEXTMETRICA *);
    static bool PrintHookExtent(void *, unsigned, const char *, unsigned, Gdi::SIZE *);
    static bool PrintHookWidths(void *, unsigned, unsigned, unsigned, int *);
    static bool PrintHookPatBlt(void *, unsigned, int, int, int, int, unsigned);
    void PrintInstallHooks(PrintDc &);
    struct PrintEnumSink;
    PrintEnumSink *print_enum = nullptr;
    unsigned print_enum_gate = 0;
    static constexpr unsigned PrintEnumOrdinal = 0x10100; // the private gate of GDI
    unsigned PrintEnumDeviceFonts(unsigned hdc, const char *face, unsigned char *rows, unsigned capacity);
    unsigned PrintEnumCallback(const Win16Frame &frame);
    unsigned PrintEngineEnumerate(unsigned face, unsigned callback, unsigned data);
    // GDI32 ordinals 100 to 104; src/x86_print_setup.cpp.
    bool PrintFindDriver(const char *printer, char *model, unsigned model_bytes, char *file, unsigned file_bytes);
    bool PrintRegistryKeyExists(const char *path);
    unsigned PrintSetupLibrary(const char *file);
    unsigned PrintSetupCall(unsigned module, unsigned entry, const unsigned *push, unsigned count, bool &ok);
    unsigned PrinterSetupApi(unsigned ordinal, const unsigned *a);
    bool Win16OwnsGate(unsigned gate) const;
    bool Win16Transition(Import &);
    unsigned Win16GlobalReAlloc(unsigned handle, unsigned size, unsigned flags);
    unsigned Win16LoadNamedLibrary(const char *name);
    // A module handle of our side. Above every selector of the LDT, so it is
    // never mistaken for the module database of a loaded NE image.
    enum : unsigned { Win16SystemHandle = 0xff00 };
    unsigned Win16SystemModule(const char *name);
    const char *Win16SystemModuleName(unsigned handle) const;
    bool Win16SystemModulePath(unsigned handle, char *path, unsigned capacity) const;
    const Win98Ne::Image *Win16SystemImage(unsigned handle);
    // Canonical system-VM bytes; flat windows and Win16 selectors share them.
    bool EnsureSystemVmMemory();
    unsigned char *system_vm_storage = nullptr, *system_vm_memory = nullptr;
    bool system_vm_shared = false;  // system_vm_storage is a LinuxSharedMemory block
    bool Win16ResourceCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    bool Win16RegistryCall(unsigned ordinal, const Win16Frame &, unsigned &result, unsigned &words);
    unsigned Win16LoadLibrary(const char *path, const unsigned char *bytes, unsigned length,
                              unsigned &error);

  private:

    void UpdateWindowTree(unsigned hwnd, unsigned depth); // USER with flag 1, src/x86_user.cpp
    unsigned CopyImageApi(const unsigned *);
    unsigned IconInfoApi(const unsigned *);
    unsigned UserNameApi(const unsigned *);
    unsigned SegmentMapApi(ApiName, const unsigned *);
    unsigned CopyBitmapImage(unsigned handle, unsigned cx, unsigned cy, unsigned flags, bool copy_pixels = true);
    bool DrawResourceBitmap(unsigned target, const unsigned char *data, unsigned bytes, unsigned width, unsigned height, unsigned flags);
    void FreeGdi();
    // Win98 keeps ownership in the GDI heap. This cleanup index grows with
    // the shared object table; it must not impose a separate process limit.
    struct GdiObject { unsigned handle = 0, kind = 0; };
    struct GdiObjects {
        GdiObject *entries = nullptr;
        unsigned capacity = 0;
        GdiObject *begin() { return entries; }
        GdiObject *end() { return entries ? entries + capacity : nullptr; }
    } gdi_objects;
    unsigned NewIcon(WinImage::Image *,unsigned resource=0,bool shared=false);
    struct Icon {WinImage::Image *data=nullptr;unsigned generation=1,resource=0;bool shared=false,cursor=false;unsigned hotspot_x=0,hotspot_y=0;Application *owner=nullptr;};
    static Icon *SharedIcons();
    void FreeIcons();
    Icon *FindIcon(unsigned);
    struct ImageList {bool used=false;unsigned generation=1,width=0,height=0,count=0,background=0xffffffff;WinImage::Image *items[128]={};} image_lists[16];
    ImageList *FindImageList(unsigned);
    static ImageList *SharedImageLists();
    struct ShellFileInfoEnvironment;
    unsigned ShellFileInfoApi(ApiName, const unsigned *);
    struct ShellIconCacheEnvironment;
    unsigned ShellIconCacheApi(unsigned, const unsigned *);
    unsigned ShellBuiltinIcon(unsigned, unsigned, unsigned, unsigned);
    unsigned ShellExtractIconPair(const char *, unsigned, unsigned, unsigned, unsigned, unsigned &, unsigned &);
    unsigned CrtOutputApi(ApiName,const unsigned*);
    unsigned CrtFloatApi(ApiName,const unsigned*);
    bool ReturnDoubleBits(unsigned long long);
    // The x87 and SSE state of a fresh thread. Win32 threads start with
    // control word 027Fh (53-bit precision); the other
    // callers keep FNINIT's 037Fh.
    static constexpr unsigned Win32FpuControl = 0x027f;
    bool InitializeFpu(unsigned control = 0x037f);
    unsigned CrtApi(ApiName, const unsigned *);
    struct CrtTimeEnvironment;
    unsigned CrtTimeApi(ApiName,const unsigned *);
    unsigned CrtTimeExport(const char *);
    unsigned CrtTimeName(bool);
    unsigned CrtTimeFormatApi(ApiName,const unsigned *);
    unsigned CrtSystemClockApi(ApiName,const unsigned *);
    unsigned long long CrtClockValue();
    struct CrtThreads;
    CrtThreads *crt_threads = nullptr;
    bool InitializeThreadCrt();
    unsigned CrtThreadApi(ApiName, const unsigned *);
    unsigned CrtErrno();
    void SetCrtErrno(unsigned);
    void RunCrtThread();
    void FinishCrtThread(unsigned result, bool legacy);
    void DetachCrtThread();
    void ReleaseCrtThreadData(unsigned index);
    void FreeThreadCrt();
    unsigned SystemMetric(unsigned);
    struct CrtLocale;
    CrtLocale *crt_locale = nullptr;
    unsigned LocaleApi(ApiName, const unsigned *);
    unsigned LocaleExport(const char *);
    unsigned LocaleCodePage() const;
    struct ExceptionEnvironment;
    unsigned ExceptionApi(ApiName, const unsigned *);
    unsigned LocaleInfo(const unsigned *);
    unsigned NlsExtendedApi(ApiName,const unsigned *);
    const char *LocaleValue(unsigned,unsigned);
    unsigned DateTimeFormatApi(ApiName, const unsigned *);
    unsigned NumberFormatApi(ApiName,const unsigned *);
    void FreeLocale();
    struct CrtArguments;
    CrtArguments *crt_arguments = nullptr;
    bool InitializeArguments();
    void FreeArguments();
    unsigned ArgumentsApi(ApiName, const unsigned *);
    unsigned ArgumentsExport(const char *);
    Win98CrtMultibyte::State crt_multibyte;
    unsigned crt_data = 0, crt_code_page = 0, crt_app_type = 0, crt_math_error = 0, crt_argv = 0, crt_env = 0;

    unsigned Call(unsigned procedure, const unsigned *arguments, unsigned count);
    // Registers a call sets beyond the arguments, and the ones it reads back.
    struct CallRegisters
    {
        unsigned ebx = 0, ecx = 0, edx = 0;
        bool frame = false; // EBP points at the return address
    };
    unsigned Call(unsigned procedure, const unsigned *arguments, unsigned count, CallRegisters *registers, bool *frame_abandoned = nullptr);
    int EditWordBreak(unsigned procedure, const char *, unsigned current, unsigned length, unsigned code);
    int Win16WordBreakCallback(unsigned procedure, const char *, unsigned current, unsigned length, unsigned code);
    unsigned CallWindow(unsigned, unsigned, unsigned = 0, unsigned = 0);
    unsigned LoadMenu(unsigned id, unsigned module = 0, bool shared = true);
    // A menu resource named by a string, as LoadMenuA and a dialog template
    // name it. src/x86_app.cpp.
    unsigned LoadNamedMenu(const char *name, unsigned module = 0);
    unsigned ParseMenu(const unsigned char *, unsigned, unsigned &, unsigned,bool extended=false);
    unsigned SetFocus(unsigned);
    struct ActivationEnvironment;
    Win98Activation::Task activation_tasks[ThreadCapacity];
    unsigned ActivationApi(ApiName, const unsigned *);
    unsigned ActivateWindow(unsigned, bool mouse = false);
    bool ForegroundPermission();
    unsigned ActivateInQueue(unsigned queue, unsigned hwnd, unsigned flags);
    unsigned SwitchForeground(unsigned hwnd, unsigned flags);
    unsigned SendActivation(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    unsigned SetForegroundApi(unsigned hwnd);
    void HandForegroundOn();
    void LeaveForeground(bool any_queue = false, bool keep_focus = true);
    Win98Activation::Queue OwnQueue() { return User::ThreadActivation(ThreadId()); }
    void WriteOwnQueue(const Win98Activation::Queue &q) { User::WriteThreadActivation(ThreadId(), q); }
    unsigned ShowWindow(unsigned, unsigned);
    unsigned ShowTransition(unsigned hwnd, unsigned command, unsigned before, bool visible);
    bool QueryMinMax(unsigned hwnd, Win98MinMax::Info &info);
    unsigned ActivateNext(unsigned hwnd, unsigned code);
    void ForgetActive(unsigned hwnd);
    bool TaskDeactivating(unsigned hwnd);
    unsigned WaitForInputIdleApi(const unsigned *a);
    Application *ChildApplication(unsigned slot, bool &done);
    Application *ProcessFromHandle(unsigned handle);
    unsigned DuplicateMappingFrom(Application &source, unsigned object);
    unsigned DefaultWindowMessage(unsigned, unsigned, unsigned, unsigned);
    unsigned ContextMenuOfFrame(unsigned hwnd, unsigned lp);
    unsigned DefaultKeyMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    unsigned KeyMenuCommand(unsigned hwnd, unsigned lp);
    unsigned ButtonProcedure(unsigned, unsigned, unsigned, unsigned); // native BUTTON class, token 0x71000030
    unsigned StaticProcedure(unsigned, unsigned, unsigned, unsigned); // STATIC, USER, src/x86_static_control.cpp
    unsigned StaticSetIcon(unsigned hwnd, unsigned icon);
    unsigned StaticSetBitmap(unsigned hwnd, unsigned bitmap);
    void StaticLoadIcon(unsigned hwnd, unsigned create);
    unsigned CreateWindow(const unsigned *, bool mdi_structure = false);
    void InitializeCreatedWindowState(unsigned hwnd);
    void FinishCreatedWindow(unsigned hwnd, bool show, unsigned command);
    unsigned GetText(unsigned, unsigned, unsigned);
    unsigned Edit(unsigned, unsigned, unsigned, unsigned);
    unsigned IndirectDialogApi(ApiName, const unsigned *);
    unsigned CreateDialog(unsigned instance, unsigned resource, unsigned owner, unsigned procedure,
                          unsigned parameter, bool modal = false);
    unsigned CreateDialogTemplate(const unsigned char *, unsigned length, unsigned owner,
                                  unsigned procedure, unsigned parameter, bool modal = false);
    unsigned CreateDialogTemplate(const unsigned char *, unsigned length, unsigned owner,
                                  unsigned procedure, unsigned parameter, bool modal, unsigned instance);
    struct NlsEnvironment;
    struct NlsSystem;
    NlsSystem *nls_system = nullptr;
    Win98SystemLocale::State system_locale;
    Win98DefaultLocale::State default_locale;
    bool InitializeNls();
    void FreeNls();
    // The NLS heap of the session went away with its last holder.
    static void ForgetSessionNls(unsigned heap);
    unsigned DefaultLocaleApi(ApiName);
    unsigned UserDefaultLocale();
    unsigned NlsApi(unsigned ordinal, const unsigned *args);
    struct CharacterTypeEnvironment;
    unsigned StringTypeApi(const unsigned *args);
    unsigned CharacterCaseApi(ApiName,const unsigned*);
    unsigned CompareStringApi(const unsigned*);
    unsigned CharacterApi(unsigned, const unsigned *);
    unsigned WideStringApi(ApiName, const unsigned *);
    struct FindEnvironment;
    Win98Find::State find_state;
    unsigned FindApi(unsigned parameters, bool replace);
    struct FileDialogEnvironment;
    unsigned FileDialogApi(ApiName,const unsigned*);
    unsigned BuildFileDialog(unsigned,bool);
    unsigned FileDialogMessage(unsigned,unsigned,unsigned,unsigned);
    unsigned SerialUiDialog(unsigned,unsigned,unsigned,unsigned);
    unsigned SerialUiEnum(unsigned,unsigned,unsigned,unsigned,unsigned);
    unsigned SerialUiPageCallback(unsigned,unsigned);
    void FreeSerialUiPages();
    unsigned SerialUiMessage(unsigned,unsigned,unsigned,unsigned);
    unsigned FindProcedure(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    // recorded: the caller has no work of its own after the loop, or keeps
    // what that work needs in a record of the thread (Thread::HostFrame).
    unsigned RunModal(unsigned hwnd, bool recorded = false);
    // The kinds of Thread::HostFrame.
    enum HostFrameKind : unsigned
    {
        HostNone,
        HostModal,       // RunModal: a window, b owner, c focus before, d bit 0 owner was disabled, bit 1 recorded caller
        HostDialogBox,   // DialogBoxParam and its family: a owner to enable again, b that owner
        HostMessageBox,  // BuildMessageBox: a the box
        HostDialogProcedure, // DialogProcedure around the procedure of the program: a window, b message, c wParam, d lParam
        HostPopupMenu,   // TrackPopupMenu: a original flags; finish USER's popup session after the loop
        HostMenu,        // RunMenuLoop with a recorded caller
        HostSizeMove,    // DefWindowProc's move/size/scroll command; the tracker owns the remaining work
        HostFrameTrack,  // RunFrameTrackLoop with a recorded caller
        HostFileDialog,  // native file dialog: a window, b 16-bit provider
        HostDialogInit,  // WM_INITDIALOG: a window, b first item, c visible
        HostCreateWindow, // creation phases: a window, b CREATESTRUCT/id, c rectangle, d phase/show
        HostWin16Create, // the edit limit to apply after the common creation
        HostWin16Delivery, // staged callback arguments and the work after its return
        HostSentWait = 15, // synchronous sender wait; typed Thread::sent_wait owns its remaining work
        HostSentDelivery = 16, // received send: restore metadata/active record and finish once
        HostSendTimeout = 17, // completion status and result-address after a synchronous send
        HostSendCopy = 18, // receiver guest allocation retained for a copied send
        HostCrtThread = 14, // root CRT callback: a legacy _beginthread; finish PTD/handle cleanup on return
        HostWin16HostedLoad = 80,
        HostWin16HostedLibrary = 81,
        HostWin16HostedEnter = 82,
        HostWin16HostedCall = 83,
        HostFaultBox = 32, // final exception UI: a window, b slot, c fault depth, d exit code
        HostBarMenu = 33, // a=0 mouse, 1 keyboard, 2 key-release cleanup; b=thread for 2
        HostPropertyApi = 40, // a header scratch, b completed ANSI conversion owned; d=3 input release owns data[0]
        HostPropertySheet = 41, // a sheet state, b window; d presentation reached
        HostSerialDialog = 42, // a controller key; finish DCB/scratch/module ownership
        HostSerialAdvanced = 43, // a controller key, b window; d modal presentation reached
        HostCommConfig = 44, // a provider DLL reference; convert its error to BOOL and release
        HostSerialMessage = 45, // advanced-button handler: return TRUE after its nested dialog
        HostWin16Controller = 38, // staged native hook/message and its controller remainder
        HostWin16Color = 34,
        HostWin16Font = 35,
        HostWin16PrintApi = 36,
        HostWin16Print = 37, // d=0 dialog run, d=1 nested setup rest
        HostPropertyPage = 60, // a Unicode page, b destruction; callback return precedes reference/field cleanup
        HostPropertyCollect = 61, // a collected state, b input cursor, c page index, d inline descriptors
        HostPropertyNotify = 62,
        HostPropertyPageWindow = 63,
        HostPropertyActivate = 64,
        HostPropertyInit = 65,
        HostPropertyRelease = 66,
        HostPropertyProcedure = 67, // typed provider result; phase 2 is exclusively WM_COMMAND/ID_HELP
        HostPropertyModalFinish = 68,
        HostPropertyNonClient = 69,
        HostPropertyReleaseInput = 70, // a header, b decremented reverse cursor; immediately below matching API phase 3
        HostPropertyRemove = 71,
        HostPropertyApply = 72,
        HostPropertyCommand = 73,
        HostPropertyAdd = 75,
        HostPropertyPageNonClient = 76,
        HostPropertyRemoveRequest = 77,
        HostEndTask = 46, // a target task; perform termination after the confirmation
        HostEndTaskDialog = 47, // a confirmation window; normalize its modal result
        HostCloseProgram = 48, // a system dialog, b prior foreground; d modal presentation reached
        HostCloseProgramMessage = 49, // a Close Program window; close it only when EndTask succeeds
        HostPifSheet = 50, // a guest-owned module/header/page/template resource block; d modal phase
        HostPifAdvanced = 51, // a Advanced Program Settings window; d modal phase
        HostPifAdvancedMessage = 53, // a page index, b page window; refresh controls after Advanced
        HostDosToolbar = 54, // a DOS window; stop its temporary service timer and clear sheet/mouse flags
        HostSettingsApi = 55, // a saved GetMessage/PeekMessage argument block around host settings requests
        HostDosUiRoot = 56, // only DOS thread zero after window/session/sound initialization; a its window
        HostWin16Sheet = 90, // heap-owned converted pages/templates and the remaining release loop
        HostWin16PageDestroy = 91, // far page whose release callback must return before GlobalFree
        HostWin16PageProcedure = 92, // HWND/message; narrow the result and retire WM_NCDESTROY registration
        HostPropertyQuery = 74, // typed current page and both sibling-query parameters
        HostWin16FileMessage = 93, // a file HWND; typed operation keeps hook/dispatch phase
        HostWin16FileAccept = 94, // accepted path and attributes across Create/Overwrite/Warning
        HostWin16FileOutput = 95, // output fields already written; consume FileOK and end once
        HostWin16FileSelect = 96, // selection fields already set; return after registered hook
        HostWin16FileDrive = 97, // failed drive warning; rebuild original combo on return
        HostButtonCommand = 99, // return zero after the parent notification
        HostDispatchPaint = 98, // a window, b/c paint parameters, d prior posted-message flag
    };
    bool PushHostFrame(unsigned kind, unsigned a = 0, unsigned b = 0, unsigned c = 0, unsigned d = 0);
    void PopHostFrame();
    // After a load, in the place of the call a gate stands for: the frames
    // of the host this thread had are entered again, and the call's answer
    // comes back as if it had never been interrupted.
    unsigned ResumeHostCall();
    unsigned ResumeWin16ControllerPhase();
    unsigned ResumeSerialUiDialog(unsigned key, bool advanced);
    unsigned FinishSerialUiDialog(unsigned key, unsigned result, bool advanced);
    unsigned FinishCommConfigDialog(unsigned result, unsigned module);
    unsigned ConfirmEndTask(unsigned owner, unsigned task, bool recorded);
    unsigned FinishEndTask(unsigned result, unsigned task);
    bool SavedCloseProgramThread(unsigned index) const;
    void ResumeCloseProgramDialog();
    unsigned FinishCloseProgramDialog(unsigned answer, unsigned previous);
    void FinishPifPropertySheet(unsigned resources);
    void FinishPifAdvancedDialog();
    void FinishPifAdvancedMessage(unsigned index, unsigned hwnd);
    void FinishDosVmToolbar(unsigned window);
    bool SavedDosUiThread(unsigned index) const;
    void RunDosVmLoop();
    void ResumeDosVm();
    bool SavedFaultBoxThread(unsigned index) const;
    bool ResumeFaultBox();
    void DiscardSavedMessageBoxes();
    void DiscardSavedFileDialogs();
    void DiscardSavedWin16FileDialogs();
    void DiscardSavedWin16ColorDialogs();
    void DiscardSavedWin16FontDialogs();
    void DiscardSavedWin16PrintDialogs();
    unsigned FinishWin16PrintDialog(unsigned hwnd, bool setup, unsigned result);
    unsigned FinishWin16PrintApi(unsigned pd, unsigned scratch, unsigned result);
    unsigned FinishWin16PrintSetup(unsigned hwnd, unsigned result);
    bool Win16ChoiceDispatchResumable(unsigned hwnd, unsigned procedure, unsigned message, unsigned wp) const;
    bool FinishWin16ChoicePreparation(unsigned hwnd, unsigned kind);
    unsigned FinishWin16ColorDialog(unsigned hwnd, unsigned result);
    unsigned FinishWin16FontDialog(unsigned hwnd, unsigned result);
    unsigned FinishFileDialog(unsigned hwnd, unsigned result);
    unsigned FinishWin16FileDialog(unsigned hwnd, unsigned result, bool entered);
    void FinishWin16FilePreparation(unsigned hwnd);
    struct Win16FileWork;
    unsigned ResumeWin16FileWork(unsigned kind, unsigned index);
    bool Win16FileWorkRecorded(unsigned thread, unsigned index, unsigned kind, unsigned phase) const;
    bool Win16FileDispatchResumable(unsigned hwnd, unsigned message) const;
    unsigned FinishDialogInit(unsigned hwnd, unsigned first, bool visible, unsigned result);
    unsigned ContinueCreateWindow(bool resumed);
    unsigned FinishWin16Delivery(unsigned record, unsigned result);
    unsigned Win16StateWords(const Import &) const;
    unsigned FinishMessageBox(void *frame, unsigned result);
    unsigned ResumePropertyApi(unsigned scratch, bool ansi);
    unsigned ResumePropertyPage(unsigned page, bool destroy);
    unsigned ContinuePropertyCollect(unsigned state, unsigned header, bool ansi, bool resumed);
    struct PropertyWork;
    unsigned ResumePropertyWork(unsigned kind, unsigned index);
    bool PropertySheetRecordable(unsigned hwnd,unsigned message,unsigned wp,unsigned lp);
    bool PropertyWorkRecorded(unsigned thread,unsigned index,unsigned kind,unsigned phase) const;
    void FinishPropertyNonClient(unsigned hwnd);
    unsigned ContinuePropertyInit(PropertyWork*,bool);
    unsigned ContinuePropertySheet(PropertyWork*,bool);
    unsigned ContinuePropertyReleaseInput(unsigned header,bool resumed=false,bool recorded=false);
    unsigned ResumePropertySheet(unsigned state);
    void FinishPropertySheet(unsigned state);
    unsigned MenuTrackingApi(ApiName,const unsigned*);
    unsigned RunMenuLoop();
    unsigned FinishBarMenu(unsigned result, bool keyboard);
    // The nested pump of the move/size and scrollbar trackers, and the two
    // messages those trackers hand a rectangle to.
    unsigned RunFrameTrackLoop();
    unsigned FrameTrackRect(unsigned hwnd, unsigned message, unsigned wparam, Gdi::RECT *rect);
    void FrameTrackCommit(unsigned hwnd, int x, int y, int width, int height, unsigned flags);
    // Owner-draw menu items: the original MEASUREITEMSTRUCT and DRAWITEMSTRUCT
    // layouts in guest memory, delivered over the synchronous send path.
    void MeasureMenuItem(unsigned owner, unsigned id, unsigned data, int *cx, int *cy);
    void DrawMenuItem(unsigned owner, const unsigned *fields);
    bool DialogMessage(unsigned hwnd, User::Message &);
    // Parts of IsDialogMessage, src/x86_dialog.cpp.
    unsigned DialogCode(unsigned hwnd, User::Message &);                 // WM_GETDLGCODE with the MSG
    void DialogSetFocus(unsigned hwnd);                                  //
    bool EditDialogKey(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    void DialogDefaultButton(unsigned dialog, unsigned old, unsigned now); //
    unsigned DialogFindMnemonic(unsigned dialog, unsigned hwnd, unsigned character); //
    unsigned DialogGotoMnemonic(unsigned dialog, unsigned hwnd, unsigned character); //
    bool DialogCharacter(unsigned dialog, User::Message &);
    unsigned DialogProcedure(unsigned, unsigned, unsigned, unsigned);
    unsigned DialogDefault(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    unsigned MessageBoxApi(ApiName,const unsigned*);
    unsigned ShowMessageBox(const Win98MessageBox::Parameters&);
    unsigned BuildMessageBox(const Win98MessageBox::Parameters&,const Win98MessageBox::Buttons&);
    bool MessageBoxMessage(unsigned,unsigned,unsigned,unsigned,unsigned&);
    bool MessageBoxWindow(unsigned);
    unsigned MessageBox(unsigned owner, const char *text, const char *title, unsigned flags);
    void ReportMissingModule();
    void RecordMissingImport(const char *name);
    void RecordMissingExport(const char *importer, const char *exporter, const char *name, unsigned ordinal);
    void EndDialog(unsigned hwnd, unsigned result);
    void DrawControl(unsigned parent, unsigned control, unsigned dc, const Gdi::RECT &, unsigned state);
    struct Dialog
    {
        unsigned hwnd = 0, procedure = 0, owner = 0, font = 0, previous_focus = 0, default_id = 1, result = 0,
                 xunit = 6, yunit = 13, find_data = 0, saved_focus = 0;
        // Whether closing the dialog should re-enable its owner. Preserve an
        // owner that was already disabled before the dialog opened.
        bool ended = false, modal = false, restore_owner = false, show_pending = false;
    };
    // Win98 keeps the state of a dialog in the extra window bytes of class
    // #32770, so a process can hold as many dialogs at once as it can hold
    // windows; there is no table and no count. This set is the equivalent:
    // every record has its own allocation, so the pointer a caller holds
    // across a guest callback stays valid while the set grows, and a record
    // whose window is gone is reused. It used to be a fixed table of sixteen.
    struct DialogSet
    {
        Dialog **records = nullptr;
        unsigned count = 0, capacity = 0;
        Dialog *Find(unsigned hwnd) const;
        Dialog *Take();
        void Release();
    } dialogs;
    Dialog *FindDialog(unsigned);

    // Now is the clock of the machine, which stands during a pause; HostNow
    // runs through it and belongs to the wall clock alone. PauseMachine is
    // the one switch of the pause (src/x86_threads.cpp).
    static unsigned long long Now();
    // The clock of a paused machine set to a value: a loaded state goes on
    // at the time it was saved at (src/x86_state_machine.cpp).
    static void SetNow(unsigned long long machine_microseconds);
    static unsigned long long HostNow();
    static void PauseMachine(bool pause);
    static bool MachinePaused();
    static unsigned long long PausedMicroseconds();
    // The Win98 millisecond clock (src/win98_system_clock.h):
    // TickCount is GetTickCount, the VMM cell as the timer interrupts left
    // it; SystemTime is Get_System_Time (timeGetTime), which updates the
    // cell first; ArmTimeout is what a VMM timeout of that length does to it.
    static unsigned TickCount();
    static unsigned SystemTime();
    static void ArmTimeout(unsigned milliseconds);
    static bool ChangeMinimumPeriod(unsigned period, bool begin);
    struct Timer
    {
        bool used = false, pending = false, caret = false, wait_observed=false;
        unsigned hwnd = 0, id = 0, thread = 0, procedure = 0, interval = 0;
        unsigned long long deadline = 0;
        User::Message message = {};
    } timers[64];
    unsigned next_timer = 0x1000;
    // A timer of this port's own (the DOS window's service timer while its
    // property sheet runs) keeps the period it asks for; the next SetTimer
    // reads and clears the flag. Guest timers get the floor of Win98's
    // system timer (TimerApi).
    bool exact_timer_request = false;
    struct CaretEnvironment;
    Win98Caret::State caret;
    bool caret_edit=false,caret_edit_shown=false;
    void EditCaret(unsigned,bool);
    unsigned caret_blink=500;
    unsigned CaretApi(ApiName,const unsigned*);
    void CaretPulse();void FreeCarets();void CaretWindowDestroyed(unsigned);
    unsigned DrawCaret(unsigned,unsigned);
    bool CaretPaintBracket(unsigned,bool);
    void XorCaret(const Win98Caret::State&);
    // Inversions of the caret kept while another thread has the screen
    // locked (src/x86_caret.cpp).
    struct CaretInversion {
        unsigned owner = 0, bitmap = 0;
        int x = 0, y = 0, width = 0, height = 0;
        bool operator==(const CaretInversion &o) const
        {
            return owner == o.owner && bitmap == o.bitmap && x == o.x && y == o.y && width == o.width && height == o.height;
        }
    };
    CaretInversion caret_owed[8];
    unsigned caret_owed_count = 0;
    void InvertCaret(const CaretInversion &);
    void FlushCaretInversions();
    unsigned TimerApi(ApiName, const unsigned *);
    bool TimerReady(unsigned thread) const;
    unsigned MultiwaitApi(ApiName,const unsigned*);
    struct MultiwaitEnvironment;
    unsigned WaitQueueStatus(unsigned,bool=false,unsigned=~0u);
    unsigned QueueStatusApi(ApiName,const unsigned *);
    bool MultiwaitReady(unsigned);
    unsigned AcquireWaitObject(unsigned,unsigned);
    bool TimerMessage(User::Message &, unsigned hwnd, unsigned minimum, unsigned maximum, bool remove);
    struct ShellThreadEnvironment;
    struct ShellOsEnvironment;
    struct ShellModuleEnvironment;
    unsigned ShellModuleApi(unsigned, const unsigned *);
    struct ShellStopwatchEnvironment;
    bool AttachShellStopwatch();
    void FreeShellStopwatch();
    unsigned ShellStopwatchApi(unsigned);
    unsigned ShellOsApi(unsigned);
    void FreeShellOs();
    unsigned ShellThreadApi(ApiName,const unsigned *);
    void RunShellThread();
    void FreeShellThreads();
    struct ShellProcessReferenceEnvironment;
    unsigned ShellProcessReferenceApi(ApiName,const unsigned *);
    void FreeShellProcessReference();
    struct SendState;
    bool HasSentMessages(unsigned);
    unsigned SentQueueStatus(unsigned,bool);
    bool SentMessageReady(unsigned);
    void SentWaitText(unsigned index, char *out, unsigned capacity);
    struct SendWait;
    struct SendDelivery;
    unsigned ContinueSentWait(SendWait *, bool resumed, bool *completed = nullptr);
    unsigned ResumeSentWait(unsigned process, unsigned slot, unsigned low, unsigned high);
    unsigned FinishSentDelivery(SendDelivery *, unsigned result);
    unsigned ResumeSentDelivery(unsigned process, unsigned slot, unsigned low, unsigned high);
    bool SentWaitGate(unsigned gate) const;
    bool SentWaitGate() const { return SentWaitGate(pending); }
    unsigned FinishSendTimeout(unsigned output, unsigned result, bool completed);
    unsigned FinishSendCopy(unsigned process, unsigned block, unsigned result, unsigned output = 0, unsigned capacity = 0);
    void ProcessSentMessages();
    unsigned SendCrossThread(unsigned,unsigned,unsigned,unsigned,unsigned timeout=0,unsigned flags=0,bool *completed=nullptr);
    // SendNotifyMessage to a window of another thread: queued, no wait.
    unsigned SendNotify(unsigned hwnd,unsigned message,unsigned wp,unsigned lp);
    unsigned SentMessageApi(ApiName,const unsigned *);
    void FreeSentMessages();
    unsigned ThreadApi(ApiName, const unsigned *);
    unsigned KernelAsyncApi(ApiName, const unsigned *);
    bool EnqueueApc(unsigned,unsigned,const unsigned *,unsigned,unsigned);
    bool DeliverApcs();
    unsigned KeyboardLayoutApi(ApiName, const unsigned *);
    struct KeyboardLayoutEnvironment;
    Win98KeyboardLayout::State keyboard_layout_state{1,1,0,0,{{0x04090409,1,0}}};
    struct ThreadControlEnvironment;
    unsigned ThreadControlApi(ApiName, const unsigned *);
    void InitializeThreadControl(unsigned index);
    void KernelThreadLock(bool acquire, bool global);
    void PreemptThreadControl();
    void ScanThreadPriorities();
    void AccountThreadQuantum(unsigned index, unsigned long long began);
    unsigned process_priority_base = 8, next_control_event = 1;
    unsigned CreateThread(const unsigned *, unsigned crt_start_data = 0);
    static Fiber *NewThreadFiber();
    static void FreeThreadFiber(Fiber *);
    unsigned Teb() const;
    bool Runnable(unsigned);
    void ReleaseThread(unsigned);
    bool Signaled(unsigned, unsigned thread);
    unsigned MutexApi(ApiName, const unsigned *);
    unsigned SemaphoreApi(ApiName, const unsigned *);
    void AbandonMutexes(unsigned thread);
    struct CriticalEnvironment;
    struct TlsEnvironment;
    // The record of a section. A section from InitializeCriticalSection keeps
    // its record in the session's record arena, guest memory at the address
    // that is its identity, so the guest code of EnterCriticalSection and
    // LeaveCriticalSection can take the uncontended case itself; the private
    // kernel locks keep theirs here. src/x86_critical.cpp.
    struct Critical
    {
        unsigned id = 0;
        Win98Critical::Mutex own;
        Win98Critical::Mutex &data;
        Critical *next = nullptr;
        unsigned waiters[16] = {}, waiting = 0;
        explicit Critical(Win98Critical::Mutex *record = nullptr) : data(record ? *record : own) {}
        Critical(const Critical &) = delete;
        Critical &operator=(const Critical &) = delete;
        ~Critical();
    } critical_system, critical_threads, critical_tls, critical_crt_exit, critical_crt_locale, critical_crt_multibyte, critical_path, critical_path_task;
    unsigned tls_bitmap[3] = {};
    unsigned crt_heap = 0;
    unsigned TlsApi(ApiName, const unsigned *);
    Critical *critical_buckets[64] = {};
    unsigned critical_serial = 1, critical_list = 1;
    static Critical *global_critical_buckets[64];
    static unsigned next_critical_identity;
    unsigned win16_lock = 0;
    unsigned Win16LockRecord();
    // The lock of the screen while a program has the primary surface locked:
    // which calls touch the screen's pixels and wait for it in another
    // thread. src/x86_screen_lock.cpp.
    static unsigned char ScreenPixelRule(ApiName name);
    bool ScreenPixelsTouched(unsigned rule, const unsigned *args);
    bool ScreenWait(unsigned rule, const unsigned *args);
    void WaitScreenFree();
    // Set by the Win16 gate for its first Api call, the one that does
    // what the 16-bit program asked for (Application::Api, ScreenWait).
    bool win16_screen_check = false;
    // The Win16 gate that runs, for the name in a report of the guard.
    const char *win16_gate_dll = nullptr, *win16_gate_name = nullptr;
    // The guard behind the list of ScreenPixelRule (Gdi::SetScreenGuard): a
    // call of another thread that touches the screen's pixels while the lock
    // is held, without having waited at its gate, is a call the list or a
    // deferral of USER misses. It is reported once per call as
    // "SCREEN LOCK MISS" (at most 256 lines) and counted; tools/screen_pixel_exports.py and
    // tools/test_ddraw_win16lock_user_guest.py read the report.
    static void ScreenTouched(bool write);
    void NoteScreenMiss(bool write);
    static bool ScreenRulesEnabled();
    unsigned screen_miss_count = 0, screen_miss_lines = 0, screen_miss_call = 0;
    const char *screen_miss_name = nullptr;
    unsigned ThunkLockCount();
    void ThunkLockCount(unsigned);
    void PromoteCritical(unsigned);
    Critical *FindCritical(unsigned);
    bool CriticalReady(unsigned, unsigned);
    bool HoldsCriticalSection();
    void WaitCritical(unsigned);
    void WakeCritical(unsigned);
    bool CriticalSession(const Critical *) const;
    bool CriticalThreadGone(unsigned, bool) const;
    void SweepCriticalWaiters(Critical *);

    void FreeCritical();
    void ReleaseHeapMutex(unsigned);
    unsigned PointerProbeApi(ApiName, const unsigned *);
    Win98PointerProbe::State pointer_probe_state;
    unsigned TimeApi(ApiName, const unsigned *);
    struct TimezoneEnvironment;
    unsigned TimezoneApi(ApiName, const unsigned *);
    unsigned EffectiveTimeBias();
    unsigned long long CurrentFileTime();
    static Files::TimeStamp FileClock();
    unsigned FileTimeApi(ApiName,const unsigned*);
    unsigned CriticalApi(ApiName, const unsigned *);
    void CrtLock(bool acquire, unsigned id = 0);
    unsigned NewKernelHandle(unsigned, unsigned);
    unsigned LocalHandleApi(ApiName, const unsigned *);
    struct ToolhelpState;
    ToolhelpState *toolhelp = nullptr;
    unsigned process_flags = 0;
    unsigned ToolhelpApi(ApiName,const unsigned *);
    void FreeToolhelp();
    void ReferenceSnapshot(unsigned);
    void ReleaseSnapshot(unsigned);
    struct LzState;
    LzState *lz_state = nullptr;
    unsigned LzService(unsigned index,const unsigned *args);
    unsigned LegacyFileHandleApi(ApiName,const unsigned *);
    unsigned LegacyThunkConnect(const unsigned *);
    // Ordinal-only KERNEL32 exports 1..60.
    unsigned KernelOrdinalApi(unsigned ordinal, const unsigned *);
    unsigned VxDCallApi(const unsigned *);
    unsigned UnimpStubApi();
    unsigned MappedBufferApi(bool allocate);
    unsigned ThunkTransitionStop(unsigned ordinal);
    unsigned OemStringApi(unsigned ordinal, const unsigned *);
    Win98Selectors::Table selectors;
    unsigned selector_nodes = 0;
    unsigned SegmentMap(unsigned pointer, bool arena = false);
    unsigned SegmentNode(unsigned segmented);
    void SegmentUnmapNode(unsigned node);
    unsigned SegmentSlotLinear(unsigned slot_value);
    unsigned Win16GlobalApi(unsigned ordinal, const unsigned *);
    bool BuildOemTables();
    bool oem_tables_ready = false;
    unsigned char ansi_high[128] = {}, oem_high[128] = {};
    unsigned win16_dos_environment = 0;
    struct Win16Global { unsigned handle = 0, pointer = 0, size = 0, flags = 0, locks = 0, segment = 0; } win16_globals[64];
    void FreeLzState();
    struct CommState;
    struct CommOwner { CommState *state = nullptr; ~CommOwner(); } comm;
    // The parallel port, LPT1, whose far end is the host printer sink
    // (src/x86_kernel_lpt.cpp, src/printer_port.h).
    struct LptState;
    struct LptOwner { LptState *state = nullptr; ~LptOwner(); } lpt;
    unsigned LptPort(unsigned);
    bool LptHandle(unsigned) const;
    unsigned LptApi(ApiName, const unsigned *);
    unsigned CommApi(ApiName, const unsigned *);
    unsigned CommPort(unsigned);
    bool CommReady(unsigned) const;
    void CommPump();
    struct PipeState;
    PipeState *pipes = nullptr;
    unsigned PipeApi(ApiName,const unsigned *);
    unsigned DuplicatePipe(const unsigned *);
    bool OwnsPipe(unsigned) const;
    bool PipeReady(unsigned,bool) const;
    bool InheritPipes(const Application &);
    void FreePipes();
    unsigned KeyboardGameApi(ApiName,const unsigned *);
    struct MultimediaState;
    MultimediaState *multimedia = nullptr;
    void FreeMultimedia();
    // WINMM: the timer family and the entries that answer without a device.
    unsigned MultimediaTimerApi(ApiName,const unsigned *);
    void RunMultimediaTimerThread();
    void FreeMultimediaTimers();
    unsigned MultimediaDeviceApi(ApiName,const unsigned *);
    void JoystickRegistry(const AH_Status &status);
    void JoystickForceFeedback(unsigned machine, unsigned oem);
    unsigned MultimediaDeferredApi(ApiName,const unsigned *);
    // WINMM's table of installable drivers : one 18h-byte entry per
    // OpenDriver, the handle is the one-based index. src/x86_mm_driver.cpp.
    struct InstallableDriver
    {
        unsigned module = 0, process = 0, procedure = 0, driver_id = 0;
        unsigned short flags = 0, usage = 0;
    };
    InstallableDriver installable_drivers[64];
    unsigned InstallableDriverApi(ApiName,const unsigned *);
    InstallableDriver *InstallableDriverOf(unsigned handle);
    unsigned InstallableDriverSend(unsigned handle, unsigned message, unsigned first, unsigned second);
    unsigned InstallableDriverRelease(unsigned handle, bool disable);
    unsigned InstallableDriverOpen(const unsigned *a, bool wide);
    // WINMM: wave output and PlaySound on the mixer of src/sound.h, and the
    // callbacks the multimedia thread of the process delivers for them.
    // src/x86_wave_out.cpp.
    unsigned WaveOutApi(ApiName,const unsigned *);
    unsigned SoundApi(ApiName,const unsigned *);
    // The shared handle store of WINMM. Its records are the 4Ch byte blocks
    // every entry of the family probes, and they live in the session arena
    // because WINMM keeps them in a heap it creates with HEAP_SHARED.
    bool JoinMultimediaHandles();
    unsigned AllocateMultimediaHandle(unsigned bytes);
    void FreeMultimediaHandle(unsigned address,unsigned bytes);
    bool WaveOutDeviceOfHandle(unsigned,void *&);
    void WaveOutNotify(void *,unsigned,unsigned);
    unsigned WaveFormatOf(unsigned,Sound::Format &);
    unsigned WaveOutCapabilities(unsigned,unsigned);
    unsigned PreferredWaveOutDevice(unsigned &);
    unsigned MappedMidiOutDevice();
    unsigned PlaySoundWorker(unsigned,unsigned,unsigned);
    void ClosePlayedSound(void *);
    bool SoundOfAlias(const char *,char *,unsigned,bool application=false);
    unsigned ReadSoundFile(const char *,unsigned char *,unsigned);
    unsigned ReadNamedSound(const char *,unsigned char *,unsigned);
    unsigned SubmitSound(void *,const unsigned char *,unsigned,bool loop,unsigned tag);
    void WaitForPlayedSound(void *);
    // SystemStart and SystemExit, played as MSGSRV32.EXE's own sounds and not
    // as a sound of the calling process. src/x86_wave_out.cpp.
    unsigned PlaySessionSound(const char *alias, bool wait);
    // WINMM: MIDI output and the MIDIEVENT stream on the synthesizer of
    // src/sound_synth.h. src/x86_midi_out.cpp.
    unsigned MidiOutApi(ApiName,const unsigned *);
    bool MidiDeviceOfHandle(unsigned,unsigned,void *&);
    void MidiNotify(void *,unsigned,unsigned);
    unsigned MidiOutCapabilities(unsigned,unsigned);
    unsigned OpenMidiDevice(unsigned,unsigned,unsigned,unsigned,void *&);
    unsigned MidiHeaderCheck(unsigned,unsigned);
    bool ReadStreamEvent(unsigned,unsigned,void *);
    void PumpMidiStream(void *);
    void RetireMidiHeader(void *,unsigned);
    void PumpMidiCallbacks();
    bool MidiActive();
    void FreeMidiOut();
    // MCI for waveaudio and sequencer. src/x86_mci.cpp.
    unsigned MciSendCommand(ApiName,const unsigned *);
    unsigned MciSendString(ApiName,const unsigned *);
    unsigned MciDeviceIdentifier(ApiName,const unsigned *);
    unsigned MciCommand(unsigned,unsigned,unsigned,unsigned *,unsigned);
    unsigned OpenMciDevice(const char *,const char *,const char *,unsigned,unsigned &,const unsigned *block=nullptr,unsigned count=0);
    // Devices of original 16-bit MCI drivers (src/x86_mci.cpp).
    unsigned OpenMciDriverDevice(const char *kind, const char *driver, const char *element, const char *alias,
                                 unsigned flags, const unsigned *block, unsigned count, unsigned &identifier);
    unsigned MciDriverCommand(void *device, unsigned message, unsigned flags, unsigned block);
    // A command string for a device a 16-bit driver serves, parsed by the
    // command tables as MMSYSTEM parses it (src/x86_mci.cpp).
    unsigned MciDriverString(void *device, const void *words, char *answer, unsigned capacity, unsigned callback);
    unsigned MciSysInfo(unsigned identifier, unsigned flags, unsigned block);
    void MciProfile(const char *section, const char *key, char *out, unsigned capacity, bool system);
    bool MciInstalled(const char *name);
    bool MciHelperCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words);
    void CloseMciDevice(void *);
    unsigned ParseSequence(void *,unsigned);
    unsigned MciToBytes(void *,unsigned);
    unsigned MciFromBytes(void *,unsigned);
    void PumpMciDevices();
    bool MciActive();
    void FreeMci();
    unsigned ReadGuestWord(unsigned);
    static bool ReadGuestBytes(void *,unsigned char *,unsigned);
    void PumpWaveCallbacks();
    void CompleteWaveBlocks(void *device);
    bool WaveOutActive();
    void FreeWaveOut();
    // WINMM: wave input on the microphone of src/sound.h. src/x86_wave_in.cpp.
    unsigned WaveInApi(ApiName,const unsigned *);
    bool WaveInDeviceOfHandle(unsigned,void *&);
    void WaveInNotify(void *,unsigned,unsigned);
    void WaveInReturnHead(void *);
    void FillWaveIn(void *);
    void PumpWaveIn();
    bool WaveInActive();
    void FreeWaveIn();
    unsigned WaveInCapabilities(unsigned,unsigned);
    unsigned PreferredWaveInDevice(unsigned &);
    void EnsureMultimediaThread();
    unsigned MultimediaErrorText(unsigned,unsigned,unsigned);
    bool MultimediaDeviceOfHandle(unsigned,unsigned,unsigned &);
    struct ConsoleState;
    ConsoleState *console = nullptr;
    unsigned console_handles[64]{};
    unsigned console_ctrl_handlers[32]{}, console_ctrl_count = 0;
    bool console_ignore_ctrl_c = false;
    bool OwnsConsole(unsigned handle) const;
    bool InheritConsole(const Application &, bool inherit, unsigned flags);
    unsigned OpenConsoleFile(const unsigned *);
    unsigned standard_handles[3]{}, console_generation = 1;
    unsigned ConsoleApi(ApiName, const unsigned *);
    bool ConsoleReady() const;
    void ConsoleKey(unsigned,unsigned,bool,unsigned,bool);
    void FreeConsoleState();
    unsigned ProcessDwordApi(const unsigned *);
    // KERNEL32 ordinal-only exports 61..122.
    struct KernelOrdinalsBState;
    KernelOrdinalsBState *kernel_ordinals_b = nullptr;
    unsigned KernelOrdinalsBApi(unsigned, const unsigned *);
    void FreeKernelOrdinalsB();
    struct KernelHandle
    {
        unsigned kind = 0, object = 0, generation = 1, access = 0x1fffff;
        bool inherit = false;
        // The process a thread handle (kind 2) belongs to, when it is not this
        // one. DuplicateHandle carries a thread across processes in Windows 98,
        // and the thread table is the only per-process one of the kernel
        // objects. Null means this process. src/x86_threads.cpp.
        Application *owner = nullptr;
    } kernel_handles[KernelHandleCapacity];
    // A handle value: 60h in the top byte, the generation of the slot in
    // the next 14 bits and the slot number from one in the low ten bits.
    // Numbers above KernelHandleCapacity are the session's global handles.
    static constexpr unsigned KernelHandleValue(unsigned number, unsigned generation)
    {
        return 0x60000000u | ((generation & KernelGenerationMask) << 10) | number;
    }
    static constexpr unsigned KernelHandleNumber(unsigned value) { return value & 0x3ff; }
    static constexpr unsigned KernelHandleGeneration(unsigned value) { return (value >> 10) & KernelGenerationMask; }
    // The answer FindKernelHandle gives for the current-thread pseudo-handle
    // FFFFFFFEh. It is not a table entry, so it carries no generation and
    // cannot be closed; see src/x86_threads.cpp.
    KernelHandle current_thread_handle;
    KernelHandle current_process_handle;
    // ConvertToGlobalHandle duplicates supported objects into the
    // session-wide handle table and encodes the handle with 453A4D3Ch.
    // Events, mutexes, semaphores, mappings, and processes can be shared this
    // way. DirectSound uses shared events to communicate with DDHELP.
    struct GlobalKernelHandle
    {
        unsigned handle = 0;
        KernelHandle slot;
    };
    static GlobalKernelHandle global_kernel_handles[64];
    unsigned PublishGlobalHandle(unsigned);
    KernelHandle *FindKernelHandle(unsigned);
    Application *ThreadHandleOwner(const KernelHandle *);
    void TickWaitableTimers();
    // TickWaitableTimers runs from Runnable, sixteen times per scheduler pass,
    // and its scan over every kernel object slot was 3 to 5 percent of a game
    // profile. A process that armed no timer skips the
    // scan; one whose armed timers are all relative skips it until the
    // earliest of them is due. SetWaitableTimer clears both marks.
    bool timers_armed = false;
    unsigned long long timers_next_due = 0;
    void CancelTimerApcs(unsigned);
    unsigned WaitableTimerApi(ApiName,const unsigned *);
    // Events, mutexes, semaphores and file mappings live in one session-wide
    // namespace, the way KERNEL32 keeps them in its shared arena above
    // 80000000h. A record belongs to no process; a process only owns handles
    // to it.
    // Events, mutexes and semaphores of the session. Win98 keeps them in
    // the shared arena without a small limit; the IE 5 setup with its
    // CRYPT32 watchers holds well over a hundred events at once.
    static constexpr unsigned KernelObjectSlots = 1024;
    // counts to 0x104 and then tests for the terminator,
    // so 260 characters are still a name and 261 are not. The over-long name
    // leaves with -1 and Windows 98 answers ERROR_INVALID_NAME.
    static constexpr unsigned KernelObjectNameLimit = 260;
    static constexpr unsigned KernelObjectNameBytes = 262;
    struct Event
    {
        unsigned references = 0;
        bool manual = false, signaled = false, named = false;
        bool timer=false,active=false,absolute=false;
        unsigned period=0,callback=0,argument=0,thread=0,thread_id=0;
        unsigned long long due=0;
        // A waitable timer's callback and its thread belong to one process.
        Application *timer_owner = nullptr;
        char name[KernelObjectNameBytes] = {};
    };
    static Event events[KernelObjectSlots];
    struct KernelMutex
    {
        unsigned references = 0, owner = 0, recursion = 0;
        bool abandoned = false, named = false;
        char name[KernelObjectNameBytes] = {};
    };
    static KernelMutex kernel_mutexes[KernelObjectSlots];
    struct Semaphore
    {
        Win98Semaphore::State data{};
        unsigned references = 0;
        bool named = false;
        char name[KernelObjectNameBytes] = {};
    };
    static Semaphore semaphores[KernelObjectSlots];
    // Kinds as in KernelHandle::kind: 1 event/timer, 3 mutex, 4 semaphore,
    // 5 file mapping. Zero means the name is free.
    static unsigned NamedObjectKind(const char *name);
    static unsigned FindNamedObject(unsigned kind, const char *name);
    static bool MappingNameTaken(const char *name);
    void FreeKernelObjectHandles();
    // Kernel handle kind 8: the original 12-byte type-9 change-notification
    // object. "signaled" is its manual-reset ring-0 event at offset 4; "dos"
    // is the IFS search handle at offset 8, with the watch state the native
    // RAM-volume provider keeps for it.
    struct ChangeNotification
    {
        unsigned references = 0, dos = 0, node = 0, filter = 0;
        bool subtree = false, signaled = false;
    } change_notifications[16];
    bool change_observer_installed = false;
    struct ChangeEnvironment;
    unsigned ChangeNotificationApi(ApiName, const unsigned *);
    static void ObserveFileChange(void *context, unsigned directory, unsigned change);
    void NoteFileChange(unsigned directory, unsigned change);
    bool WatchesDirectory(const ChangeNotification &, unsigned directory) const;
    void CloseChangeNotification(unsigned object);
    void FreeChangeNotifications();
    struct ExceptionTask
    {
        // SEH dispatch can yield; stack marks belong to this thread.
        bool calling_exception_handler = false;
        unsigned handler_frame[32] = {}, handler_frames = 0, abandon_from = ~0u;
        unsigned abandoned_callbacks = 0;
    };
    struct Thread
    {
        User::MetadataTask message_metadata;
        Application *app = nullptr;
        Fiber *fiber = nullptr;
        CpuContext *context = nullptr;
        // The window a thread blocked in a send reply wait is waiting on. It
        // is a diagnostic only; the send itself carries its own record.
        unsigned send_window = 0;
        SendWait *sent_wait = nullptr;
        SendDelivery *sent_delivery = nullptr;
        bool sent_completed = false;
        unsigned id = 0, references = 0, stack_size = 0, stack_base = 0, pending = 0, depth = 0, last_error = 0, result = 0,
                 suspended = 0;
        ExceptionTask exceptions;
        unsigned relative_priority = 0, control_event = 0;
        unsigned context_redirect = 0, redirect_eip = 0, redirect_esp = 0, redirect_eax = 0;
        unsigned keyboard_layout = 0, file_api_flags = 0;
        unsigned cursor_handle = ~0u; // new USER queue: IDC_WAIT; resolve its OEM handle lazily
        Win98ThreadControl::SuspendState control_suspend;
        Win98ThreadControl::PriorityState control_priority;
        unsigned control_last_run = 0, control_quantum_us = 0;
        unsigned crt_tls = 0, crt_start_data = 0;
        unsigned static_tls_blocks[32] = {};
        bool crt_legacy = false;
        bool waiting = false, done = false, alertable = false;
        struct Apc {unsigned procedure=0,args[3]{},count=0,io=0;} apcs[64];
        unsigned apc_count=0,pulse_handle=0,execution_state=0x80000000;
        struct Multiwait {unsigned objects[64]={},count=0,mask=0,flags=0;bool all=false,message=false;} multiwait;
        unsigned block = 0, wait_handle = 0;
        // Set while the thread waits for the lock of the screen (block 17,
        // src/x86_screen_lock.cpp); the call that waited takes the hand-off.
        bool screen_wait = false;
        unsigned message_window = 0, message_minimum = 0, message_maximum = 0;
        unsigned long long deadline = 0;
        // The return mark of each open Application::Call of this thread, by
        // depth: the stack address of the mark and the argument bytes the
        // callee may pop. A return through the mark of an outer call tells a
        // loop that the guest has left it.
        // gate: the call of the system that made the callback, zero when the
        // port made it on its own; a saved state names it when it cannot
        // take the thread (src/x86_state_machine.cpp).
        // context: the registers before the callback, which Call puts back
        // when it returns; a state carries the one of a callback it can take
        // up again (Application::StateResumeDispatch).
        // tail: what the host has left to do when the callback returns, as
        // far as a saved state can take it up (TailNone: work it cannot);
        // frames: the records of host frames the thread had when the
        // callback was made; win16 and saved_sp: a callback of the 16-bit
        // side (Win16Invoke) and the stack pointer Win16Deliver puts
        // back.
        struct CallMark
        {
            unsigned sp = 0, bytes = 0, gate = 0;
            CpuContext *context = nullptr;
            unsigned tail = 0, frames = 0, win16 = 0, saved_sp = 0;
            unsigned long long host_frame = 0; // trace anchor across an engine's translated guest frame; host only
        } call_marks[33];
        // The hand-over of a tail position down the frames of the host,
        // from a gate or a record to the callback it makes
        // (Application::GiveTail).
        unsigned tail = 0, tail_sp = 0;
        // The top of the flat stack a generic thunk of a Win16 task may use
        // next (Win16FlatEnter); zero is the stack base of the TIB.
        unsigned win16_flat_top = 0;
        // The 16-bit states the open generic thunks of this thread left,
        // innermost last (Win16FlatEnter, Win16TaskStack).
        CpuContext *win16_flat_saved[16] = {};
        unsigned win16_flat_depth = 0;
        bool hosted_returned = false;
        unsigned hosted_return_words = 0;
        // The thread comes out of a saved state: its fiber is new and goes
        // on from the registers, without the start of a thread (Run).
        bool state_resumed = false;
        // What this thread holds on its host stack, as far as a saved state
        // can take it up again : a record for every frame of the host that waits for the
        // person or for the guest, outermost first. A modal loop keeps its
        // few locals here; a call that has work of its own after the loop
        // keeps what that work needs. A state is taken with such frames on
        // the stack only when every one of them has its record, and after a
        // load each frame is entered again by its record instead of made
        // anew (Application::ResumeHostCall).
        struct HostFrame
        {
            unsigned kind = 0, a = 0, b = 0, c = 0, d = 0;
            unsigned data[10] = {}; // staged Win16 delivery, described in FinishWin16Delivery
        };
        static constexpr unsigned HostFrameCapacity = 256;
        HostFrame host_frames[HostFrameCapacity];
        unsigned host_frame_count = 0;
        // The thread waits inside the loop of its innermost recorded frame,
        // with nothing of the host between that loop and the wait.
        bool host_waiting = false;
        // After a load: the frames are being entered again, and this is the
        // next record to take.
        bool host_resume = false;
        unsigned host_resume_at = 0;
        // And the callback level being built again, and the innermost one.
        unsigned host_resume_level = 0, host_resume_top = 0;
        // A sleep the running call asked for and that begins when the call
        // has returned, before the next guest instruction: the pace of the
        // frames (Application::PaceFrame, Application::Step).
        unsigned long long sleep_after_call = 0;
    } threads[ThreadCapacity];
    ExceptionTask &ExceptionState() { return threads[current_thread].exceptions; }
    unsigned current_thread = 0, next_thread_id = 2, threads_created = 1, scheduler_cursor = 0;
    unsigned control_scan_period = 48;
    unsigned long long control_scan_due = 0;
    unsigned budget = 0;
    // The turn of the running thread: when it began and whether the
    // last run ended because the slice of blocks was used up.
    unsigned long long turn_start = 0; bool slice_stop = false;
    char program_name[128] = {};
    // The guest memory and the processor of the process. The DOS process has
    // memory only (CpuBackend::None) until its first x86 module brings a
    // processor with AttachProcessor (src/x86_dos_vm.cpp).
    Cpu *cpu = nullptr;
    bool HasProcessor() const { return cpu && cpu->Backend() != CpuBackend::None; }
    // The process is an ARM64 program (PE32+ for machine AA64h): its code
    // runs natively, and the registers of X86::Cpu are the call frame between
    // it and the providers (src/arm64_cpu.cpp, docs/ARM64-APPS.md).
    bool Arm64Process() const { return cpu && cpu->Backend() == CpuBackend::Arm64; }
    // The region the processor of an ARM64 process keeps its resume address
    // and its callback entries in, inside the port's own reservation.
    static constexpr unsigned Arm64AreaBase = 0x71f00000, Arm64AreaBytes = 0x10000;
    // Behind it, readable and writable: the 32-bit forms of structs the
    // system keeps between calls.
    static constexpr unsigned Arm64ShadowBase = Arm64AreaBase + Arm64AreaBytes, Arm64ShadowBytes = 0x4000;
    // The 64-bit part of a thread's TEB page, where x18 of an ARM64 thread
    // points (Teb64Offset of src/arm64_cpu.cpp), and in it the array of the
    // static TLS blocks of the thread, eight bytes for each of 32 modules.
    static constexpr unsigned Arm64Teb64Offset = 0x800, Arm64TlsArrayOffset = 0xa00;
    bool AttachProcessor();
    Pe32::Image image;
    Logger log;
    State state = Failed;
    char error[256] = {};
    // Gates are 16 bytes apart from GateBase on; the region is 1 MiB. Two
    // slots of the first 64 KiB are the Exit and Callback return addresses.
    static constexpr unsigned GateRegionBytes = 0x100000;
    static constexpr unsigned ImportCapacity = GateRegionBytes / 16 - 2;
    // Where Dispatch sends an import. Bound on the first call.
    enum Route : unsigned char
    {
        RouteUnbound, RouteApi, RouteCrt, RouteGdi, RouteShellProcessReference, RoutePathProbe,
        RouteShellRegistryOrdinal, RouteCabinet, RouteShellCommand, RouteShellFileInfo, RouteShellDragDrop,
        RouteShellIconCache, RouteShellPolicy, RouteRegisterShell, RouteCommonControl, RouteShellStopwatch,
        RouteShellModule, RouteShellOs, RouteShellThread, RouteShellRegistry, RouteShellPathSyntax,
        RouteShellSearch, RoutePath, RouteCommonInit, RouteDisplayDriver, RouteOpenGL, RouteGlide,
        RouteDxDriver
    };
    struct Import
    {
        char dll[64], name[128];
        unsigned argc, calls;
        unsigned name_next; // Slot + 1 in ImportTable's name bucket, zero at the end.
        unsigned id, route_argument;
        Route route;
        // The return instruction in this import's gate: 0xc3 for cdecl,
        // 0xc2 with the pop count above it for stdcall, 0 for a gate that
        // still holds hlt and always stops the run.
        unsigned fast_return;
        // True while the only reason this record exists is the export
        // directory of a native provider's header page. The program itself has
        // not bound the name, so the import census must not count it.
        bool header_only;
        // 0, or the rule of ScreenPixelRule: this export writes or reads the
        // pixels of a DC or a window and waits while another thread has the
        // primary surface locked (src/x86_screen_lock.cpp).
        unsigned char screen_pixels;
        // 1 no, 2 yes: a call a saved state can take up again while a
        // callback of it runs or while it waits in a recorded loop
        // (Application::TailGate); 0 not looked at yet.
        unsigned char tail_gate;
    };
    // Import records live in chunks that never move: Dispatch keeps a
    // reference to its record while the handler may bind further imports.
    struct ImportTable
    {
        static constexpr unsigned ChunkBits = 10, ChunkSize = 1u << ChunkBits;
        Import *chunks[(ImportCapacity + 2 + ChunkSize - 1) / ChunkSize] = {};
        Import &operator[](unsigned index) { return chunks[index >> ChunkBits][index & (ChunkSize - 1)]; }
        const Import &operator[](unsigned index) const { return chunks[index >> ChunkBits][index & (ChunkSize - 1)]; }
        static constexpr unsigned NameBuckets = 4096;
        unsigned names[NameBuckets] = {};
        unsigned Find(const char *dll, const char *name, unsigned limit) const;
        void Index(unsigned slot);
        bool Reserve(unsigned index);
        ~ImportTable();
    } imports;
    // Appends an import record and returns the address of its gate, or 0.
    unsigned NewImport(const char *dll, const char *name, unsigned argc);
    // One gate slot per dll and name for the whole session, so a gate address
    // that travels in a shared section means the same thing in every process.
    // Built in src/x86_session_arena.cpp.
    static constexpr unsigned ExitGateSlot = 0xffe, CallbackGateSlot = 0xfff;
    static ImportTable session_gates;
    static unsigned session_gate_count;
    static unsigned SessionGateSlot(const char *dll, const char *name);
    static const char *SessionGateName(unsigned slot, const char *&dll);
    static void PublishSessionGate(unsigned slot, unsigned argc);
    void AdoptSessionGate(unsigned slot);
    void ReachSessionGate(unsigned slot);
    void BindRoute(Import &);
    // Whether this import may answer inside uc_emu_start, and which return
    // instruction its gate then carries. Zero means the ordinary way.
    unsigned FastGateReturn(const Import &item) const;
    // Writes that return instruction into the gate, or puts hlt back.
    void ArmFastGate(Import &item, unsigned gate);
    bool ArmCriticalBody(Import &item, unsigned gate);
    static constexpr unsigned TrivialBodyGate = 0xea;
    unsigned TrivialProcedure(Import &item, unsigned gate);
    void LoadError();    // last_error from TEB+60h, where the guest bodies keep it
    void StoreError();   // and back
    void WithdrawFastGate(Import &item, unsigned gate, const char *why);
    unsigned import_count = 0, pending = 0, api_calls = 0, callbacks = 0, depth = 0, native_turns = 0;
    unsigned hal_presents = 0;   // HalPresents
    unsigned FindImport(const char *dll, const char *name);
    bool waiting = false;
    // Diagnostic export-stub mode. Off by default; nothing here implements a
    // Windows service.
public:
    enum StubKind { StubNone = 0, StubImport = 1, StubDynamic = 2, StubOriginalError = 3 };
    struct StubRecord
    {
        char dll[64] = {}, name[128] = {};
        unsigned kind = 0, calls = 0, first_call = 0, last_call = 0;
        unsigned first_eip = 0, first_return = 0, pop_bytes = 0;
        bool pop_known = false;
        unsigned first_args[8] = {};
    };
    static constexpr unsigned StubCapacity = 1024;
    void EnableExportStubs() { stub_mode = true; }
    // Test override of the DLL rule: resolve this DLL to its native ARM64
    // provider instead of loading the original guest PE code. Only the tests
    // of our implementations use it.
    bool AddNativeModule(const char *name);
    // Registers a DLL built for ARM64 that ARM64 processes load under the name
    // of a system file (DDRAW.DLL, DSOUND.DLL). The bytes stay the caller's.
    struct Arm64SystemModule { const char *name; const unsigned char *bytes; unsigned size; };
    static bool AddArm64SystemModule(const char *name, const unsigned char *bytes, unsigned size);
    static const Arm64SystemModule *FindArm64SystemModule(const char *leaf);
    // Flat thunk connections registered by the original ThunkConnect32.
    struct ThunkConnection
    {
        char dll16[64] = {}, dll32[64] = {}, name[128] = {};
        unsigned block = 0, signature = 0, checksum = 0, instance = 0, trap = 0;
        // One armed code area per template shape: 0 takes the index byte from
        // [ebp-4], 1 from CL. See win98_flat_thunk.h.
        unsigned entry_address[2] = {}, entry_gate[2] = {};
        // Token table published at block+8, and the Win16 module catalogue
        // entry behind it when the 16-bit side is a Route::Native16 module.
        unsigned tokens = 0, catalog = 0;
        // The hosted original 16-bit module behind the block, if the 16-bit
        // side runs as original code (src/x86_thunk_hosted.cpp).
        unsigned hosted = 0;
        bool connected = false;
    };
    static constexpr unsigned ThunkConnectionCapacity = 16;
    // Base of the 64 KiB API gate region; x86_app.cpp owns its layout.
    static constexpr unsigned GateBase = 0x70000000;
    // The LDT of the process, beside the descriptor page it already reserves.
    // Entry four of the GDT points at it. A process that starts as a 16-bit
    // program gets it in src/ne_loader.cpp; a 32-bit process needs it as soon
    // as it loads a 16-bit module, and the Control Panel of this tree loads NE
    // applets. Every process therefore gets it.
    static constexpr unsigned Win16LdtBase = 0x71e04000, Win16LdtBytes = 0x4000;
    // Direct API routines and their first-call flags occupy two pages in
    // the existing service reservation, between the LDT and thread blocks.
    static constexpr unsigned TrivialCodeBase = Win16LdtBase + Win16LdtBytes;
    // The page block every process maps for itself: the GDT at its start, the
    // TEB of the first thread at +2000h and native kernel scratch at +3000h;
    // the TEBs of the other threads follow at ThreadTeb. All of it lies in
    // the port's own reservation 71E00000h..72200000h (src/x86_memory.cpp).
    // Win98 keeps its thread and process databases in the shared arena, so
    // nothing of it takes private addresses, and OLE32 (C1000h
    // bytes) and CRTDLL reach their preferred bases.
    static constexpr unsigned SystemPageBase = 0x71e00000, SystemPageBytes = 0x4000,
                              MainTeb = SystemPageBase + 0x2000, ProcessPage = SystemPageBase + 0x3000,
                              ThreadTebBase = 0x71e10000;
    static constexpr unsigned TebOf(unsigned index) { return index ? ThreadTebBase + index * 4096 : MainTeb; }
    unsigned ThunkConnectionCount() const { return thunk_connection_count; }
    const ThunkConnection &ThunkConnectionAt(unsigned i) const { return thunk_connections[i]; }
    unsigned ThunkTransitions() const { return thunk_transitions; }
    unsigned NativeModuleCount() const { return native_module_count; }
    const char *NativeModuleAt(unsigned i) const
    {
        for (auto *entry = native_modules; entry; entry = entry->next)
            if (!i--) return entry->name;
        return "";
    }
    bool ExportStubsEnabled() const { return stub_mode; }
    bool AddStubArgumentBytes(const char *dll, const char *name, unsigned bytes);
    unsigned StubRecordCount() const { return stub_count; }
    const StubRecord &StubRecordAt(unsigned i) const { return stubs[i]; }
    unsigned StubCalls() const { return stub_calls; }

private:
    struct StubArgument
    {
        char dll[64], name[128];
        unsigned bytes;
    };
    ThunkConnection thunk_connections[ThunkConnectionCapacity];
    unsigned thunk_connection_count = 0, thunk_transitions = 0;
    static constexpr unsigned FlatThunkGate = 0xfffffffdu;
    // argc bit of an export Win98 has and this port does not. The low byte is
    // the argument count the original's ret removes, 255 when the tool could
    // not read it.
    static constexpr unsigned MissingExportGate = 0x10000000u;
    unsigned FlatThunkApi(ApiName, const unsigned *);
    unsigned UserHeapProbeCall();
    // SHELL.DLL's PIF-property forwarders behind thunk indices 5, 4, 3 and 2.
    // The six shared words of SHELL's data segment live here, one set per
    // process, because that is what one loaded SHELL.DLL would have owned.
    unsigned ShellPifCall(unsigned index, unsigned handle, const unsigned *words);
    // The native PIFMGR the forwarders reach; src/x86_pifmgr.cpp.
    struct PifMgrState;
    struct PifMgrEnvironment;
    PifMgrState *pifmgr = nullptr;
    unsigned PifMgrOpenHandle(const char *path);
    unsigned PifMgrGroup(unsigned handle, unsigned group, unsigned char *out, unsigned cb);
    unsigned PifMgrSetGroup(unsigned handle, unsigned group, const unsigned char *in, unsigned cb);
    bool PifMgrFlushHandle(unsigned handle);
    void PifMgrCloseHandle(unsigned handle);
    // The property pages of a DOS program behind PIFMGR ordinal 18 and the
    // three COMMCTRL page thunks; src/x86_pif_pages.cpp.
    struct PifPagesState;
    PifPagesState *pif_pages = nullptr;
public:
    struct PifUi;
private:
    unsigned PifMgrSetNamed(unsigned handle, const char *name, const unsigned char *in, unsigned cb);
    unsigned PifPagesThunk(unsigned which, const unsigned *words, unsigned frame);
    unsigned PifPageMessage(unsigned, unsigned, unsigned, unsigned);
    unsigned PifAdvancedMessage(unsigned, unsigned, unsigned, unsigned);
    void FreePifPages();
    bool PifPagesModule();
    bool PifPagesString(unsigned id, char *out, unsigned capacity);
    unsigned char *PifPagesTemplate(unsigned id, bool page, unsigned &length, char *caption, unsigned capacity,
                                    unsigned &cx, unsigned &cy);
    unsigned PifPagesCreate(const char *path, unsigned notify, unsigned *handles16, unsigned capacity, bool segmented);
    unsigned PifPageKinds() const;
    unsigned PifPageTemplateId(unsigned kind) const;
    unsigned PifPageFind(unsigned handle16, unsigned hwnd, unsigned parameter);
    void PifPageRelease(unsigned index);
    unsigned PifPageDialog(unsigned index, unsigned parent);
    void PostNotify(unsigned window, unsigned group);
    void PifPageRealMode(unsigned hwnd, bool realmode);
    void PifPageMessageBox(unsigned hwnd, unsigned sheet, unsigned string, unsigned a, unsigned b);
    void PifPageSetNumbers(unsigned hwnd, const void *rows, unsigned count, const unsigned char *group);
    void PifPageGetNumbers(unsigned hwnd, const void *rows, unsigned count, unsigned char *group);
    bool PifPageCheckNumbers(unsigned hwnd, unsigned sheet, const void *rows, unsigned count);
    void PifFontList(unsigned hwnd, const unsigned char *fnt);
    unsigned PifWoaCells(unsigned (*cells)[2], unsigned capacity, char *path = nullptr, unsigned path_capacity = 0);
    bool PifFontCell(const unsigned char *fnt, const unsigned char *win, unsigned &width, unsigned &height, bool &truetype);
    unsigned PifFontDrawItem(unsigned lp, bool compact = false);
    unsigned PifFontPreview(unsigned index, unsigned hwnd);
    void PifProgramIcon(unsigned index);
    void PifProgramEnable(unsigned index);
    void PifChangeIcon(unsigned index);
    void PifAdvancedDialog(unsigned index);
    // The extra pages of the machine's own options (src/x86_pif_options.cpp).
    unsigned PifOptionPageCount() const;
    unsigned char *PifOptionTemplate(unsigned page, unsigned &length, char *caption, unsigned capacity, unsigned &cx,
                                     unsigned &cy);
    unsigned PifOptionMessage(unsigned index, unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    void PifOptionMidiFiles(unsigned hwnd, unsigned id);
    void PifOptionSection(unsigned props, char *out, unsigned capacity);
    void PifOptionEnable(unsigned hwnd, unsigned page);
    // The kind and the property handle of a page record; ~0u and 0 for none.
    unsigned PifPageKind(unsigned index) const;
    unsigned PifPageProps(unsigned index) const;
public:
    // The property sheet of a DOS program opened by its MS-DOS window.
    unsigned PifPropertySheet(const char *path, unsigned owner, unsigned start_page, const char *title);
private:
    unsigned PifMgrCall(unsigned ordinal, const unsigned *words);
    bool PifMgrOpenPath(const char *path, Win98PifMgr::Record &record);
    Win98PifMgr::Record *PifMgrRecord(unsigned handle);
    void FreePifMgr();
    unsigned ShellModuleHandle16(unsigned name);
    unsigned ShellModuleFileName16(unsigned handle, unsigned buffer, unsigned size);
    unsigned ShellAboutResources(unsigned resources, unsigned space);
    unsigned CplEntry16(unsigned instance, unsigned entry, unsigned hwnd, unsigned message, unsigned first,
                        unsigned second);
    Win98ShellPif::State shell_pif;
    unsigned shell_pif_loads = 0;
    // Win16 local handles are words. The native heap hands out full pointers,
    // so the flat thunk path keeps a word token per live allocation.
    struct UserHeapHandle { unsigned token = 0, pointer = 0; } user_heap_handles[16];
    unsigned next_user_heap_token = 1;
    unsigned FlatThunkTrap(const char *, unsigned block, unsigned entry);
    ThunkConnection *FlatThunkEntry(unsigned);
    bool FlatThunkArm(ThunkConnection &, unsigned entry);
    bool FlatThunkHostedLink(ThunkConnection &connection, unsigned block16, unsigned name);
    bool FlatThunkTransition(Import &);
    bool FlatThunkFault(char *, unsigned);
    bool FlatThunkDispatch(ThunkConnection *, unsigned index, unsigned frame, unsigned arguments,
                           unsigned measured, unsigned caller, unsigned &result, unsigned &words);
    // KERNEL32 16/32-bit bridge exports; x86_kernel_thunks.inc, see
    // Tokens stand in for 16-bit targets.
    static constexpr unsigned ThunkTokenBase = 0xf7c00000u;
    struct ThunkMemory;
    unsigned thunk_support = 0, thunk_callback_pages = 0;
    unsigned ThunkSupportPage();
    unsigned ThunkToken(unsigned connection, unsigned index);
    unsigned ThunkTokenTable(unsigned connection);
    unsigned Win16CatalogTokenTable(unsigned connection, unsigned handle, const unsigned char *ordinals,
                                    unsigned count);
    ThunkConnection *ThunkTokenDecode(unsigned token, unsigned &index);
    void ThunkTargetFail(const char *, unsigned token);
    bool QtThunkEntry(unsigned token);
    bool FtPrologEntry(unsigned edx);
    bool FtThunkEntry();
    bool FtExitEntry(unsigned bytes);
    bool CallbackEntry(unsigned bytes);
    unsigned CallbackEntryTake();
    unsigned CallbackEntryFind(unsigned pointer, bool segmented);
    void CallbackEntryRelease(unsigned entry);
    unsigned CallbackApi(ApiName, const unsigned *);
    unsigned ThunkInstanceApi(unsigned instance, bool segmented_to_linear, bool preserve_null);
    unsigned ThunkBridgeApi(ApiName, const unsigned *);
public:
    // The fs:[0] handler FT_Prolog records; x86_exceptions.inc resumes the
    // stub through FtThunkFault when an exception reaches it.
    static constexpr unsigned FtThunkHandler = 0x71000e20;
    bool FtThunkFault(unsigned frame, unsigned context);
private:
    // Per-process route overrides, unbounded: PLAN.md section 0 dropped the
    // limit of eight. The table of routes is src/dll_routes.h.
    struct NativeModuleName {char name[64] = {}; NativeModuleName *next = nullptr;};
    NativeModuleName *native_modules = nullptr;
    unsigned native_module_count = 0;
    StubRecord stubs[StubCapacity];
    StubArgument *stub_arguments = nullptr;
    unsigned stub_count = 0, stub_calls = 0, stub_argument_count = 0, stub_argument_capacity = 0;
    unsigned stub_logged_calls = 0;
    bool stub_mode = false;

public:
    // Always-on ring of dispatched API calls and the agent-readable crash
    // report built from it. The ring costs one struct
    // store per call and never allocates, so it runs without --trace-calls.
    CrashReport::ApiRing api_ring;
    void CollectCrashReport(CrashReport::Report &);
    // Called once when an application stops in Fail. Off by default so test
    // runners that expect failures write nothing; the host and the probe set it.
    using CrashHook = void (*)(void *context, Application &app, const char *reason);
    static void SetCrashHook(void *context, CrashHook);
    // A runner with a person in front of it switches the error box of an
    // unhandled exception on (src/x86_fault_box.inc); the process then ends
    // alone, as under Win98, instead of stopping with a failure.
    static void SetFaultBox(bool);
    // ExitWindowsEx ends the session; leaving the machine belongs to the host,
    // which owns the restart path. Without a provider the export fails with
    // ERROR_CALL_NOT_IMPLEMENTED instead of pretending.
    using SessionEndHook = void (*)(void *context, Application &app, unsigned flags,
                                    unsigned exit_code);
    static void SetSessionEndHook(void *context, SessionEndHook);
    static bool HasSessionEndHook();
    // True once an ExitWindowsEx has its WM_QUERYENDSESSION answered yes and
    // is sending WM_ENDSESSION; flags and code are the ones the hook gets.
    static bool SessionCommitted(unsigned &flags, unsigned &code);

  private:
    void NotifyCrash(const char *reason);
    struct CrashNaming;
    void CollectCrashProcess(CrashReport::Report &, bool child);
    void CollectCrashThread(unsigned index, CrashReport::ThreadInfo &, const CrashNaming &);
    void DescribeCrashWait(unsigned index, CrashReport::ThreadInfo &);
    void ChooseCrashSuspect(CrashReport::Report &);
    static void *crash_context;
    static CrashHook crash_hook;
    static bool crash_hook_busy;
    static void *session_end_context;
    static SessionEndHook session_end_hook;
    static bool session_committed;
    static unsigned session_committed_flags, session_committed_code;

  public:
    bool trace_calls = false;
    unsigned trace_from = 0; // APITRACE only from this call count on
    bool input_idle = false; // set at the first GetMessage/PeekMessage (WaitForInputIdle)
    bool startup_show_used = false; // STARTF_USESHOWWINDOW applies to the first captioned top-level show
    Application *parent_process = nullptr; // set for child processes; only the root composes the screen
    unsigned long long last_compose = 0;
    // Set while ComposeQuiet asks who else is runnable; see ComposeDue.
    static bool compose_probe;
    bool AnyRunnable();
    unsigned ThreadEip(unsigned index); // EIP of a thread, live or from its saved context
    // Any thread of this process or of a live descendant is runnable.
    bool SystemRunnable();
    bool AnyRunnableExcept(const Application *app, unsigned thread);
    void ComposeIfQuiet();
    bool ComposeQuiet();
    bool ComposeDue();
    unsigned size_reported = 0; // window DefWindowProc last sent WM_SIZE to (WM_WINDOWPOSCHANGED)
    // Window and nonclient hit location of the last WM_NCRBUTTONDOWN.
    unsigned frame_right_window = 0, frame_right_hit = 0;

private:
    StubRecord *StubEntry(const char *dll, const char *name, unsigned kind);
    bool StubPopBytes(const char *dll, const char *name, unsigned &bytes) const;
    void NoteStubCall(const char *dll, const char *name, unsigned kind, unsigned eip, unsigned ret,
                      unsigned pop, bool known, const unsigned *args);
    bool StubDispatch(Import &);
    // An export of a native provider that Win98 has and this port does not:
    // GetProcAddress hands out its gate, and the first real call comes here.
    bool MissingExportCall(Import &);
    struct Block
    {
        unsigned address, size;
        bool used;
        bool moveable = false;
        unsigned data = 0, locks = 0, flags = 0;
        unsigned local_size = 0;
    } allocations[8192] = {};
    // Keep the initial array for existing saved states. More small GlobalAlloc
    // blocks need metadata pages, not a larger fixed per-process limit (Sonic CD).
    struct AllocationPage
    {
        Block blocks[8192] = {};
        AllocationPage *next = nullptr;
    };
    AllocationPage *allocation_pages = nullptr;
    template<class B, class P> struct AllocationRange
    {
        B *first;
        P *pages;
        struct Iterator
        {
            B *block, *limit;
            P *next;
            B &operator*() const { return *block; }
            bool operator!=(const Iterator &other) const { return block != other.block; }
            void operator++()
            {
                if (++block == limit)
                {
                    block = next ? next->blocks : nullptr;
                    limit = block ? block + 8192 : nullptr;
                    next = next ? next->next : nullptr;
                }
            }
        };
        Iterator begin() const { return {first, first + 8192, pages}; }
        Iterator end() const { return {nullptr, nullptr, nullptr}; }
    };
    AllocationRange<Block, AllocationPage> AllocationBlocks() { return {allocations, allocation_pages}; }
    AllocationRange<const Block, const AllocationPage> AllocationBlocks() const { return {allocations, allocation_pages}; }
    struct HeapSystem;
    HeapSystem *heap_system = nullptr;
    unsigned HeapApi(ApiName, const unsigned *);
    unsigned HeapExtendedApi(ApiName,const unsigned *);
    unsigned LocalMemoryApi(ApiName, const unsigned *);
    unsigned ReallocateFixed(const unsigned *);
    unsigned KernelMiscApi(ApiName,const unsigned *);
    // Fibers, debugging, write watch, power and system messages:
    unsigned MiscFamilyApi(ApiName, const unsigned *);
    unsigned DebugApi(ApiName, const unsigned *);
    unsigned debugger_process = 0;
    bool debug_stopped = false, debug_children = false;
    unsigned FiberApi(ApiName, const unsigned *);
    unsigned WriteWatchApi(ApiName, const unsigned *);
    unsigned FatalAppExit(unsigned text);
    // The error box of an unhandled exception, src/x86_fault_box.inc: from
    // the guest's EXCEPTION_RECORD and CONTEXT, or from the code, the address
    // and the 51 CONTEXT words directly. `reason` names the report the host
    // writes; null when the caller has written it already.
    bool FaultBox(unsigned record, unsigned context);
    bool FaultBox(unsigned code, unsigned address, const unsigned *registers, const char *reason);
    // The same box for a process that stops in Fail on its own thread: the
    // stop of a function this port does not carry ends the program the way
    // an unhandled exception does.
    void FailBox(State before);
    // Step counts the runs of the processor it is inside; a Fail within one
    // (a hook) leaves its box for the moment the processor has returned.
    unsigned cpu_runs = 0;
    bool fail_box_deferred = false;
    State fail_box_before = Running;
    // The NE module and its segment number behind a code selector of the
    // 16-bit side, for the error box; false outside every module.
    bool Win16CodeModule(unsigned selector, char *leaf, unsigned capacity, unsigned &segment);
    unsigned FaultBoxMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp);
    bool fault_notified = false;
    static bool fault_box;
    bool RegisterWriteWatch(unsigned base, unsigned pages);
    void ReleaseWriteWatch(unsigned base);
    void FreeWriteWatches();
public:
    void MarkWriteWatch(unsigned address, unsigned size); // Unicorn write hook target.
    // A mode change moved a shown system colour; its brush follows in place.
    static void FollowSystemColour(unsigned index, unsigned colorref);
private:
    struct WriteWatch { unsigned base = 0, pages = 0; unsigned char *dirty = nullptr; } write_watches[8];
    unsigned write_watch_count = 0;
    unsigned long long write_watch_hook = 0;
    Win98KernelMisc::SysMsgList<16> sys_msg_handlers;
    bool wakeup_latency_flag = false;
    static unsigned wakeup_latency_count;
    unsigned CommParseApi(ApiName,const unsigned *);
    void FreeHeaps();
    unsigned ReserveHeapPages(unsigned, unsigned);
    unsigned CommitHeapPages(unsigned, unsigned);
    unsigned DecommitHeapPages(unsigned, unsigned);
    unsigned ReleaseHeapPages(unsigned);
    unsigned next_alloc = 0, last_error = 0, command_line = 0, command_line_w = 0;
    // USER32!SetDebugErrorLevel writes here, as the original writes it into the
    // USER thread information block. No original export reads it back.
    unsigned debug_error_level = 0;
    unsigned short process_error_mode = 0, legacy_error_mode = 0;
    struct Font
    {
        unsigned handle;
        unsigned char logfont[60];
        unsigned requested_charset; // Original GDI logical-object tail byte.
    };
    // The logical fonts of the process. Win98 keeps them in the GDI heap and
    // has no count of its own; the table grows. A fixed 64 made every further
    // CreateFontIndirect fail with error 8 once a browser had probed its font
    // list, and its dialogs fell back to the System font.
    struct FontTable
    {
        Font *items = nullptr;
        unsigned count = 0;
        FontTable() = default;
        FontTable(const FontTable &) = delete;
        FontTable &operator=(const FontTable &) = delete;
        ~FontTable() { delete[] items; }
        Font *begin() { return items; }
        Font *end() { return items + count; }
        const Font *begin() const { return items; }
        const Font *end() const { return items + count; }
        // A free slot, growing the table when none is left; null without memory.
        Font *Free()
        {
            for (unsigned i = 0; i < count; ++i)
                if (!items[i].handle) return &items[i];
            const unsigned next = count ? count * 2 : 64;
            auto *grown = new (std::nothrow) Font[next]();
            if (!grown) return nullptr;
            for (unsigned i = 0; i < count; ++i) grown[i] = items[i];
            delete[] items;
            items = grown;
            const unsigned first = count;
            count = next;
            return &items[first];
        }
    } fonts;
    const unsigned char *font_bytes = nullptr;
    unsigned font_size = 0, fixed = 0, system = 0, stock[18] = {};
    // The shared menu resources of the process: LoadMenu answers the same
    // handle for the same resource of the same module. Win98 keeps that list in
    // the USER heap of the module and has no fixed count, so the table grows. It never has to hold more than the 1.023 menus of the provider.
    struct Menu
    {
        unsigned id, handle, module;
    } *menus = nullptr;
    unsigned menu_count = 0, menu_capacity = 0, main_window = 0, exit_code = 0;
    unsigned ClassApi(ApiName name,const unsigned *arguments);
    unsigned RegisterNativeClass(const unsigned fields[12],const char *name,const char *menu=nullptr);
    unsigned GetNativeClassInfo(unsigned instance,const char *name,unsigned fields[12]);
    unsigned PublishClassMenu(unsigned token);
    unsigned LoadClassMenu(unsigned token,unsigned module);
    struct ClassMenuLease {unsigned token,guest;} class_menu_leases[256]{};
    struct EditHandle
    {
        unsigned hwnd, memory;
    } edit_handles[64] = {};
    // The staging slots ReadString hands out. A slot is made when it is first
    // needed and holds as much as an edit control of this port does, because a
    // WM_SETTEXT of a full control passes through here.
    static constexpr unsigned StringCapacity = 65536;
    char *strings[16] = {};
    char *StringSlot();
    unsigned next_string = 0;
    int mouse_x = 0, mouse_y = 0;
    bool user_initialized = false;
    bool drag_accept[256] = {};
    struct ClipboardContext;
    ClipboardContext *clipboard = nullptr;
    bool InitializeClipboard();
    bool InitializeUserProviders(unsigned version);
    void BindUserProviders();
    void BindClipboardProviders();
    // Expected Windows version of the module (NE 3Eh, PE subsystem). USER sets
    // WFWIN31COMPAT from it; see EraseAgain in src/x86_user.cpp.
    unsigned user_expected_version = 0x400;
    bool EraseAgain() const { return user_expected_version < 0x30a; }
    void FreeClipboard();
    void DetachClipboardThread();
    unsigned ClipboardApi(ApiName, const unsigned *);
    void ClipboardAllocationFreed(unsigned);
    void DestroyClipboardWindow(unsigned);
    unsigned EditClipboard(unsigned, bool, char *, unsigned);
    // The pasteboard of the Mac behind the session's clipboard
    // (src/host_clipboard.h): takes what the host offered since the last
    // call, on a thread of the guest, and hands the host what a program put
    // there after the clipboard closed.
    void ServeHostClipboard();
    void PublishClipboard();
    void RenderHostClipboard(unsigned format);
    unsigned ClipboardAtom(unsigned, unsigned, unsigned char *, unsigned);
    static unsigned system_brushes[32];
    struct ShellCommandEnvironment;
    struct ShellExecuteEnvironment;
    unsigned ShellExecuteApi(ApiName,const unsigned*);
    unsigned ShellCommandApi(unsigned,const unsigned*);
    unsigned PathProbeApi(ApiName,const unsigned*);
    unsigned ShellPathSyntaxApi(ApiName,const unsigned*);
    unsigned ShellSearchApi(ApiName,const unsigned*);
    unsigned ProfileApi(ApiName, const unsigned *);
    void DesktopSetting(const char *, const char *, char *, unsigned);
    bool SetDesktopWallpaper(const char *, unsigned);
    unsigned PaintDesktop(unsigned);
    void LoadDesktopWallpaper();
    unsigned ProfileString(const char*,const char*,const char*,char*,unsigned,const char* = nullptr);
    unsigned cursor_count = 0;
    unsigned cursor = 0;
    unsigned CurrentCursor();
    // The sixteen OEM cursors USER loads at startup, in the order of its own
    // table at segment 21: resource 100+index. See win98_cursor.h.
    unsigned system_cursors[16] = {};
    unsigned default_application_icon = 0; // IDI_APPLICATION, USER entry 0
    unsigned SystemCursor(unsigned identifier);
    bool DrawCursorShape(Gdi::HDC, int x, int y, unsigned cursor);
    unsigned DefaultSetCursor(unsigned hwnd, unsigned wparam, unsigned lparam);
    // IDC_APPSTARTING feedback: the process we are waiting for and its deadline.
    unsigned feedback_process = 0, feedback_deadline = 0;
    void BeginLaunchFeedback(unsigned child_slot, unsigned startup_flags);
    void UpdateLaunchFeedback();
    void HandOffPointer();
    Registry::Session registry{Registry::System()};
    Files::Session files{Files::System()};
    unsigned FileApi(ApiName, const unsigned *);
    // Pipes, mailslots, VxD devices and Winsock handle services.
    struct KernelPipeState;
    struct KernelPipeOwner { KernelPipeState *state = nullptr; ~KernelPipeOwner(); } kernel_pipes;
    unsigned KernelPipesApi(ApiName, const unsigned *);
    unsigned Vwin32DeviceControl(const unsigned *);
    bool KernelPipeDeviceName(unsigned);
    bool OwnsKernelPipeHandle(unsigned) const;
    bool KernelPipeReady(unsigned) const;
    static bool KernelSocketHandle(unsigned);
    bool AnyGuestObject(unsigned);
    unsigned FileExtendedApi(ApiName, const unsigned *);
    unsigned VolumeInformationApi(ApiName, const unsigned *);
    unsigned NextFile(unsigned slot, unsigned output);
    // The open FindFirstFileA searches. The handle carries the slot in its
    // low byte; Win98 has no small limit, and a program that never calls
    // FindClose (Kroko-Mania checks sixteen files that way before its script)
    // must not run out.
    static constexpr unsigned FileSearchCapacity = 256;
    struct FileSearch
    {
        bool used = false;
        unsigned directory = 0, index = 0, generation = 1;
        bool device = false;
        unsigned dots = 2;   // "." and ".." still to come (0), one left (1), done (2)
        char pattern[256] = {};
    } file_searches[FileSearchCapacity];
    struct Positions
    {
        bool used = false;
        unsigned generation = 1, count = 0, args[64][7] = {};
    } positions[8];
    unsigned DrawCaptionApi(ApiName,const unsigned *);
    unsigned DrawTextApi(ApiName,const unsigned *);
    unsigned PositionApi(ApiName, const unsigned *);
    unsigned PositionWindows(unsigned count, unsigned (*args)[7],unsigned region_window=0,unsigned region=0);
    unsigned PositionSend(unsigned hwnd, unsigned message, unsigned wp, unsigned lp, unsigned block = 0);
    unsigned ComputerNameApi(ApiName, const unsigned *);
    unsigned RegistryApi(ApiName, const unsigned *);
    unsigned RegistryCall(ApiName, const unsigned *);
    // src/x86_advapi_registry.cpp
    unsigned AdvapiRegistryApi(ApiName, const unsigned *);
    // src/x86_registry_hives.cpp: the six hive services of VMM.
    unsigned RegistryHiveApi(ApiName, const unsigned *);
    unsigned HiveReadFile(const char *path, unsigned char *&bytes, unsigned &size);
    unsigned HiveWriteFile(const char *path, const unsigned char *bytes, unsigned size);
    void HiveCollect(void *writer, unsigned node, unsigned writer_key, unsigned depth);
    unsigned HiveSave(unsigned node, const char *path, unsigned word16);
    unsigned HiveFlush(unsigned node);
    bool HiveOfKey(unsigned node, unsigned &mount_node);
    unsigned HiveReplace(unsigned key, const char *subkey, const char *fresh, const char *old);
  public:
    // At the end of the session: a hive that RegReplaceKey
    // flagged swaps its file for the "~~R" copy, and the replaced keys come
    // into the registry of the next start. The host calls it where
    // Windows 98 would shut down or restart, not after a hang.
    static void SystemExitHives();
  private:
    // src/x86_user32_completion.cpp, src/x86_gdi32_completion.cpp and
    // src/x86_winmm_completion.cpp: the last missing exports (branch dlls).
    unsigned User32CompletionApi(ApiName, const unsigned *);
    unsigned Comdlg32CompletionApi(ApiName, const unsigned *);
    // src/x86_user32_gates.cpp: the USER.EXE gates of phase 2 of branch dlls.
    unsigned User32GateApi(ApiName, const unsigned *);
    unsigned PlaySoundEvent(unsigned id);
    bool ThreadHung(unsigned thread_id, unsigned milliseconds);
    unsigned DragDetect(unsigned hwnd, int x, int y);
    unsigned DragObject(unsigned parent, unsigned from, unsigned format, unsigned data, unsigned cursor);
    // Depth of keybd_event, mouse_event and SendInput: their input passes a
    // BlockInput, the host's does not.
    unsigned injected_input = 0;
    // src/x86_win_event.cpp: SetWinEventHook, UnhookWinEvent, NotifyWinEvent.
    unsigned WinEventApi(ApiName, const unsigned *);
    void WinEventNotify(unsigned event, unsigned hwnd, unsigned object, unsigned child, bool window_queue);
    bool HasWinEvents(unsigned thread_index) const;
    void ProcessWinEvents();
    void FreeWinEvents();
    static bool WinEventHooksInstalled();
    unsigned Gdi32CompletionApi(ApiName, const unsigned *);
    unsigned EnumIcmProfiles(unsigned dc, unsigned procedure, unsigned parameter);
    void IcmDirectory(char (&out)[260]);
    unsigned UpdateIcmRegKey(unsigned identifier, unsigned file, unsigned command);
    unsigned WinmmCompletionApi(ApiName, const unsigned *);
    unsigned MultimediaRecord(unsigned handle, unsigned types, unsigned &device);
    bool MultimediaMessageArguments(unsigned message, unsigned first, unsigned second, unsigned expected);
    unsigned MultimediaTile(unsigned linear, unsigned bytes);
    unsigned MultimediaHeaderApi(ApiName, const unsigned *);
    struct MultimediaTileRun { unsigned selector = 0, count = 0; } multimedia_tiles[16];
    // src/x86_mci.cpp: the thread that opened an MCI device, MCIGETCREATORTASK.
    unsigned MciCreatorTask(unsigned identifier);
    // src/x86_mmio.cpp: WINMM's buffered file I/O after the original.
    unsigned MmioApi(ApiName, const unsigned *);
    unsigned mmio_dos_gate = 0, mmio_mem_gate = 0;
    bool MmioHandle(unsigned handle);
    unsigned MmioAllocate(unsigned bytes);
    void MmioFree(unsigned pointer);
    unsigned MmioGate(bool memory);
    bool MmioMove(unsigned destination, unsigned source, unsigned bytes);
    unsigned MmioCall(unsigned info, unsigned message, unsigned first, unsigned second);
    unsigned MmioTransfer(unsigned info, unsigned message, unsigned first, unsigned second);
    unsigned MmioFindProcedure(unsigned fcc, unsigned process);
    void MmioResolveProcedure(unsigned name, unsigned info);
    unsigned MmioFlush(unsigned info, unsigned flags);
    unsigned MmioSetBuffer(unsigned info, unsigned pointer, unsigned bytes);
    unsigned MmioExpand(unsigned info, unsigned needed);
    unsigned MmioAdvance(unsigned info, unsigned user, unsigned flags);
    unsigned MmioDosProcedure(unsigned info, unsigned message, unsigned first, unsigned second);
    unsigned MmioDosOpen(unsigned info, unsigned name);
    unsigned MmioMemProcedure(unsigned info, unsigned message, unsigned first, unsigned second);
    void FreeRegistryNotifications();
    // src/x86_advapi_crypto.cpp
    unsigned AdvapiCryptoApi(ApiName, const unsigned *);
    unsigned CryptoObjectEntry(ApiName, const unsigned *a, unsigned offset, unsigned signature);
    // src/x86_crypto_provider.cpp: the provider half of the CryptoAPI.
    unsigned CryptoAcquire(const unsigned *);
    unsigned CryptoProviderCall(ApiName, const unsigned *);
    unsigned CryptoVerifyImage(const char *path, const unsigned char *signature, unsigned size);
    unsigned CryptoVerifyImageGate(const unsigned *);
    unsigned CryptoReturnWindowGate(const unsigned *);
    unsigned crypto_verify_gate = 0, crypto_window_gate = 0;
    bool CryptoRegistryKey(unsigned root, const char *path, unsigned &node);
    // CryptSetProvParam keeps both of these without a provider: the window
    // handle of PP_CLIENT_HWND and the counted block of parameter 11.
    unsigned crypto_client_window = 0, crypto_context_info = 0, crypto_context_info_size = 0;
};
} // namespace X86
