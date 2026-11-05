// SPDX-License-Identifier: GPL-2.0-or-later
// The DOS virtual machine as a host provider, the way Sound::Output is one.
// A DOS program is a process of its own loader, the third entry side beside
// the PE and NE loaders: it has a process object, a thread, a window of the
// class `tty` and an exit code, but no x86 code of its own; it gets a
// processor only with an x86 module (src/x86_address_space.h).
// The machine that runs the program is DOSBox Pure, compiled natively for
// ARM64 and linked into the host runners. src/ knows it only through this
// interface; tools/host_dos_vm.cpp answers it over the libretro API and needs
// no SDL, so the headless runners link it as well. Without a provider the
// loader refuses a DOS program.
#pragma once
#include "dos_voodoo_frame.h"
namespace DosVm
{
// What the PIF says about the start, in the terms the machine understands.
struct Launch
{
    // The program as a DOS command line relative to `directory`, "OMF.EXE" or
    // "CHARLIE.EXE -nosound"; empty starts the shell alone, the MS-DOS prompt.
    char command[1024] = {};
    // The guest working directory, "D:\GAMES\OMF"; empty is the root of C:.
    char directory[260] = {};
    // Memory in the PIF's units: conventional RAM is always 640 KB, the
    // extended memory of the machine is `memory_mb` megabytes.
    unsigned memory_mb = 16;
    bool xms = true, ems = true;
    // The rest is for the window, not the machine, and travels with the launch
    // so a provider can log one line about it; the machine detects idling
    // itself, so the idle sensitivity of the PIF is only logged.
    bool fullscreen = false;
    bool close_on_exit = true;
    unsigned idle_sensitivity = 50;
    // The DOS keyboard layout, as DOSBox names it: the layout of the Windows
    // session, so the keys of a German keyboard type German in DOS.
    char keyboard_layout[8] = "us";
    // Lines the machine runs before the program, each ending in a newline:
    // the PIF's batch file, the AUTOEXEC.BAT section of MS-DOS mode.
    char prelude[2048] = {};
    // Initial size of the Screen page in text rows; 0 keeps the mode.
    unsigned lines = 0;
    // "Fast pasting" of the Misc page.
    bool fast_paste = true;
    // The options of the machine the PIF's own pages set, "name=value"
    // lines in the names of the machine (sblaster_type=sbpro2). Where a PIF
    // field above says the same, the field wins. A DOS path as a value
    // (a soundfont) is the provider's to translate.
    char options[2048] = {};
    // The host draws a 3dfx Voodoo of the machine with its GPU
    // (src/dos_voodoo.h): the machine hands over what the card draws
    // (Provider::VoodooFrame) instead of drawing it in software.
    bool voodoo_host = false;
};
// A picture of the DOS screen. XRGB8888 rows without padding; the pointer is
// the provider's and stays valid until the next Frame call of that session.
struct Frame
{
    const unsigned *pixels = nullptr;
    unsigned width = 0, height = 0;
    // Raised with every new picture; a caller compares it with the one it
    // last copied.
    unsigned serial = 0;
};
// A machine that runs one program and ends with it (the game machine): the window of a DOS program closes when the program
// has ended, instead of staying as "Finished - NAME" as under Win98 without
// "Close on exit". Only a PIF of the program's own, one a person made for
// it, still keeps the window: there the setting was chosen.
void EndWithProgram(bool on);
bool EndsWithProgram();
struct Session;
struct Provider
{
    void *owner = nullptr;
    // Starts the machine with the program. Null on refusal, with the reason
    // in `error`. Which host directories stand behind the drive letters is
    // the provider's knowledge, the way the drives of the Windows side are
    // the host runner's. Every Start is a machine of its own; several run at
    // once.
    Session *(*Start)(void *owner, const Launch &, char *error, unsigned capacity) = nullptr;
    void (*Stop)(void *owner, Session *) = nullptr;
    // Feed requested ADC input on the host thread, using the shared clock.
    void (*Microphone)(void *owner, Session *, unsigned long long now) = nullptr;
    // The newest picture, when its serial differs from `seen`.
    bool (*NewFrame)(void *owner, Session *, unsigned seen, Frame &out) = nullptr;
    // A keyboard transition as WM_KEYDOWN carries it: the virtual key, the
    // PC scancode of set 1 and the extended bit. The virtual key names the
    // key on the layout the session uses; the scancode and the bit tell the
    // two shifts, controls and alts apart and the keypad from the cursor keys.
    void (*Key)(void *owner, Session *, unsigned virtual_key, unsigned scancode, bool extended,
                bool down) = nullptr;
    // A mouse step, the pointer's place on the DOS picture in 1/65536 of its
    // width and height (0 to 65535), and the button state, bit 0 left, bit
    // 1 right, bit 2 middle. The DOS mouse driver takes the place: in a
    // window Win98's VMD puts the DOS pointer where the Windows pointer is; the step drives the motion counters (mickeys).
    void (*Mouse)(void *owner, Session *, int dx, int dy, unsigned x, unsigned y, unsigned buttons) = nullptr;
    // Mixed sound of the machine, 44100 stereo frames a second, signed 16
    // bit, into `out`; answers how many frames it wrote.
    unsigned (*Audio)(void *owner, Session *, short *out, unsigned frames) = nullptr;
    // The name of the program DOS runs right now, eight characters as the
    // program segment prefix names it, "DOSBOX" while only the shell runs.
    // The window title follows it the way WINOLDAP's does.
    void (*Program)(void *owner, Session *, char *name, unsigned capacity) = nullptr;
    // Whether the machine has stopped, and how: the DOS return code of the
    // program, or a crash of the DOS side with its text.
    bool (*Finished)(void *owner, Session *, unsigned &exit_code, bool &crashed,
                     char *message, unsigned capacity) = nullptr;
    // A Windows program DOS was asked to run: Win98 hands a PE or NE image to
    // WINOLDAP and runs it as a Windows process while the DOS side goes on.
    // Answers one request at a time: the DOS path of the program, its
    // command tail and the DOS current directory.
    bool (*WindowsProgram)(void *owner, Session *, char *program, unsigned program_capacity, char *arguments,
                           unsigned arguments_capacity, char *directory, unsigned directory_capacity, bool &wait) = nullptr;
    // The end of a Windows program the DOS side waits for: a console program
    // runs inside the DOS box under Win98 and its exit code is the DOS
    // return code.
    void (*WindowsProgramDone)(void *owner, Session *, unsigned exit_code) = nullptr;
    // Paste of the toolbar: the text goes into the BIOS keyboard buffer of
    // the machine a few characters a frame.
    void (*PasteText)(void *owner, Session *, const char *text) = nullptr;
    // The text screen after the last frame, for Mark and Copy: columns, rows
    // and character/attribute words of page 0; false in a graphics mode.
    bool (*ScreenText)(void *owner, Session *, unsigned &columns, unsigned &rows, unsigned short *cells,
                       unsigned capacity) = nullptr;
    // The cursor of the text screen after the last frame: its cell, its first
    // and last scan line (bit 5 of the start hides it) and the scan lines of
    // a character cell; false in a graphics mode.
    bool (*TextCursor)(void *owner, Session *, unsigned &column, unsigned &row, unsigned &start, unsigned &end,
                       unsigned &height) = nullptr;
    // Background off: the machine stands still while the window is not the
    // foreground window, the "always suspend" of the PIF.
    void (*Pause)(void *owner, Session *, bool paused) = nullptr;
    // What the 3dfx Voodoo of the machine drew, frame after frame in the
    // order the card drew them; false when the caller has had them all. The
    // pointers of a frame stay valid until the next call for that session.
    // Every frame must be drawn: a later one builds on its buffers and its
    // textures.
    bool (*VoodooFrame)(void *owner, Session *, DosVoodooFrame &out) = nullptr;
    // The buffers the caller fills with what it drew, for the program's reads
    // through the linear frame buffer; null without a card.
    DosVoodooReadback *(*VoodooReadback)(void *owner, Session *) = nullptr;
    // The frame last taken asked for buffers (DosVoodooFrame::read_mask) and
    // the caller is done with it: drawn, the buffers filled. The machine
    // stands still in its read until then.
    void (*VoodooDrawn)(void *owner, Session *) = nullptr;
};
void SetProvider(const Provider *);
const Provider *Current();
// The rate of the mixer and of the MIDI synthesizer (src/sound.h), so
// neither the machine nor its MIDI needs a resampler.
constexpr unsigned AudioRate = 44100;
// DOS produces live audio beside its pictures. Each handoff keeps at most
// 50 ms; after a stalled consumer, old sound must not trail the live picture.
constexpr unsigned AudioQueueFrames = AudioRate / 20;
// What the loaders hand over here: a.COM, a.BAT, a.PIF, or an MZ file
// whose header names no PE or NE image. src/x86_dos_vm.cpp.
bool IsProgram(const char *name, const void *bytes, unsigned length);
} // namespace DosVm
