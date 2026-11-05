// SPDX-License-Identifier: GPL-2.0-or-later
// The WINMM MIDI output family, midiOut and midiStream, on top of the
// synthesizer in src/sound_synth.h and the mixer in src/sound.h.
// Windows 98 splits this the same way it splits the wave output: WINMM.DLL is
// the ring 3 argument check, MMSYSTEM.DLL and the card driver are the device.
// This file implements the argument checks, and the synthesizer supplies
// the device behavior.
// What the pinned WINMM.DLL says, entry by entry:
// midiOutGetNumDevs asks the driver. The count is the driver's; this port has one
// device, because the synthesizer is one device.
// midiOutGetDevCapsA : a size of zero answers zero before anything
// else, then the handle test with the type mask 77h,
// then IsBadWritePtr on the buffer for 11. The driver
// hands back 34h bytes, which is MIDIOUTCAPS, and
// the wrapper copies as many of them as the caller asked
// for. Unlike the wave form there is no 16 to 32 bit
// fixup: the block has the same shape on both sides.
// midiOutOpen pushes the type 12h and the thunk into the
// shared open worker. That worker probes the
// handle slot for four bytes (11), validates the
// callback kind (error 11), builds the 4Ch-byte
// record with the magic 12345678h at +4,
// the type at +0Ch, the callback at +30h, its instance
// at +34h and the callback kind at +38h, and on success
// sends the message selected by the type:
// 3C7h for an output device, 3C1h for an input one.
// midiStreamOpen does the same by hand, with the type 16h and
// the device identifier zero, and it does not probe
// puDeviceID at all. It sends no open message; the
// driver does that.
// The handle tests use the same checks as the wave family:
// IsBadWritePtr over 4Ch, the magic at +4 and the type
// mask 0FFFFFFE9h, which is 16h, so one handle serves
// midiOut and midiStream. midiStreamProperty
// and midiStreamPosition test 0FFFFFFE8h, so
// they also take an input handle.
// midiOutLongMsg, which midiStreamOut shares with a
// different thunk, is the clearest entry of the family:
// a size below 1Ch is 11, a header IsBadWritePtr rejects
// is 11, a reserved field at +18h that IsBadWritePtr
// rejects over 64h bytes is MIDIERR_UNPREPARED (40h),
// MHDR_INQUEUE already set is MIDIERR_STILLPLAYING
// (41h), and a reserved block without MHDR_PREPARED is
// 40h again.
// midiOutPrepareHeader goes into the same worker the wave
// family uses. Bit 10h of the type takes the MIDI
// branch: a stream handle, which carries bit 4, needs a
// header of 40h bytes, a plain output handle one of 1Ch,
// and the reserved field is at +18h instead of +1Ch.
// The device below all of that is src/sound_synth.cpp. Where the original
// answers an error because no device is there, the error stays; where a device
// answers, the contract of a device applies.
//
#include "neo98_hardware.h"
#include "sound.h"
#include "sound_synth.h"
#include "user.h"
#include "win98_mixer.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned NoError = 0, BadDeviceId = 2, Allocated = 4, InvalidHandle = 5;
constexpr unsigned NoDriver = 6, NoMemory = 7, NotSupported = 8, InvalidParameter = 11;
// MIDIERR_UNPREPARED 40h, MIDIERR_STILLPLAYING 41h, MIDIERR_NOTREADY 43h,
// MIDIERR_BADOPENMODE 46h.
constexpr unsigned Unprepared = 0x40, StillPlaying = 0x41, NotReady = 0x43;
constexpr unsigned BadOpenMode = 0x46;
constexpr unsigned MidiMapper = 0xffffffff;
constexpr unsigned RecordBytes = 0x4c, HeaderBlockBytes = 0x64;
constexpr unsigned RecordMagic = 0x12345678;
// The two device types: 12h for a plain output device, 16h for a
// stream. Both pass the mask 16h every entry of the family tests.
constexpr unsigned MidiOutType = 0x12, MidiStreamType = 0x16;
// MHDR_DONE 1, MHDR_PREPARED 2, MHDR_INQUEUE 4, MHDR_ISSTRM 8.
constexpr unsigned HeaderDone = 1, HeaderPrepared = 2, HeaderInQueue = 4;
// MM_MOM_OPEN, MM_MOM_CLOSE, MM_MOM_DONE and MM_MOM_POSITIONCB, the four
// numbers derives from the device type.
constexpr unsigned MomOpen = 0x3c7, MomClose = 0x3c8, MomDone = 0x3c9;
constexpr unsigned MomPositionCallback = 0x3ca;
constexpr unsigned CallbackMask = 0x70000, CallbackWindow = 0x10000;
constexpr unsigned CallbackThread = 0x20000, CallbackFunction = 0x30000;
constexpr unsigned CallbackEvent = 0x50000;
// The name midiOutGetDevCapsA hands out in szPname.
constexpr auto &MidiOutDeviceName = Neo98Hardware::MidiOutName;
// MOD_SYNTH. A Windows 98 with a sound card but without a synthesizer on it
// reports MOD_MIDIPORT, an external port that makes no sound of its own; the
// registry of the imported image says so itself, with "SB16 MIDI Out [330]"
// and the description "External MIDI Port". This device is not a port: it has
// voices, so it says so.
constexpr unsigned ModSynth = 2;
// MIDICAPS_VOLUME, MIDICAPS_LRVOLUME and MIDICAPS_STREAM. The volume pair
// works because a source of the mixer carries its own volume with a left and
// a right word, and the stream bit is set because this driver plays a
// MIDIEVENT stream itself instead of leaving it to WINMM.
constexpr unsigned MidiCapabilities = 0xb;

// How many headers one stream carries at once and how many finished ones wait
// for their MOM_DONE. Windows has no fixed limit; these are the bounds of this
// port, and a caller that hits them is told so.
constexpr unsigned QueuedHeaders = 64;

struct MidiDevice
{
    Application *process = nullptr;
    unsigned record = 0, source = 0;
    unsigned callback = 0, instance = 0, kind = 0;
    bool stream = false;
    Sound::Synth synth;
    // The stream half. tempo is microseconds per quarter note, division the
    // MIDIPROP_TIMEDIV value: a positive number is ticks per quarter note, a
    // number with bit 15 set is SMPTE, with the negative frame count in the
    // high byte and the ticks per frame in the low one.
    unsigned tempo = 500000, division = 96;
    bool running = false;
    unsigned queue[QueuedHeaders] = {};
    unsigned head = 0, tail = 0;
    unsigned offset = 0;            // byte offset inside the header at `head`
    unsigned long long last = 0;    // the clock of the last pump
    unsigned long long carry = 0;   // microseconds not yet turned into ticks
    unsigned long long ticks = 0;   // stream position in ticks
    unsigned long long played = 0;  // stream position in microseconds
    unsigned wait = 0;              // ticks still to wait before the next event
    bool wait_read = false;         // whether `wait` came from the stream
    unsigned done[QueuedHeaders] = {};
    unsigned done_head = 0, done_count = 0;
    unsigned callbacks[QueuedHeaders] = {};
    unsigned callback_head = 0, callback_count = 0;
};
MidiDevice devices[8];
// The master volume of the device, which a caller who passes a device number
// instead of a stream handle changes.
unsigned master_volume = 0xffffffff;

MidiDevice *DeviceOfRecord(unsigned record)
{
    for (auto &device : devices)
        if (device.process && device.record == record) return &device;
    return nullptr;
}
void RenderSynth(void *owner, short *out, unsigned frames)
{
    ((MidiDevice *)owner)->synth.Render(out, frames);
}
} // namespace

// Validate the handle: IsBadWritePtr over 4Ch, the
// magic at +4 and the type mask. Like the original it asks nothing about the
// process, because the record lives in the shared store of WINMM.
bool Application::MidiDeviceOfHandle(unsigned handle, unsigned types, void *&found)
{
    found = nullptr;
    JoinMultimediaHandles();
    if (!virtual_memory.Access(handle, RecordBytes, true)) return false;
    if (Read32(handle + 4) != RecordMagic) return false;
    if (Read32(handle + 0xc) & ~types) return false;
    found = DeviceOfRecord(handle);
    return found != nullptr;
}

void Application::MidiNotify(void *opaque, unsigned message, unsigned parameter)
{
    auto &device = *(MidiDevice *)opaque;
    switch (device.kind)
    {
    case CallbackWindow:
        User::PostMessage((User::HWND)device.callback, message, device.record, parameter);
        break;
    case CallbackThread:
        User::PostThreadMessage(device.callback, message, device.record, parameter);
        break;
    case CallbackFunction:
    {
        const unsigned arguments[] = {device.record, message, device.instance, parameter, 0};
        Call(device.callback, arguments, 5);
        break;
    }
    case CallbackEvent:
        if (message == MomDone) ThreadApi("SetEvent", &device.callback);
        break;
    default:
        break;
    }
}

namespace
{
// Reads one MIDIEVENT out of a stream buffer. `bytes` is what is left of the
// buffer from `at`. Answers the size of the whole event, or zero when the rest
// is too short to hold one.
struct StreamEvent
{
    unsigned delta = 0, identifier = 0, event = 0, size = 0, data = 0, data_bytes = 0;
};
} // namespace

bool Application::ReadStreamEvent(unsigned address, unsigned bytes, void *into)
{
    auto &event = *(StreamEvent *)into;
    if (bytes < 12 || !virtual_memory.Access(address, 12, false)) return false;
    event.delta = Read32(address);
    event.identifier = Read32(address + 4);
    event.event = Read32(address + 8);
    event.size = 12;
    event.data = address + 12;
    event.data_bytes = 0;
    const unsigned type = (event.event >> 24) & 0xff;
    // MEVT_LONGMSG (80h) and MEVT_COMMENT (82h) carry their bytes behind the
    // event, padded up to a whole dword; every other type is three bytes wide.
    if (type & 0x80)
    {
        event.data_bytes = event.event & 0xffffff;
        const unsigned padded = (event.data_bytes + 3) & ~3u;
        if (padded > bytes - 12) return false;
        event.size = 12 + padded;
    }
    return event.size <= bytes;
}

// One pass of the stream player, on the multimedia thread of the process. It
// turns the microseconds since the last pass into ticks and plays every event
// whose delta time has come due.
void Application::PumpMidiStream(void *opaque)
{
    auto &device = *(MidiDevice *)opaque;
    const unsigned long long now = Now();
    if (!device.last) device.last = now;
    if (!device.running || device.head == device.tail) { device.last = now; return; }
    unsigned long long elapsed = now - device.last;
    device.last = now;
    if (elapsed > 1000000) elapsed = 1000000;
    device.played += elapsed;
    device.carry += elapsed;
    // Ticks per second: for a positive division the tempo says how long a
    // quarter note lasts, for an SMPTE division the frame rate does.
    unsigned long long due = 0;
    if (device.division & 0x8000)
    {
        const unsigned frames = 256 - ((device.division >> 8) & 0xff);
        const unsigned subframes = device.division & 0xff;
        const unsigned long long rate = (unsigned long long)frames * (subframes ? subframes : 1);
        due = device.carry * rate / 1000000;
        if (rate) device.carry -= due * 1000000 / rate;
    }
    else
    {
        const unsigned division = device.division ? device.division : 96;
        const unsigned tempo = device.tempo ? device.tempo : 500000;
        due = device.carry * division / tempo;
        device.carry -= due * tempo / division;
    }
    device.ticks += due;
    unsigned guard = 4096;
    while (guard--)
    {
        if (device.head == device.tail) break;
        const unsigned header = device.queue[device.head % QueuedHeaders];
        if (!virtual_memory.Access(header, 0x1c, true)) { RetireMidiHeader(&device, header); continue; }
        // A stream header names the bytes to play in dwBytesRecorded and
        // falls back to dwBufferLength, which is what MMSYSTEM does.
        const unsigned data = Read32(header);
        const unsigned length = Read32(header + 8) ? Read32(header + 8) : Read32(header + 4);
        StreamEvent event;
        if (device.offset >= length ||
            !ReadStreamEvent(data + device.offset, length - device.offset, &event))
        {
            RetireMidiHeader(&device, header);
            continue;
        }
        if (!device.wait_read) { device.wait = event.delta; device.wait_read = true; }
        // The delta time of the next event has not passed yet; what is left of
        // it waits for the next pass.
        if (device.wait > due) { device.wait -= (unsigned)due; due = 0; break; }
        due -= device.wait;
        device.wait = 0;
        device.wait_read = false;
        device.offset += event.size;
        const unsigned type = (event.event >> 24) & 0x7f;
        const unsigned parameter = event.event & 0xffffff;
        // MEVT_SHORTMSG 0, MEVT_TEMPO 1, MEVT_NOP 2.
        if (type == 0) { Sound::Lock(); device.synth.ShortMessage(parameter); Sound::Unlock(); }
        else if (type == 1) device.tempo = parameter ? parameter : device.tempo;
        if ((event.event >> 24) == 0x80 && event.data_bytes)
        {
            unsigned char bytes[256];
            unsigned take = event.data_bytes < sizeof bytes ? event.data_bytes : sizeof bytes;
            if (Read(event.data, bytes, take))
            {
                Sound::Lock();
                device.synth.LongMessage(bytes, take);
                Sound::Unlock();
            }
        }
        // MEVT_F_CALLBACK asks for MOM_POSITIONCB with the header in hand.
        if (event.event & 0x40000000u && device.callback_count < QueuedHeaders)
            device.callbacks[(device.callback_head + device.callback_count++) % QueuedHeaders] =
                header;
    }
}

// A header the stream is done with: the flags of the original, and MOM_DONE
// on the multimedia thread.
void Application::RetireMidiHeader(void *opaque, unsigned header)
{
    auto &device = *(MidiDevice *)opaque;
    ++device.head;
    device.offset = 0;
    device.wait = 0;
    device.wait_read = false;
    if (virtual_memory.Access(header, 0x1c, true))
    {
        Write32(header + 0x10, (Read32(header + 0x10) & ~HeaderInQueue) | HeaderDone);
        const unsigned block = Read32(header + 0x18);
        if (block && virtual_memory.Access(block, HeaderBlockBytes, true))
            Write32(block + 0x10, (Read32(block + 0x10) & ~HeaderInQueue) | HeaderDone);
    }
    if (device.done_count < QueuedHeaders)
        device.done[(device.done_head + device.done_count++) % QueuedHeaders] = header;
}

void Application::PumpMidiCallbacks()
{
    for (auto &device : devices)
    {
        if (device.process != this) continue;
        if (device.stream) PumpMidiStream(&device);
        while (device.callback_count)
        {
            const unsigned header = device.callbacks[device.callback_head % QueuedHeaders];
            ++device.callback_head;
            --device.callback_count;
            MidiNotify(&device, MomPositionCallback, header);
        }
        while (device.done_count)
        {
            const unsigned header = device.done[device.done_head % QueuedHeaders];
            ++device.done_head;
            --device.done_count;
            MidiNotify(&device, MomDone, header);
        }
    }
}

bool Application::MidiActive()
{
    for (const auto &device : devices)
        if (device.process == this) return true;
    return false;
}

void Application::FreeMidiOut()
{
    for (auto &device : devices)
    {
        if (device.process != this) continue;
        Sound::Shared().Close(device.source);
        if (device.record) FreeMultimediaHandle(device.record, RecordBytes);
        device.process = nullptr;
        device.record = device.source = 0;
        device.head = device.tail = device.done_count = device.callback_count = 0;
        device.synth.Reset();
    }
}

// The capabilities of the one device, MIDIOUTCAPS of 34h bytes.
unsigned Application::MidiOutCapabilities(unsigned buffer, unsigned bytes)
{
    unsigned char caps[0x34] = {};
    const auto word = [&](unsigned at, unsigned value) {
        caps[at] = (unsigned char)value;
        caps[at + 1] = (unsigned char)(value >> 8);
    };
    const auto dword = [&](unsigned at, unsigned value) {
        word(at, value & 0xffff);
        word(at + 2, value >> 16);
    };
    word(0, 1);        // wMid, MM_MICROSOFT
    word(2, 0);        // wPid, unnamed product of that manufacturer
    dword(4, 0x0400);  // vDriverVersion 4.00
    memcpy(caps + 8, MidiOutDeviceName, sizeof MidiOutDeviceName - 1);
    word(0x28, ModSynth);
    word(0x2a, Sound::SynthVoices);   // wVoices
    word(0x2c, Sound::SynthVoices);   // wNotes
    word(0x2e, 0xffff);               // wChannelMask, all sixteen channels
    dword(0x30, MidiCapabilities);
    unsigned copy = bytes < sizeof caps ? bytes : (unsigned)sizeof caps;
    return Store(buffer, caps, copy) ? NoError : InvalidParameter;
}

// Opens the device behind a handle: one source of the mixer, driven by the
// synthesizer. Answers the record or zero.
unsigned Application::OpenMidiDevice(unsigned type, unsigned callback, unsigned instance,
                                     unsigned kind, void *&found)
{
    found = nullptr;
    MidiDevice *slot = nullptr;
    for (auto &device : devices)
        if (!device.process) { slot = &device; break; }
    if (!slot) return 0;
    slot->synth.Reset();
    const unsigned source = Sound::Shared().OpenGenerator(RenderSynth, slot);
    // The synthesizer belongs to the MIDI line of the mixer device.
    if (source) Sound::Shared().SetSourceGroup(source, Win98Mixer::GroupSynth);
    if (!source) return 0;
    const unsigned record = AllocateMultimediaHandle(RecordBytes);
    if (!record) { Sound::Shared().Close(source); return 0; }
    Write32(record, 0);
    Write32(record + 4, RecordMagic);
    Write32(record + 0xc, type);
    Write32(record + 0x30, callback);
    Write32(record + 0x34, instance);
    Write32(record + 0x38, kind);
    slot->process = this;
    slot->record = record;
    slot->source = source;
    slot->callback = callback;
    slot->instance = instance;
    slot->kind = kind;
    slot->stream = type == MidiStreamType;
    slot->tempo = 500000;
    slot->division = 96;
    slot->running = false;
    slot->head = slot->tail = slot->offset = 0;
    slot->done_head = slot->done_count = 0;
    slot->callback_head = slot->callback_count = 0;
    slot->last = slot->carry = slot->ticks = slot->played = 0;
    slot->wait = 0;
    slot->wait_read = false;
    found = slot;
    return record;
}

// The header check that midiOutLongMsg and midiStreamOut share.
unsigned Application::MidiHeaderCheck(unsigned header, unsigned bytes)
{
    if (bytes < 0x1c || !header || !virtual_memory.Access(header, bytes, true))
        return InvalidParameter;
    const unsigned block = Read32(header + 0x18);
    if (!block || !virtual_memory.Access(block, HeaderBlockBytes, true)) return Unprepared;
    if (Read32(header + 0x10) & HeaderInQueue) return StillPlaying;
    if (!(Read32(block + 0x10) & HeaderPrepared)) return Unprepared;
    return NoError;
}

// The device behind MIDI_MAPPER. MIDIMAP.DRV of Win98 reads
// HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Multimedia\MIDIMap:
// with UseScheme 0, CurrentInstrument is the instrument the user picked under
// "Single instrument" on the page MIDI of the Multimedia control panel, the
// szPname of a device (or the key of an instrument below
// MediaProperties\PrivateProperties\Midi\Ports\<device>\Instruments).
// This driver has one device: an entry that names it picks it, and no entry,
// a scheme or a name out of another installation leave the first device, the
// only one there is.
unsigned Application::MappedMidiOutDevice()
{
    unsigned node = 0;
    bool created = false;
    char named[96] = "";
    if (!registry.store.Open(registry.store.roots[1], "Software\\Microsoft\\Windows\\CurrentVersion\\Multimedia\\MIDIMap",
                             false, node, created))
    {
        const Registry::Value *value = nullptr;
        if (!registry.store.Query(node, "CurrentInstrument", value) && value && value->data && value->size)
        {
            unsigned length = value->size < sizeof named ? value->size : (unsigned)sizeof named - 1;
            memcpy(named, value->data, length);
            named[length] = 0;
        }
    }
    // "<device>\Instruments\<instrument>" names the device in front.
    if (char *below = strchr(named, '\\')) *below = 0;
    if (log && trace_calls)
    {
        char text[200];
        snprintf(text, sizeof text, "MIDI mapper instrument=\"%s\" -> device 0 (%s)", named,
                 strcmp(named, Neo98Hardware::MidiOutName) ? "the first device" : "as chosen");
        log(text);
    }
    return 0;
}
unsigned Application::MidiOutApi(ApiName name, const unsigned *a)
{
    auto &mixer = Sound::Shared();
    void *opaque = nullptr;
#define API(n) case API_ID(#n):
    switch (name.id)
    {
    // One device, because the synthesizer is one device.
    API(midiOutGetNumDevs)
        return 1;

    API(midiOutGetDevCapsA)
    {
        if (!a[2]) return NoError;
        unsigned device = 0;
        if (!MultimediaDeviceOfHandle(a[0], 0x77, device)) return InvalidHandle;
        if (!a[1] || !virtual_memory.Access(a[1], a[2], true)) return InvalidParameter;
        if (device != 0 && device != MidiMapper) return BadDeviceId;
        return MidiOutCapabilities(a[1], a[2]);
    }
    //  and the shared worker.
    API(midiOutOpen)
    {
        if (!a[0] || !virtual_memory.Access(a[0], 4, true)) return InvalidParameter;
        const unsigned kind = a[4] & CallbackMask;
        if (kind && kind != CallbackWindow && kind != CallbackThread &&
            kind != CallbackFunction && kind != CallbackEvent)
            return InvalidParameter;
        if (kind == CallbackFunction && !virtual_memory.Access(a[2], 1, false))
            return InvalidParameter;
        if (a[1] != 0 && a[1] != MidiMapper) return BadDeviceId;
        if (a[1] == MidiMapper) MappedMidiOutDevice();
        const unsigned record = OpenMidiDevice(MidiOutType, a[2], a[3], kind, opaque);
        if (!record) return Allocated;
        Write32(a[0], record);
        EnsureMultimediaThread();
        MidiNotify(opaque, MomOpen, 0);
        return NoError;
    }
    // It probes the handle slot and the callback and nothing else;
    // puDeviceID is read, not probed, exactly as the original leaves it.
    API(midiStreamOpen)
    {
        if (!a[0] || !virtual_memory.Access(a[0], 4, true)) return InvalidParameter;
        const unsigned kind = a[5] & CallbackMask;
        if (kind && kind != CallbackWindow && kind != CallbackThread &&
            kind != CallbackFunction && kind != CallbackEvent)
            return InvalidParameter;
        if (kind == CallbackFunction && !virtual_memory.Access(a[3], 1, false))
            return InvalidParameter;
        // cMidi is the number of streams the caller wants; Windows 98 opens
        // one and refuses anything else, because a device has one stream.
        if (a[2] != 1) return NotSupported;
        unsigned device = 0;
        if (a[1] && virtual_memory.Access(a[1], 4, true)) device = Read32(a[1]);
        if (device != 0 && device != MidiMapper) return BadDeviceId;
        if (device == MidiMapper) MappedMidiOutDevice();
        const unsigned record = OpenMidiDevice(MidiStreamType, a[3], a[4], kind, opaque);
        if (!record) return Allocated;
        if (a[1] && virtual_memory.Access(a[1], 4, true)) Write32(a[1], 0);
        Write32(a[0], record);
        EnsureMultimediaThread();
        MidiNotify(opaque, MomOpen, 0);
        return NoError;
    }
    API(midiOutClose) API(midiStreamClose)
    {
        if (!MidiDeviceOfHandle(a[0], MidiStreamType, opaque)) return InvalidHandle;
        auto &device = *(MidiDevice *)opaque;
        // A header that is still queued keeps the device open, which is what
        // MIDIERR_STILLPLAYING says.
        if (device.head != device.tail) return StillPlaying;
        MidiNotify(&device, MomClose, 0);
        mixer.Close(device.source);
        Write32(device.record + 4, 0);
        FreeMultimediaHandle(device.record, RecordBytes);
        device.process = nullptr;
        device.record = device.source = 0;
        device.running = false;
        device.done_count = device.callback_count = 0;
        device.synth.Reset();
        return NoError;
    }
    API(midiOutReset)
    {
        if (!MidiDeviceOfHandle(a[0], MidiStreamType, opaque)) return InvalidHandle;
        auto &device = *(MidiDevice *)opaque;
        // The driver sends note off on every key of every channel and hands
        // every queued header back; that is what midiOutReset promises.
        Sound::Lock();
        device.synth.AllNotesOff();
        Sound::Unlock();
        while (device.head != device.tail)
            RetireMidiHeader(&device, device.queue[device.head % QueuedHeaders]);
        device.running = false;
        return NoError;
    }
    API(midiOutShortMsg)
    {
        if (!MidiDeviceOfHandle(a[0], MidiStreamType, opaque)) return InvalidHandle;
        auto &device = *(MidiDevice *)opaque;
        // A stream handle refuses a short message outside its own stream,
        // which is MIDIERR_BADOPENMODE.
        if (device.stream) return BadOpenMode;
        Sound::Lock();
        device.synth.ShortMessage(a[1]);
        Sound::Unlock();
        return NoError;
    }
    API(midiOutLongMsg)
    {
        if (!MidiDeviceOfHandle(a[0], MidiStreamType, opaque)) return InvalidHandle;
        auto &device = *(MidiDevice *)opaque;
        const unsigned bad = MidiHeaderCheck(a[1], a[2]);
        if (bad) return bad;
        const unsigned data = Read32(a[1]), length = Read32(a[1] + 4);
        unsigned char bytes[256];
        unsigned take = length < sizeof bytes ? length : (unsigned)sizeof bytes;
        if (take && (!data || !virtual_memory.Access(data, take, false))) return InvalidParameter;
        if (take && Read(data, bytes, take))
        {
            Sound::Lock();
            device.synth.LongMessage(bytes, take);
            Sound::Unlock();
        }
        // The block is over the moment it has been sent, so it carries
        // MHDR_DONE at once and waits for its MOM_DONE on the multimedia
        // thread, the way a queued block of the wave family does.
        Write32(a[1] + 0x10, (Read32(a[1] + 0x10) & ~HeaderInQueue) | HeaderDone);
        if (device.done_count < QueuedHeaders)
            device.done[(device.done_head + device.done_count++) % QueuedHeaders] = a[1];
        EnsureMultimediaThread();
        return NoError;
    }
    API(midiOutPrepareHeader)
    {
        if (!MidiDeviceOfHandle(a[0], MidiStreamType, opaque)) return InvalidHandle;
        auto &device = *(MidiDevice *)opaque;
        const unsigned least = device.stream ? 0x40u : 0x1cu;
        if (a[2] < least || !a[1] || !virtual_memory.Access(a[1], least, true))
            return InvalidParameter;
        const unsigned data = Read32(a[1]), length = Read32(a[1] + 4);
        if (length && (!data || !virtual_memory.Access(data, length, false)))
            return InvalidParameter;
        if (Read32(a[1] + 0x10) & HeaderPrepared) return NoError;
        const unsigned block = Allocate(HeaderBlockBytes);
        if (!block) return NoMemory;
        unsigned char zero[HeaderBlockBytes] = {};
        if (!Store(block, zero, sizeof zero)) { Free(block); return NoMemory; }
        Write32(block + 0x10, HeaderPrepared);
        Write32(a[1] + 0x18, block);
        Write32(a[1] + 0x10, Read32(a[1] + 0x10) | HeaderPrepared);
        return NoError;
    }
    API(midiOutUnprepareHeader)
    {
        if (!MidiDeviceOfHandle(a[0], MidiStreamType, opaque)) return InvalidHandle;
        auto &device = *(MidiDevice *)opaque;
        const unsigned least = device.stream ? 0x40u : 0x1cu;
        if (a[2] < least || !a[1] || !virtual_memory.Access(a[1], least, true))
            return InvalidParameter;
        if (Read32(a[1] + 0x10) & HeaderInQueue) return StillPlaying;
        if (!(Read32(a[1] + 0x10) & HeaderPrepared)) return NoError;
        const unsigned block = Read32(a[1] + 0x18);
        if (block) Free(block);
        Write32(a[1] + 0x18, 0);
        Write32(a[1] + 0x10, Read32(a[1] + 0x10) & ~HeaderPrepared);
        return NoError;
    }
    //  through the shared worker.
    API(midiStreamOut)
    {
        if (!MidiDeviceOfHandle(a[0], MidiStreamType, opaque)) return InvalidHandle;
        auto &device = *(MidiDevice *)opaque;
        if (!device.stream) return BadOpenMode;
        const unsigned bad = MidiHeaderCheck(a[1], a[2]);
        if (bad) return bad;
        if (device.tail - device.head >= QueuedHeaders) return Allocated;
        const unsigned data = Read32(a[1]);
        const unsigned length = Read32(a[1] + 8) ? Read32(a[1] + 8) : Read32(a[1] + 4);
        if (length && (!data || !virtual_memory.Access(data, length, false)))
            return InvalidParameter;
        device.queue[device.tail % QueuedHeaders] = a[1];
        ++device.tail;
        Write32(a[1] + 0x10, (Read32(a[1] + 0x10) | HeaderInQueue) & ~HeaderDone);
        const unsigned block = Read32(a[1] + 0x18);
        if (block && virtual_memory.Access(block, HeaderBlockBytes, true))
            Write32(block + 0x10, (Read32(block + 0x10) | HeaderInQueue) & ~HeaderDone);
        EnsureMultimediaThread();
        return NoError;
    }
    API(midiStreamPause)
    {
        if (!MidiDeviceOfHandle(a[0], MidiStreamType, opaque)) return InvalidHandle;
        auto &device = *(MidiDevice *)opaque;
        if (!device.stream) return BadOpenMode;
        device.running = false;
        Sound::Lock();
        device.synth.AllNotesOff();
        Sound::Unlock();
        return NoError;
    }
    API(midiStreamRestart)
    {
        if (!MidiDeviceOfHandle(a[0], MidiStreamType, opaque)) return InvalidHandle;
        auto &device = *(MidiDevice *)opaque;
        if (!device.stream) return BadOpenMode;
        device.running = true;
        device.last = Now();
        EnsureMultimediaThread();
        return NoError;
    }
    API(midiStreamStop)
    {
        if (!MidiDeviceOfHandle(a[0], MidiStreamType, opaque)) return InvalidHandle;
        auto &device = *(MidiDevice *)opaque;
        if (!device.stream) return BadOpenMode;
        device.running = false;
        Sound::Lock();
        device.synth.AllNotesOff();
        Sound::Unlock();
        // Stop hands every queued header back and puts the position to zero;
        // pause does not. That is the difference the two entries carry.
        while (device.head != device.tail)
            RetireMidiHeader(&device, device.queue[device.head % QueuedHeaders]);
        device.ticks = device.played = device.carry = 0;
        return NoError;
    }
    // MIDIPROP_SET 80000000h, MIDIPROP_GET 40000000h,
    // MIDIPROP_TIMEDIV 1, MIDIPROP_TEMPO 2.
    API(midiStreamProperty)
    {
        if (!MidiDeviceOfHandle(a[0], 0x17, opaque)) return InvalidHandle;
        auto &device = *(MidiDevice *)opaque;
        if (!a[1] || !virtual_memory.Access(a[1], 1, true)) return InvalidParameter;
        const bool set = (a[2] & 0x80000000u) != 0, get = (a[2] & 0x40000000u) != 0;
        const unsigned which = a[2] & 0xffff;
        if (set == get) return InvalidParameter;
        if (which != 1 && which != 2) return InvalidParameter;
        if (!virtual_memory.Access(a[1], 8, true)) return InvalidParameter;
        if (Read32(a[1]) < 8) return InvalidParameter;
        if (which == 1)
        {
            // The time division may not change while the stream runs.
            if (set)
            {
                if (device.running) return NotReady;
                const unsigned value = Read32(a[1] + 4) & 0xffff;
                if (!value) return InvalidParameter;
                device.division = value;
            }
            else Write32(a[1] + 4, device.division);
            return NoError;
        }
        if (set) device.tempo = Read32(a[1] + 4) ? Read32(a[1] + 4) : device.tempo;
        else Write32(a[1] + 4, device.tempo);
        return NoError;
    }
    // A stream answers in TIME_MS and TIME_TICKS; anything else
    // falls back to TIME_MS, the way a driver does.
    API(midiStreamPosition)
    {
        if (!MidiDeviceOfHandle(a[0], 0x17, opaque)) return InvalidHandle;
        auto &device = *(MidiDevice *)opaque;
        if (a[2] < 0xc || !a[1] || !virtual_memory.Access(a[1], a[2], true))
            return InvalidParameter;
        unsigned type = Read32(a[1]);
        if (type != 0x20) type = 1;   // TIME_TICKS or TIME_MS
        Write32(a[1], type);
        Write32(a[1] + 4, type == 0x20 ? (unsigned)device.ticks
                                       : (unsigned)(device.played / 1000));
        if (a[2] >= 0x10) Write32(a[1] + 8, 0);
        return NoError;
    }
    // A device number reaches the master volume of the
    // driver, a handle the volume of that one stream.
    API(midiOutGetVolume) API(midiOutSetVolume)
    {
        const bool get = API_IS(name, "midiOutGetVolume");
        if (get && (!a[1] || !virtual_memory.Access(a[1], 4, true))) return InvalidParameter;
        if (MidiDeviceOfHandle(a[0], MidiStreamType, opaque))
        {
            auto &device = *(MidiDevice *)opaque;
            if (get) Write32(a[1], mixer.Volume(device.source));
            else mixer.SetVolume(device.source, a[1]);
            return NoError;
        }
        unsigned device = 0;
        if (!MultimediaDeviceOfHandle(a[0], MidiStreamType, device)) return InvalidHandle;
        if (device != 0 && device != MidiMapper) return BadDeviceId;
        // As with the wave device: the volume of the MIDI device and the
        // volume of the MIDI line of the mixer are one value.
        if (get) Write32(a[1], Sound::Shared().GroupVolume(Win98Mixer::GroupSynth));
        else Sound::Shared().SetGroupVolume(Win98Mixer::GroupSynth, a[1]);
        return NoError;
    }
    //  hands a driver message through. The two the page MIDI of the
    // Multimedia control panel asks a device number through MMSYS.CPL, before it lists a device under "Single instrument":
    // 1 with a place for a doubleword, the number of ports the driver of
    // this device has, and DRV_QUERYDRVENTRY (801h), the name of the
    // driver's key under Control\MediaResources\MIDI.
    API(midiOutMessage)
    {
        if (!MidiDeviceOfHandle(a[0], MidiStreamType, opaque))
        {
            unsigned device = 0;
            if (!MultimediaDeviceOfHandle(a[0], MidiStreamType, device)) return InvalidHandle;
            if (device != 0 && device != MidiMapper) return BadDeviceId;
            if (device == 0 && a[1] == 1 && a[2])
            {
                Write32(a[2], 1);
                return NoError;
            }
            if (device == 0 && a[1] == 0x801 && a[2])
            {
                const char *entry = Neo98Hardware::SoundResource;
                if (a[3] <= strlen(entry)) return NotSupported;
                PutString(a[2], entry);
                return NoError;
            }
        }
        return NotSupported;
    }
    default:
        break;
    }
    Fail(name);
    return NoDriver;
}
} // namespace X86
