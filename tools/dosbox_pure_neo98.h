// SPDX-License-Identifier: GPL-2.0-or-later
// What NEO98 and the DOSBox Pure core say to each other beside the libretro
// API. tools/build_dosbox_pure.py copies this file into the patched clone of
// the core as neo98_dbp.h; tools/host_dos_vm.cpp includes it from here. It is
// plain C++ without a DOSBox or libretro type, so both sides can include it.
// The core reads the state once, in init_dosbox: which host directories stand
// behind which drive letters, where the program starts and what it runs. It
// writes the state back when the program has ended: the DOS return code, or a
// crash. Everything else, memory size, machine type, cycles, goes through the
// core options of the libretro API.
#pragma once
// The record of the 3dfx Voodoo, src/dos_voodoo_frame.h; the host has read it
// with src/dos_vm.h, the clone of the core holds a copy beside this file.
#ifndef NEO98_DOS_VOODOO_FRAME_H
#include "dos_voodoo_frame.h"
#endif

// A drive whose files the host keeps itself: the write layer of a game
// machine, which the Windows side reaches through the same provider
// (tools/host_volume.h, HostVolume::Layered). The DOS drive of the core
// (tools/dosbox_pure_files.inc) holds nothing of its own; every call of DOS
// comes here, on the DOS thread, and the host answers with what both sides
// see: files, deletions, renames, 8.3 names. Paths are DOS paths below the
// root of the drive, "GAMES\\OMF\\OMF.EXE"; the root itself is "". The
// numbers returned are DOS error codes as Win32 has them (2, 3, 5, 32, 80,
// 145, 183), zero for success.
struct Neo98DbpEntry
{
    char name[13] = {};            // the 8.3 name
    unsigned attributes = 0, size = 0, date = 0, time = 0;
};
struct Neo98DbpFiles
{
    void *owner = nullptr;
    bool (*find)(void *owner, const char *path, Neo98DbpEntry &out) = nullptr;
    bool (*child)(void *owner, const char *directory, unsigned index, Neo98DbpEntry &out) = nullptr;
    unsigned (*read)(void *owner, const char *path, unsigned offset, void *buffer, unsigned size, unsigned &done) = nullptr;
    unsigned (*write)(void *owner, const char *path, unsigned offset, const void *buffer, unsigned size, unsigned &done) = nullptr;
    unsigned (*resize)(void *owner, const char *path, unsigned size) = nullptr;
    unsigned (*create)(void *owner, const char *path, unsigned attributes, bool directory) = nullptr;
    unsigned (*remove)(void *owner, const char *path, bool directory) = nullptr;
    unsigned (*rename)(void *owner, const char *from, const char *to) = nullptr;
    unsigned (*set_time)(void *owner, const char *path, unsigned date, unsigned time) = nullptr;
    // A file is open, or closed again: nobody removes or renames it meanwhile.
    void (*retain)(void *owner, const char *path) = nullptr;
    void (*release)(void *owner, const char *path) = nullptr;
    void (*space)(void *owner, unsigned long long &total, unsigned long long &available) = nullptr;
    char label[12] = {};
};

struct Neo98DbpDrive
{
    char letter = 0;               // 'C' to 'Z'
    // The host keeps the files of this drive; `directory` and
    // `overlay_directory` then only say where a file of it lies on the host.
    const Neo98DbpFiles *files = nullptr;
    char directory[1024] = {};     // host directory, with or without a trailing slash
    // The directory is read only and every write lands in a memory overlay
    // that ends with the session. The Win98 tree is mounted this way.
    bool memory_overlay = false;
    // A host directory over the read-only one that takes the writes and
    // keeps them: the same directory the Windows side writes its C: to, so
    // DOS and Windows programs see each other's files.
    char overlay_directory[1024] = {};
};

struct Neo98DbpState
{
    static constexpr unsigned DriveCapacity = 26;
    // DOS-thread reader of the host's copied microphone ring. rate=0 reads
    // the current ADC level; otherwise unsigned 8-bit mono at rate Hz.
    void *microphone_owner = nullptr;
    void (*microphone)(void *, unsigned char *, unsigned count, unsigned rate) = nullptr;
    // Set by the host before retro_load_game. While it is clear the core
    // behaves as upstream.
    bool active = false;
    Neo98DbpDrive drives[DriveCapacity];
    unsigned drive_count = 0;
    // The guest directory the program starts in, "D:\GAMES\OMF". Empty means
    // the root of C:.
    char working_directory[260] = {};
    // The program with its parameters, as a DOS command line. Empty starts
    // the interactive shell alone, which is the MS-DOS prompt.
    char command[1024] = {};
    // Lines the autoexec runs before the program, each ending in a newline:
    // the batch file of the PIF (Program page), the AUTOEXEC.BAT section of a
    // program set to MS-DOS mode.
    char prelude[2048] = {};
    // The text rows the PIF's Screen page asks for: 0 or 25 keep the mode
    // of the machine, 43 and 50 load the 8x8 font through the video BIOS
    // before the program starts (Z:NEO98LIN.COM).
    unsigned lines = 0;
    // Characters Paste sends as keyboard events per frame: the PIF's
    // "Fast pasting" gives 8, without it one, for programs that read the
    // keyboard through their own interrupt handler.
    unsigned paste_rate = 8;
    // The memory the PIF grants: extended memory (XMS) and expanded memory
    // (EMS). Both go to the [dos] section before the machine is built; the
    // size of the extended memory is the core option dosbox_pure_memory_size.
    bool xms = true, ems = true;
    // Written by the core whenever DOS starts or ends a program: the name of
    // the running program as DOS knows it, eight characters, "DOSBOX" while
    // the shell itself runs. The host reads it for the window title.
    char running_program[9] = {};
    // Written by the core. `program_finished` is set by Z:NEO98END.COM, the
    // last line of the autoexec, with the DOS return code of the program.
    // `finished` is set when the machine has stopped, also after `exit` at the
    // prompt and after a crash of the DOS side.
    bool program_finished = false;
    bool finished = false;
    bool crashed = false;
    unsigned exit_code = 0;
    char crash_message[256] = {};
    // Written by the core when DOS is asked to run a program with a PE or NE
    // image behind its stub. Win98 hands such a program to WINOLDAP and runs
    // it as a Windows process while the DOS side carries on. The host reads
    // the request when `windows_request` differs from what it saw last.
    unsigned windows_request = 0;
    volatile unsigned windows_seen = 0; // written by the host: the request it has taken
    char windows_program[260] = {};   // the DOS path, "C:\\WINDOWS\\NOTEPAD.EXE"
    char windows_arguments[128] = {}; // the command tail
    char windows_directory[260] = {}; // the DOS current directory, "D:\\GAMES\\SPONGEB"
    // A console program (PE subsystem 3) runs inside the DOS box under Win98,
    // through CONAGENT, and the DOS side waits for it; START.EXE of
    // WINDOWS\\COMMAND is one, which is how START /W waits. The core sets
    // `windows_wait` with the request and spins in CALLBACK_Idle until the
    // host answers with `windows_done` and the exit code.
    bool windows_wait = false;
    volatile bool windows_done = false;
    volatile unsigned windows_exit_code = 0;
    // Paste: the host queues OEM text; the core sends keyboard make/break
    // events through IRQ1, including to programs with their own handler.
    char paste_text[4096] = {};
    volatile unsigned paste_head = 0, paste_tail = 0;
    // The text screen after each frame, when the machine is in a text mode:
    // columns, rows and the character/attribute words of page 0, for Mark
    // and Copy of the toolbar. Zero columns in a graphics mode.
    unsigned text_columns = 0, text_rows = 0;
    unsigned short text_cells[80 * 50];
    // The cursor of the text screen: its cell, its first and last scan line
    // (bit 5 of the start hides it) and the scan lines of a character cell.
    unsigned cursor_column = 0, cursor_row = 0, cursor_start = 0, cursor_end = 0, cursor_height = 0;
    // The 3dfx Voodoo. Set by the host before retro_load_game: with a sink
    // the card's pixels are the host's to draw. The core calls it once a
    // frame, on the thread of retro_run while the DOS thread stands still,
    // with what the card drew since the last call, and on the DOS thread
    // when the program reads through the linear frame buffer what it has
    // drawn since (DosVoodooFrame::read_mask; retro_run waits for the DOS
    // thread then, so the two calls never overlap). The pointers of the
    // frame are valid during the call only. Without a sink the core draws
    // the card in software, as upstream does without an OpenGL context.
    void (*voodoo_frame)(void *owner, const DosVoodooFrame &) = nullptr;
    void *voodoo_owner = nullptr;
    // The buffers the host fills for reads through the linear frame buffer.
    DosVoodooReadback voodoo_readback;
    // The serial ports (port 0 is COM1 at 3F8h). Set by the host before
    // retro_load_game. A port the host is attached to gets a 16550 whose
    // line is the host's (tools/dosbox_pure_serial.inc); the others stay as
    // the core has them. claim takes the port for this machine and fails
    // while a Windows program holds it; all are called on the DOS thread.
    // The numbers of configure are those of src/comm_host.h.
    bool (*serial_attached)(void *owner, unsigned port) = nullptr;
    bool (*serial_claim)(void *owner, unsigned port) = nullptr;
    void (*serial_release)(void *owner, unsigned port) = nullptr;
    unsigned (*serial_read)(void *owner, unsigned port, unsigned char *out, unsigned capacity) = nullptr;
    void (*serial_write)(void *owner, unsigned port, const unsigned char *data, unsigned count) = nullptr;
    unsigned (*serial_lines)(void *owner, unsigned port) = nullptr;
    void (*serial_control)(void *owner, unsigned port, bool dtr, bool rts, bool line_break) = nullptr;
    void (*serial_configure)(void *owner, unsigned port, unsigned baud, unsigned data_bits, unsigned parity,
                             unsigned stop_bits) = nullptr;
    void *serial_owner = nullptr;
    // Saved states. Written by the core after every
    // retro_serialize and retro_unserialize: the error of its archive
    // (DBPArchive::EError, zero for none), the number that goes with it
    // (the machine type or the megabytes of the state) and its warnings.
    unsigned state_error = 0, state_error_info = 0, state_warnings = 0;
    // A state was asked for while the machine stood inside a call of its own
    // DOS or BIOS that had to let the program run in between (a page fault
    // of a DOS extender in the middle of a read, an interrupt the BIOS
    // called): the frame of that call on the DOS thread's stack is no part
    // of a state, and a state taken there crashes when it is loaded. The core refuses it and says so here; the host asks again
    // at the next frame.
    unsigned state_busy = 0;
};
// Called by the patched retro_run before every frame.
void DBP_Neo98FrameHook();
// Defined in the patched src/ints/int10.cpp: the text screen of page 0.
bool DBP_Neo98TextScreen(unsigned *columns, unsigned *rows, unsigned short *cells, unsigned capacity);
bool DBP_Neo98TextCursor(unsigned *column, unsigned *row, unsigned *start, unsigned *end, unsigned *height);

// The one state of the core. Defined in the patched dosbox_pure_libretro.cpp.
Neo98DbpState *DBP_Neo98State();
bool DBP_Neo98Active();
// Called by the patched DOS_Execute on the DOS thread.
void DBP_Neo98LaunchWindows(const char *program, const char *arguments, bool wait);
