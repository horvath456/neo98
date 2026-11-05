// SPDX-License-Identifier: GPL-2.0-or-later
// MCI: mciSendCommandA, mciSendStringA and mciGetDeviceIDA for the two device
// types this port has a device for, waveaudio and sequencer.
// Where MCI lives in Windows 98. WINMM.DLL is again only the outer edge:
// mciSendCommandA hands every message below 800h straight into the
// flat thunk and translates the parameter block of every message
// above it. mciSendStringA does the same with the command
// string. Everything that matters, the command table, the alias table, the
// device list and the drivers MCIWAVE.DRV and MCISEQ.DRV, is 16 bit and
// implemented natively here.
// The device list of the imported tree is in SYSTEM.INI:
// [mci]
// cdaudio=mcicda.drv sequencer=mciseq.drv
// waveaudio=mciwave.drv avivideo=mciavi.drv
// videodisc=mcipionr.drv vcr=mcivisca.drv
// MPEGVideo=mciqtz.drv
// Two of those seven have a device under them in this port: waveaudio over the
// wave output of src/x86_wave_out.cpp and sequencer over the synthesizer of
// src/sound_synth.cpp. The other five have no device and no driver, so an open
// answers MCIERR_DEVICE_NOT_INSTALLED, which is the number whose text in the
// MMSYSTEM string table reads "This device is not installed".
// The error numbers are not guessed. build/winmm-message/winmm_messages.h is
// the string table of the original MMSYSTEM.DLL, and every number this file
// answers is the number of its own sentence there; the same table names the
// device types (522 waveaudio, 523 sequencer), the modes (524 to 530) and the
// time formats (533 to 543), so those constants are read out of the original
// as well.
//
#include "sound.h"
#include "sound_synth.h"
#include "user.h"
#include "win16_machine.h"
#include "x86_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
namespace
{
// The MCIERR values this file uses, each with the first words of its sentence
// in the MMSYSTEM string table.
constexpr unsigned InvalidDeviceId = 257;        // "Invalid MCI device ID"
constexpr unsigned UnrecognizedKeyword = 259;    // "The command parameter used is not defined"
constexpr unsigned UnrecognizedCommand = 261;    // "The command used is not a valid MCI command"
constexpr unsigned InvalidDeviceName = 263;      // "This is not a registered MCI device"
constexpr unsigned OutOfMemory = 264;            // "There is not enough memory available"
constexpr unsigned MissingCommandString = 267;   // "The command string is empty"
constexpr unsigned ParamOverflow = 268;          // "The output string was too large"
constexpr unsigned MissingStringArgument = 269;  // "requires a character-string parameter"
constexpr unsigned BadInteger = 270;             // "The integer entered is invalid"
constexpr unsigned MissingParameter = 273;       // "is missing a paramter"
constexpr unsigned UnsupportedFunction = 274;    // "does not support the specified command"
constexpr unsigned FileNotFound = 275;           // "Cannot find the specified file"
constexpr unsigned OutOfRange = 282;             // "The parameter is out of range"
constexpr unsigned DeviceTypeRequired = 287;     // "Cannot find the specified device"
constexpr unsigned DuplicateAlias = 289;         // "The specified alias is already being used"
constexpr unsigned MissingDeviceName = 292;      // "requires an alias, file, driver, or device"
constexpr unsigned BadTimeFormat = 293;          // "The specified value for the time format"
constexpr unsigned NoClosingQuote = 294;         // "A closing double-quotation mark is missing"
constexpr unsigned InvalidFile = 296;            // "The file cannot be played"
constexpr unsigned NonapplicableFunction = 302;  // "cannot be performed in the given order"
constexpr unsigned NullParameterBlock = 297;     // "A null parameter block was passed to MCI"
constexpr unsigned DeviceNotInstalled = 306;     // "This device is not installed"
constexpr unsigned InvalidParameter = 11;        // MMSYSERR_INVALPARAM

// The device types and the modes, read out of the string table above.
constexpr unsigned TypeWaveAudio = 522, TypeSequencer = 523;
constexpr unsigned ModeStop = 525, ModePlay = 526, ModePause = 529;
// The time formats: 0 milliseconds, 8 bytes, 9 samples, 10 tmsf.
constexpr unsigned FormatMilliseconds = 0, FormatBytes = 8, FormatSamples = 9;

// The commands. MCI_OPEN is 803h and the block runs on from there.
constexpr unsigned Open = 0x803, Close = 0x804, Play = 0x806, Seek = 0x807;
constexpr unsigned Stop = 0x808, Pause = 0x809, Info = 0x80a, GetDevCaps = 0x80b;
constexpr unsigned Set = 0x80d, SysInfo = 0x810, Break = 0x811, AskStatus = 0x814, Resume = 0x855;
// The flags.
constexpr unsigned Notify = 1, Wait = 2, From = 4, To = 8;
constexpr unsigned OpenShareable = 0x100, OpenElement = 0x200, OpenAlias = 0x400;
constexpr unsigned OpenTypeId = 0x1000, OpenType = 0x2000;
constexpr unsigned SeekToStart = 0x100, SeekToEnd = 0x200;
constexpr unsigned StatusItem = 0x100, GetDevCapsItem = 0x100, SetTimeFormat = 0x400;
// MM_MCINOTIFY and MCI_NOTIFY_SUCCESSFUL, MCI_NOTIFY_ABORTED, MCI_NOTIFY_FAILURE.
constexpr unsigned McmNotify = 0x3b9;
constexpr unsigned NotifySuccessful = 1, NotifyAborted = 4, NotifyFailure = 8;

// How much media one open device may hold. A wave file of the tree is far
// under this, and a MIDI file is tiny; a file that does not fit is refused
// with MCIERR_OUT_OF_MEMORY instead of failing later.
constexpr unsigned MediaSlots = 4;
constexpr unsigned MediaBytes = 2u << 20;
unsigned char media[MediaSlots][MediaBytes];
bool media_used[MediaSlots] = {};

// One event of a sequence, in the packing midiStream uses: the tick it falls
// on and a MIDIEVENT event word, so MEVT_SHORTMSG is a channel message and
// MEVT_TEMPO a tempo change.
struct SequenceEvent
{
    unsigned tick, event;
};

struct MciDevice
{
    Application *process = nullptr;
    unsigned identifier = 0;
    // The task that opened the device, the word MMSYSTEM keeps at node+28h
    // for MCIGETCREATORTASK. A task is a thread here, the value
    // GetWindowTask answers.
    unsigned creator = 0;
    unsigned break_key = 3, break_window = 0; // default Ctrl+Break (VK_CANCEL)
    bool break_enabled = true;
    bool sequencer = false, shareable = false;
    char alias[80] = {}, element[264] = {};
    unsigned slot = ~0u;
    unsigned format = FormatMilliseconds;
    unsigned mode = ModeStop;
    // waveaudio: the PCM of the element and where in it the device stands.
    Sound::Format wave;
    unsigned offset = 0, bytes = 0;    // the data chunk inside the slot
    unsigned position = 0, target = 0; // byte positions inside the data chunk
    unsigned source = 0;
    // sequencer: the merged events of the file and the clock over them.
    Sound::Synth synth;
    unsigned events = 0, next = 0;
    unsigned division = 96, tempo = 500000;
    unsigned long long ticks = 0, carry = 0, last = 0;
    unsigned ticks_target = 0, ticks_total = 0, milliseconds_total = 0;
    // The notification the multimedia thread still owes the caller.
    unsigned window = 0, pending = 0;
    // A device whose driver is the original 16-bit MCI driver its type names
    // in SYSTEM.INI [mci] (src/win16_driver.cpp): the USER handle of the
    // driver, the device type and the custom command table DRV_OPEN reported,
    // the data of mciSetDriverData and the name the type is installed under.
    unsigned driver = 0, driver_type = 0, command_table = 0xffff, driver_data = 0;
    char installed[32] = {};
};
MciDevice devices[8];
unsigned next_identifier = 1;

MciDevice *DeviceOfIdentifier(unsigned identifier)
{
    for (auto &device : devices)
        if (device.process && device.identifier == identifier) return &device;
    return nullptr;
}
// MMSYSTEM handles MCI_BREAK itself, before dispatch to a driver.
// is its default yield procedure; the low async-key-state bit marks
// a new press, and an optional HWND restricts it to the active window.
unsigned SetBreak(MciDevice &device, unsigned flags, unsigned key, unsigned window)
{
    if ((flags & 0x500) == 0x500) return 284; // MCIERR_FLAGS_NOT_COMPATIBLE
    if (!(flags & 0x500)) return MissingParameter;
    device.break_enabled = !(flags & 0x400);
    if (device.break_enabled)
    {
        device.break_key = key & 0xffff;
        device.break_window = flags & 0x200 ? window & 0xffff : 0;
    }
    return 0;
}
bool BreakPressed(MciDevice &device, unsigned thread)
{
    if (!device.break_enabled ||
        (device.break_window && device.break_window != User::ThreadActivation(thread).active) ||
        !(User::GetAsyncKeyState(device.break_key) & 1)) return false;
    User::Message message;
    while (User::PeekMessage(&message, (User::HWND)device.break_window, 0x100, 0x108, true)) {}
    return true;
}
void RenderSequencer(void *owner, short *out, unsigned frames)
{
    ((MciDevice *)owner)->synth.Render(out, frames);
}
bool Same(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b)
    {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return !*a && !*b;
}
// The RIFF walk of MCIWAVE.DRV over the element it opened.
bool ParseRiff(const unsigned char *bytes, unsigned length, Sound::Format &format,
               unsigned &offset, unsigned &size)
{
    const auto dword = [&](unsigned at) {
        return (unsigned)bytes[at] | ((unsigned)bytes[at + 1] << 8) |
               ((unsigned)bytes[at + 2] << 16) | ((unsigned)bytes[at + 3] << 24);
    };
    const auto word = [&](unsigned at) {
        return (unsigned)bytes[at] | ((unsigned)bytes[at + 1] << 8);
    };
    if (length < 44 || memcmp(bytes, "RIFF", 4) || memcmp(bytes + 8, "WAVE", 4)) return false;
    unsigned at = 12;
    bool have_format = false;
    offset = size = 0;
    while (at + 8 <= length)
    {
        const unsigned chunk = dword(at + 4), body = at + 8;
        if (chunk > length - body) break;
        if (!memcmp(bytes + at, "fmt ", 4) && chunk >= 16)
        {
            if (word(body) != 1) return false;
            format.channels = word(body + 2);
            format.rate = dword(body + 4);
            format.bits = word(body + 14);
            have_format = true;
        }
        else if (!memcmp(bytes + at, "data", 4)) { offset = body; size = chunk; }
        at = body + chunk + (chunk & 1);
    }
    return have_format && size && Sound::Mixer::Supports(format);
}
} // namespace

// How many bytes of PCM one frame takes, which is what every position of a
// waveaudio device is counted in underneath.
static unsigned FrameBytes(const Sound::Format &format)
{
    const unsigned bytes = format.channels * (format.bits / 8);
    return bytes ? bytes : 1;
}

// A position of the guest, in the time format of the device, as a byte offset
// into the data chunk. The three formats a waveaudio device accepts are
// milliseconds, bytes and samples, which is what MCIWAVE.DRV offers.
unsigned Application::MciToBytes(void *opaque, unsigned value)
{
    auto &device = *(MciDevice *)opaque;
    const unsigned align = FrameBytes(device.wave);
    if (device.format == FormatBytes) return value - value % align;
    if (device.format == FormatSamples) return value * align;
    const unsigned long long samples =
        (unsigned long long)value * (device.wave.rate ? device.wave.rate : 1) / 1000;
    return (unsigned)(samples * align);
}
unsigned Application::MciFromBytes(void *opaque, unsigned value)
{
    auto &device = *(MciDevice *)opaque;
    const unsigned align = FrameBytes(device.wave);
    if (device.format == FormatBytes) return value;
    if (device.format == FormatSamples) return value / align;
    return (unsigned)((unsigned long long)(value / align) * 1000 /
                      (device.wave.rate ? device.wave.rate : 1));
}

// The variable length number of a standard MIDI file.
static unsigned ReadVariable(const unsigned char *bytes, unsigned length, unsigned &at)
{
    unsigned value = 0;
    for (unsigned i = 0; i < 4 && at < length; ++i)
    {
        const unsigned byte = bytes[at++];
        value = (value << 7) | (byte & 0x7f);
        if (!(byte & 0x80)) break;
    }
    return value;
}

// Reads a standard MIDI file out of the slot and leaves the merged events in
// the same slot, behind the file. MCISEQ.DRV does the same walk over MThd and
// MTrk; the merge is a k way pass over the tracks, so nothing is sorted.
unsigned Application::ParseSequence(void *opaque, unsigned length)
{
    auto &device = *(MciDevice *)opaque;
    unsigned char *bytes = media[device.slot];
    const auto big16 = [&](unsigned at) { return (unsigned)bytes[at] << 8 | bytes[at + 1]; };
    const auto big32 = [&](unsigned at) {
        return (unsigned)bytes[at] << 24 | (unsigned)bytes[at + 1] << 16 |
               (unsigned)bytes[at + 2] << 8 | bytes[at + 3];
    };
    unsigned start = 0;
    // An RMID file is a RIFF with the whole MIDI file in its "data" chunk.
    if (length > 20 && !memcmp(bytes, "RIFF", 4) && !memcmp(bytes + 8, "RMID", 4)) start = 20;
    if (length < start + 22 || memcmp(bytes + start, "MThd", 4)) return InvalidFile;
    const unsigned header = big32(start + 4);
    if (header < 6) return InvalidFile;
    const unsigned tracks = big16(start + 10);
    const unsigned division = big16(start + 12);
    if (!division || !tracks) return InvalidFile;
    device.division = division;
    device.tempo = 500000;
    // One cursor per track: where it stands, the tick of its next event and
    // the running status it carries.
    struct Cursor { unsigned at, end, tick, status; bool live; };
    Cursor cursors[64];
    unsigned count = 0, at = start + 8 + header;
    while (count < 64 && at + 8 <= length && !memcmp(bytes + at, "MTrk", 4))
    {
        unsigned size = big32(at + 4);
        if (size > length - at - 8) size = length - at - 8;
        Cursor &cursor = cursors[count++];
        cursor.at = at + 8;
        cursor.end = at + 8 + size;
        cursor.status = 0;
        cursor.live = true;
        cursor.tick = ReadVariable(bytes, cursor.end, cursor.at);
        at = cursor.end;
    }
    if (!count) return InvalidFile;
    // The events go behind the file in the same slot, rounded up to four.
    const unsigned base = (length + 3) & ~3u;
    SequenceEvent *out = (SequenceEvent *)(media[device.slot] + base);
    const unsigned room = (MediaBytes - base) / (unsigned)sizeof(SequenceEvent);
    unsigned written = 0, total = 0;
    while (written < room)
    {
        unsigned pick = 64, best = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            if (!cursors[i].live || cursors[i].at >= cursors[i].end) continue;
            if (pick == 64 || cursors[i].tick < best) { pick = i; best = cursors[i].tick; }
        }
        if (pick == 64) break;
        Cursor &cursor = cursors[pick];
        unsigned status = bytes[cursor.at];
        if (status & 0x80) ++cursor.at;
        else status = cursor.status;
        if (status < 0x80) { cursor.live = false; continue; }
        if (status == 0xff)
        {
            const unsigned kind = cursor.at < cursor.end ? bytes[cursor.at++] : 0x2f;
            const unsigned size = ReadVariable(bytes, cursor.end, cursor.at);
            if (kind == 0x51 && size == 3 && cursor.at + 3 <= cursor.end)
                out[written++] = SequenceEvent{cursor.tick,
                                               0x01000000u | ((unsigned)bytes[cursor.at] << 16) |
                                                   ((unsigned)bytes[cursor.at + 1] << 8) |
                                                   bytes[cursor.at + 2]};
            cursor.at += size;
            if (kind == 0x2f) cursor.live = false;
        }
        else if (status == 0xf0 || status == 0xf7)
        {
            const unsigned size = ReadVariable(bytes, cursor.end, cursor.at);
            cursor.at += size;
        }
        else
        {
            cursor.status = status;
            const unsigned kind = status & 0xf0;
            const unsigned wanted = (kind == 0xc0 || kind == 0xd0) ? 1u : 2u;
            if (cursor.at + wanted > cursor.end) { cursor.live = false; continue; }
            unsigned message = status;
            message |= (unsigned)bytes[cursor.at++] << 8;
            if (wanted == 2) message |= (unsigned)bytes[cursor.at++] << 16;
            out[written++] = SequenceEvent{cursor.tick, message};
        }
        if (cursor.tick > total) total = cursor.tick;
        if (cursor.at < cursor.end) cursor.tick += ReadVariable(bytes, cursor.end, cursor.at);
        else cursor.live = false;
    }
    device.events = written;
    device.offset = base;
    device.ticks_total = total;
    // The length in milliseconds, walked with the tempo changes of the file.
    unsigned long long microseconds = 0, tempo = 500000, previous = 0;
    for (unsigned i = 0; i < written; ++i)
    {
        microseconds += (unsigned long long)(out[i].tick - previous) * tempo / division;
        previous = out[i].tick;
        if ((out[i].event >> 24) == 1) tempo = out[i].event & 0xffffff;
    }
    device.milliseconds_total = (unsigned)(microseconds / 1000);
    return 0;
}

void Application::CloseMciDevice(void *opaque)
{
    auto &device = *(MciDevice *)opaque;
    if (device.driver)
    {
        // Only the end of the process reaches this for a driver's device
        // (FreeMci); MCI_CLOSE goes through MciDriverCommand. The process
        // takes the driver with it, and its threads are gone by then: MCIAVI's
        // close waits for its playback thread, so no driver code runs here.
        device = MciDevice{};
        return;
    }
    if (device.source) Sound::Shared().Close(device.source);
    if (device.slot < MediaSlots) media_used[device.slot] = false;
    device.process = nullptr;
    device.identifier = 0;
    device.source = 0;
    device.slot = ~0u;
    device.events = device.next = 0;
    device.pending = 0;
    device.synth.Reset();
}

// MCIGETCREATORTASK, MMSYSTEM: zero for an identifier without a
// device, otherwise the task of the opener.
unsigned Application::MciCreatorTask(unsigned identifier)
{
    const MciDevice *device = identifier ? DeviceOfIdentifier(identifier) : nullptr;
    return device && device->process == this ? device->creator : 0;
}
void Application::FreeMci()
{
    for (auto &device : devices)
        if (device.process == this) CloseMciDevice(&device);
}
bool Application::MciActive()
{
    for (const auto &device : devices)
        if (device.process == this) return true;
    return false;
}

// One pass of the sequencer and the wave player, on the multimedia thread of
// the process. A device that has reached its end stops and leaves its notify.
void Application::PumpMciDevices()
{
    for (auto &device : devices)
    {
        if (device.process != this) continue;
        if (device.mode == ModePlay && !device.sequencer)
        {
            const unsigned long long played = Sound::Shared().Played(device.source);
            const unsigned reached = device.position + (unsigned)played;
            if (!Sound::Shared().Busy(device.source) || reached >= device.target)
            {
                Sound::Shared().Close(device.source);
                device.source = 0;
                device.position = device.target;
                device.mode = ModeStop;
                if (device.window) device.pending = NotifySuccessful;
            }
        }
        else if (device.mode == ModePlay && device.sequencer)
        {
            const unsigned long long now = Now();
            if (!device.last) device.last = now;
            unsigned long long elapsed = now - device.last;
            device.last = now;
            if (elapsed > 1000000) elapsed = 1000000;
            device.carry += elapsed;
            const unsigned tempo = device.tempo ? device.tempo : 500000;
            const unsigned long long due = device.carry * device.division / tempo;
            device.carry -= due * tempo / device.division;
            device.ticks += due;
            const SequenceEvent *events =
                (const SequenceEvent *)(media[device.slot] + device.offset);
            while (device.next < device.events && events[device.next].tick <= device.ticks)
            {
                const SequenceEvent &event = events[device.next++];
                if ((event.event >> 24) == 1) device.tempo = event.event & 0xffffff;
                else
                {
                    Sound::Lock();
                    device.synth.ShortMessage(event.event);
                    Sound::Unlock();
                }
            }
            if (device.next >= device.events || device.ticks >= device.ticks_target)
            {
                Sound::Lock();
                device.synth.AllNotesOff();
                Sound::Unlock();
                device.mode = ModeStop;
                if (device.window) device.pending = NotifySuccessful;
            }
        }
        if (device.pending && device.window)
        {
            User::PostMessage((User::HWND)device.window, McmNotify, device.pending,
                              device.identifier);
            device.pending = 0;
            device.window = 0;
        }
        else if (device.pending) device.pending = 0;
    }
}

// MCI_OPEN. `type` and `element` are what the caller named; either may be
// empty, and the element decides the type when the caller named none, the way
// the MCI command table does through the file extension.
unsigned Application::OpenMciDevice(const char *type, const char *element, const char *alias,
                                   unsigned flags, unsigned &identifier, const unsigned *block, unsigned count)
{
    identifier = 0;
    char kind[32] = {};
    if (type && type[0])
    {
        unsigned i = 0;
        while (type[i] && i + 1 < sizeof kind && type[i] != ' ' && type[i] != '!')
        {
            kind[i] = type[i];
            ++i;
        }
        kind[i] = 0;
    }
    else if (element && element[0])
    {
        const char *dot = nullptr;
        for (const char *p = element; *p; ++p)
            if (*p == '.') dot = p;
        if (dot)
        {
            // MMSYSTEM asks WIN.INI [mci extensions] for the type of
            // the extension ("avi=AVIVideo" in the tree).
            MciProfile("mci extensions", dot + 1, kind, sizeof kind, false);
            if (!kind[0] && Same(dot, ".wav")) snprintf(kind, sizeof kind, "waveaudio");
            else if (!kind[0] && (Same(dot, ".mid") || Same(dot, ".midi") || Same(dot, ".rmi")))
                snprintf(kind, sizeof kind, "sequencer");
        }
        if (!kind[0]) return InvalidDeviceName;
    }
    else return MissingDeviceName;
    const bool sequencer = Same(kind, "sequencer");
    if (!sequencer && !Same(kind, "waveaudio"))
    {
        // Every other type is the MCI driver SYSTEM.INI [mci] names for it,
        // opened as the original 16-bit driver it is.
        char driver[128] = {};
        MciProfile("mci", kind, driver, sizeof driver, true);
        if (!driver[0] || !MciInstalled(kind)) return InvalidDeviceName;
        return OpenMciDriverDevice(kind, driver, element, alias, flags, block, count, identifier);
    }
    // MCISEQ and MCIWAVE are compound devices, and a compound device opens
    // without an element: the device is there and holds no file. Programs
    // open "sequencer" or "waveaudio" that way to learn whether music and
    // sound can be offered at all; Shanghai II greys out both menu entries
    // when the open fails. Measured with the original MCISEQ.DRV of
    // the tree: MCI_OPEN with MCI_OPEN_TYPE "sequencer" and no element answers
    // 0, MCI_GETDEVCAPS and MCI_CLOSE on it 0.
    const bool empty = !element || !element[0];
    if (alias && alias[0])
        for (const auto &device : devices)
            if (device.process && Same(device.alias, alias)) return DuplicateAlias;
    MciDevice *slot = nullptr;
    for (auto &device : devices)
        if (!device.process) { slot = &device; break; }
    if (!slot) return OutOfMemory;
    unsigned store = ~0u;
    unsigned length = 0;
    if (!empty)
    {
        for (unsigned i = 0; i < MediaSlots; ++i)
            if (!media_used[i]) { store = i; break; }
        if (store == ~0u) return OutOfMemory;
        length = ReadSoundFile(element, media[store], MediaBytes);
        if (!length) return FileNotFound;
    }
    slot->process = this;
    slot->break_enabled = true;
    slot->break_key = 3;
    slot->break_window = 0;
    slot->slot = store;
    slot->sequencer = sequencer;
    slot->shareable = (flags & OpenShareable) != 0;
    slot->format = FormatMilliseconds;
    slot->mode = ModeStop;
    slot->position = slot->target = slot->offset = slot->bytes = 0;
    slot->source = 0;
    slot->events = slot->next = 0;
    slot->ticks = slot->carry = slot->last = 0;
    slot->window = slot->pending = 0;
    slot->synth.Reset();
    snprintf(slot->element, sizeof slot->element, "%s", empty ? "" : element);
    // Without an element the device is known by its type, as "sequencer".
    snprintf(slot->alias, sizeof slot->alias, "%s", alias && alias[0] ? alias : empty ? kind : element);
    slot->division = 96;
    slot->tempo = 500000;
    slot->ticks_target = slot->ticks_total = slot->milliseconds_total = 0;
    if (empty)
    {
        slot->identifier = next_identifier++;
        slot->creator = threads[current_thread].id;
        identifier = slot->identifier;
        return 0;
    }
    media_used[store] = true;
    unsigned bad = 0;
    if (sequencer) bad = ParseSequence(slot, length);
    else if (!ParseRiff(media[store], length, slot->wave, slot->offset, slot->bytes))
        bad = InvalidFile;
    if (bad)
    {
        media_used[store] = false;
        slot->process = nullptr;
        slot->slot = ~0u;
        return bad;
    }
    slot->identifier = next_identifier++;
    slot->creator = threads[current_thread].id;
    identifier = slot->identifier;
    return 0;
}

// The command worker. `arguments` is the parameter block in the order MCI
// packs it: index 0 is dwCallback and every other index is the field at four
// times its number, so the block of the guest and the block mciSendStringA
// builds have the same shape.
unsigned Application::MciCommand(unsigned identifier, unsigned message, unsigned flags,
                                 unsigned *arguments, unsigned count)
{
    auto &mixer = Sound::Shared();
    if (message == Open)
    {
        unsigned made = 0;
        const char *type = nullptr;
        if ((flags & (OpenType | OpenTypeId)) == (OpenType | OpenTypeId))
        {
            if (arguments[2] == TypeWaveAudio) type = "waveaudio";
            else if (arguments[2] == TypeSequencer) type = "sequencer";
            else return InvalidDeviceName;
        }
        else if ((flags & OpenType) && arguments[2])
            type = ReadString(arguments[2]);
        char type_text[64] = {}, element_text[264] = {}, alias_text[80] = {};
        if (type)
            snprintf(type_text, sizeof type_text, "%s", type);
        if ((flags & OpenElement) && arguments[3])
            snprintf(element_text, sizeof element_text, "%s", ReadString(arguments[3]));
        if ((flags & OpenAlias) && arguments[4])
            snprintf(alias_text, sizeof alias_text, "%s", ReadString(arguments[4]));
        const unsigned bad = OpenMciDevice(type ? type_text : nullptr,
                                           (flags & OpenElement) && arguments[3] ? element_text : nullptr,
                                           (flags & OpenAlias) && arguments[4] ? alias_text : nullptr, flags, made, arguments,
                                           count);
        if (bad) return bad;
        arguments[1] = made;
        if ((flags & Notify) && arguments[0])
            User::PostMessage((User::HWND)arguments[0], McmNotify, NotifySuccessful, made);
        return 0;
    }
    auto *found = DeviceOfIdentifier(identifier);
    if (!found) return InvalidDeviceId;
    auto &device = *found;
    if (device.process != this && !device.shareable) return InvalidDeviceId;
    const unsigned window = (flags & Notify) && count ? arguments[0] : 0;
    const auto finish = [&](unsigned result) {
        if (!(flags & Notify) || !window) return result;
        if (result) User::PostMessage((User::HWND)window, McmNotify, NotifyFailure, identifier);
        else if (device.mode != ModePlay)
            User::PostMessage((User::HWND)window, McmNotify, NotifySuccessful, identifier);
        return result;
    };
    // A command that ends a play whose notification is still owed tells the
    // window so: MCI_NOTIFY_ABORTED. A program that stops its music and then
    // waits for that message before it closes the device (Shanghai II, with
    // 90 ticks of patience) stood still for five seconds without it.
    const auto abort_play = [&] {
        if ((device.mode == ModePlay || device.mode == ModePause) && device.window)
            User::PostMessage((User::HWND)device.window, McmNotify, NotifyAborted, identifier);
        device.window = 0;
        device.pending = 0;
    };
    switch (message)
    {
    case Close:
        abort_play();
        CloseMciDevice(&device);
        return 0;
    case Play:
    {
        // A device opened without an element has nothing to play. Which
        // error MCISEQ and MCIWAVE answer here is not measured; the sequence
        // error says what is missing.
        if (device.slot >= MediaSlots) return finish(NonapplicableFunction);
        // A second play supersedes the first, which is what the notify value
        // MCI_NOTIFY_SUPERSEDED says; this port aborts the old one instead of
        // leaving two players on one device.
        if (device.mode == ModePlay && device.window)
            User::PostMessage((User::HWND)device.window, McmNotify, NotifyAborted, identifier);
        device.window = 0;
        device.pending = 0;
        if (device.sequencer)
        {
            const unsigned division = device.division ? device.division : 96;
            const unsigned long long per_tick = 500000ull / division;
            unsigned from = device.next < device.events ? (unsigned)device.ticks : 0;
            unsigned to = device.ticks_total;
            if (flags & From)
                from = (unsigned)((unsigned long long)arguments[1] * 1000 /
                                  (per_tick ? per_tick : 1));
            if (flags & To)
                to = (unsigned)((unsigned long long)arguments[2] * 1000 /
                                (per_tick ? per_tick : 1));
            if (to > device.ticks_total) to = device.ticks_total;
            if (from > to) return OutOfRange;
            if (!device.source)
            {
                device.source = mixer.OpenGenerator(RenderSequencer, &device);
                if (!device.source) return OutOfMemory;
            }
            device.ticks = from;
            device.ticks_target = to;
            device.carry = 0;
            device.last = 0;
            device.next = 0;
            const SequenceEvent *events =
                (const SequenceEvent *)(media[device.slot] + device.offset);
            // Everything before the start still has to reach the synthesizer,
            // because a program change or a tempo before it is still in force.
            while (device.next < device.events && events[device.next].tick < from)
            {
                const SequenceEvent &event = events[device.next++];
                if ((event.event >> 24) == 1) device.tempo = event.event & 0xffffff;
                else if ((event.event & 0xf0) != 0x90 && (event.event & 0xf0) != 0x80)
                {
                    Sound::Lock();
                    device.synth.ShortMessage(event.event);
                    Sound::Unlock();
                }
            }
            device.mode = ModePlay;
        }
        else
        {
            unsigned from = device.position, to = device.bytes;
            if (flags & From) from = MciToBytes(&device, arguments[1]);
            if (flags & To) to = MciToBytes(&device, arguments[2]);
            if (to > device.bytes) to = device.bytes;
            if (from > to) return OutOfRange;
            if (device.source) { mixer.Close(device.source); device.source = 0; }
            device.source = mixer.Open(device.wave);
            if (!device.source) return OutOfMemory;
            device.position = from;
            device.target = to;
            if (to > from)
                mixer.Submit(device.source, media[device.slot] + device.offset + from, to - from,
                             identifier, 0, 0);
            device.mode = ModePlay;
        }
        device.window = window;
        EnsureMultimediaThread();
        if (flags & Wait)
        {
            if (device.break_enabled) User::GetAsyncKeyState(device.break_key);
            while (device.mode == ModePlay && state != Failed && state != Exited)
            {
                if (BreakPressed(device, ThreadId()))
                {
                    unsigned stop[] = {0};
                    MciCommand(identifier, Stop, 0, stop, 1);
                    break;
                }
                Sound::Shared().Advance(Now());
                PumpMciDevices();
                auto &thread = threads[current_thread];
                thread.block = 3;
                thread.deadline = Now() + 5000;
                Suspend(Idle);
            }
            return finish(0);
        }
        return 0;
    }
    case Stop:
        if (device.sequencer)
        {
            Sound::Lock();
            device.synth.AllNotesOff();
            Sound::Unlock();
        }
        else if (device.source)
        {
            if (device.mode == ModePlay || device.mode == ModePause)
                device.position += (unsigned)mixer.Played(device.source);
            mixer.Close(device.source);
            device.source = 0;
        }
        abort_play();
        device.mode = ModeStop;
        return finish(0);
    case Pause:
        if (device.mode != ModePlay) return finish(0);
        if (device.sequencer)
        {
            Sound::Lock();
            device.synth.AllNotesOff();
            Sound::Unlock();
        }
        else mixer.Pause(device.source);
        device.mode = ModePause;
        return finish(0);
    case Resume:
        if (device.mode != ModePause) return finish(0);
        if (!device.sequencer) mixer.Restart(device.source);
        device.last = 0;
        device.mode = ModePlay;
        EnsureMultimediaThread();
        return 0;
    case Seek:
    {
        if (device.mode == ModePlay || device.mode == ModePause)
        {
            abort_play();
            if (device.sequencer)
            {
                Sound::Lock();
                device.synth.AllNotesOff();
                Sound::Unlock();
            }
            else if (device.source)
            {
                mixer.Close(device.source);
                device.source = 0;
            }
            device.mode = ModeStop;
        }
        if (device.sequencer)
        {
            unsigned to = 0;
            if (flags & SeekToEnd) to = device.ticks_total;
            else if (!(flags & SeekToStart))
            {
                if (!(flags & To)) return MissingParameter;
                const unsigned division = device.division ? device.division : 96;
                to = (unsigned)((unsigned long long)arguments[1] * 1000 * division / 500000);
            }
            if (to > device.ticks_total) return OutOfRange;
            device.ticks = to;
            device.next = 0;
            return finish(0);
        }
        unsigned to = 0;
        if (flags & SeekToEnd) to = device.bytes;
        else if (!(flags & SeekToStart))
        {
            if (!(flags & To)) return MissingParameter;
            to = MciToBytes(&device, arguments[1]);
        }
        if (to > device.bytes) return OutOfRange;
        device.position = to;
        return finish(0);
    }
    case Set:
    {
        if (!(flags & SetTimeFormat)) return finish(0);
        const unsigned wanted = arguments[1];
        if (device.sequencer)
        {
            if (wanted != FormatMilliseconds) return BadTimeFormat;
        }
        else if (wanted != FormatMilliseconds && wanted != FormatBytes && wanted != FormatSamples)
            return BadTimeFormat;
        device.format = wanted;
        return finish(0);
    }
    case AskStatus:
    {
        if (!(flags & StatusItem)) return MissingParameter;
        const unsigned item = arguments[2];  // dwItem
        unsigned value = 0;
        switch (item)
        {
        case 1: // MCI_STATUS_LENGTH
            value = device.sequencer ? device.milliseconds_total
                                     : MciFromBytes(&device, device.bytes);
            break;
        case 2: // MCI_STATUS_POSITION
            if (device.sequencer)
            {
                const unsigned division = device.division ? device.division : 96;
                value = (unsigned)(device.ticks * 500000 / division / 1000);
            }
            else
            {
                unsigned at = device.position;
                if (device.mode == ModePlay || device.mode == ModePause)
                    at += (unsigned)mixer.Played(device.source);
                if (at > device.bytes) at = device.bytes;
                value = MciFromBytes(&device, at);
            }
            break;
        case 3: value = 1; break;                       // MCI_STATUS_NUMBER_OF_TRACKS
        case 4: value = device.mode; break;             // MCI_STATUS_MODE
        case 5: value = 1; break;                       // MCI_STATUS_MEDIA_PRESENT
        case 6: value = device.format; break;           // MCI_STATUS_TIME_FORMAT
        case 7: value = 1; break;                       // MCI_STATUS_READY
        case 8: value = 1; break;                       // MCI_STATUS_CURRENT_TRACK
        default: return UnrecognizedKeyword;
        }
        arguments[1] = value;   // dwReturn
        return finish(0);
    }
    case GetDevCaps:
    {
        if (!(flags & GetDevCapsItem)) return MissingParameter;
        unsigned value = 0;
        switch (arguments[2])
        {
        case 1: value = 0; break;  // CAN_RECORD: no recording device in this port
        case 2: value = 1; break;  // HAS_AUDIO
        case 3: value = 0; break;  // HAS_VIDEO
        case 4: value = device.sequencer ? TypeSequencer : TypeWaveAudio; break;
        case 5: value = 1; break;  // USES_FILES
        case 6: value = 1; break;  // COMPOUND_DEVICE
        case 7: value = 0; break;  // CAN_EJECT
        case 8: value = 1; break;  // CAN_PLAY
        case 9: value = 0; break;  // CAN_SAVE
        default: return UnrecognizedKeyword;
        }
        arguments[1] = value;
        return finish(0);
    }
    case Info:
        return UnsupportedFunction;
    default:
        break;
    }
    return UnrecognizedCommand;
}

unsigned Application::MciSendCommand(ApiName name, const unsigned *a)
{
    (void)name;
    const unsigned identifier = a[0], message = a[1], flags = a[2], block = a[3];
    if (message == SysInfo)
        return MciSysInfo(identifier, flags, block);
    if (message == Break)
    {
        auto *device = DeviceOfIdentifier(identifier);
        if (!device || (device->process != this && !device->shareable)) return InvalidDeviceId;
        if ((flags & 0x500) == 0x500) return 284;
        if (!(flags & 0x500)) return MissingParameter;
        if ((flags & (0x100 | Notify)) && !block) return NullParameterBlock;
        if (block && !virtual_memory.Access(block, flags & 0x200 ? 12 : 8, true)) return InvalidParameter;
        const unsigned result = SetBreak(*device, flags, block ? Read32(block + 4) : 0,
                                         block && (flags & 0x200) ? Read32(block + 8) : 0);
        if (!result && (flags & Notify) && Read32(block))
            User::PostMessage((User::HWND)Read32(block), McmNotify, NotifySuccessful, identifier);
        return result;
    }
    // A device of a 16-bit MCI driver gets every message but MCI_OPEN,
    // MCI_BREAK and MCI_SYSINFO from the driver path, the messages below 800h included:
    //  hands those to the driver unchanged.
    if (message != Open)
        if (MciDevice *device = DeviceOfIdentifier(identifier))
            if (device->driver && (device->process == this || device->shareable))
                return MciDriverCommand(device, message, flags, block);
    // Without a driver of its own, an installable driver message finds no
    // device.
    if (message < 0x800) return InvalidDeviceId;
    // The parameter blocks this port reads are at most seven dwords wide, and
    // every one of them starts with dwCallback. MCI_OPEN has two more for the
    // window style and the parent of an animation or digital video device.
    unsigned arguments[7] = {};
    unsigned count = 1;
    if (message == Open) count = flags & 0x30000 ? 7 : 5;
    else if (message == Play || message == AskStatus) count = 4;
    else if (message == GetDevCaps || message == Set) count = 3;
    else if (message == Seek) count = 2;
    if (block)
    {
        if (!virtual_memory.Access(block, count * 4, true)) return InvalidParameter;
        for (unsigned i = 0; i < count; ++i) arguments[i] = Read32(block + i * 4);
    }
    else
    {
        // No block: MMSYSTEM hands the null pointer to the driver unchanged
        // (its only MCIERR_NULL_PARAMETER_BLOCK is in the open path),
        // and MCISEQ and MCIWAVE read the block only for what the flags
        // name. So a command whose flags name nothing in the block works
        // without one: Shanghai II rewinds its music with
        // MCI_SEEK(MCI_SEEK_TO_START | MCI_WAIT, NULL) before every repeat.
        // What would read or fill the block is refused here instead of
        // faulting the way the driver does.
        const bool reads = message == Open || message == AskStatus || message == GetDevCaps ||
                           (flags & Notify) || (message == Play && (flags & (From | To))) ||
                           (message == Seek && (flags & To)) || (message == Set && (flags & ~(Notify | Wait)));
        if (reads) return NullParameterBlock;
    }
    const unsigned result = MciCommand(identifier, message, flags, arguments, count);
    // Three commands write back, and all three write their value at +4:
    // wDeviceID for open, dwReturn for status and for capability.
    if (!result && block && (message == Open || message == AskStatus || message == GetDevCaps))
        Write32(block + 4, arguments[1]);
    return result;
}

// ---------------------------------------------------------------------------
// mciSendStringA

namespace
{
struct Words
{
    char text[512] = {};
    const char *word[32] = {};
    unsigned count = 0;
};
// Splits the command string the way the MCI parser does: on spaces, with a
// double quoted run as one word.
unsigned Split(const char *command, Words &out)
{
    unsigned at = 0, length = 0;
    while (command[at] == ' ') ++at;
    if (!command[at]) return MissingCommandString;
    while (command[at] && out.count < 32 && length + 1 < sizeof out.text)
    {
        if (command[at] == '"')
        {
            ++at;
            out.word[out.count++] = out.text + length;
            while (command[at] && command[at] != '"' && length + 1 < sizeof out.text)
                out.text[length++] = command[at++];
            if (command[at] != '"') return NoClosingQuote;
            ++at;
            out.text[length++] = 0;
        }
        else
        {
            out.word[out.count++] = out.text + length;
            while (command[at] && command[at] != ' ' && length + 1 < sizeof out.text)
                out.text[length++] = command[at++];
            out.text[length++] = 0;
        }
        while (command[at] == ' ') ++at;
    }
    return out.count ? 0u : MissingCommandString;
}
bool Number(const char *text, unsigned &value)
{
    if (!text || !*text) return false;
    value = 0;
    for (const char *p = text; *p; ++p)
    {
        if (*p < '0' || *p > '9') return false;
        value = value * 10 + (unsigned)(*p - '0');
    }
    return true;
}
} // namespace

// The command tables drivers registered with mciLoadCommandResource: the
// module and the place of the RCDATA resource in its file.
namespace
{
struct CommandTable
{
    unsigned module = 0, offset = 0, bytes = 0;
};
CommandTable command_tables[16];
} // namespace

// --- Command strings for driver devices ------------------------------------
// MMSYSTEM parses a command string by tables : the table the
// driver registered with mciLoadCommandResource, then the core table of
// MMSYSTEM.DLL. A table is a run of entries, each a name, a doubleword and a
// word that says what the entry is; a command begins with its head (the
// message number) and ends with MCI_END_COMMAND. The parser builds the
// 16-bit parameter block the driver reads straight from the entries: the
// callback, the room of the return value, then one place per entry.
namespace
{
enum : unsigned { TableHead = 0, TableString = 1, TableInteger = 2, TableEnd = 3, TableReturn = 4, TableFlag = 5,
                  TableEndList = 6, TableRect = 7, TableConstant = 8, TableEndConstant = 9 };
struct TableEntry
{
    const char *name = "";
    unsigned value = 0, type = TableEndList;
};
bool NextEntry(const unsigned char *&at, const unsigned char *end, TableEntry &entry)
{
    const unsigned char *p = at;
    while (p < end && *p) ++p;
    if (p + 7 > end) return false;
    entry.name = (const char *)at;
    entry.value = p[1] | p[2] << 8 | p[3] << 16 | (unsigned)p[4] << 24;
    entry.type = p[5] | p[6] << 8;
    at = p + 7;
    return true;
}
// How many of the words from `at` on the (possibly several word) name of an
// entry covers; zero when it does not match.
unsigned NameWords(const char *name, const Words &words, unsigned at)
{
    unsigned used = 0;
    if (!*name) return 0;
    for (const char *p = name; *p;)
    {
        while (*p == ' ') ++p;
        const char *start = p;
        while (*p && *p != ' ') ++p;
        if (p == start) break;
        if (at + used >= words.count) return 0;
        const char *word = words.word[at + used];
        const unsigned length = (unsigned)(p - start);
        if (strlen(word) != length) return 0;
        for (unsigned i = 0; i < length; ++i)
        {
            char x = start[i], y = word[i];
            if (x >= 'A' && x <= 'Z') x += 32;
            if (y >= 'A' && y <= 'Z') y += 32;
            if (x != y) return 0;
        }
        ++used;
    }
    return used;
}
bool SignedNumber(const char *text, unsigned &value)
{
    const bool negative = text && *text == '-';
    if (!Number(negative ? text + 1 : text, value)) return false;
    if (negative) value = 0u - value;
    return true;
}
struct ParsedCommand
{
    unsigned message = 0, flags = 0, result_kind = 0;
    unsigned char block[0x100] = {};
    struct { unsigned offset; const char *text; } strings[8];
    unsigned string_count = 0;
};
// Parses the words from `first` on against the command `verb` of one table.
// `found` says whether the table has the command at all.
unsigned ParseByTable(const unsigned char *table, unsigned bytes, const char *verb, const Words &words, unsigned first,
                      ParsedCommand &out, bool &found)
{
    const unsigned char *at = table, *end = table + bytes, *begin = nullptr;
    TableEntry entry;
    found = false;
    while (NextEntry(at, end, entry))
    {
        if (entry.type == TableEndList) break;
        if (entry.type == TableHead && Same(entry.name, verb))
        {
            found = true;
            out.message = entry.value;
            begin = at;
            break;
        }
    }
    if (!found) return 0;
    auto put = [&](unsigned offset, unsigned value) {
        if (offset + 4 <= sizeof out.block) memcpy(out.block + offset, &value, 4);
    };
    // The room of the return value follows the callback.
    unsigned start = 4;
    {
        const unsigned char *peek = begin;
        if (NextEntry(peek, end, entry) && entry.type == TableReturn)
        {
            out.result_kind = entry.value;
            start += entry.value == TableString || entry.value == TableRect ? 8 : entry.value ? 4 : 0;
            begin = peek;
        }
    }
    for (unsigned i = first; i < words.count;)
    {
        unsigned offset = start, used = 0;
        const unsigned char *scan = begin;
        while (!used && NextEntry(scan, end, entry) && entry.type != TableEnd && entry.type != TableEndList)
        {
            switch (entry.type)
            {
            case TableFlag:
                if ((used = NameWords(entry.name, words, i))) out.flags |= entry.value;
                break;
            case TableInteger:
                if ((used = NameWords(entry.name, words, i)))
                {
                    unsigned value = 0;
                    if (i + used >= words.count) return MissingParameter;
                    if (!SignedNumber(words.word[i + used], value)) return BadInteger;
                    put(offset, value);
                    out.flags |= entry.value;
                    ++used;
                }
                offset += 4;
                break;
            case TableString:
                if ((used = NameWords(entry.name, words, i)))
                {
                    if (i + used >= words.count) return MissingParameter;
                    if (out.string_count >= 8) return ParamOverflow;
                    out.strings[out.string_count++] = {offset, words.word[i + used]};
                    out.flags |= entry.value;
                    ++used;
                }
                offset += 4;
                break;
            case TableRect:
                if ((used = NameWords(entry.name, words, i)))
                {
                    if (i + used + 4 > words.count) return MissingParameter;
                    for (unsigned k = 0; k < 4; ++k)
                    {
                        unsigned value = 0;
                        if (!SignedNumber(words.word[i + used + k], value)) return BadInteger;
                        if (offset + 2 * k + 2 <= sizeof out.block)
                        {
                            const unsigned short word = (unsigned short)value;
                            memcpy(out.block + offset + 2 * k, &word, 2);
                        }
                    }
                    out.flags |= entry.value;
                    used += 4;
                }
                offset += 8;
                break;
            case TableConstant:
            {
                // A group: its own name (may be empty) and flag, then the
                // names of its values up to MCI_END_CONSTANT.
                const unsigned group = entry.value;
                const unsigned named = NameWords(entry.name, words, i);
                TableEntry constant;
                bool taken = false;
                while (NextEntry(scan, end, constant) && constant.type != TableEndConstant)
                {
                    if (taken || constant.type != TableInteger) continue;
                    const unsigned at_word = i + named;
                    const unsigned match = (named || !*entry.name) ? NameWords(constant.name, words, at_word) : 0;
                    if (match)
                    {
                        put(offset, constant.value);
                        out.flags |= group;
                        used = named + match;
                        taken = true;
                    }
                }
                if (named && !taken)
                {
                    // The name of the group with a number behind it.
                    unsigned value = 0;
                    if (i + named >= words.count) return MissingParameter;
                    if (!SignedNumber(words.word[i + named], value)) return BadInteger;
                    put(offset, value);
                    out.flags |= group;
                    used = named + 1;
                }
                offset += 4;
                break;
            }
            default:
                break;
            }
        }
        if (!used) return UnrecognizedKeyword;
        i += used;
    }
    return 0;
}
// The core table and the strings of MMSYSTEM.DLL, read from the file of the
// tree: the 16-bit MMSYSTEM itself is native here, its resources are not.
struct MmsystemFile
{
    unsigned char *bytes = nullptr;
    unsigned size = 0;
    Win98Ne::Image image;
    bool tried = false;
} mmsystem_file;
const Words *pending_open_words = nullptr;
} // namespace

unsigned Application::MciDriverString(void *opaque, const void *word_list, char *answer, unsigned capacity,
                                      unsigned callback)
{
    auto &device = *(MciDevice *)opaque;
    const Words &words = *(const Words *)word_list;
    const char *verb = words.word[0];
    if (!mmsystem_file.tried)
    {
        mmsystem_file.tried = true;
        unsigned handle = 0, status = 0, size = 0, done = 0, error = 0;
        if (!files.Open("C:\\WINDOWS\\SYSTEM\\MMSYSTEM.DLL", 1, 1, 3, 0, handle, status))
        {
            if (!files.Size(handle, size) && size && size < 8u * 1024 * 1024 &&
                (mmsystem_file.bytes = (unsigned char *)malloc(size)) && !files.Read(handle, mmsystem_file.bytes, size, done) &&
                done == size && mmsystem_file.image.Parse(mmsystem_file.bytes, size, error))
                mmsystem_file.size = size;
            files.Close(handle);
        }
    }
    ParsedCommand parsed;
    bool found = false;
    unsigned bad = 0;
    Win16Module *driver_module = nullptr;
    if (device.command_table && device.command_table <= sizeof command_tables / sizeof command_tables[0] &&
        command_tables[device.command_table - 1].bytes)
    {
        const auto &table = command_tables[device.command_table - 1];
        driver_module = Win16ModuleOf(table.module);
        if (driver_module)
            bad = ParseByTable(driver_module->image.Data() + table.offset, table.bytes, verb, words, 2, parsed, found);
    }
    if (!found && mmsystem_file.size)
    {
        unsigned offset = 0, bytes = 0;
        if (mmsystem_file.image.ResourceAt(0x800a, 0, "CORE", offset, bytes))
            bad = ParseByTable(mmsystem_file.bytes + offset, bytes, verb, words, 2, parsed, found);
    }
    if (!found) return UnrecognizedCommand;
    if (bad) return bad;
    if (parsed.message == Break)
    {
        unsigned key = 0, window = 0;
        memcpy(&key, parsed.block + 4, 4);
        memcpy(&window, parsed.block + 8, 2);
        const unsigned result = SetBreak(device, parsed.flags, key, window);
        if (!result && (parsed.flags & Notify) && callback)
            User::PostMessage((User::HWND)callback, McmNotify, NotifySuccessful, device.identifier);
        answer[0] = 0;
        return result;
    }
    if (!Win16HostedReady()) return OutOfMemory;
    // The block at 0, the strings from 200h, the text a driver returns at 500h.
    const unsigned memory = Win16GlobalAlloc(0x40, 0x800);
    Win16Block *area = memory ? Win16GlobalBlock(memory) : nullptr;
    if (!area) return OutOfMemory;
    const unsigned base = area->memory, far = area->selector << 16;
    unsigned text = 0x200;
    for (unsigned i = 0; i < parsed.string_count; ++i)
    {
        const unsigned length = (unsigned)strlen(parsed.strings[i].text) + 1;
        if (text + length > 0x500) { Win16GlobalFree(memory); return ParamOverflow; }
        Store(base + text, parsed.strings[i].text, length);
        const unsigned pointer = far | text;
        memcpy(parsed.block + parsed.strings[i].offset, &pointer, 4);
        text += (length + 1) & ~1u;
    }
    if (parsed.flags & Notify) memcpy(parsed.block, &callback, 4);
    if (parsed.result_kind == TableString)
    {
        const unsigned pointer = far | 0x500, room = 0x200;
        memcpy(parsed.block + 4, &pointer, 4);
        memcpy(parsed.block + 8, &room, 4);
    }
    Store(base, parsed.block, sizeof parsed.block);
    if ((parsed.flags & Wait) && device.break_enabled) User::GetAsyncKeyState(device.break_key);
    const unsigned result = Win16SendDriverMessage(device.driver, parsed.message, parsed.flags, far);
    if (state == Failed) return 0;
    unsigned error = result & 0xffff;
    answer[0] = 0;
    if (!error && parsed.result_kind == TableString)
    {
        const char *returned = ReadString(base + 0x500);
        snprintf(answer, capacity, "%s", returned ? returned : "");
    }
    else if (!error && parsed.result_kind == TableRect)
    {
        short box[4] = {};
        Read(base + 4, box, sizeof box);
        snprintf(answer, capacity, "%d %d %d %d", box[0], box[1], box[2], box[3]);
    }
    else if (!error && parsed.result_kind)
    {
        const unsigned value = Read32(base + 4);
        snprintf(answer, capacity, "%u", value);
        // MMSYSTEM: with MCI_RESOURCE_RETURNED the high word of the
        // value names a string, in the driver (MCI_RESOURCE_DRIVER) or in
        // MMSYSTEM; the string interface answers with that string.
        if (result & 0x10000)
        {
            const unsigned id = value >> 16;
            snprintf(answer, capacity, "%u", value & 0xffff);
            if ((result & 0x100000) && driver_module)
            {
                if (Win16LoadStringInto(driver_module->instance, id, base + 0x500, 0xff))
                    snprintf(answer, capacity, "%s", ReadString(base + 0x500));
            }
            else if (mmsystem_file.size)
            {
                // A string table of sixteen strings, each with its length in front.
                unsigned offset = 0, bytes = 0;
                if (mmsystem_file.image.ResourceAt(0x8006, 0x8000 | (id / 16 + 1), nullptr, offset, bytes))
                {
                    const unsigned char *p = mmsystem_file.bytes + offset, *end = p + bytes;
                    for (unsigned k = 0; k < id % 16 && p < end; ++k) p += 1 + *p;
                    if (p < end && *p && p + 1 + *p <= end)
                        snprintf(answer, capacity, "%.*s", (int)*p, (const char *)p + 1);
                }
            }
        }
    }
    Win16GlobalFree(memory);
    return error;
}

unsigned Application::MciSendString(ApiName name, const unsigned *a)
{
    (void)name;
    if (!a[0] || !virtual_memory.Access(a[0], 1, false)) return InvalidParameter;
    if (a[1] && a[2] && !virtual_memory.Access(a[1], a[2], true)) return InvalidParameter;
    const char terminator = 0;
    if (a[1] && a[2]) Store(a[1], &terminator, 1);
    const char *command = ReadString(a[0]);
    if (!command) return InvalidParameter;
    Words words;
    const unsigned split = Split(command, words);
    if (split) return split;
    const char *verb = words.word[0];
    char answer[256] = {};

    if (Same(verb, "open"))
    {
        const char *element = words.count > 1 ? words.word[1] : nullptr;
        const char *type = nullptr, *alias = nullptr;
        unsigned flags = 0;
        // The words of the core table. What it does not know belongs to the
        // command table of the driver that will serve the device ("parent",
        // "style" of digital video); a built-in device knows none of them.
        Words rest;
        for (unsigned i = 2; i < words.count; ++i)
        {
            if (Same(words.word[i], "type") && i + 1 < words.count) type = words.word[++i];
            else if (Same(words.word[i], "alias") && i + 1 < words.count) alias = words.word[++i];
            else if (Same(words.word[i], "shareable")) flags |= OpenShareable;
            else if (Same(words.word[i], "wait")) flags |= Wait;
            else if (rest.count < 32) rest.word[rest.count++] = words.word[i];
        }
        if (!element) return MissingDeviceName;
        // The flags MMSYSTEM sets for the words it found : a driver
        // reads the element and the alias only when their flags say so.
        if (alias) flags |= OpenAlias;
        pending_open_words = rest.count ? &rest : nullptr;
        struct Clear { ~Clear() { pending_open_words = nullptr; } } clear;
        // "open cdaudio" names a device type without an element; "open x.wav"
        // names an element whose extension picks the type.
        unsigned made = 0;
        unsigned bad = 0;
        bool named_type = false;
        for (const char *p = element; *p; ++p)
            if (*p == '.' || *p == '\\' || *p == ':') named_type = true;
        if (!type && !named_type)
        {
            bad = OpenMciDevice(element, nullptr, alias, flags | OpenType, made);
            // The string parser cannot tell an alias from a device type
            // before it has resolved the name, so a name it cannot resolve is
            // the same "cannot find the specified device" every other string
            // command answers, and not the "not a registered MCI device" of
            // an explicit lpstrDeviceType in the block form.
            if (bad == InvalidDeviceName) bad = DeviceTypeRequired;
        }
        else bad = OpenMciDevice(type, element, alias, flags | OpenElement | (type ? OpenType : 0), made);
        if (bad) return bad;
        // Words nobody took: no driver device, so no table that knows them.
        if (pending_open_words)
        {
            const unsigned close_arguments[5] = {};
            unsigned copy[5];
            memcpy(copy, close_arguments, sizeof copy);
            MciCommand(made, Close, 0, copy, 5);
            return UnrecognizedKeyword;
        }
        snprintf(answer, sizeof answer, "%u", made);
    }
    else
    {
        if (words.count < 2) return MissingDeviceName;
        unsigned identifier = 0;
        for (const auto &device : devices)
            if (device.process && Same(device.alias, words.word[1])) identifier = device.identifier;
        if (!identifier)
        {
            unsigned number = 0;
            if (Number(words.word[1], number) && DeviceOfIdentifier(number)) identifier = number;
        }
        if (!identifier) return DeviceTypeRequired;
        // A device of a 16-bit driver: its command table says what the words
        // mean. Closing goes the common way, which also lets the driver go.
        if (auto *device = DeviceOfIdentifier(identifier); device && ((device->driver && !Same(verb, "close")) || Same(verb, "break")))
        {
            const unsigned result = MciDriverString(device, &words, answer, sizeof answer, a[3]);
            if (result) return result;
            if (a[1] && a[2])
            {
                const unsigned length = (unsigned)strlen(answer);
                if (length + 1 > a[2]) return ParamOverflow;
                if (!Store(a[1], answer, length + 1)) return InvalidParameter;
            }
            return 0;
        }
        unsigned message = 0, flags = 0, arguments[5] = {};
        if (Same(verb, "play")) message = Play;
        else if (Same(verb, "stop")) message = Stop;
        else if (Same(verb, "pause")) message = Pause;
        else if (Same(verb, "resume")) message = Resume;
        else if (Same(verb, "seek")) message = Seek;
        else if (Same(verb, "close")) message = Close;
        else if (Same(verb, "status")) message = AskStatus;
        else if (Same(verb, "set")) message = Set;
        else if (Same(verb, "capability")) message = GetDevCaps;
        else return UnrecognizedCommand;
        unsigned item = 0;
        for (unsigned i = 2; i < words.count; ++i)
        {
            const char *word = words.word[i];
            if (Same(word, "wait")) flags |= Wait;
            else if (Same(word, "notify")) flags |= Notify;
            else if (Same(word, "from") && i + 1 < words.count)
            {
                if (!Number(words.word[++i], arguments[1])) return BadInteger;
                flags |= From;
            }
            else if (Same(word, "to") && i + 1 < words.count)
            {
                // "seek to start" and "seek to end" are the two places where
                // the word behind "to" is a name and not a number.
                const char *next = words.word[i + 1];
                if (message == Seek && Same(next, "start")) { flags |= SeekToStart; ++i; }
                else if (message == Seek && Same(next, "end")) { flags |= SeekToEnd; ++i; }
                else
                {
                    unsigned value = 0;
                    if (!Number(words.word[++i], value)) return BadInteger;
                    if (message == Seek) arguments[1] = value;
                    else arguments[2] = value;
                    flags |= To;
                }
            }
            else if (Same(word, "to")) return MissingParameter;
            else if (message == AskStatus)
            {
                if (Same(word, "position")) item = 2;
                else if (Same(word, "length")) item = 1;
                else if (Same(word, "mode")) item = 4;
                else if (Same(word, "ready")) item = 7;
                else if (Same(word, "media")) item = 5;
                else if (Same(word, "time") && i + 1 < words.count &&
                         Same(words.word[i + 1], "format")) { item = 6; ++i; }
                else if (Same(word, "current") || Same(word, "track")) item = 8;
                else return UnrecognizedKeyword;
                flags |= StatusItem;
                arguments[2] = item;
            }
            else if (message == Set && Same(word, "time") && i + 2 < words.count &&
                     Same(words.word[i + 1], "format"))
            {
                const char *format = words.word[i + 2];
                if (Same(format, "milliseconds") || Same(format, "ms"))
                    arguments[1] = FormatMilliseconds;
                else if (Same(format, "bytes")) arguments[1] = FormatBytes;
                else if (Same(format, "samples")) arguments[1] = FormatSamples;
                else return BadTimeFormat;
                flags |= SetTimeFormat;
                i += 2;
            }
            else return UnrecognizedKeyword;
        }
        // "seek to start" splits into two words; the number form took the
        // first branch already.
        if (message == Seek && !(flags & (To | SeekToStart | SeekToEnd)))
            return MissingParameter;
        const unsigned result = MciCommand(identifier, message, flags, arguments, 5);
        if (result) return result;
        if (message == AskStatus || message == GetDevCaps)
        {
            if (item == 4)
            {
                // The mode is a word of the MMSYSTEM string table, not a
                // number, when it comes back through the string interface.
                const char *text = arguments[1] == ModePlay    ? "playing"
                                   : arguments[1] == ModePause ? "paused"
                                                               : "stopped";
                snprintf(answer, sizeof answer, "%s", text);
            }
            else if (item == 6)
            {
                const char *text = arguments[1] == FormatBytes     ? "bytes"
                                   : arguments[1] == FormatSamples ? "samples"
                                                                   : "milliseconds";
                snprintf(answer, sizeof answer, "%s", text);
            }
            else if (item == 5 || item == 7)
                snprintf(answer, sizeof answer, "%s", arguments[1] ? "true" : "false");
            else snprintf(answer, sizeof answer, "%u", arguments[1]);
        }
    }
    if (a[1] && a[2])
    {
        const unsigned length = (unsigned)strlen(answer);
        if (length + 1 > a[2]) return ParamOverflow;
        if (!Store(a[1], answer, length + 1)) return InvalidParameter;
    }
    return 0;
}

//  probes the name for 80h characters and then asks the 16-bit side
// for the identifier behind the alias.
unsigned Application::MciDeviceIdentifier(ApiName name, const unsigned *a)
{
    (void)name;
    if (!a[0] || !virtual_memory.Access(a[0], 1, false)) return 0;
    const char *alias = ReadString(a[0]);
    if (!alias || !alias[0] || strlen(alias) >= 0x80) return 0;
    for (const auto &device : devices)
        if (device.process && Same(device.alias, alias)) return device.identifier;
    return 0;
}

// ---------------------------------------------------------------------------
// Devices of original 16-bit MCI drivers
// MMSYSTEM opens the driver a device type names in SYSTEM.INI [mci] with
// USER's OpenDriver and hands it MCI_OPEN_DRIVER_PARMS in lParam2:
// the new device identifier, the parameters that follow the file name on the
// [mci] line, wCustomCommandTable FFFFh and wType zero, which DRV_OPEN fills
// in. Then it sends MCI_OPEN_DRIVER (801h) with the caller's flags and open
// block, and every later command goes to the driver's DriverProc with
// the 16-bit form of its parameter block. MCI_CLOSE is MCI_CLOSE_DRIVER (802h)
// and CloseDriver. The drivers are original code hosted in
// the process (src/win16_driver.cpp); the 32-to-16 conversion of the blocks is
// the one WINMM does before its thunk : dwords stay dwords, window
// and DC handles become words with a reserved word beside them, a RECT becomes
// four shorts and strings and answer buffers are copied into 16-bit memory.
//

namespace
{
constexpr unsigned OpenDriverMessage = 0x801, CloseDriverMessage = 0x802;
// The field kinds of a 32-bit parameter block, one character each:
// d DWORD, copied in and back
// w HWND or UINT: a WORD and a reserved WORD in the 16-bit block
// h HDC or HWND: a WORD alone
// r RECT: four LONGs, four SHORTs in the 16-bit block, copied in and back
// s a string the driver reads: copied into 16-bit memory
// o an answer buffer; its size is the dword that follows
struct Shape
{
    unsigned message;
    const char *fields;
};
const Shape shapes[] = {
    {0x802, "d"},      // MCI_CLOSE_DRIVER
    {0x804, "d"},      // MCI_CLOSE
    {0x806, "ddd"},    // MCI_PLAY (dwFrom, dwTo)
    {0x807, "dd"},     // MCI_SEEK (dwTo)
    {0x808, "d"},      // MCI_STOP
    {0x809, "d"},      // MCI_PAUSE
    {0x80a, "dodd"},   // MCI_INFO (lpstrReturn, dwRetSize, dwItem of MCI_DGV_INFO_PARMS)
    {0x80b, "ddd"},    // MCI_GETDEVCAPS (dwReturn, dwItem)
    {0x80d, "ddddd"},  // MCI_SET (dwTimeFormat, dwAudio, the two MCI_DGV_SET_PARMS dwords)
    {0x80e, "dd"},     // MCI_STEP (dwFrames)
    {0x814, "ddddsd"}, // MCI_STATUS (dwReturn, dwItem, dwTrack, lpstrDrive, dwReference)
    {0x830, "dd"},     // MCI_CUE (dwTo)
    {0x835, "dddd"},   // MCI_SIGNAL (dwPosition, dwUserParm, dwPeriod)
    {0x840, "d"},      // MCI_REALIZE
    {0x841, "dwws"},   // MCI_WINDOW (hWnd, nCmdShow, lpstrText)
    {0x842, "dr"},     // MCI_PUT (rc)
    {0x843, "dr"},     // MCI_WHERE (rc)
    {0x844, "dr"},     // MCI_FREEZE (rc)
    {0x845, "dr"},     // MCI_UNFREEZE (rc)
    {0x854, "drh"},    // MCI_UPDATE (rc, hDC)
    {0x855, "d"},      // MCI_RESUME
    {0x873, "ddddss"}, // MCI_SETAUDIO (dwItem, dwValue, dwOver, lpstrAlgorithm, lpstrQuality)
    {0x876, "ddddssd"},// MCI_SETVIDEO (the same and dwSourceNumber)
    {0x87a, "d"},      // MCI_CONFIGURE
};
const Shape *ShapeOf(unsigned message)
{
    for (const auto &shape : shapes)
        if (shape.message == message)
            return &shape;
    return nullptr;
}
// How many 32-bit fields of a shape the caller's flags let the driver read.
// The extended blocks of digital video only reach their extra fields under
// the flags that name them; before that the caller's block may end.
unsigned FieldsUsed(unsigned message, unsigned flags, unsigned count)
{
    if (message == 0x80a) return flags & 0x20000 ? 4 : 3;              // MCI_DGV_INFO_ITEM
    if (message == 0x80d) return flags & 0x30000 ? 5 : 3;              // MCI_DGV_SET_FILEFORMAT, _SPEED
    if (message == 0x814) return flags & 0x60000 ? 6 : 4;              // MCI_DGV_STATUS_DISKSPACE, _REFERENCE
    // MCI_WINDOW: lpstrText counts only with MCI_ANIM_WINDOW_TEXT; a caller that
    // sets the window alone leaves the field as it found it (Media Player).
    if (message == 0x841) return flags & 0x80000 ? 4 : 3;
    return count;
}
} // namespace

void Application::MciProfile(const char *section, const char *key, char *out, unsigned capacity, bool system)
{
    out[0] = 0;
    const unsigned block = Allocate(0x240);
    if (!block)
        return;
    PutString(block, section);
    PutString(block + 0x40, key);
    PutString(block + 0x80, "");
    PutString(block + 0x90, "system.ini");
    if (system)
    {
        const unsigned query[] = {block, block + 0x40, block + 0x80, block + 0x100, 0x100, block + 0x90};
        Api("GetPrivateProfileStringA", query);
    }
    else
    {
        const unsigned query[] = {block, block + 0x40, block + 0x80, block + 0x100, 0x100};
        Api("GetProfileStringA", query);
    }
    snprintf(out, capacity, "%s", ReadString(block + 0x100));
    Free(block);
}

// Whether MMSYSTEM takes an entry of SYSTEM.INI [mci] into its list of
// installed devices. The list is what MCI_SYSINFO counts and
// names and what an open by type looks its driver up in (263 for a
// name that is not in it). Two things keep an entry out:
// - : the value Disabled under
// HKLM\System\CurrentControlSet\Control\MediaResources\MCI\<name> exists
// and does not begin with "0". Setup installs the drivers for a Pioneer
// LaserDisc player and a VISCA recorder that way (MMOPT.INF, Disabled "1"),
// so nothing opens COM1 for them until the user enables the device in the
// Multimedia control panel;
// - : for a 32-bit process, a line for the name in WIN.INI
// [MCICompatibility].
bool Application::MciInstalled(const char *name)
{
    const unsigned area = Allocate(0x200);
    if (!area) return true;
    bool installed = true;
    char key[0x100];
    snprintf(key, sizeof key, "System\\CurrentControlSet\\Control\\MediaResources\\MCI\\%s", name);
    PutString(area + 0x20, key);
    PutString(area + 0x120, "Disabled");
    const unsigned open[] = {0x80000002u, area + 0x20, 0, 0x20019, area};
    if (!Api("RegOpenKeyExA", open))
    {
        Write32(area + 4, 0x10);
        Write32(area + 8, 0);
        const unsigned query[] = {Read32(area), area + 0x120, 0, 0, area + 8, area + 4};
        unsigned char first = 0;
        if (!Api("RegQueryValueExA", query) && Read(area + 8, &first, 1) && first != '0') installed = false;
        const unsigned close[] = {Read32(area)};
        Api("RegCloseKey", close);
    }
    if (installed && !Win16Process())
    {
        PutString(area + 0x20, "MCICompatibility");
        PutString(area + 0x40, name);
        PutString(area + 0x1f0, "");
        const unsigned query[] = {area + 0x20, area + 0x40, area + 0x1f0, area + 0xc0, 0x105};
        if (Api("GetProfileStringA", query)) installed = false;
    }
    Free(area);
    return installed;
}

unsigned Application::OpenMciDriverDevice(const char *kind, const char *driver, const char *element,
                                          const char *alias, unsigned flags, const unsigned *block, unsigned count,
                                          unsigned &identifier)
{
    identifier = 0;
    if (alias && alias[0])
        for (const auto &device : devices)
            if (device.process && Same(device.alias, alias)) return DuplicateAlias;
    MciDevice *slot = nullptr;
    for (auto &device : devices)
        if (!device.process) { slot = &device; break; }
    if (!slot) return OutOfMemory;
    // The [mci] line: the driver file, then its parameters.
    char file[128] = {};
    snprintf(file, sizeof file, "%s", driver);
    const char *parameters = "";
    for (char *p = file; *p; ++p)
        if (*p == ' ')
        {
            *p = 0;
            parameters = driver + (p - file) + 1;
            break;
        }
    if (!Win16HostedReady())
        return OutOfMemory;
    const unsigned memory = Win16GlobalAlloc(0x40, 0x400);
    Win16Block *area = memory ? Win16GlobalBlock(memory) : nullptr;
    if (!area) return OutOfMemory;
    const unsigned base = area->memory, far = area->selector << 16;
    auto put_word = [&](unsigned at, unsigned value) { Win16Word(base + at, value & 0xffff); };
    auto put_dword = [&](unsigned at, unsigned value) { Write32(base + at, value); };
    auto put_text = [&](unsigned at, const char *text, unsigned capacity) {
        char copy[264] = {};
        snprintf(copy, sizeof copy < capacity ? sizeof copy : capacity, "%s", text ? text : "");
        Store(base + at, copy, (unsigned)strlen(copy) + 1);
        return far | at;
    };
    const unsigned device_id = next_identifier;
    // MCI_OPEN_DRIVER_PARMS at +0: wDeviceID, lpstrParams, wCustomCommandTable,
    // wType; the parameters at +10h.
    put_word(0, device_id);
    put_dword(2, put_text(0x10, parameters, 0x70));
    put_word(6, 0xffff);
    put_word(8, 0);
    const unsigned handle = Win16OpenDriver(file, nullptr, far);
    if (state == Failed || !handle)
    {
        Win16GlobalFree(memory);
        return state == Failed ? OutOfMemory : DeviceNotInstalled;
    }
    *slot = MciDevice{};
    slot->process = this;
    slot->identifier = device_id;
    ++next_identifier;
    slot->creator = threads[current_thread].id;
    slot->driver = handle;
    slot->command_table = Win16Word(base + 6);
    slot->driver_type = Win16Word(base + 8);
    slot->shareable = (flags & OpenShareable) != 0;
    snprintf(slot->installed, sizeof slot->installed, "%s", kind);
    snprintf(slot->element, sizeof slot->element, "%s", element ? element : "");
    snprintf(slot->alias, sizeof slot->alias, "%s",
             alias && alias[0] ? alias : element && element[0] ? element : kind);
    // MCI_OPEN_PARMS16 at +80h: dwCallback, wDeviceID and a reserved word,
    // then the three far pointers; the digital video and animation blocks add
    // dwStyle and hWndParent with a reserved word.
    const unsigned open = 0x80;
    put_dword(open, block && count ? block[0] : 0);
    put_word(open + 4, device_id);
    put_word(open + 6, 0);
    if (flags & OpenTypeId) put_dword(open + 8, block && count > 2 ? block[2] : 0);
    else put_dword(open + 8, put_text(0x100, kind, 0x40));
    if (flags & 0x800) put_dword(open + 12, block && count > 3 ? block[3] : 0); // MCI_OPEN_ELEMENT_ID
    else put_dword(open + 12, element ? put_text(0x140, element, 0x104) : 0);
    put_dword(open + 16, alias ? put_text(0x250, alias, 0x80) : 0);
    put_dword(open + 20, block && count > 5 ? block[5] : 0);
    put_word(open + 24, block && count > 6 ? block[6] : 0);
    put_word(open + 26, 0);
    if (pending_open_words)
    {
        // The words of a command string the core table did not know: the
        // driver's own table says what they are (digital video: "parent" and
        // "style"), and their places lie behind the three strings.
        const Words rest = *pending_open_words;
        pending_open_words = nullptr;
        ParsedCommand parsed;
        bool found = false;
        unsigned bad = UnrecognizedKeyword;
        const unsigned table = slot->command_table;
        if (table && table <= sizeof command_tables / sizeof command_tables[0] && command_tables[table - 1].bytes)
            if (Win16Module *module = Win16ModuleOf(command_tables[table - 1].module))
                bad = ParseByTable(module->image.Data() + command_tables[table - 1].offset, command_tables[table - 1].bytes,
                                   "open", rest, 0, parsed, found);
        if (!found) bad = UnrecognizedKeyword;
        if (bad || parsed.string_count)
        {
            Win16GlobalFree(memory);
            Win16CloseDriver(handle, 0, 0, true);
            *slot = MciDevice{};
            return bad ? bad : UnrecognizedKeyword;
        }
        Store(base + open + 20, parsed.block + 20, 0x40);
        flags |= parsed.flags;
    }
    const unsigned result = Win16SendDriverMessage(handle, OpenDriverMessage, flags, far | open) & 0xffff;
    Win16GlobalFree(memory);
    if (state == Failed)
        return OutOfMemory;
    if (log)
    {
        char text[320];
        snprintf(text, sizeof text, "MCI open %s driver=%s device=%u type=%u table=%04x result=%u", kind, file,
                 device_id, slot->driver_type, slot->command_table, result);
        log(text);
    }
    if (result)
    {
        Win16CloseDriver(handle, 0, 0, true);
        *slot = MciDevice{};
        return result;
    }
    identifier = device_id;
    return 0;
}

// Every command but MCI_OPEN and MCI_SYSINFO for a device of a 16-bit driver.
unsigned Application::MciDriverCommand(void *opaque, unsigned message, unsigned flags, unsigned block)
{
    auto &device = *(MciDevice *)opaque;
    if (message < 0x800)
    {
        // DRV_QUERYCONFIGURE carries nothing in its two parameters.
        if (message == 8)
            return Win16SendDriverMessage(device.driver, message, flags, block);
        char text[160];
        snprintf(text, sizeof text,
                 "Driver message %u to the 16-bit MCI driver of %s: its parameters are not converted",
                 message, device.installed);
        Fail(text);
        return 0;
    }
    const unsigned sent = message == Close ? CloseDriverMessage : message;
    const Shape *shape = ShapeOf(sent);
    if (!shape)
    {
        char text[200];
        snprintf(text, sizeof text,
                 "MCI message %03xh to the 16-bit MCI driver of %s: its parameter block is not converted",
                 message, device.installed);
        Fail(text);
        return 0;
    }
    const unsigned fields = FieldsUsed(sent, flags, (unsigned)strlen(shape->fields));
    if (block && !virtual_memory.Access(block, fields * 4 + (strchr(shape->fields, 'r') ? 12 : 0), true))
        return InvalidParameter;
    const unsigned memory = Win16GlobalAlloc(0x40, 0x800);
    Win16Block *area = memory ? Win16GlobalBlock(memory) : nullptr;
    if (!area) return OutOfMemory;
    const unsigned base = area->memory, far = area->selector << 16;
    unsigned in = 0, out = 0, text = 0x100;
    struct Back { char kind; unsigned from, to, size; };
    Back back[8] = {};
    unsigned backs = 0;
    for (unsigned i = 0; block && i < fields; ++i)
    {
        const char kind = shape->fields[i];
        const unsigned value = Read32(block + in);
        switch (kind)
        {
        case 'd':
            Write32(base + out, value);
            back[backs++] = {kind, in, out, 4};
            in += 4, out += 4;
            break;
        case 'w':
            Win16Word(base + out, value & 0xffff);
            Win16Word(base + out + 2, 0);
            in += 4, out += 4;
            break;
        case 'h':
            Win16Word(base + out, value & 0xffff);
            in += 4, out += 2;
            break;
        case 'r':
            for (unsigned k = 0; k < 4; ++k)
                Win16Word(base + out + k * 2, Read32(block + in + k * 4) & 0xffff);
            back[backs++] = {kind, in, out, 8};
            in += 16, out += 8;
            break;
        case 's':
        {
            unsigned pointer = 0;
            if (value)
            {
                const char *string = ReadString(value);
                const unsigned length = string ? (unsigned)strlen(string) : 0;
                if (text + length + 1 > 0x800) { Win16GlobalFree(memory); return ParamOverflow; }
                Store(base + text, string ? string : "", length + 1);
                pointer = far | text;
                text += (length + 2) & ~1u;
            }
            Write32(base + out, pointer);
            in += 4, out += 4;
            break;
        }
        case 'o':
        {
            const unsigned size = i + 1 < fields ? Read32(block + in + 4) : 0;
            unsigned pointer = 0;
            if (value && size)
            {
                if (text + size > 0x800) { Win16GlobalFree(memory); return ParamOverflow; }
                pointer = far | text;
                back[backs++] = {kind, value, text, size};
                text += (size + 1) & ~1u;
            }
            Write32(base + out, pointer);
            in += 4, out += 4;
            break;
        }
        }
    }
    if ((flags & Wait) && device.break_enabled) User::GetAsyncKeyState(device.break_key);
    unsigned result = Win16SendDriverMessage(device.driver, sent, flags, block ? far : 0);
    if (state == Failed)
        return 0;
    // MMSYSTEM: a driver that answers with MCI_RESOURCE_RETURNED in
    // the high word has put a string resource beside the value of dwReturn
    // (MAKEMCIRESOURCE); the caller of the command interface gets the value
    // alone. The answer itself is the low word, and a driver's own error
    // (from MCIERR_CUSTOM_DRIVER_, 200h, on) carries the device in its
    // high word.
    const unsigned raw = result;
    if ((result & 0x10000) && block)
        Win16Word(base + 6, 0);
    result &= 0xffff;
    if (result >= 0x200)
        result |= device.identifier << 16;
    if (log)
    {
        char text[200];
        snprintf(text, sizeof text, "MCI %s message=%03x flags=%08x result=%08x first=%08x second=%08x",
                 device.installed, sent, flags, raw, block && fields > 1 ? Read32(base + 4) : 0,
                 block && fields > 2 ? Read32(base + 8) : 0);
        log(text);
    }
    for (unsigned i = 0; i < backs; ++i)
    {
        const Back &item = back[i];
        if (item.kind == 'd')
            Write32(block + item.from, Read32(base + item.to));
        else if (item.kind == 'r')
            for (unsigned k = 0; k < 4; ++k)
                Write32(block + item.from + k * 4, (unsigned)(int)(short)Win16Word(base + item.to + k * 2));
        else if (item.kind == 'o')
        {
            char answer[0x800] = {};
            const unsigned size = item.size < sizeof answer ? item.size : sizeof answer;
            Read(base + item.to, answer, size);
            answer[size - 1] = 0;
            Store(item.from, answer, (unsigned)strlen(answer) + 1);
        }
    }
    Win16GlobalFree(memory);
    if (message == Close)
    {
        Win16CloseDriver(device.driver, 0, 0, true);
        device = MciDevice{};
    }
    return result;
}

// MCI_SYSINFO, answered by MMSYSTEM itself for every device (Windows 98 builds
// the list of installed devices out of SYSTEM.INI [mci]). QUANTITY writes a
// DWORD into lpstrReturn, NAME the dwNumber-th name, and with MCI_SYSINFO_OPEN
// both count the devices this process has open instead; INSTALLNAME answers
// the [mci] name a device was opened under. wDeviceType selects the entries
// named like the type.
unsigned Application::MciSysInfo(unsigned identifier, unsigned flags, unsigned block)
{
    constexpr unsigned Quantity = 0x100, OpenOnly = 0x200, Name = 0x400, InstallName = 0x800;
    if (!block) return NullParameterBlock;
    if (!virtual_memory.Access(block, 20, true)) return InvalidParameter;
    const unsigned answer = Read32(block + 4), capacity = Read32(block + 8), number = Read32(block + 12);
    const unsigned type = Read32(block + 16) & 0xffff;
    static const char *const type_names[] = {"vcr",       "videodisc",    "overlay", "cdaudio",
                                             "dat",       "scanner",      "animation", "digitalvideo",
                                             "other",     "waveaudio",    "sequencer"};
    // With MCI_ALL_DEVICE_ID every entry counts and
    // wDeviceType is not read at all (MPLAYER leaves FFFFFFFFh in it); for
    // any other identifier a zero type is MCIERR_DEVICE_TYPE_REQUIRED, and a
    // type no entry has simply matches nothing.
    const bool all = (identifier & 0xffff) == 0xffff;
    const char *wanted = !all && type >= 513 && type <= 523 ? type_names[type - 513] : nullptr;
    const bool unknown_type = !all && !wanted;
    auto put = [&](const char *text) -> unsigned {
        const unsigned length = (unsigned)strlen(text);
        if (!answer || !capacity) return ParamOverflow;
        if (!virtual_memory.Access(answer, capacity, true)) return InvalidParameter;
        if (length + 1 > capacity) return ParamOverflow;
        Store(answer, text, length + 1);
        return 0;
    };
    if (flags & InstallName)
    {
        MciDevice *device = DeviceOfIdentifier(identifier);
        if (!device) return InvalidDeviceId;
        return put(device->driver ? device->installed : device->sequencer ? "sequencer" : "waveaudio");
    }
    if (!(flags & (Quantity | Name))) return MissingParameter;
    if (!all && !type) return DeviceTypeRequired;
    // The names: the open devices, or the keys of [mci].
    char names[1024] = {};
    unsigned count = 0;
    const char *list[64] = {};
    if (flags & OpenOnly)
    {
        unsigned used = 0;
        for (const auto &device : devices)
        {
            if (device.process != this) continue;
            const char *installed = device.driver ? device.installed : device.sequencer ? "sequencer" : "waveaudio";
            if (unknown_type || (wanted && !Same(installed, wanted))) continue;
            const unsigned length = (unsigned)strlen(device.alias);
            if (used + length + 1 > sizeof names || count == 64) break;
            memcpy(names + used, device.alias, length + 1);
            list[count++] = names + used;
            used += length + 1;
        }
    }
    else
    {
        const unsigned area = Allocate(0x440);
        if (!area) return OutOfMemory;
        PutString(area, "mci");
        PutString(area + 0x10, "");
        PutString(area + 0x20, "system.ini");
        const unsigned query[] = {area, 0, area + 0x10, area + 0x40, 0x400, area + 0x20};
        const unsigned bytes = Api("GetPrivateProfileStringA", query);
        Read(area + 0x40, names, bytes < sizeof names ? bytes : sizeof names - 1);
        Free(area);
        for (unsigned at = 0; at < sizeof names && names[at] && count < 64;)
        {
            const char *name = names + at;
            at += (unsigned)strlen(name) + 1;
            if (unknown_type || (wanted && !Same(name, wanted))) continue;
            if (!MciInstalled(name)) continue;
            list[count++] = name;
        }
    }
    if (flags & Quantity)
    {
        if (!answer || !virtual_memory.Access(answer, 4, true)) return InvalidParameter;
        Write32(answer, count);
        return 0;
    }
    if (!number || number > count) return OutOfRange;
    return put(list[number - 1]);
}

// The MMSYSTEM entries an MCI driver calls back (MCIAVI imports all of them):
// 705 mciLoadCommandResource(hInstance, lpResName, wType): the RCDATA
// resource of the driver's module that holds its command table; the
// answer is the table's number, FFFFh (MCI_NO_COMMAND_TABLE) without it
// 713 mciFreeCommandResource(wTable)
// 707 mciSetDriverData(wDeviceID, dwData), 708 mciGetDriverData(wDeviceID)
// 710 mciDriverYield(wDeviceID): the default break-key yield procedure
// 711 mciDriverNotify(hwndCallback, wDeviceID, uStatus): MM_MCINOTIFY
// 1104 DrvDefDriverProc, the same rule as USER's DefDriverProc
// 31 DriverCallback(dwCallback, uFlags, hDevice, uMessage, dwUser,
// dwParam1, dwParam2): a posted message for DCB_WINDOW and DCB_TASK,
// a far call for DCB_FUNCTION

bool Application::MciHelperCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words)
{
    result = 0;
    switch (ordinal)
    {
    case 705: // mciLoadCommandResource(hInstance, lpResName, wType)
    {
        words = 4;
        Win16Module *module = Win16ModuleOf(frame.Word(3));
        const unsigned far = frame.Long(1);
        char name[64] = {};
        unsigned id = 0;
        if (far >> 16)
            snprintf(name, sizeof name, "%s", ReadString(frame.Pointer(1)));
        else
            id = far & 0xffff;
        unsigned offset = 0, bytes = 0;
        result = 0xffff;
        // RT_RCDATA: a type and a name by number carry bit 15 in the resource
        // table. Without the bit the table of MCIAVI was never found, and its
        // device had no command table.
        if (!module || !module->image.ResourceAt(0x800a, id ? id | 0x8000 : 0, name[0] ? name : nullptr, offset, bytes))
            return true;
        for (unsigned i = 0; i < sizeof command_tables / sizeof command_tables[0]; ++i)
            if (!command_tables[i].bytes)
            {
                command_tables[i] = {module->handle, offset, bytes};
                result = i + 1;
                break;
            }
        return true;
    }
    case 713: // mciFreeCommandResource(wTable)
    {
        words = 1;
        const unsigned table = frame.Word(0);
        if (table && table <= sizeof command_tables / sizeof command_tables[0] && command_tables[table - 1].bytes)
        {
            command_tables[table - 1] = {};
            result = 1;
        }
        return true;
    }
    case 707: // mciSetDriverData(wDeviceID, dwData)
    {
        words = 3;
        MciDevice *device = DeviceOfIdentifier(frame.Word(2));
        if (device)
        {
            device->driver_data = frame.Long(0);
            result = 1;
        }
        return true;
    }
    case 708: // mciGetDriverData(wDeviceID)
    {
        words = 1;
        const MciDevice *device = DeviceOfIdentifier(frame.Word(0));
        result = device ? device->driver_data : 0;
        return true;
    }
    case 710: // mciDriverYield(wDeviceID)
    {
        words = 1;
        auto *device = DeviceOfIdentifier(frame.Word(0));
        result = device && BreakPressed(*device, ThreadId()) ? 0xffffu : 0;
        return true;
    }
    case 711: // mciDriverNotify(hwndCallback, wDeviceID, uStatus)
        words = 3;
        result = frame.Word(2) &&
                 User::PostMessage((User::HWND)frame.Word(2), McmNotify, frame.Word(0), frame.Word(1));
        return true;
    case 1104: // DrvDefDriverProc(dwDriverID, hDriver, wMessage, lParam1, lParam2)
    {
        words = 8;
        const unsigned message = frame.Word(4);
        result = message == 1 || message == 2 || message == 4 || message == 5 || message == 6 || message == 9 ||
                         message == 10
                     ? 1u
                     : 0u;
        return true;
    }
    case 31: // DriverCallback(dwCallback, uFlags, hDevice, uMessage, dwUser, dwParam1, dwParam2)
    {
        words = 11;
        const unsigned callback = frame.Long(9), type = frame.Word(8) & 7, device = frame.Word(7);
        const unsigned message = frame.Word(6), user = frame.Long(4), first = frame.Long(2), second = frame.Long(0);
        if (!callback)
            return true;
        if (type == 1) // DCB_WINDOW
            result = User::PostMessage((User::HWND)(callback & 0xffff), message, device, first);
        else if (type == 2) // DCB_TASK
        {
            const unsigned post[] = {callback & 0xffff, message, device, first};
            result = Api("PostThreadMessageA", post) != 0;
        }
        else if (type == 3) // DCB_FUNCTION
        {
            const unsigned push[] = {device, message, user >> 16, user & 0xffff, first >> 16, first & 0xffff,
                                     second >> 16, second & 0xffff};
            bool returned = false;
            Win16HostedCall(callback, push, 8, 0, nullptr, &returned);
            result = returned;
        }
        return state != Failed;
    }
    default:
        break;
    }
    return false;
}
} // namespace X86
