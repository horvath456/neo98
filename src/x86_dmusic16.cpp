// SPDX-License-Identifier: GPL-2.0-or-later
// Implement the nine DMUSIC16 port-driver operations through MMSYSTEM. MIDI
// scheduling uses timers and ordinary MIDI input/output calls. Catalogue
// ordinals are target indices plus one. Each private port record tracks its
// device, direction, sharing mode, and notification state. Invalid handles
// return MMSYSERR_INVALHANDLE; systems without MIDI devices receive the
// MMSYSTEM errors.
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
// MMSYSERR_NOERROR 0, BADDEVICEID 2, ALLOCATED 4, INVALHANDLE 5, NODRIVER 6,
// NOMEM 7, INVALPARAM 11. The error 46h is the answer the module keeps for
// a second exclusive open of a device it already holds.
constexpr unsigned NoError = 0, BadDeviceId = 2, Allocated = 4, InvalidHandle = 5;
constexpr unsigned NoDriver = 6, NoMemory = 7, InvalidParameter = 11, Busy = 0x46;
// MIDI_MAPPER as a 16-bit device identifier.
constexpr unsigned MidiMapper = 0xffff;
// The masks the validator takes: 1 is an input port, 2 an output
// port, 3 either.
constexpr unsigned MaskInput = 1, MaskOutput = 2, MaskEither = 3;
// The channel table at +12h of the port record, 16 groups of 16 channels in
// the original; refuses a group or a channel above 15 outright.
constexpr unsigned ChannelLimit = 16;

// The device record, LocalAlloc(LMEM_ZEROINIT, 4Ch) : one per
// device number and direction, shared by the ports that open it. +6 counts
// the ports, +8 the active ones, +0Ah is the device number, bit 0 of +0Ch an
// input and bit 2 shared use, +0Eh the MMSYSTEM handle, and +18h..+21h the
// queue of music_events still to play (head, tail, count; Windows 98 takes the head),
// +22h the music_events that have gone out and wait to be given back.
struct MusicDevice
{
    Application *process = nullptr;
    unsigned id = 0, ports = 0, active = 0, handle = 0;
    bool input = false, shared = false;
    unsigned head = 0, tail = 0, count = 0, done = 0; // node numbers, 0 is none
};
// A port, LocalAlloc(LMEM_ZEROINIT, 14h) : +8 the device, +0Eh
// active, +10h the task that opened it.
struct Port
{
    Application *process = nullptr;
    MusicDevice *device = nullptr;
    bool input = false, active = false;
    unsigned notify = 0;               // the ring-0 event Windows 98 stores at +0Ah
    unsigned char mapped[ChannelLimit] = {}; // the channel table at +12h
    unsigned mapped_port[ChannelLimit] = {};
};
// Sixteen ports are more than the original's own local heap of 2000h bytes
// holds at 14h bytes a piece plus its queues, and far more than DMusic32 opens.
Port music_ports[16];
MusicDevice music_devices[16];
// An event of the queue, : +0 the next one, +4 its time in
// milliseconds, +8 its REFERENCE_TIME, +10h bit 0 a long message, +12h its
// size, +14h the bytes of a short message or the MIDIHDR of a long one. The
// original takes them from pools of 0AAh; this port keeps one pool per
// machine, and an empty pool is MMSYSERR_NOMEM as. A long message
// keeps its MIDIHDR and bytes in guest memory, because MMSYSTEM reads them
// there.
struct QueuedEvent
{
    Application *process = nullptr;
    unsigned next = 0, ms = 0, size = 0, header = 0;
    unsigned long long time = 0;
    bool extended = false, sent = false;
    unsigned char bytes[4] = {};
};
constexpr unsigned EventCount = 2048;
QueuedEvent music_events[EventCount + 1]; // number 0 is "none"
unsigned NewEvent(Application *process)
{
    for (unsigned i = 1; i <= EventCount; ++i)
        if (!music_events[i].process)
        {
            music_events[i] = QueuedEvent{};
            music_events[i].process = process;
            return i;
        }
    return 0;
}
// Argument words per catalogue ordinal, counted off the DMUSIC32 stub.
const unsigned char words[9] = {6, 7, 3, 11, 2, 1, 5, 4, 2};
} // namespace

unsigned Application::DMusic16Words(unsigned ordinal)
{
    return ordinal >= 1 && ordinal <= 9 ? unsigned(words[ordinal - 1]) : ~0u;
}

namespace
{
// The validator, retf 8. It answers the port record or nothing, and
// nothing is MMSYSERR_INVALHANDLE to every caller.
Port *PortOf(Application *process, unsigned handle, unsigned mask)
{
    if (!handle || handle > 16) return nullptr;
    Port &port = music_ports[handle - 1];
    if (port.process != process) return nullptr;
    if (!(mask & (port.input ? MaskInput : MaskOutput))) return nullptr;
    return &port;
}
// The mask of an interrupt flag the original saves and clears around its
// lists has no counterpart here: the port runs one guest
// thread at a time, and the pump below is one of them.
} // namespace

void Application::FreeDMusic16Ports()
{
    for (auto &port : music_ports)
        if (port.process == this) port = Port{};
    for (auto &device : music_devices)
        if (device.process == this) device = MusicDevice{};
    for (auto &event : music_events)
        if (event.process == this) event = QueuedEvent{};
}

// The catalogue ordinals of DMUSIC16.DLL. `stack` addresses the last word the
// 32-bit stub pushed, so the routine's first argument lies at the top of the
// block and the offsets below are counted from there.
unsigned Application::DMusic16(unsigned ordinal, unsigned stack)
{
    const unsigned result = DMusic16Answer(ordinal, stack);
    if (log)
    {
        static const char *const names[] = {"", "MapChannel", "Read", "SetNotifyEvent", "Send", "SetActive",
                                            "Close", "Open", "AllocMappedBuffer", "FreeMappedBuffer"};
        char text[96];
        snprintf(text, sizeof text, "DMUSIC16 ordinal=%u %s result=%08x", ordinal,
                 ordinal <= 9 ? names[ordinal] : "?", result);
        log(text);
    }
    return result;
}
unsigned Application::DMusic16Answer(unsigned ordinal, unsigned stack)
{
    if (DMusic16Words(ordinal) == ~0u) return 0;
    auto word = [&](unsigned at) {
        unsigned short value = 0;
        Read(stack + at, &value, 2);
        return unsigned(value);
    };
    auto dword = [&](unsigned at) { return Read32(stack + at); };
    auto flat = [&](unsigned at) { return selectors.Linear(Read32(stack + at)); };

    switch (ordinal)
    {
    // MapChannel(WORD hInPort, DWORD dwGroup, DWORD dwChannel,
    // WORD hOutPort). The input port is checked with mask 1 and the output
    // port, when it is not zero, with mask 2; and refuse a
    // group or a channel above 15 with MMSYSERR_INVALPARAM. The rest is a
    // write into the table at +12h, so this entry is reconstructible whole:
    // no device is involved.
    case 1:
    {
        Port *in = PortOf(this, word(10), MaskInput);
        if (!in) return InvalidHandle;
        const unsigned out_handle = word(0);
        if (out_handle && !PortOf(this, out_handle, MaskOutput)) return InvalidHandle;
        const unsigned group = dword(6), channel = dword(2);
        if (group >= ChannelLimit || channel >= ChannelLimit) return InvalidParameter;
        in->mapped[channel] = (unsigned char)group;
        in->mapped_port[channel] = out_handle;
        return NoError;
    }
    // Windows 98 Read(WORD hPort, DWORD dwFlatBuffer, LPDWORD pcbSize,
    // LPDWORD pTimeStamp). An input port that never became active recorded
    // nothing, and Windows 98 answers an empty queue with zero and a size of
    // zero. Without a MIDI input device no port ever becomes active, so that
    // is the only answer this entry can give.
    case 2:
    {
        Port *port = PortOf(this, word(12), MaskInput);
        if (!port) return InvalidHandle;
        const unsigned size = flat(4), stamp = flat(0);
        if (size && virtual_memory.Access(size, 4, true)) Write32(size, 0);
        if (stamp && virtual_memory.Access(stamp, 4, true)) Write32(stamp, 0);
        return NoError;
    }
    // SetNotifyEvent(WORD hPort, DWORD hRing0Event). It stores the
    // OpenVxDHandle result at +0Ah and answers zero; no device is involved, so
    // this entry is reconstructible whole.
    case 3:
    {
        Port *port = PortOf(this, word(4), MaskInput);
        if (!port) return InvalidHandle;
        port->notify = dword(0);
        return NoError;
    }
    // Windows 98 Send(WORD hPort, SEGPTR lpBuffer, DWORD cbBuffer, DWORD msTime,
    // REFERENCE_TIME rtTime). First release music_events that have finished.
    // An empty buffer is a flush and answers zero; a
    // buffer under 14h bytes, or an event whose rounded size passes the rest
    // of the buffer, is MMSYSERR_INVALPARAM. Each event is
    // a DMUS_EVENTHEADER (cbEvent, dwChannelGroup, rtDelta, dwFlags) and its
    // bytes, rounded to eight; channel groups above one are skipped.
    // Its time is msTime plus rtDelta / 10000, its place
    // in the queue is by rtTime plus rtDelta, after every event that is not
    // later : the walk starts behind the tail when rtTime is not
    // earlier than the tail, and at the head otherwise. Up to
    // four bytes are a short message; a longer one gets a MIDIHDR
    // that midiOutPrepareHeader prepares on the device's handle, and a failure
    // there is the answer. Then re-arm the timer, handled here by
    // the multimedia thread pump.
    case 4:
    {
        Port *port = PortOf(this, word(20), MaskOutput);
        if (!port) return InvalidHandle;
        MusicDevice &device = *port->device;
        DMusic16Release(&device, false);
        unsigned buffer = selectors.Linear(dword(16)), left = dword(12);
        const unsigned ms_base = dword(8);
        const unsigned long long base = (unsigned long long)dword(0) << 32 | dword(4);
        if (!left) return NoError;
        unsigned previous = 0, cursor = 0;
        if (device.count && !(base < music_events[device.tail].time)) previous = device.tail;
        else if (device.count) cursor = device.head;
        while (left)
        {
            if (left < 0x14 || !buffer || !virtual_memory.Access(buffer, 0x14, false)) return InvalidParameter;
            const unsigned bytes = Read32(buffer), group = Read32(buffer + 4);
            const unsigned long long delta = (unsigned long long)Read32(buffer + 12) << 32 | Read32(buffer + 8);
            const unsigned rounded = (bytes + 0x1b) & ~7u;
            if (rounded > left || !virtual_memory.Access(buffer, rounded, false)) return InvalidParameter;
            const unsigned data = buffer + 0x14;
            buffer += rounded;
            left -= rounded;
            if (group > 1) continue;
            const unsigned number = NewEvent(this);
            if (!number) return NoMemory;
            QueuedEvent &event = music_events[number];
            event.time = base + delta;
            // divides with DIV EBX; a quotient past 32 bits would
            // fault in the original, and this keeps its low half.
            event.ms = ms_base + unsigned(delta / 10000);
            event.size = bytes;
            if (bytes <= 4)
                Read(data, event.bytes, bytes);
            else
            {
                event.extended = true;
                event.header = Allocate(0x40 + bytes);
                if (!event.header)
                {
                    event = QueuedEvent{};
                    return NoMemory;
                }
                unsigned char header[0x40] = {};
                const unsigned place = event.header + 0x40;
                memcpy(header, &place, 4);
                memcpy(header + 4, &bytes, 4);
                Store(event.header, header, sizeof header);
                auto *copy = new unsigned char[bytes];
                if (Read(data, copy, bytes)) Store(place, copy, bytes);
                delete[] copy;
                const unsigned prepare[] = {device.handle, event.header, 0x40};
                if (const unsigned error = Api("midiOutPrepareHeader", prepare))
                {
                    Free(event.header);
                    event = QueuedEvent{};
                    return error;
                }
            }
            while (cursor && !(event.time < music_events[cursor].time))
            {
                previous = cursor;
                cursor = music_events[cursor].next;
            }
            if (previous) music_events[previous].next = number;
            else device.head = number;
            event.next = cursor;
            if (!cursor) device.tail = number;
            previous = number;
            ++device.count;
        }
        return NoError;
    }
    // SetActive(WORD hPort, WORD fActivate) with the body.
    // Activating a port that is active answers zero; otherwise the device
    // counts one more active port, and the first one opens it (:
    // midiOutOpen of the device number with the function as callback,
    // CALLBACK_FUNCTION; for input); a failure takes the count back
    // and is the answer. Deactivating counts down, and the last one plays
    // what is queued, resets and closes.
    case 5:
    {
        Port *port = PortOf(this, word(2), MaskEither);
        if (!port) return InvalidHandle;
        MusicDevice &device = *port->device;
        if (word(0))
        {
            if (port->active) return NoError;
            ++device.active;
            unsigned error = NoError;
            if (device.active == 1)
            {
                if (device.input)
                    error = BadDeviceId; // midiInOpen, and no MIDI input exists here
                else
                {
                    const unsigned out = Allocate(4);
                    if (!out) error = NoMemory;
                    else
                    {
                        Write32(out, 0);
                        const unsigned open[] = {out, device.id == MidiMapper ? ~0u : device.id, 0, 0, 0};
                        error = Api("midiOutOpen", open);
                        device.handle = error ? 0 : Read32(out);
                        Free(out);
                        if (!error) EnsureMultimediaThread();
                    }
                }
            }
            if (error)
            {
                --device.active;
                return error;
            }
            port->active = true;
            return NoError;
        }
        if (!port->active) return NoError;
        if (device.active) --device.active;
        if (!device.active && !device.input && device.handle)
        {
            DMusic16Play(&device, true);
            const unsigned handle[] = {device.handle};
            Api("midiOutReset", handle);
            Api("midiOutClose", handle);
            DMusic16Release(&device, true);
            device.handle = 0;
        }
        port->active = false;
        return NoError;
    }
    // Close(WORD hPort) with the body : SetActive(port, 0), the
    // port leaves its device, and the device with its last port goes too.
    case 6:
    {
        Port *port = PortOf(this, word(0), MaskEither);
        if (!port) return InvalidHandle;
        if (port->active)
        {
            const unsigned short arguments[] = {0, (unsigned short)word(0)};
            const unsigned scratch = Allocate(4);
            if (scratch)
            {
                Store(scratch, arguments, sizeof arguments);
                DMusic16(5, scratch);
                Free(scratch);
            }
        }
        MusicDevice *device = port->device;
        *port = Port{};
        if (device && device->ports && !--device->ports)
        {
            DMusic16Release(device, true);
            *device = MusicDevice{};
        }
        return NoError;
    }
    // Open clears the output handle first. Shared input ports fail with
    // MMSYSERR_ALLOCATED. Validate the device number, allowing MIDI_MAPPER
    // only for output. Allocation failure returns MMSYSERR_NOMEM. Reusing a
    // device requires sharing on both handles; actual playback starts with
    // SetActive.
    case 7:
    {
        const unsigned device_id = word(8), output = word(6), shared = word(4);
        const unsigned out = flat(0);
        if (!out || !virtual_memory.Access(out, 2, true)) return InvalidParameter;
        const unsigned short zero = 0;
        Store(out, &zero, 2);
        if (!output && shared) return Allocated;
        if (output ? device_id != MidiMapper && Api("midiOutGetNumDevs", nullptr) <= device_id
                   : Api("midiInGetNumDevs", nullptr) <= device_id)
            return BadDeviceId;
        Port *port = nullptr;
        for (auto &candidate : music_ports)
            if (!candidate.process) { port = &candidate; break; }
        if (!port) return NoMemory;
        MusicDevice *device = nullptr;
        for (auto &candidate : music_devices)
            if (candidate.process == this && candidate.id == device_id && candidate.input == !output)
                device = &candidate;
        if (device)
        {
            if (!shared || !device->shared) return Busy;
            ++device->ports;
        }
        else
        {
            for (auto &candidate : music_devices)
                if (!candidate.process) { device = &candidate; break; }
            if (!device) return NoMemory;
            *device = MusicDevice{};
            device->process = this;
            device->id = device_id;
            device->input = !output;
            device->shared = shared != 0;
            device->ports = 1;
        }
        *port = Port{};
        port->process = this;
        port->device = device;
        port->input = !output;
        const unsigned short handle = (unsigned short)(port - music_ports + 1);
        Store(out, &handle, 2);
        return NoError;
    }
    // AllocMappedBuffer(DWORD lpFlatAddress, DWORD cb): KERNEL 613,
    // KRNL386, with EAX the address and ECX the size, answers in ECX.
    // Nothing for a null address or a zero size; otherwise a row of
    // ((cb - 1) >> 16) + 1 selectors from AllocSelectorArray, each
    // based 64 KiB further and limited to the bytes left from there.
    // ECX is the first selector in the high word and the
    // count in the low one, which is how KERNEL 614 frees the row again.
    case 8:
    {
        const unsigned address = dword(4), bytes = dword(0);
        if (!address || !bytes) return 0;
        const unsigned count = ((bytes - 1) >> 16) + 1;
        if (count > 0xffff) return 0;
        const unsigned first = selectors.CreateRun(count);
        if (!first) return 0;
        unsigned rest = bytes - 1;
        for (unsigned i = 0; i < count; ++i, rest -= 0x10000)
        {
            const unsigned selector = first + i * 8;
            auto *entry = selectors.Find(selector << 16);
            entry->base = address + i * 0x10000;
            entry->limit = rest;
            if (rest < 0x10000) break;
        }
        return first << 16 | count;
    }
    // FreeMappedBuffer(DWORD segptr), KERNEL 614 : nothing
    // for a zero selector or a zero count, otherwise the row goes back.
    case 9:
    {
        const unsigned value = dword(0), first = value >> 16, count = value & 0xffff;
        if (!first || !count) return 0;
        for (unsigned i = 0; i < count; ++i)
            selectors.Unmap((first + i * 8) << 16);
        return 0;
    }
    default:
        break;
    }
    return 0;
}

// The timer routine : every device gets the music_events that are due by
// timeGetTime + 3 ms. A short message goes out with midiOutShortMsg as
// (data[0] | data[1] << 8 | data[2] << 16), a long one with midiOutLongMsg
// after its MIDIHDR has dwUser 1; both wait on the list of sent
// music_events until the next Send gives them back. The original arms a one-shot
// timeSetEvent for the next due event; here the multimedia thread
// of the process runs this every few milliseconds while music_events wait. With
// `flush`, at the last deactivation, every event goes at once, but a
// short message whose status byte has bit 7 or bit 4 set is dropped

void Application::DMusic16Play(void *opaque, bool flush)
{
    auto &device = *(MusicDevice *)opaque;
    const unsigned now = Api("timeGetTime", nullptr) + 3;
    while (device.count && (flush || (int)(music_events[device.head].ms - now) <= 0))
    {
        const unsigned number = device.head;
        QueuedEvent &event = music_events[number];
        device.head = event.next;
        if (!--device.count) device.head = device.tail = 0;
        if (!event.extended)
        {
            if (!flush || !(event.bytes[0] & 0x90))
            {
                const unsigned message[] = {device.handle,
                                            unsigned(event.bytes[0]) | unsigned(event.bytes[1]) << 8 |
                                                unsigned(event.bytes[2]) << 16};
                Api("midiOutShortMsg", message);
            }
        }
        else
        {
            event.sent = true;
            Write32(event.header + 12, 1);
            const unsigned message[] = {device.handle, event.header, 0x40};
            Api("midiOutLongMsg", message);
        }
        event.next = device.done;
        device.done = number;
    }
}
// with the walker : a sent long message stays while its
// MIDIHDR has dwUser set (the callback Windows 98 clears it with MOM_DONE, which
// this port reads as MHDR_DONE); every other event is unprepared and freed.
// `all` frees the long ones too ( at the close).
void Application::DMusic16Release(void *opaque, bool all)
{
    auto &device = *(MusicDevice *)opaque;
    unsigned *link = &device.done;
    while (*link)
    {
        QueuedEvent &event = music_events[*link];
        const bool busy = event.extended && !all && !(Read32(event.header + 16) & 1); // MHDR_DONE
        if (busy)
        {
            link = &event.next;
            continue;
        }
        const unsigned next = event.next;
        if (event.extended)
        {
            const unsigned header[] = {device.handle, event.header, 0x40};
            if (device.handle) Api("midiOutUnprepareHeader", header);
            Free(event.header);
        }
        event = QueuedEvent{};
        *link = next;
    }
    if (all)
        while (device.count)
        {
            QueuedEvent &event = music_events[device.head];
            const unsigned next = event.next;
            if (event.extended)
            {
                const unsigned header[] = {device.handle, event.header, 0x40};
                if (device.handle) Api("midiOutUnprepareHeader", header);
                Free(event.header);
            }
            event = QueuedEvent{};
            device.head = next;
            --device.count;
        }
    if (!device.count) device.head = device.tail = 0;
}
void Application::PumpDMusic16()
{
    for (auto &device : music_devices)
        if (device.process == this && device.count && device.handle) DMusic16Play(&device, false);
}
bool Application::DMusic16Active()
{
    for (const auto &device : music_devices)
        if (device.process == this && device.count) return true;
    return false;
}
} // namespace X86
