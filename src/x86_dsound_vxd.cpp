// SPDX-License-Identifier: GPL-2.0-or-later
// Native DSOUND.VXD and the DirectSound device driver. Windows 98 uses wave
// output when no DirectSound driver is available, with three queued blocks
// and substantial latency. With a driver, DSOUND.DLL opens the VxD and sends
// IDsDriver/IDsDriverBuffer operations through DeviceIoControl. This
// implementation uses ring sources in the host mixer (src/sound.h). Each
// request carries an argument block and returns a four-byte HRESULT.
#include "sound.h"
#include "win98_mixer.h"
#include "x86_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strcasecmp: newlib keeps it out of string.h
namespace X86
{
namespace
{
// HRESULTs of dsound.h and winerror.h.
constexpr unsigned Ok = 0, NotImplemented = 0x80004001, NoInterface = 0x80004002;
constexpr unsigned Failure = 0x80004005, OutOfMemory = 0x8007000e, InvalidParameter = 0x80070057;
constexpr unsigned NoDriver = 0x88780078, BadFormat = 0x88780064, InvalidCall = 0x88780032;
constexpr unsigned Allocated = 0x8878000a;
// DSDDESC_USESYSTEMMEMORY 4: DSOUND hands the memory of a secondary buffer to
// the driver. DSDDESC_DONTNEEDPRIMARYLOCK 8 and DSDDESC_DONTNEEDSECONDARYLOCK
// 10h: the memory is plain memory, nothing happens at Lock and Unlock.
// DSDDESC_DONTNEEDWRITELEAD 20h: the write cursor of the driver is the real
// one, DSOUND adds nothing to it.
constexpr unsigned DescriptionFlags = 4 | 8 | 0x10 | 0x20;
// DSDRIVERDESC of dsdriver.h.
constexpr unsigned DescriptionBytes = 0x22c;
constexpr unsigned DescName = 4, DescModule = 0x104, DescDevNode = 0x204, DescChecksum = 0x20a;
// The card as the Device Manager names it (src/neo98_hardware.h).
constexpr auto &DeviceName = Neo98Hardware::SoundName;
// The file the page Driver of the card names (Neo98Hardware::SoundLoader):
// the DirectX diagnosis shows its version and date.
constexpr auto &DriverModule = Neo98Hardware::SoundLoader;
// Legacy Win98 DirectSound VxD driver zero. The private property path at
// Windows 98 selects the provider by GUID family; puts the VxD driver
// index in the low four bits of Data1. An arbitrary GUID plays through the
// public interface but cannot be configured by dxdiag.
constexpr unsigned char DriverGuid[16] = {0xc0, 0x92, 0x0b, 0x3d, 0xfc, 0xab, 0xce, 0x11,
                                          0xa3, 0xb3, 0x00, 0xaa, 0x00, 0x4a, 0x9f, 0x0c};
// DSBCAPS_PRIMARYBUFFER and DSBPLAY_LOOPING.
constexpr unsigned PrimaryBuffer = 1, PlayLooping = 1;
// The primary buffer of this card: 32 KiB, the size DSOUND asks for.
constexpr unsigned PrimaryBytes = 0x8000;
constexpr unsigned DriverHandle = 0xd5000001;
constexpr unsigned BufferHandle = 0xd5010000, DestinationHandle = 0xd5020000, MixerHandle = 0xd5030000;

// One buffer of the driver, an IDsDriverBuffer of the original. Its memory is
// a block of the session arena, so every process sees it at one address and
// the mixer reads the same bytes on the thread of the host. A buffer holds a
// source of the mixer only while it plays; its cursor and volume persist
// between plays. Creation still reserves one advertised hardware mixing slot.
struct Buffer
{
    bool used = false, primary = false, looping = false, ended = false;
    unsigned generation = 0;
    unsigned memory = 0, bytes = 0, source = 0, cursor = 0, volume = 0xffffffff;
    unsigned char *host = nullptr;
    Sound::Format format;
    Application *owner = nullptr;
};
constexpr unsigned BufferCapacity = 128;
Buffer buffers[BufferCapacity];
// Keep one mixer source for the primary buffer. DSDRIVERCAPS counts buffers
// that can be mixed together, not entries in the driver's bookkeeping array.
// DSOUND can place further public buffers in its software mixer.
constexpr unsigned HardwareSecondaryCapacity = Sound::MaximumSources - 1;
unsigned FreeHardwareBuffers()
{
    unsigned used = 0;
    for (const auto &buffer : buffers) if (buffer.used && !buffer.primary) ++used;
    return used < HardwareSecondaryCapacity ? HardwareSecondaryCapacity - used : 0;
}
// The mixer destination of DSOUND: the primary buffer with the software mixer
// on top. One per open driver in the original; this card has one driver.
struct Destination
{
    bool used = false, playing = false;
    unsigned buffer = 0, rate = 22050;
};
Destination destinations[4];
bool mixer_running = false;
unsigned driver_opens = 0, primary_memory = 0;

// A handle names a slot and the use the slot is in, so a handle of a buffer
// that is gone never reaches the buffer that took its place.
unsigned HandleOf(const Buffer &buffer)
{
    return BufferHandle | (buffer.generation & 0xff) << 8 | (unsigned)(&buffer - buffers);
}
Buffer *BufferOf(unsigned handle)
{
    const unsigned index = handle & 0xff;
    if ((handle & 0xffff0000u) != BufferHandle || index >= BufferCapacity || !buffers[index].used) return nullptr;
    return ((handle >> 8) & 0xff) == (buffers[index].generation & 0xff) ? &buffers[index] : nullptr;
}
Destination *DestinationOf(unsigned handle)
{
    const unsigned index = handle - DestinationHandle;
    return handle >= DestinationHandle && index < 4 && destinations[index].used ? &destinations[index] : nullptr;
}
// A buffer that plays to its end gives its source back; a card signals the end
// by a play cursor at the end of the buffer (DSOUND.DLL tests exactly that
// and then stops the buffer itself).
void SettleBuffer(Buffer &buffer)
{
    if (!buffer.source || Sound::Shared().RingPlaying(buffer.source)) return;
    Sound::Shared().Close(buffer.source);
    buffer.source = 0;
    buffer.cursor = 0;
    buffer.ended = true;
}
bool StartBuffer(Buffer &buffer, bool looping)
{
    auto &mixer = Sound::Shared();
    SettleBuffer(buffer);
    buffer.ended = false;
    buffer.looping = looping;
    if (!buffer.source)
    {
        buffer.source = mixer.OpenRing(buffer.host, buffer.bytes, buffer.format);
        if (!buffer.source) return false;
        mixer.SetSourceGroup(buffer.source, Win98Mixer::GroupWave);
        mixer.SetVolume(buffer.source, buffer.volume);
        mixer.SetRingCursor(buffer.source, buffer.cursor);
    }
    mixer.PlayRing(buffer.source, looping);
    return true;
}
void HaltBuffer(Buffer &buffer)
{
    SettleBuffer(buffer);
    if (!buffer.source) return;
    buffer.cursor = Sound::Shared().RingCursor(buffer.source);
    Sound::Shared().Close(buffer.source);
    buffer.source = 0;
}
void ReleaseBuffer(Buffer &buffer)
{
    if (buffer.source) Sound::Shared().Close(buffer.source);
    const unsigned generation = buffer.generation + 1;
    buffer = Buffer{};
    buffer.generation = generation;
}
// The primary buffer sounds when a program plays it itself, which DSOUND
// allows with DSSCL_WRITEPRIMARY: it stops its mixer (23h) and plays the
// destination (34h). While the mixer runs, the primary buffer is the place
// the mixer of the original mixes into; this one mixes in the card, so the
// memory stays silent and is not played.
void PlayPrimary(Destination &destination)
{
    Buffer *buffer = BufferOf(destination.buffer);
    if (!buffer) return;
    if (destination.playing && !mixer_running) StartBuffer(*buffer, true);
    else HaltBuffer(*buffer);
}

// The software mixer. In the original it is the half of DSOUND's mixer that
// runs in ring 0: the DLL builds a CMixSource for every buffer that is not a
// hardware buffer (DSBCAPS_LOCSOFTWARE, position notifications, 3D) and hands
// its address to the VxD with MixListAdd; from then on the VxD reads the
// object at every mixing pass and the DLL only changes fields in it. The
// object lies in the shared heap of DSOUND, so it is session memory here and
// this file reads it through the bytes of the host.
//
// The mixer of the original mixes ahead into the primary buffer. This one
// does not need to: the card plays any number of buffers, so a source of the
// software mixer is a ring like a hardware buffer, and what the DLL changes
// in the object (volume, rate, the looping flag) is read at every pass.
//
// The fields of CMixSource this file reads, by the version of DSOUND.DLL.
// 4.09: Play, Stop, GetBytePosition, SetVolumePan.
// 4.08 has the same object.
struct SourceLayout
{
    unsigned bytes, memory, state, format, shift, rate, mute[3], left, right, next, notes;
};
constexpr SourceLayout Layout409 = {0x0c, 0x10, 0x28, 0x30, 0x34, 0x38, {0x3c, 0x40, 0x90}, 0x44, 0x48, 0x60, 0x8c};
// 4.06: Play, Stop. The same object without the third mute
// word, which came with DirectX 8; earlier versions test the other two flags.
constexpr SourceLayout Layout406 = {0x0c, 0x10, 0x28, 0x30, 0x34, 0x38, {0x3c, 0x40, 0x40}, 0x44, 0x48, 0x60, 0x8c};
// The states of a source in the mixer, MIXSOURCESTATE of the original: the DLL
// only asks whether it is zero.
constexpr unsigned SourceStopped = 0, SourceLooping = 2, SourceNotLooping = 3;
// H_16_BITS and H_LOOP of the format word.
constexpr unsigned Format16 = 1, FormatLoop = 0x100;
// CDsbNotes, the position notifications of a buffer : the
// number of entries, the number of them that name a position, the entries
// {offset, ring-0 event} in the order of their offsets with the stop entries
// (offset FFFFFFFFh) last, and the position the last pass reached.
constexpr unsigned NotesCount = 0, NotesPositions = 4, NotesEntries = 8, NotesLast = 0x10;
struct MixSource
{
    unsigned guest = 0, source = 0, bytes = 0, rate = 0, volume = 0, last = 0;
    unsigned char *object = nullptr;
    const SourceLayout *layout = nullptr;
    bool looping = false;
    Application *owner = nullptr;
};
constexpr unsigned MixSourceCapacity = Sound::MaximumSources;
MixSource mix_sources[MixSourceCapacity];
unsigned Field(const unsigned char *object, unsigned offset)
{
    unsigned value = 0;
    memcpy(&value, object + offset, 4);
    return value;
}
void SetField(unsigned char *object, unsigned offset, unsigned value) { memcpy(object + offset, &value, 4); }
MixSource *MixSourceOf(unsigned guest)
{
    for (auto &entry : mix_sources)
        if (entry.guest == guest && entry.source) return &entry;
    return nullptr;
}
} // namespace

// The version of the VxD. The DLL compares the number of ioctl 3Fh with the
// one it was built with and closes the device when they differ, as it would
// with a DSOUND.VXD left over from another DirectX: 06030604h in 4.06,
// 90000804h in 4.08, 88000904h in 4.09.
// The interface behind the number is the same in all three: the same codes,
// the same argument blocks, the same CMixSource but for one word. So this
// VxD does not keep a list of DLLs. It reads the number the DSOUND.DLL of the
// process expects out of its code: the one wrapper that pushes 3Fh and the
// handle of the VxD (6A 3F FF 35), and the call of it that is followed by
// CMP EAX, imm32. A DSOUND.DLL without that pair is not of this interface
// (4.05 of DirectX 5 has no ioctl 3Fh); it gets no VxD and takes the wave
// device, as before.
//
// The one word of CMixSource that differs is the third mute flag at +90h,
// which DirectX 8 added: where it exists, the DLL tests the three in a row

unsigned Application::DsoundVxdVersion()
{
    if (dsound_vxd_version) return dsound_vxd_version;
    for (unsigned i = 0; i < module_watermark; ++i)
    {
        const Module &module = modules[i];
        if (!module.handle || !module.path[0]) continue;
        const char *leaf = module.path;
        for (const char *p = module.path; *p; ++p)
            if (*p == '\\' || *p == '/' || *p == ':') leaf = p + 1;
        if (strcasecmp(leaf, "DSOUND.DLL")) continue;
        if (!virtual_memory.Access(module.handle, 0x40, false)) return 0;
        const unsigned header = module.handle + Read32(module.handle + 0x3c);
        if (!virtual_memory.Access(header, 0xf8 + 0x28, false)) return 0;
        // The first section is the code.
        const unsigned bytes = Read32(header + 0xf8 + 8), start = module.handle + Read32(header + 0xf8 + 0xc);
        if (bytes < 0x1000 || bytes > (4u << 20) || !virtual_memory.Access(start, bytes, false)) return 0;
        unsigned char *code = (unsigned char *)malloc(bytes);
        if (!code) return 0;
        unsigned version = 0;
        bool third = false;
        if (Read(start, code, bytes))
        {
            unsigned wrapper = 0;
            for (unsigned at = 0; at + 4 <= bytes && !wrapper; ++at)
                if (code[at] == 0x6a && code[at + 1] == 0x3f && code[at + 2] == 0xff && code[at + 3] == 0x35) wrapper = at;
            for (unsigned at = 0; wrapper && at + 10 <= bytes && !version; ++at)
            {
                if (code[at] != 0xe8 || code[at + 5] != 0x3d) continue;
                int relative = 0;
                memcpy(&relative, code + at + 1, 4);
                const unsigned target = at + 5 + (unsigned)relative;
                if (target <= wrapper && wrapper - target < 0x40) memcpy(&version, code + at + 6, 4);
            }
            static const unsigned char mutes[] = {0x33, 0xc0, 0x39, 0x41, 0x3c, 0x75};
            static const unsigned char last[] = {0x39, 0x81, 0x90, 0x00, 0x00, 0x00};
            for (unsigned at = 0; at + 13 <= bytes && !third; ++at)
                third = !memcmp(code + at, mutes, 6) && !memcmp(code + at + 7, last, 6);
        }
        free(code);
        dsound_vxd_version = version;
        dsound_vxd_third_mute = third;
        return version;
    }
    return 0;
}

// CreateFileA("\\\\.\\DSOUND.VXD") succeeds when the mixer can play and the
// DSOUND.DLL of the process is one this file knows.
bool Application::DsoundVxdOpen()
{
    if (!DsoundVxdVersion()) return false;
    // NEO98_DSOUND_WAVE=1 leaves DSOUND without its VxD, on the wave device:
    // the reference tools/test_dsound_3d_guest.py compares this driver with.
    if (const char *wave = getenv("NEO98_DSOUND_WAVE"); wave && *wave == '1') return false;
    // The primary buffer is memory of the card, and DSOUND reaches it from
    // every process that uses the device, whichever of them created it: a
    // program writes it with DSSCL_WRITEPRIMARY. So every process that opens
    // the VxD maps it, at the one address.
    const unsigned view = ReserveSessionView("NEO98 DirectSound primary buffer", PrimaryBytes);
    if (!view || !CommitSessionView(view >> 12, PrimaryBytes >> 12)) return false;
    primary_memory = view;
    ++dsound_vxd_opens;
    return true;
}

void Application::DsoundVxdTrace(unsigned code, unsigned in, unsigned bytes, unsigned result)
{
    static const bool wanted = getenv("NEO98_DSOUND_TRACE") != nullptr;
    if (!log || (!wanted && !trace_calls)) return;
    char text[200];
    unsigned v[6] = {};
    for (unsigned i = 0; i < 6 && (i + 1) * 4 <= bytes; ++i) v[i] = Read32(in + i * 4);
    snprintf(text, sizeof text, "DSOUNDVXD %02x in=%08x,%08x,%08x,%08x,%08x,%08x result=%08x process=%x", code,
             v[0], v[1], v[2], v[3], v[4], v[5], result, process_id);
    log(text);
}

// A process that ends or closes the device. DSOUND releases its buffers itself,
// in DDHELP for a process that died; a buffer the process leaves behind
// stops here, so nothing of it keeps sounding. The primary buffer and the
// destination belong to the device object of DSOUND, which every process of
// the session shares; they go when the last process has gone.
void Application::FreeDsoundVxd()
{
    if (!dsound_vxd_opens) return;
    dsound_vxd_opens = 0;
    for (auto &buffer : buffers)
        if (buffer.used && buffer.owner == this && !buffer.primary) ReleaseBuffer(buffer);
    for (auto &entry : mix_sources)
    {
        if (!entry.source || entry.owner != this) continue;
        Sound::Shared().Close(entry.source);
        entry = MixSource{};
    }
    for (auto *process : session_processes)
        if (process && process != this && process->dsound_vxd_opens) return;
    for (auto &buffer : buffers)
        if (buffer.used) ReleaseBuffer(buffer);
    for (auto &entry : mix_sources)
    {
        if (entry.source) Sound::Shared().Close(entry.source);
        entry = MixSource{};
    }
    for (auto &destination : destinations) destination = Destination{};
    mixer_running = false;
    driver_opens = 0;
}

// An event DSOUND holds as a ring-0 handle of OpenVxDHandle.
void Application::DsoundVxdSetEvent(unsigned handle)
{
    const unsigned object = (handle & 0xffffffu) - 1;
    if ((handle & 0xff000000u) == 0x8e000000u && object < KernelObjectSlots) events[object].signaled = true;
}

// The position notifications of a source between the place the last pass
// reached and `position`; with `stopped` the buffer has played to its end and
// the stop entries are due as well.
void Application::DsoundVxdNotify(void *opaque, unsigned position, bool stopped)
{
    Application &app = *this;
    MixSource &entry = *(MixSource *)opaque;
    const auto SetRing0Event = [](unsigned handle) { DsoundVxdSetEvent(handle); };
    const unsigned notes_address = Field(entry.object, entry.layout->notes);
    unsigned char *notes = notes_address ? app.SessionBytes(notes_address, 0x18) : nullptr;
    if (!notes) { entry.last = position; return; }
    const unsigned count = Field(notes, NotesCount), positions = Field(notes, NotesPositions);
    const unsigned table = Field(notes, NotesEntries);
    unsigned char *entries = table && count && count <= 4096 ? app.SessionBytes(table, count * 8) : nullptr;
    if (!entries || positions > count) { entry.last = position; return; }
    const unsigned from = entry.last, to = stopped ? entry.bytes : position;
    for (unsigned i = 0; i < positions; ++i)
    {
        const unsigned offset = Field(entries, i * 8);
        const bool passed = from <= to ? offset >= from && offset < to : offset >= from || offset < to;
        if (passed) SetRing0Event(Field(entries, i * 8 + 4));
    }
    for (unsigned i = positions; stopped && i < count; ++i) SetRing0Event(Field(entries, i * 8 + 4));
    entry.last = stopped ? 0 : position;
    SetField(notes, NotesLast, entry.last);
}

// One pass over the sources of the software mixer: what the DLL has changed
// in a CMixSource reaches its ring, a buffer that has played to its end leaves
// the list the way the mixer of the original takes it out, and the position
// notifications that have come due are signalled.
void Application::DsoundVxdServe()
{
    auto &mixer = Sound::Shared();
    for (auto &buffer : buffers)
        if (buffer.used) SettleBuffer(buffer);
    for (auto &entry : mix_sources)
    {
        if (!entry.source) continue;
        const SourceLayout &layout = *entry.layout;
        if (!mixer.RingPlaying(entry.source))
        {
            DsoundVxdNotify(&entry, 0, true);
            SetField(entry.object, layout.next, 0);
            SetField(entry.object, layout.state, SourceStopped);
            mixer.Close(entry.source);
            entry = MixSource{};
            continue;
        }
        const unsigned format = Field(entry.object, layout.format);
        const bool looping = (format & FormatLoop) != 0;
        if (looping != entry.looping)
        {
            entry.looping = looping;
            mixer.PlayRing(entry.source, looping);
            SetField(entry.object, layout.state, looping ? SourceLooping : SourceNotLooping);
        }
        const unsigned rate = Field(entry.object, layout.rate);
        if (rate != entry.rate) { entry.rate = rate; mixer.SetRingRate(entry.source, rate); }
        unsigned left = Field(entry.object, layout.left), right = Field(entry.object, layout.right);
        if (left > 0xffff) left = 0xffff;
        if (right > 0xffff) right = 0xffff;
        unsigned volume = left | right << 16;
        for (unsigned mute : layout.mute)
            if (Field(entry.object, mute)) volume = 0;
        if (volume != entry.volume) { entry.volume = volume; mixer.SetVolume(entry.source, volume); }
        DsoundVxdNotify(&entry, mixer.RingCursor(entry.source), false);
    }
}

bool Application::DsoundVxdMixing()
{
    for (const auto &entry : mix_sources)
        if (entry.source) return true;
    for (const auto &buffer : buffers)
        if (buffer.used && buffer.source) return true;
    return false;
}

bool Application::DsoundVxdClocked()
{
    return Sound::Shared().RingsPlaying() != 0;
}

// WAVEFORMATEX of the guest as a format of the mixer.
bool Application::DsoundVxdFormat(unsigned address, Sound::Format &format)
{
    unsigned char raw[16] = {};
    if (!address || !Read(address, raw, 16)) return false;
    if ((raw[0] | raw[1] << 8) != 1) return false;   // WAVE_FORMAT_PCM
    format.channels = raw[2] | raw[3] << 8;
    format.rate = raw[4] | raw[5] << 8 | raw[6] << 16 | (unsigned)raw[7] << 24;
    format.bits = raw[14] | raw[15] << 8;
    return Sound::Mixer::Supports(format);
}

unsigned Application::DsoundDevNode()
{
    // The same devnode CONFIGMG and MediaResources publish for this card.
    // A made-up handle lets DSOUND match waveOut to its driver, but prevents
    // its private properties from finding the device's registry key.
    auto &store = registry.store;
    unsigned sound = 0, enumeration = 0;
    bool created = false;
    if (store.Open(store.roots[2], Neo98Hardware::SoundNode, false, sound, created) ||
        store.Open(store.roots[2], "Enum", false, enumeration, created)) return 0;
    unsigned keys[Registry::DevnodeCapacity] = {};
    const unsigned count = Registry::Devnodes(store, enumeration, keys, Registry::DevnodeCapacity);
    for (unsigned i = 0; i < count; ++i)
        if (keys[i] == sound) return 0xc29a1000u + i * 0x80u;
    return 0;
}

unsigned Application::DsoundVxdControl(const unsigned *a)
{
    const unsigned code = a[1], in = a[2], in_bytes = a[3], out = a[4], out_bytes = a[5];
    if (code == ~0u) { FreeDsoundVxd(); return 0; }   // DIOC_CLOSEHANDLE
    if (!code) return 0;                               // DIOC_OPEN
    if (in_bytes && !virtual_memory.Access(in, in_bytes, false)) return 87;
    if (out_bytes && !virtual_memory.Access(out, out_bytes, true)) return 87;
    auto &mixer = Sound::Shared();
    // The handle of the VxD is a global one (FILE_FLAG_GLOBAL_HANDLE), so a
    // process calls in here that never opened the device: DDHELP, when the
    // mixer of DSOUND runs there and writes the primary buffer. It maps the
    // memory of the card at its first call.
    if (!dsound_vxd_mapped)
    {
        const unsigned view = ReserveSessionView("NEO98 DirectSound primary buffer", PrimaryBytes);
        dsound_vxd_mapped = view && CommitSessionView(view >> 12, PrimaryBytes >> 12);
    }
    // Without a host device the mixer runs on the clock of the port; a caller
    // that asks for a cursor gets the cursor of this moment.
    mixer.Advance(Now());
    const auto arg = [&](unsigned index) { return (index + 1) * 4 <= in_bytes ? Read32(in + index * 4) : 0u; };
    const auto writable = [&](unsigned address, unsigned bytes) {
        return address && virtual_memory.Access(address, bytes, true);
    };
    unsigned result = Ok;
    bool answered = true;
    switch (code)
    {
    case 0x01: case 0x02:
        break;
    case 0x3f:
        result = DsoundVxdVersion();
        break;
    // The mutex of the mixer lives in the VxD of the original, because its
    // mixer runs there. This one keeps no state the DLL shares, so the DLL
    // gets none and takes its own.
    case 0x21:
        result = 0;
        break;
    case 0x03: case 0x04:
    {
        // One driver. The enumeration starts with no predecessor and ends
        // behind the first; a description is asked for by its identifier.
        const unsigned guid = code == 3 ? arg(1) : arg(0), description = code == 3 ? arg(2) : arg(1);
        unsigned char asked[16] = {};
        if (code == 3 ? arg(0) != 0 : !Read(guid, asked, 16) || memcmp(asked, DriverGuid, 16)) { result = NoDriver; break; }
        if (!writable(guid, 16) || !writable(description, DescriptionBytes)) { result = InvalidParameter; break; }
        unsigned char block[DescriptionBytes] = {};
        block[0] = DescriptionFlags;
        memcpy(block + DescName, DeviceName, sizeof DeviceName);
        memcpy(block + DescModule, DriverModule, sizeof DriverModule);
        const unsigned devnode = DsoundDevNode();
        memcpy(block + DescDevNode, &devnode, 4);
        // The word a certified driver carries: Windows 98 adds the characters of
        // the module name to B3h and compares the remainder by F7h.
        unsigned sum = 0xb3;
        for (const char *p = DriverModule; *p; ++p) sum += (unsigned)(signed char)*p;
        const unsigned short check = (unsigned short)(sum % 0xf7);
        memcpy(block + DescChecksum, &check, 2);
        if (code == 3) Store(guid, DriverGuid, 16);
        Store(description, block, sizeof block);
        break;
    }
    case 0x06:
    {
        unsigned char asked[16] = {};
        if (!Read(arg(0), asked, 16) || memcmp(asked, DriverGuid, 16)) { result = NoDriver; break; }
        if (!writable(arg(1), 4)) { result = InvalidParameter; break; }
        ++driver_opens;
        Write32(arg(1), DriverHandle);
        break;
    }
    case 0x07:
        if (driver_opens) --driver_opens;
        break;
    case 0x08:
    {
        // DSDRIVERCAPS: DSCAPS without its size. Both primary shapes, every
        // secondary shape, any rate. No 3D in hardware; DSOUND then places
        // such a buffer in its software mixer.
        if (!writable(arg(1), 0x5c)) { result = InvalidParameter; break; }
        unsigned caps[23] = {};
        caps[0] = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x100 | 0x200 | 0x400 | 0x800;
        caps[1] = 100;
        caps[2] = 100000;
        caps[3] = 1;
        caps[4] = caps[5] = caps[6] = HardwareSecondaryCapacity;
        caps[7] = caps[8] = caps[9] = FreeHardwareBuffers();
        Store(arg(1), caps, sizeof caps);
        break;
    }
    case 0x09:
    {
        Sound::Format format;
        const unsigned flags = arg(2), size_address = arg(4), memory_address = arg(5), handle_address = arg(6);
        if (!writable(size_address, 4) || !writable(memory_address, 4) || !writable(handle_address, 4))
        { result = InvalidParameter; break; }
        if (!DsoundVxdFormat(arg(1), format)) { result = BadFormat; break; }
        if (!(flags & PrimaryBuffer) && !FreeHardwareBuffers()) { result = Allocated; break; }
        Buffer *slot = nullptr;
        for (auto &buffer : buffers) if (!buffer.used) { slot = &buffer; break; }
        if (!slot) { result = Allocated; break; }
        unsigned memory = Read32(memory_address), bytes = Read32(size_address);
        if (flags & PrimaryBuffer)
        {
            // The primary buffer is the card's own memory.
            memory = primary_memory;
            bytes = PrimaryBytes;
        }
        unsigned char *host = memory && bytes ? SessionBytes(memory, bytes) : nullptr;
        // Memory outside the session arena cannot be played from the thread of
        // the host; DSOUND then takes its software mixer for this buffer.
        if (!host) { result = flags & PrimaryBuffer ? OutOfMemory : InvalidCall; break; }
        const unsigned generation = slot->generation;
        *slot = Buffer{};
        slot->generation = generation;
        slot->used = true;
        slot->primary = (flags & PrimaryBuffer) != 0;
        slot->memory = memory;
        slot->bytes = bytes;
        slot->host = host;
        slot->format = format;
        slot->owner = this;
        if (slot->primary) memset(host, format.bits == 8 ? 0x80 : 0, bytes);
        Write32(size_address, bytes);
        Write32(memory_address, memory);
        Write32(handle_address, HandleOf(*slot));
        break;
    }
    case 0x0a:
    {
        // A duplicate plays the memory of its original with a cursor and a
        // volume of its own.
        Buffer *original = BufferOf(arg(1));
        if (!original || !writable(arg(2), 4)) { result = InvalidParameter; break; }
        if (!original->primary && !FreeHardwareBuffers()) { result = Allocated; break; }
        Buffer *slot = nullptr;
        for (auto &buffer : buffers) if (!buffer.used) { slot = &buffer; break; }
        if (!slot) { result = Allocated; break; }
        const unsigned generation = slot->generation;
        *slot = *original;
        slot->generation = generation;
        slot->source = 0;
        slot->cursor = 0;
        slot->ended = false;
        slot->owner = this;
        Write32(arg(2), HandleOf(*slot));
        break;
    }
    case 0x0b:
        if (Buffer *buffer = BufferOf(arg(0)))
        {
            for (auto &destination : destinations)
                if (destination.used && destination.buffer == arg(0)) destination.buffer = 0;
            ReleaseBuffer(*buffer);
        }
        break;
    // Lock and Unlock. The description says DSOUND need not call them; a
    // caller that does gets the two stretches of the ring.
    case 0x0c:
    {
        Buffer *buffer = BufferOf(arg(0));
        const unsigned position = arg(5), length = arg(6);
        if (!buffer || position >= buffer->bytes || length > buffer->bytes || !writable(arg(1), 4) || !writable(arg(2), 4))
        { result = InvalidParameter; break; }
        const unsigned first = length < buffer->bytes - position ? length : buffer->bytes - position;
        Write32(arg(1), buffer->memory + position);
        Write32(arg(2), first);
        if (writable(arg(3), 4)) Write32(arg(3), length > first ? buffer->memory : 0);
        if (writable(arg(4), 4)) Write32(arg(4), length - first);
        break;
    }
    case 0x0d:
        if (!BufferOf(arg(0))) result = InvalidParameter;
        break;
    case 0x0e:
    {
        Buffer *buffer = BufferOf(arg(0));
        Sound::Format format;
        if (!buffer) { result = InvalidParameter; break; }
        if (!DsoundVxdFormat(arg(1), format)) { result = BadFormat; break; }
        buffer->format = format;
        buffer->cursor = 0;
        if (buffer->source) mixer.SetRingFormat(buffer->source, format);
        break;
    }
    case 0x0f:
        if (Buffer *buffer = BufferOf(arg(0)))
        {
            if (arg(1) < 100 || arg(1) > 100000) { result = InvalidParameter; break; }
            buffer->format.rate = arg(1);
            if (buffer->source) mixer.SetRingRate(buffer->source, arg(1));
        }
        else result = InvalidParameter;
        break;
    case 0x10:
    {
        // DSVOLUMEPAN: the first two dwords are the amplitude factors of the
        // left and the right channel, 0 to FFFFh, volume and pan together.
        Buffer *buffer = BufferOf(arg(0));
        if (!buffer || !virtual_memory.Access(arg(1), 8, false)) { result = InvalidParameter; break; }
        const unsigned left = Read32(arg(1)), right = Read32(arg(1) + 4);
        buffer->volume = (left > 0xffff ? 0xffff : left) | (right > 0xffff ? 0xffff : right) << 16;
        if (buffer->source) mixer.SetVolume(buffer->source, buffer->volume);
        break;
    }
    case 0x11:
        if (Buffer *buffer = BufferOf(arg(0)))
        {
            SettleBuffer(*buffer);
            buffer->ended = false;
            buffer->cursor = arg(1) < buffer->bytes ? arg(1) : 0;
            if (buffer->source) mixer.SetRingCursor(buffer->source, buffer->cursor);
        }
        else result = InvalidParameter;
        break;
    case 0x12:
    {
        // The play cursor is where the mixer reads next. The write cursor is
        // ahead of it by what the host takes in one piece: that stretch may be
        // gone the next moment, everything behind it is still read. A buffer
        // that has played to its end stands at its end until it is stopped.
        Buffer *buffer = BufferOf(arg(0));
        if (!buffer) { result = InvalidParameter; break; }
        SettleBuffer(*buffer);
        unsigned play = buffer->cursor, write = buffer->cursor;
        if (buffer->ended) play = write = buffer->bytes;
        else if (buffer->source)
        {
            play = mixer.RingCursor(buffer->source);
            unsigned lead = mixer.RingLead(buffer->source);
            if (lead >= buffer->bytes) lead = 0;
            write = (play + lead) % buffer->bytes;
        }
        if (writable(arg(1), 4)) Write32(arg(1), play);
        if (writable(arg(2), 4)) Write32(arg(2), write);
        break;
    }
    case 0x13:
        if (Buffer *buffer = BufferOf(arg(0)))
        {
            // No voice left for it: every source of the mixer is playing.
            if (!StartBuffer(*buffer, (arg(3) & PlayLooping) != 0)) result = Allocated;
        }
        else result = InvalidParameter;
        break;
    case 0x14:
        if (Buffer *buffer = BufferOf(arg(0)))
        {
            HaltBuffer(*buffer);
            buffer->ended = false;
        }
        else result = InvalidParameter;
        break;
    // An event DSOUND holds as a ring-0 handle of OpenVxDHandle.
    case 0x15:
        DsoundVxdSetEvent(arg(0));
        break;
    case 0x16:
        if ((arg(0) & 0xff000000u) == 0x8e000000u && (arg(0) & 0xffffffu) - 1 < KernelObjectSlots &&
            events[(arg(0) & 0xffffffu) - 1].references)
            --events[(arg(0) & 0xffffffu) - 1].references;
        break;
    // PageFile_Get_Version, VMM_Test_Debug_Installed and VMCPD_Get_Version.
    // DSOUND.DLL 4.06 asks them to decide where its mixer runs : in
    // the VxD, when the page file does not go through DOS (pager type 2 or
    // more), VMCPD is 4.02 or later and the coprocessor level is 3, else in
    // DDHELP. Windows 98 is VMCPD 4.10 with a 32-bit pager, so the mixer is
    // the VxD's, as it always is for DSOUND 4.09, which no longer asks.
    case 0x1e: case 0x1f: case 0x20:
    {
        // NEO98_DSOUND_RING3=1 leaves the three unanswered, as a VxD on a
        // machine that pages through DOS would in effect: DSOUND 4.06 then
        // mixes in DDHELP. tools/test_dsound_stream_guest.py --ring3.
        static const char *ring3 = getenv("NEO98_DSOUND_RING3");
        if (ring3 && *ring3 == '1')
        {
            DsoundVxdTrace(code, in, in_bytes, 0xffffffff);
            return 50;   // ERROR_NOT_SUPPORTED
        }
        if (code == 0x1f) { result = 0; break; }
        answered = false;
        if (code == 0x20)
        {
            if (writable(arg(0), 4)) Write32(arg(0), 4);
            if (writable(arg(1), 4)) Write32(arg(1), 10);
            if (writable(arg(2), 4)) Write32(arg(2), 4);
            break;
        }
        if (writable(arg(0), 4)) Write32(arg(0), 0x040a);
        if (writable(arg(1), 4)) Write32(arg(1), 0);
        if (writable(arg(2), 4)) Write32(arg(2), 2);
        break;
    }
    // IUnknown and IDsDriverPropertySet of a buffer. This driver has no
    // property sets: no 3D and no speaker configuration in hardware.
    case 0x39:
        if (writable(arg(2), 4)) Write32(arg(2), 0);
        result = NoInterface;
        break;
    case 0x3b:
        result = 0;
        break;
    case 0x3c: case 0x3d: case 0x3e:
        result = NotImplemented;
        break;

    // The mixer destination: the primary buffer as the place the software
    // mixer writes to. New takes {memory, bytes, buffer, &wave device, wave
    // device number, description flags} and answers its handle.
    case 0x2c:
    {
        result = 0;
        if (!virtual_memory.Access(arg(0), 0x18, false)) break;
        for (unsigned i = 0; i < 4; ++i)
        {
            if (destinations[i].used) continue;
            destinations[i] = Destination{};
            destinations[i].used = true;
            destinations[i].buffer = Read32(arg(0) + 8);
            if (Buffer *buffer = BufferOf(destinations[i].buffer)) destinations[i].rate = buffer->format.rate;
            result = DestinationHandle + i;
            break;
        }
        break;
    }
    case 0x2d: case 0x2f:
        if (Destination *destination = DestinationOf(arg(0)))
        {
            destination->playing = false;
            PlayPrimary(*destination);
            if (code == 0x2d) *destination = Destination{};
        }
        break;
    case 0x2e:
        if (!DestinationOf(arg(0))) result = Failure;
        break;
    case 0x30:
    {
        // The format of the primary buffer. The card plays every PCM shape,
        // so the format DSOUND asks for is the one it gets; the buffer starts
        // over as silence in the new shape.
        Destination *destination = DestinationOf(arg(0));
        Sound::Format format;
        if (!destination) { result = Failure; break; }
        if (!DsoundVxdFormat(arg(1), format)) { result = BadFormat; break; }
        if (Buffer *buffer = BufferOf(destination->buffer))
        {
            HaltBuffer(*buffer);
            buffer->format = format;
            buffer->cursor = 0;
            memset(buffer->host, format.bits == 8 ? 0x80 : 0, buffer->bytes);
            PlayPrimary(*destination);
        }
        destination->rate = format.rate;
        break;
    }
    case 0x31:
        answered = false;
        break;
    case 0x32:
        if (!DestinationOf(arg(0)) || !writable(arg(1), 4)) { result = Failure; break; }
        Write32(arg(1), MixerHandle + (arg(0) - DestinationHandle));
        break;
    case 0x33:
        answered = false;
        break;
    case 0x34: case 0x35:
        answered = false;
        if (Destination *destination = DestinationOf(arg(0)))
        {
            destination->playing = code == 0x34;
            PlayPrimary(*destination);
        }
        break;
    case 0x36:
        result = DestinationOf(arg(0)) ? DestinationOf(arg(0))->rate : 0;
        break;

    // The software mixer.
    case 0x22: case 0x23:
        if (code == 0x23) result = mixer_running ? 1 : 0;
        mixer_running = code == 0x22;
        for (auto &destination : destinations)
        {
            if (!destination.used) continue;
            PlayPrimary(destination);
            // A mixer that runs writes the primary buffer all the time;
            // what a program left in it is gone from then on.
            Buffer *buffer = BufferOf(destination.buffer);
            if (buffer && mixer_running) memset(buffer->host, buffer->format.bits == 8 ? 0x80 : 0, buffer->bytes);
        }
        break;
    // PlayWhenIdle and StopWhenIdle keep the primary buffer running without a
    // source; this card has nothing to keep running. FilterOn and FilterOff
    // switch the delay filter of a 3D buffer, which this mixer does not have.
    case 0x24: case 0x25: case 0x28: case 0x29:
        answered = false;
        // No DSOUND this file knows switches the filter on for a 3D buffer:
        // position, distance and Doppler arrive as volume, balance and rate
        // (tools/test_dsound_3d_guest.py). A caller that does is on record.
        if (code == 0x28 && log) log("DSOUNDVXD FilterOn: the delay filter of a 3D buffer is not applied");
        break;
    // SignalRemix: the DLL has changed a source. Nothing is mixed ahead here,
    // so there is nothing to mix again; the change is read now.
    case 0x2b:
        answered = false;
        DsoundVxdServe();
        break;
    case 0x26:
    {
        // MixListAdd: the source starts at its next-mix position. The mixer
        // of the original sets the state; the DLL takes a state of zero for
        // "not in the list" and adds the source again.
        answered = false;
        // The object of the DLL of this process, with two or three mute
        // words. Its first word is the signature 'MSRC'; an object without
        // it is not a CMixSource this file can read.
        const SourceLayout *layout = !DsoundVxdVersion() ? nullptr : dsound_vxd_third_mute ? &Layout409 : &Layout406;
        unsigned char *object = layout ? SessionBytes(arg(1), 0xc0) : nullptr;
        if (!object && log) log("DSOUNDVXD mix source outside the session arena");
        if (object && Field(object, 0) != 0x4352534d)
        {
            if (log) log("DSOUNDVXD mix source without its signature");
            break;
        }
        if (!object || MixSourceOf(arg(1))) break;
        MixSource *slot = nullptr;
        for (auto &entry : mix_sources) if (!entry.source) { slot = &entry; break; }
        const unsigned memory = Field(object, layout->memory), bytes = Field(object, layout->bytes);
        const unsigned word = Field(object, layout->format), shift = Field(object, layout->shift);
        unsigned char *host = slot && memory && bytes && shift <= 2 ? SessionBytes(memory, bytes) : nullptr;
        if (!host) break;
        Sound::Format format;
        format.bits = word & Format16 ? 16 : 8;
        format.channels = (1u << shift) / (format.bits / 8);
        format.rate = Field(object, layout->rate);
        const unsigned source = mixer.OpenRing(host, bytes, format);
        if (!source) break;
        mixer.SetSourceGroup(source, Win98Mixer::GroupWave);
        *slot = MixSource{};
        slot->guest = arg(1);
        slot->source = source;
        slot->bytes = bytes;
        slot->object = object;
        slot->layout = layout;
        slot->rate = format.rate;
        slot->volume = 0xffffffff;
        slot->looping = (word & FormatLoop) != 0;
        slot->owner = this;
        slot->last = (Field(object, layout->next) << shift) % bytes;
        mixer.SetRingCursor(source, slot->last);
        SetField(object, layout->state, slot->looping ? SourceLooping : SourceNotLooping);
        mixer.PlayRing(source, slot->looping);
        DsoundVxdServe();
        break;
    }
    case 0x27:
        answered = false;
        if (MixSource *entry = MixSourceOf(arg(1)))
        {
            SetField(entry->object, entry->layout->state, SourceStopped);
            mixer.Close(entry->source);
            *entry = MixSource{};
        }
        break;
    case 0x2a:
    {
        // GetBytePosition, as for a hardware buffer: the play cursor is where
        // the mixer reads next, the write cursor one block of the host ahead.
        answered = false;
        DsoundVxdServe();
        unsigned play = 0, write = 0;
        if (MixSource *entry = MixSourceOf(arg(1)))
        {
            play = mixer.RingCursor(entry->source);
            unsigned lead = mixer.RingLead(entry->source);
            if (lead >= entry->bytes) lead = 0;
            write = (play + lead) % entry->bytes;
        }
        if (writable(arg(2), 4)) Write32(arg(2), play);
        if (writable(arg(3), 4)) Write32(arg(3), write);
        break;
    }
    default:
        DsoundVxdTrace(code, in, in_bytes, 0xdeadc0de);
        return 50;   // ERROR_NOT_SUPPORTED
    }
    DsoundVxdTrace(code, in, in_bytes, result);
    if (answered && out_bytes >= 4) Write32(out, result);
    if (a[6] && virtual_memory.Access(a[6], 4, true)) Write32(a[6], answered && out_bytes >= 4 ? 4 : 0);
    return 0;
}
} // namespace X86
