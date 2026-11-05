// SPDX-License-Identifier: GPL-2.0-or-later
// What the Win16 loader of PLAN.md step 9 owns for one process: the modules
// it placed, the global heap blocks of the 16-bit side, the gates, the task
// and the Pascal frame a gate arrives with.
//
// The definitions used to sit in src/ne_loader.cpp. They are here because the
// USER and GDI gates of src/win16_user.cpp read the same state; the loader
// still owns the lifetime of every field.
#pragma once
#include "win98_ne_image.h"
#include "x86_app.h"
namespace X86
{
// One block of the 16-bit global heap.
struct Application::Win16Block
{
    unsigned handle = 0, selector = 0, memory = 0, size = 0, flags = 0, locks = 0;
    // GlobalPageLock's count, apart from the lock count of GlobalLock.
    unsigned page_locks = 0;
    // A block of GlobalDOSAlloc in conventional memory (below 1 MiB): its
    // memory goes back to that arena and never moves.
    bool dos = false;
    // Number of consecutive descriptors covering the block, one per 64-KiB
    // portion.
    unsigned tiles = 1;
    // A segment of a module. KRNL386 makes a global block for every one of
    // them, so GlobalHandle, GlobalSize and GlobalFlags answer for a segment
    // selector; the loader owns the memory, so GlobalFree does not release it.
    bool segment = false;
};
// One 16-bit module and everything the loader made for it.
struct Application::Win16Module
{
    Win98Ne::Image image;
    unsigned char *file = nullptr;
    char path[260] = {}, name[16] = {};
    unsigned handle = 0;    // module database selector; its first word is "NE"
    unsigned database = 0;  // flat address behind that selector
    unsigned instance = 0;  // automatic data segment selector
    // The bytes the header asks for in the automatic data segment, the
    // end LocalInit gets. The segment itself is placed with the full 64
    // KiB behind its descriptor, so the local heap can grow the way the
    // default notification of KRNL386 grows it.
    unsigned instance_bytes = 0;
    unsigned references = 0;
    unsigned segments = 0;
    // By segment number, one based, so the tables hold `segments + 1` words.
    // They grow with the module instead of holding a fixed ceiling: the
    // Visual-Basic runtimes of the collection have 96, 101 and 107 segments,
    // and a fixed table of 64 refused all six programs behind them.
    unsigned *selector = nullptr, *memory = nullptr;
    bool library = false, entered = false;
};
// Everything the 16-bit side of one process owns.
struct Application::Win16Machine
{
    // A process that hosts Control Panel applets holds each applet with the
    // libraries it imports (SYSDM: SETUPX and SETUP4; NETCPL: NETDI and
    // NETOS; MODEM: SETUPX, TAPI and UMDM16) at the same time.
    static constexpr unsigned ModuleCapacity = 32;
    Win16Module modules[ModuleCapacity];
    // The day of the month (plus one) of the last BIOS clock read, for the
    // midnight flag of INT 1Ah.
    unsigned bios_day = 0;
    // The registrations of TOOLHELP.DLL (src/win16_toolhelp.cpp): one record
    // per task each for NotifyRegister and InterruptRegister. `order` keeps
    // the order of TOOLHELP's lists, which append.
    struct ToolhelpRecord { unsigned task = 0, procedure = 0, flags = 0, order = 0; };
    static constexpr unsigned ToolhelpRecords = 16;
    ToolhelpRecord toolhelp_notify[ToolhelpRecords], toolhelp_interrupt[ToolhelpRecords];
    unsigned toolhelp_order = 0;
    // A 16-bit block for the NFYLOADSEG and NFYSTARTDLL records a
    // notification points at.
    unsigned toolhelp_block = 0;
    unsigned module_count = 0;
    unsigned math_control = 0x1332;
    // Collected WIN87EM exception bits. Subfunction 8 adds coprocessor
    // status; subfunction 1 clears them.
    unsigned math_status = 0;
    struct Accelerator { unsigned module = 0, offset = 0, native = 0; };
    Accelerator accelerators[64] = {};
    // One 64-KiB code descriptor per window of the gate region, made when a
    // gate in that window is first needed.
    unsigned gate_selector[16] = {};
    unsigned gates[512] = {};
    unsigned gate_count = 0;
    unsigned task = 0, prefix = 0, stack_selector = 0, instance_handle = 0;
    // The thread that runs the task. A task word is the selector of its task
    // database; the message queue of USER belongs to this thread
    // (Win16TaskThread).
    unsigned task_thread = 0;
    unsigned command_prefix = 0; // flat address of the program segment prefix
    // The environment block of the task. Its selector is the word at 2Ch of
    // the program segment prefix, the word GetDOSEnvironment reads,
    // and the block the C startup of a 16-bit program walks to find its own
    // path. A zero there made every such program read through a null selector.
    unsigned environment = 0, environment_selector = 0, environment_bytes = 0;
    // Global heap blocks of the 16-bit side. The handle shape and the size
    // arithmetic are the ones of KRNL386; see win98_ne_task.h. Every segment
    // of every module is a block too, the way KRNL386 makes one, so the table
    // grows with the modules instead of holding a fixed ceiling: the
    // Visual-Basic runtimes alone bring a hundred segments.
    Win16Block *globals = nullptr;
    unsigned global_capacity = 0;
    unsigned ldt_ready = 0;
    // USER.5 InitApp has run for this task.
    bool initialized = false;
    // Window procedures. A 16-bit one is a far pointer that native dispatch
    // reaches through a mark; a native one handed to 16-bit code is a far
    // pointer of the gate region with the native procedure beside it.
    struct Procedure { unsigned far_pointer = 0, native = 0; } procedures[256];
    unsigned procedure_count = 0;
    // Guest memory the gates stage 32-bit structures in, one block per
    // nesting depth of the gates: a gate whose shared body calls back into
    // the task can meet a second gate there, and that one must not wipe the
    // staging of the first.
    static constexpr unsigned ScratchLevels = 34;
    unsigned scratch[ScratchLevels] = {};
    // The ring the widened copy of a structure a 16-bit caller owns lives in.
    // A ring and not one slot, because a message that carries a structure can
    // reach a window procedure that sends the next one.
    unsigned staging = 0, staging_slot = 0;
    // The slot WM_MDIGETACTIVE left its maximized flag in.
    unsigned answer_staged = 0;
    // Retain far pointers translated by gates so creation-message
    // CREATESTRUCT fields can restore them. Use a ring because creation can
    // nest; lookup by flat address makes stale entries harmless.
    struct FarAlias { unsigned flat = 0, far_pointer = 0; } far_aliases[64];
    unsigned far_alias_next = 0;
    struct ImageAlias
    {
        unsigned native = 0, instance = 0, id = 0, generation = 1;
        bool cursor = false;
        char label[64] = {};
    } image_aliases[256];
    // A file is a word on the 16-bit side, the way DOS names it, and a tagged
    // dword in the native file session. This is the table between the two;
    // the index plus five is the 16-bit handle, because zero to four are the
    // standard DOS handles every task starts with.
    unsigned files[64] = {};
    // Zero to four are the devices the task inherits from the system VM:
    // CON as standard input, output and error, AUX and PRN. A bit is
    // set once the task closed that handle, like the JFT entry DOS frees.
    unsigned standard_closed = 0;
    // INT 21h/AH=59h reports the most recent DOS failure with its class,
    // suggested action and locus. Successful calls do not erase it.
    unsigned dos_error = 0, dos_error_class = 0, dos_error_action = 0, dos_error_locus = 0;
    void RememberDosError(unsigned error)
    {
        dos_error = error;
        switch (error)
        {
        case 1: case 6:
            dos_error_class = 7; dos_error_action = 4; dos_error_locus = 1; break;
        case 2: case 3: case 15: case 18:
            dos_error_class = 8; dos_error_action = 3; dos_error_locus = 2; break;
        case 4:
            dos_error_class = 1; dos_error_action = 4; dos_error_locus = 1; break;
        case 5:
            dos_error_class = 3; dos_error_action = 3; dos_error_locus = 2; break;
        case 33:
            dos_error_class = 10; dos_error_action = 2; dos_error_locus = 2; break;
        default:
            dos_error_class = 13; dos_error_action = 4; dos_error_locus = 1; break;
        }
    }
    // The local block that carries the text of one edit control between
    // EM_GETHANDLE and EM_SETHANDLE.
    struct EditBuffer { unsigned hwnd = 0, handle = 0; } edit_buffers[16];
    // The message deliveries that are running. A 16-bit window procedure
    // hands the far pointer it was given straight on to DefWindowProc, and
    // the message translation has to find the 32-bit structure behind it
    // again instead of handing a 16-bit shape to a 32-bit reader.
    struct Delivery { unsigned message = 0, staged = 0, staged_far = 0, original = 0, structure = 0; };
    Delivery deliveries[16];
    unsigned delivery_count = 0;
    // The initialization entries of the shipped libraries have run.
    bool libraries_entered = false;
    // A 32-bit process that only hosts 16-bit libraries, such as a printer
    // driver GDI loads for a printer DC (src/x86_win16_hosted.cpp). It has no
    // task, and its CPU runs flat code at privilege zero between the calls.
    bool hosted = false;
    // Per thread of the process: the 16-bit stack the hosted calls run on,
    // the lowest offset a running call still uses, and the flat contexts a
    // callback into 32-bit code goes back to.
    struct HostedThread
    {
        unsigned thread = 0, stack = 0, low = 0, depth = 0;
        CpuContext *flat[16] = {};
        // The task GetCurrentTask answers on this thread: set for a thread
        // of MMSYSTEM's mmThreadCreate, which is a task of its own under
        // Win98 (src/win16_mmthread.cpp); zero means the task of the process.
        unsigned task = 0;
    };
    HostedThread hosted_threads[16];
    // Conventional memory of GlobalDOSAlloc (src/ne_loader.cpp): the
    // paragraph-aligned blocks handed out between 10000h and A0000h.
    struct DosBlock
    {
        unsigned base = 0, size = 0;
    } dos_blocks[32];
    unsigned hosted_stub = 0;
    // The resources FindResource named and LoadResource placed. A 16-bit
    // program holds a resource by handle over the life of its window, so the
    // block stays where it is once it has been staged.
    struct Resource
    {
        unsigned module = 0, type = 0, id = 0, offset = 0, bytes = 0, selector = 0, memory = 0;
        // Global-heap handle returned by LoadResource and accepted by
        // LockResource/GlobalLock.
        unsigned handle = 0;
        char label[32] = {};
    };
    Resource resources[64];
    unsigned resource_count = 0;
    // The modules of our side a program asked for by name with LoadLibrary.
    // They have no file and no segments; their handle names the table below.
    char system_modules[8][16] = {};
    unsigned system_module_count = 0;
    // The file of the tree behind each of them (USER.EXE for USER), read the
    // first time a program asks such a handle for a resource. Win98 loaded
    // that file as the module, and its resources are what LoadString with
    // the handle of USER answers: SYSDM takes the version "4.10.2222" from
    // string 204h of USER.
    unsigned char *system_files[8] = {};
    Win98Ne::Image system_images[8];
    bool system_read[8] = {};
    // The disk transfer address DOS function 1Ah set, as the far pointer it
    // came in as; function 2Fh hands it back.
    unsigned transfer_address = 0, transfer_selector = 0, transfer_offset = 0;
    // The search DOS function 4Eh opened and 4Fh carries on. DOS keeps its
    // state in the first twenty one bytes of the disk transfer address; this
    // port keeps the handle of the native session here and a mark there.
    unsigned find_handle = 0;
    // KERNEL.56 Throw leaves through the frame of the Catch that made the
    // buffer, not through its own caller, so the gate hands the rebuilt stack
    // and far address back to the transition.
    unsigned throw_pending = 0, throw_ss = 0, throw_sp = 0, throw_cs = 0, throw_ip = 0;
    // The COMCTL32 the first COMMCTRL entry attached, so the classes of the
    // common controls exist for this task.
    unsigned common_controls = 0;
    // The thunks of MakeProcInstance: eight bytes each in one block with one
    // code descriptor.
    // `uses` counts the MakeProcInstance calls that share one thunk; a slot
    // with no uses is free again.
    struct ProcedureThunk { unsigned procedure = 0, instance = 0, uses = 0; } thunks[64];
    unsigned thunk_count = 0, thunk_block = 0, thunk_selector = 0;
    // The SOUND module, which under Win98 is MMSOUND.DRV. Its queue and the
    // words of its data segment are one for the whole system and live in
    // src/pc_speaker.cpp and src/win16_sound.cpp; a task only keeps the word
    // GetThresholdEvent handed it, as selector:0.
    unsigned sound_event = 0;
    static constexpr unsigned WaveStreams = 8, WaveHeaders = 32;
    struct WaveStream
    {
        unsigned word = 0, native = 0, callback = 0, instance = 0, kind = 0;
    } wave_streams[WaveStreams];
    struct WaveHeader
    {
        unsigned stream = 0, far_pointer = 0, flat = 0, native = 0;
    } wave_headers[WaveHeaders];
    // The ports OpenComm opened (USER.200 to 215, src/win16_comm.inc,
    // ): USER's entry and COMM.DRV's copy of the 25-byte DCB
    // ( + 61h each) for COM1 to COM10, and the event words CEVT hands
    // out, one word per port in `event_block`.
    static constexpr unsigned CommPorts = 10;
    struct CommPort
    {
        unsigned handle = 0;
        bool open = false, ungot = false;
        unsigned char ungot_char = 0;
        unsigned char dcb[25] = {};
        // EnableCommNotification: the window WM_COMMNOTIFY goes to and the
        // two thresholds, -1 for none.
        unsigned notify_window = 0;
        int notify_receive = -1, notify_transmit = -1;
    } comm_ports[CommPorts];
    unsigned comm_event_block = 0;
    // The first megabyte behind __0000H to __F000H (Win16RealModeSelector):
    // the block and one selector per paragraph, indexed by paragraph / 40h.
    unsigned real_mode_image = 0;
    // DOS's country tables (Application::Win16DosCountry): loaded, and the
    // information of INT 21h 6501h.
    bool country_loaded = false;
    unsigned char country_info[0x26] = {};
    unsigned real_mode_selectors[0x400] = {};
};
// A 16-bit Pascal frame. The far call pushed the return address, the
// arguments are behind it, and the callee removes them.
struct Application::Win16Frame
{
    Application &app;
    unsigned stack = 0, ip = 0, cs = 0;
    unsigned Word(unsigned index) const { return const_cast<Application &>(app).Win16Word(stack + 4 + index * 2); }
    unsigned Long(unsigned index) const { return Word(index) | Word(index + 1) << 16; }
    // A far pointer argument, translated to the flat address it names.
    unsigned Pointer(unsigned index) const
    {
        const unsigned offset = Word(index), selector = Word(index + 1);
        return selector ? app.Win16Linear(selector, offset) : 0;
    }
};
} // namespace X86
