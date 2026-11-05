// SPDX-License-Identifier: GPL-2.0-or-later
// The WINMM wave output family and PlaySound, on top of the mixer in
// src/sound.h.
// Windows 98 splits this in two. WINMM.DLL is the ring-3 argument check, and
// everything it checks is readable in the pinned DLL; below it MMSYSTEM.DLL
// and the wave driver of the sound card do the work. Both halves are ours by
// the DLL rule of PLAN.md, so this file carries the checks of the first half
// exactly and the mixer is the second half.
// What the pinned WINMM.DLL says, entry by entry:
// waveOutGetNumDevs is MOV CL,11h and a jump into the thunk. The
// count is the driver's; this port has one device.
// waveOutGetDevCapsA : a size of zero answers zero before anything
// else, then the handle test with the type mask 77h,
// then IsBadWritePtr on the buffer for 11. The driver
// hands back 32h bytes, which is the 16-bit WAVEOUTCAPS;
//  copies them,  clears the word at 2Eh
// that only the 32-bit layout has, and moves
// dwSupport from 2Eh in the 16-bit block to 30h.
// waveOutOpen pushes the type 22h and the thunk into the
// shared open worker. That worker probes the
// format for 0Eh bytes (11), probes the handle slot for
// four (11), validates the callback kind at
// (11), builds the 4Ch byte record with the
// magic 12345678h at +4, the type at +0Ch, the callback
// at +30h, its instance at +34h and the callback kind at
// +38h, and on success, unless WAVE_FORMAT_QUERY is set,
// sends 3BBh, which computes from the type.
// waveOutPrepareHeader goes into : handle test with mask
// 77h, then, because the type carries 20h, a WAVEHDR of
// at least 20h bytes that IsBadWritePtr accepts, and a
// buffer that IsBadReadPtr accepts for an output device
// ( tests bit 0 of the type, which only an input
// device carries).
// waveOutWrite is the clearest one. A size below 20h or a
// bad header is 11; a reserved field at +1Ch that
// IsBadWritePtr rejects for 64h bytes is WAVERR_UNPREPARED
// (22h); WHDR_INQUEUE already set is WAVERR_STILLPLAYING
// (21h); a reserved block without WHDR_PREPARED is 22h
// again. Then sets WHDR_INQUEUE and clears
// WHDR_DONE before the driver ever sees the block.
// the rest waveOutPause, waveOutRestart,
// waveOutReset, waveOutBreakLoop,
// waveOutGetPosition, waveOutGetPitch
// and their siblings are all the same four instructions:
// IsBadWritePtr over 4Ch, the magic at +4, the type mask
// 0FFFFFFDDh, which is 22h, and then the driver.
// PlaySoundA asks waveOutGetNumDevs first, refuses flags
// outside 201720DFh, and calls the worker.
//  refuses SND_LOOP without SND_ASYNC with
// MMSYSERR_INVALFLAG,  probes a name for 90h
// characters,  keeps one sound per process, and
// when the worker fails and SND_NODEFAULT is not set
//  plays the alias ".Default" instead.
// sndPlaySoundA is PlaySoundA with GetModuleHandleA(0).
//
#include "neo98_hardware.h"
#include "ms_adpcm.h"
#include "sound.h"
#include "user.h"
#include "win98_mixer.h"
#include "x86_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
namespace
{
// MMSYSERR_NOERROR 0, BADDEVICEID 2, ALLOCATED 4, INVALHANDLE 5, NODRIVER 6,
// NOMEM 7, NOTSUPPORTED 8, INVALFLAG 10, INVALPARAM 11.
constexpr unsigned NoError = 0, BadDeviceId = 2, Allocated = 4, InvalidHandle = 5;
constexpr unsigned NoDriver = 6, NoMemory = 7, NotSupported = 8, InvalidFlag = 10;
constexpr unsigned InvalidParameter = 11;
// The answer of the PlaySound worker for a sound it did not find; it is the
// only one retries with the default sound.
constexpr unsigned NotFound = 0xffffffff;
// WAVERR_BADFORMAT 32, WAVERR_STILLPLAYING 33, WAVERR_UNPREPARED 34.
constexpr unsigned BadFormat = 32, StillPlaying = 33, Unprepared = 34;
constexpr unsigned WaveMapper = 0xffffffff;
// The name waveOutGetDevCapsA hands out in szPname. It is the name of this
// driver, and it is the string the preferred device of the wave mapper is
// named by; see PreferredWaveOutDevice.
constexpr auto &WaveOutDeviceName = Neo98Hardware::WaveOutName;
// DRVM_MAPPER_PREFERRED_GET, the one message of the wave mapper this port
// answers, and the status bit it hands back.
constexpr unsigned MapperPreferredGet = 0x2015;
// DRV_QUERYMAPPABLE asks a device whether the wave mapper may use it. Return
// MMSYSERR_NOERROR when supported. SysTray uses this query to find the device
// for its volume icon.
constexpr unsigned QueryMappable = 0x805;
// DRV_QUERYDEVNODE, DRV_RESERVED + 2.
constexpr unsigned QueryDevNode = 0x802;
constexpr unsigned MapperPreferredOnly = 1;
// The device record and the internal block PrepareHeader hands out. Both
// sizes are the ones the originals probe with IsBadWritePtr.
constexpr unsigned RecordBytes = 0x4c, HeaderBlockBytes = 0x64;
constexpr unsigned RecordMagic = 0x12345678, WaveOutType = 0x22;
// WHDR_DONE 1, WHDR_PREPARED 2, WHDR_BEGINLOOP 4, WHDR_ENDLOOP 8, WHDR_INQUEUE 10h.
constexpr unsigned HeaderDone = 1, HeaderPrepared = 2, HeaderInQueue = 0x10;
// MM_WOM_OPEN, MM_WOM_CLOSE and MM_WOM_DONE, which derives from the
// device type and which are the same numbers a callback function receives.
constexpr unsigned WomOpen = 0x3bb, WomClose = 0x3bc, WomDone = 0x3bd;
// The callback kinds accepts.
constexpr unsigned CallbackMask = 0x70000, CallbackWindow = 0x10000;
constexpr unsigned CallbackThread = 0x20000, CallbackFunction = 0x30000;
constexpr unsigned CallbackEvent = 0x50000;
// WAVE_FORMAT_QUERY 1, WAVE_ALLOWSYNC 2, WAVE_MAPPED 4, WAVE_FORMAT_DIRECT 8.
constexpr unsigned FormatQuery = 1;
// WAVE_FORMAT_PCM.
constexpr unsigned PcmTag = 1;
// The TIME_ constants of MMTIME.
constexpr unsigned TimeMilliseconds = 1, TimeSamples = 2, TimeBytes = 4, TimeSmpte = 8;

// One open wave output stream. The identity a program sees is the guest record
// at `record`; everything else lives here, where no guest can reach it.
struct WaveDevice
{
    Application *process = nullptr;
    unsigned record = 0, source = 0;
    unsigned callback = 0, instance = 0, kind = 0;
    unsigned block_align = 1, rate = 44100;
};
WaveDevice devices[Sound::MaximumSources];

// One PlaySound per process, as keeps it.
struct SoundPlayer
{
    Application *process = nullptr;
    unsigned source = 0, block = 0, tag = 0;
    bool loop = false;
};
SoundPlayer players[16];
// The sound of the session, which no process owns (Application::PlaySessionSound).
SoundPlayer session_player;
// The whole of the sound PlaySound is playing, read before it is queued.
unsigned char sound_bytes[2u << 20];

// The mixer copies a submitted block itself, through Application::ReadGuestBytes
// below, because a guest address is not a host pointer in this port.
struct GuestBytes
{
    Application *app;
    unsigned address;
};
WaveDevice *DeviceOfRecord(unsigned record)
{
    for (auto &device : devices)
        if (device.process && device.record == record) return &device;
    return nullptr;
}
// The shared handle store. WINMM.DLL keeps every handle record in one heap it
// creates with HeapCreate(4000000h, 2000h, 0); 4000000h is
// HEAP_SHARED, and the heap handle it keeps the result in lives in the.data
// section of WINMM, which carries IMAGE_SCN_MEM_SHARED. Both halves say the
// same thing: a WINMM handle belongs to the session, not to a process, and the
// entries prove it by testing nothing but IsBadWritePtr, the magic at +4 and
// the type at +0Ch. DirectSound depends on exactly that, because DDHELP.EXE
// opens the wave device and the client process asks it for its position.
// The store here is the session counterpart: one named block of the arena that
// every process maps at the same address, carved into slots of a fixed size.
constexpr unsigned HandleStoreBytes = 0x10000;
constexpr unsigned HandleSlotBytes = 0x80;
constexpr unsigned HandleSlots = HandleStoreBytes / HandleSlotBytes;
unsigned handle_store_base = 0;
unsigned char handle_slot_used[HandleSlots] = {};
} // namespace

// Application has no 16-bit reader, so the places that need one read two bytes
// and put them together.
unsigned Application::ReadGuestWord(unsigned address)
{
    unsigned char bytes[2] = {};
    return Read(address, bytes, 2) ? (unsigned)bytes[0] | ((unsigned)bytes[1] << 8) : 0;
}
bool Application::ReadGuestBytes(void *owner, unsigned char *into, unsigned bytes)
{
    auto &guest = *(GuestBytes *)owner;
    return guest.app->Read(guest.address, into, bytes);
}
// Maps the shared handle store into this process. Windows 98 does it when the
// loader maps the shared sections of WINMM.DLL; here every entry of the family
// calls it, which is idempotent and costs one table lookup once the process
// holds the block.
bool Application::JoinMultimediaHandles()
{
    const unsigned base = ReserveSessionView("NEO98 WINMM handles", HandleStoreBytes);
    if (!base) return false;
    if (!handle_store_base)
    {
        handle_store_base = base;
        if (!CommitSessionView(base >> 12, HandleStoreBytes >> 12)) { handle_store_base = 0; return false; }
    }
    return base == handle_store_base;
}
// One record out of the store. A slot is bigger than the 4Ch bytes the wave
// family needs, because the MCI records of the same store are longer.
unsigned Application::AllocateMultimediaHandle(unsigned bytes)
{
    if (!bytes || bytes > HandleSlotBytes || !JoinMultimediaHandles()) return 0;
    for (unsigned i = 0; i < HandleSlots; ++i)
    {
        if (handle_slot_used[i]) continue;
        handle_slot_used[i] = 1;
        const unsigned address = handle_store_base + i * HandleSlotBytes;
        unsigned char zero[HandleSlotBytes] = {};
        if (Store(address, zero, HandleSlotBytes)) return address;
        handle_slot_used[i] = 0;
        return 0;
    }
    return 0;
}
void Application::FreeMultimediaHandle(unsigned address, unsigned bytes)
{
    if (!handle_store_base || address < handle_store_base) return;
    const unsigned offset = address - handle_store_base;
    if (offset % HandleSlotBytes || offset >= HandleStoreBytes) return;
    unsigned char zero[HandleSlotBytes] = {};
    Store(address, zero, bytes <= HandleSlotBytes ? bytes : HandleSlotBytes);
    handle_slot_used[offset / HandleSlotBytes] = 0;
}
// Handle validation. The three steps are the
// original's, and like the original they ask nothing about the process: the
// record is in the shared store, so the device behind it is found from every
// process that holds the handle.
bool Application::WaveOutDeviceOfHandle(unsigned handle, void *&found)
{
    found = nullptr;
    JoinMultimediaHandles();
    if (!virtual_memory.Access(handle, RecordBytes, true)) return false;
    if (Read32(handle + 4) != RecordMagic) return false;
    if (Read32(handle + 0xc) & ~WaveOutType) return false;
    found = DeviceOfRecord(handle);
    return found != nullptr;
}

// Device notifications. A callback function
// receives the message number itself; a window and a thread receive it as a
// posted message with the handle in wParam; an event is only signalled.
void Application::WaveOutNotify(void *opaque, unsigned message, unsigned parameter)
{
    auto &device = *(WaveDevice *)opaque;
    if (device.process->Win16WaveOutNotify(device.record, message, parameter))
        return;
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
        if (message == WomDone) ThreadApi("SetEvent", &device.callback);
        break;
    default:
        break;
    }
}

// Every block the mixer has finished with gets its WHDR flags and its
// WOM_DONE, in the order it was written.
void Application::CompleteWaveBlocks(void *opaque)
{
    auto &device = *(WaveDevice *)opaque;
    unsigned header = 0;
    while ((header = Sound::Shared().Complete(device.source)))
    {
        if (virtual_memory.Access(header, 0x20, true))
        {
            const unsigned flags = Read32(header + 0x10);
            Write32(header + 0x10, (flags & ~HeaderInQueue) | HeaderDone);
            const unsigned block = Read32(header + 0x1c);
            if (block && virtual_memory.Access(block, HeaderBlockBytes, true))
                Write32(block + 0x10, (Read32(block + 0x10) & ~HeaderInQueue) | HeaderDone);
        }
        WaveOutNotify(&device, WomDone, header);
    }
}

// Runs on the multimedia thread of the process.
void Application::PumpWaveCallbacks()
{
    for (auto &device : devices)
        if (device.process == this) CompleteWaveBlocks(&device);
    PumpWaveIn();
    for (auto &player : players)
    {
        if (player.process != this || !player.source) continue;
        if (Sound::Shared().Complete(player.source) && !player.loop) ClosePlayedSound(&player);
    }
}

bool Application::WaveOutActive()
{
    for (const auto &device : devices)
        if (device.process == this) return true;
    for (const auto &player : players)
        if (player.process == this && player.source) return true;
    return false;
}

void Application::FreeWaveOut()
{
    for (auto &device : devices)
    {
        if (device.process != this) continue;
        Sound::Shared().Close(device.source);
        if (device.record) FreeMultimediaHandle(device.record, RecordBytes);
        device = WaveDevice{};
    }
    for (auto &player : players)
    {
        if (player.process != this) continue;
        if (player.source) Sound::Shared().Close(player.source);
        player = SoundPlayer{};
    }
    Win16FreeWaveOut();
}

// Reads WAVEFORMATEX out of the guest. `bytes` is how much of it the caller is
// allowed to touch;  probes 0Eh, and PCM needs the two bytes after it.
unsigned Application::WaveFormatOf(unsigned address, Sound::Format &format)
{
    if (!address || !virtual_memory.Access(address, 0xe, false)) return InvalidParameter;
    const unsigned tag = ReadGuestWord(address);
    if (tag != PcmTag) return BadFormat;
    if (!virtual_memory.Access(address, 0x10, false)) return InvalidParameter;
    format.channels = ReadGuestWord(address + 2);
    format.rate = Read32(address + 4);
    format.bits = ReadGuestWord(address + 0xe);
    return Sound::Mixer::Supports(format) ? NoError : BadFormat;
}

// The capabilities of the one device. Mid and pid name the driver, and this
// port is its own driver, so both name it and neither is copied from a card.
// dwFormats lists the twelve PCM shapes of the standard three rates;
// dwSupport carries WAVECAPS_VOLUME, WAVECAPS_LRVOLUME and
// WAVECAPS_SAMPLEACCURATE and deliberately not WAVECAPS_PITCH or
// WAVECAPS_PLAYBACKRATE, because the mixer does not change pitch.
unsigned Application::WaveOutCapabilities(unsigned buffer, unsigned bytes)
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
    dword(4, 0x0400);  // vDriverVersion 4.00, the version Windows 98 drivers carry
    memcpy(caps + 8, WaveOutDeviceName, sizeof WaveOutDeviceName - 1);
    dword(0x28, 0xfff);
    word(0x2c, 2);
    word(0x2e, 0);
    dword(0x30, 0x2c);
    unsigned copy = bytes < sizeof caps ? bytes : (unsigned)sizeof caps;
    return Store(buffer, caps, copy) ? NoError : InvalidParameter;
}

// The preferred playback device of the wave mapper. Windows 98 keeps it under
// HKEY_CURRENT_USER\Software\Microsoft\Multimedia\Sound Mapper: the value
// "Playback" is the szPname of the device the user picked in the multimedia
// control panel, and "PreferredOnly" says whether the mapper may fall back to
// another one. The installed image of this port carries exactly that key, with
// "SB16 Wave Out [220]" and "SB16 Wave In [220]", so the names really are the
// ones waveOutGetDevCapsA hands out. The same two value names stand in
// MSACM.DRV, which is the wave mapper of that tree ("!wavemapper:Microsoft
// Sound Mapper"), and DSOUND.DLL Windows 98 reads them itself when the mapper
// does not answer.
// This driver has one device. A "Playback" that names it picks it, and no
// entry, or an entry that names a device this machine does not have, leaves
// the first device, which is what the mapper opens for WAVE_MAPPER anyway.
unsigned Application::PreferredWaveOutDevice(unsigned &flags)
{
    flags = 0;
    unsigned node = 0;
    bool created = false;
    if (registry.store.Open(registry.store.roots[1],
                            "Software\\Microsoft\\Multimedia\\Sound Mapper",
                            false, node, created))
        return 0;
    const auto number = [&](const char *value_name, unsigned &into) {
        const Registry::Value *value = nullptr;
        if (registry.store.Query(node, value_name, value) || !value || !value->data ||
            value->size < 4)
            return false;
        const unsigned char *bytes = (const unsigned char *)value->data;
        into = bytes[0] | bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24;
        return true;
    };
    unsigned only = 0;
    if (number("PreferredOnly", only) && only) flags |= MapperPreferredOnly;
    const Registry::Value *playback = nullptr;
    if (registry.store.Query(node, "Playback", playback) || !playback || !playback->data ||
        !playback->size)
        return 0;
    char named[64] = {};
    unsigned length = playback->size;
    if (length >= sizeof named) length = sizeof named - 1;
    memcpy(named, playback->data, length);
    // The name is matched against the devices this driver has, which is the
    // same walk DSOUND.DLL does with waveOutGetDevCapsA. This
    // driver has one device, so the walk is one comparison. A name out of
    // another installation, like the "SB16 Wave Out [220]" of this image,
    // names no device of this machine; the mapper then stays on the first
    // device and cannot hold itself to a device that is not there, so the
    // PreferredOnly bit goes with it.
    if (strcmp(named, WaveOutDeviceName) != 0) flags &= ~MapperPreferredOnly;
    return 0;
}

unsigned Application::WaveOutApi(ApiName name, const unsigned *a)
{
    auto &mixer = Sound::Shared();
    void *opaque = nullptr;
#define API(n) case API_ID(#n):
    switch (name.id)
    {
    // One device, because the mixer is one device.
    API(waveOutGetNumDevs)
        return 1;

    API(waveOutGetDevCapsA)
    {
        if (!a[2]) return NoError;
        unsigned device = 0;
        if (!MultimediaDeviceOfHandle(a[0], 0x77, device)) return InvalidHandle;
        if (!a[1] || !virtual_memory.Access(a[1], a[2], true)) return InvalidParameter;
        if (device != 0 && device != WaveMapper) return BadDeviceId;
        return WaveOutCapabilities(a[1], a[2]);
    }
    //  and the shared worker.
    API(waveOutOpen)
    {
        Sound::Format format;
        const unsigned bad = WaveFormatOf(a[2], format);
        if (bad == InvalidParameter) return InvalidParameter;
        if (a[1] != 0 && a[1] != WaveMapper) return BadDeviceId;
        const unsigned kind = a[5] & CallbackMask;
        if (kind && kind != CallbackWindow && kind != CallbackThread &&
            kind != CallbackFunction && kind != CallbackEvent)
            return InvalidParameter;
        // A caller without a handle slot is only asking whether the format
        // plays;  reaches the driver with a null handle and nothing
        // else happens.
        if (!a[0]) return bad;
        if (!virtual_memory.Access(a[0], 4, true)) return InvalidParameter;
        if (kind == CallbackFunction && !virtual_memory.Access(a[3], 1, false))
            return InvalidParameter;
        if (bad) return bad;
        if (a[5] & FormatQuery) return NoError;
        WaveDevice *slot = nullptr;
        for (auto &device : devices)
            if (!device.process) { slot = &device; break; }
        if (!slot) return Allocated;
        const unsigned source = mixer.Open(format);
        // Every wave stream belongs to the wave line of the mixer device, so
        // the volume of that line reaches it. src/win98_mixer.h.
        if (source) mixer.SetSourceGroup(source, Win98Mixer::GroupWave);
        if (!source) return Allocated;
        const unsigned record = AllocateMultimediaHandle(RecordBytes);
        if (!record) { mixer.Close(source); return NoMemory; }
        Write32(record, 0);               // the device identifier, as leaves it
        Write32(record + 4, RecordMagic);
        Write32(record + 0xc, WaveOutType);
        Write32(record + 0x30, a[3]);
        Write32(record + 0x34, a[4]);
        Write32(record + 0x38, kind);
        *slot = WaveDevice{};
        slot->process = this;
        slot->record = record;
        slot->source = source;
        slot->callback = a[3];
        slot->instance = a[4];
        slot->kind = kind;
        slot->rate = format.rate;
        slot->block_align = format.channels * (format.bits / 8);
        Write32(a[0], record);
        EnsureMultimediaThread();
        WaveOutNotify(slot, WomOpen, 0);
        return NoError;
    }
    API(waveOutClose)
    {
        if (!WaveOutDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        auto &device = *(WaveDevice *)opaque;
        if (mixer.Busy(device.source)) return StillPlaying;
        WaveOutNotify(&device, WomClose, 0);
        mixer.Close(device.source);
        Write32(device.record + 4, 0);
        FreeMultimediaHandle(device.record, RecordBytes);
        device = WaveDevice{};
        return NoError;
    }
    //  for the wave branch of the shared prepare worker.
    API(waveOutPrepareHeader)
    {
        if (!WaveOutDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        if (a[2] < 0x20 || !a[1] || !virtual_memory.Access(a[1], 0x20, true))
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
        Write32(a[1] + 0x1c, block);
        Write32(a[1] + 0x10, Read32(a[1] + 0x10) | HeaderPrepared);
        return NoError;
    }
    API(waveOutUnprepareHeader)
    {
        if (!WaveOutDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        if (a[2] < 0x20 || !a[1] || !virtual_memory.Access(a[1], 0x20, true))
            return InvalidParameter;
        if (Read32(a[1] + 0x10) & HeaderInQueue) return StillPlaying;
        if (!(Read32(a[1] + 0x10) & HeaderPrepared)) return NoError;
        const unsigned block = Read32(a[1] + 0x1c);
        if (block) Free(block);
        Write32(a[1] + 0x1c, 0);
        Write32(a[1] + 0x10, Read32(a[1] + 0x10) & ~HeaderPrepared);
        return NoError;
    }
    // in its order.
    API(waveOutWrite)
    {
        if (!WaveOutDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        auto &device = *(WaveDevice *)opaque;
        if (a[2] < 0x20 || !a[1] || !virtual_memory.Access(a[1], a[2], true))
            return InvalidParameter;
        const unsigned block = Read32(a[1] + 0x1c);
        if (!block || !virtual_memory.Access(block, HeaderBlockBytes, true)) return Unprepared;
        if (Read32(a[1] + 0x10) & HeaderInQueue) return StillPlaying;
        if (!(Read32(block + 0x10) & HeaderPrepared)) return Unprepared;
        const unsigned data = Read32(a[1]), length = Read32(a[1] + 4);
        const unsigned flags = Read32(a[1] + 0x10), loops = Read32(a[1] + 0x14);
        if (length && (!data || !virtual_memory.Access(data, length, false)))
            return InvalidParameter;
        GuestBytes copy{this, data};
        const unsigned refused =
            mixer.Submit(device.source, length, a[1], flags, loops, Application::ReadGuestBytes, &copy);
        if (refused) return refused == 1 ? Allocated : NoMemory;
        Write32(block + 0x4, length);
        Write32(block + 0x14, loops);
        Write32(block + 0x10, (Read32(block + 0x10) | HeaderInQueue) & ~HeaderDone);
        Write32(a[1] + 0x10, (flags | HeaderInQueue) & ~HeaderDone);
        return NoError;
    }
    API(waveOutPause)
    {
        if (!WaveOutDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        mixer.Pause(((WaveDevice *)opaque)->source);
        return NoError;
    }
    API(waveOutRestart)
    {
        if (!WaveOutDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        mixer.Restart(((WaveDevice *)opaque)->source);
        return NoError;
    }
    API(waveOutReset)
    {
        if (!WaveOutDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        mixer.Reset(((WaveDevice *)opaque)->source);
        // The driver hands every pending block back before WODM_RESET
        // returns: the caller finds WHDR_DONE set and WHDR_INQUEUE clear and
        // may write the same headers again at once. WaveMix does exactly
        // that for every new sound, and a header that still said INQUEUE got
        // WAVERR_STILLPLAYING from then on.
        CompleteWaveBlocks(opaque);
        return NoError;
    }
    API(waveOutBreakLoop)
    {
        if (!WaveOutDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        mixer.BreakLoop(((WaveDevice *)opaque)->source);
        return NoError;
    }
    // The driver answers in the format the caller asked for and
    // falls back to TIME_BYTES for a format a wave device cannot give.
    API(waveOutGetPosition)
    {
        if (!WaveOutDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        auto &device = *(WaveDevice *)opaque;
        if (a[2] < 0xc || !a[1] || !virtual_memory.Access(a[1], a[2], true))
            return InvalidParameter;
        const unsigned long long played = mixer.Played(device.source);
        const unsigned long long samples = device.block_align ? played / device.block_align : 0;
        unsigned type = Read32(a[1]);
        if (type != TimeMilliseconds && type != TimeSamples && type != TimeSmpte) type = TimeBytes;
        Write32(a[1], type);
        if (type == TimeSmpte)
        {
            // MMTIME.smpte is four bytes plus the frame rate at +8.
            const unsigned long long seconds = device.rate ? samples / device.rate : 0;
            const unsigned long long frames =
                device.rate ? (samples % device.rate) * 30 / device.rate : 0;
            const unsigned char smpte[4] = {(unsigned char)(seconds / 3600),
                                           (unsigned char)(seconds / 60 % 60),
                                           (unsigned char)(seconds % 60), (unsigned char)frames};
            Store(a[1] + 4, smpte, 4);
            if (a[2] >= 0x10) Write32(a[1] + 8, 30);
            return NoError;
        }
        unsigned long long value = played;
        if (type == TimeSamples) value = samples;
        if (type == TimeMilliseconds) value = device.rate ? samples * 1000 / device.rate : 0;
        Write32(a[1] + 4, (unsigned)value);
        if (a[2] >= 0x10) Write32(a[1] + 8, 0);
        return NoError;
    }
    // A device identifier reaches the driver's master
    // volume, a handle the volume of that one stream; the mixer has both,
    // because a source carries its own volume.
    API(waveOutGetVolume) API(waveOutSetVolume)
    {
        const bool get = API_IS(name, "waveOutGetVolume");
        if (get && (!a[1] || !virtual_memory.Access(a[1], 4, true))) return InvalidParameter;
        if (WaveOutDeviceOfHandle(a[0], opaque))
        {
            auto &device = *(WaveDevice *)opaque;
            if (get) Write32(a[1], mixer.Volume(device.source));
            else mixer.SetVolume(device.source, a[1]);
            return NoError;
        }
        unsigned device = 0;
        if (!MultimediaDeviceOfHandle(a[0], WaveOutType, device)) return InvalidHandle;
        if (device != 0 && device != WaveMapper) return BadDeviceId;
        // The volume of the device is the volume of the wave line of the
        // mixer device; both entries name the same value, the way they do on
        // a card whose mixer driver owns it. src/win98_mixer.h.
        if (get) Write32(a[1], Sound::Shared().GroupVolume(Win98Mixer::GroupWave));
        else Sound::Shared().SetGroupVolume(Win98Mixer::GroupWave, a[1]);
        return NoError;
    }
    //  turns a stream handle back into its device identifier.
    //  tests the handle first, with no pass-through for a device
    // identifier: zero and -1 fail the 4Ch-byte probe like any other value. Then the answer is probed for four bytes, and the 16-bit
    // routine writes the device index as a word that the stub widens with
    // its sign. There is one wave device, index 0.
    API(waveOutGetID)
    {
        unsigned device = 0;
        if (MultimediaRecord(a[0], WaveOutType, device)) return InvalidHandle;
        if (!a[1] || !virtual_memory.Access(a[1], 4, true)) return InvalidParameter;
        if (!WaveOutDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        Write32(a[1], 0);
        return NoError;
    }
    // The four entries behind WAVECAPS_PITCH and WAVECAPS_PLAYBACKRATE. The
    // capabilities of this device carry neither bit, so the contract of a
    // driver that cannot do it is the answer, and it is not a stub: a program
    // that asks gets the same MMSYSERR_NOTSUPPORTED a Sound Blaster gave.
    API(waveOutGetPitch) API(waveOutSetPitch)
    API(waveOutGetPlaybackRate) API(waveOutSetPlaybackRate)
    {
        if (!WaveOutDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        const bool get = API_IS(name, "waveOutGetPitch") || API_IS(name, "waveOutGetPlaybackRate");
        if (get && (!a[1] || !virtual_memory.Access(a[1], 4, true))) return InvalidParameter;
        return NotSupported;
    }
    //  hands a private driver message through. This driver has no
    // private messages of its own, so a message to a device is
    // MMSYSERR_NOTSUPPORTED, and that is the honest answer to
    // DRV_QUERYDSOUNDDESC (805h) as well: there is no DirectSound driver
    // below this one.
    // WAVE_MAPPER is the other half of this entry. The mapper is a driver of
    // its own, and DRVM_MAPPER_PREFERRED_GET (2015h) is its message: the first
    // parameter takes the device the user picked in the multimedia control
    // panel, the second the status bits; see PreferredWaveOutDevice.
    // DSOUND.DLL asks exactly this before anything else, and the
    // answer is what puts it on its emulation path.
    // DRVM_MAPPER_PREFERRED_SET stays MMSYSERR_NOTSUPPORTED: nothing in this
    // port writes the preferred device, and a set that changes nothing would
    // be a lie.
    API(waveOutMessage)
    {
        // DRVM_MAPPER_STATUS (2000h) of the wave mapper: with
        // WAVEOUT_MAPPER_STATUS_DEVICE the device the mapper plays on, with
        // WAVEOUT_MAPPER_STATUS_MAPPED whether it converts. This port's mapper
        // is its one device and plays PCM as it comes, so device 0, no
        // conversion. MCIAVI asks it after opening its audio.
        if (a[1] == 0x2000 && (a[2] == 0 || a[2] == 1) && WaveOutDeviceOfHandle(a[0], opaque))
        {
            if (!a[3] || !virtual_memory.Access(a[3], 4, true)) return InvalidParameter;
            Write32(a[3], 0);
            return NoError;
        }
        if (!WaveOutDeviceOfHandle(a[0], opaque))
        {
            unsigned device = 0;
            if (!MultimediaDeviceOfHandle(a[0], WaveOutType, device)) return InvalidHandle;
            if (device != 0 && device != WaveMapper) return BadDeviceId;
            // The provider's single device supports wave mapping; WAVE_MAPPER
            // resolves here.
            if (device == 0 && a[1] == QueryMappable) return NoError;
            // DRV_QUERYDEVNODE: the devnode of the card. DSOUND.DLL
            // goes through the wave devices with it to find the one that
            // belongs to a DirectSound driver, whose description names the
            // same devnode (src/x86_dsound_vxd.cpp).
            if (device == 0 && a[1] == QueryDevNode)
            {
                if (!a[2] || !virtual_memory.Access(a[2], 4, true)) return InvalidParameter;
                Write32(a[2], DsoundDevNode());
                return NoError;
            }
            if (device == WaveMapper && a[1] == MapperPreferredGet)
            {
                if (!a[2] || !virtual_memory.Access(a[2], 4, true)) return InvalidParameter;
                if (!a[3] || !virtual_memory.Access(a[3], 4, true)) return InvalidParameter;
                unsigned flags = 0;
                const unsigned preferred = PreferredWaveOutDevice(flags);
                Write32(a[2], preferred);
                Write32(a[3], flags);
                return NoError;
            }
        }
        return NotSupported;
    }
    default:
        break;
    }
    Fail(name);
    return NoError;
#undef API
}

// ---------------------------------------------------------------------------
// PlaySound

namespace
{
// The RIFF walk PlaySound does on the block it has in hand. Answers the offset
// and length of the data chunk and fills the format, or false when the bytes
// are not a PCM WAVE file.
bool ParseWave(const unsigned char *bytes, unsigned length, Sound::Format &format,
               unsigned &offset, unsigned &size, MsAdpcm::Layout *adpcm = nullptr)
{
    bool compressed = false;
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
        const unsigned chunk = dword(at + 4);
        const unsigned body = at + 8;
        if (chunk > length - body) break;
        if (!memcmp(bytes + at, "fmt ", 4) && chunk >= 16)
        {
            // Tag 1 is PCM. Tag 2 is Microsoft ADPCM, which the wave mapper of
            // Win98 hands to MSADP32.ACM; the caller decodes it to 16 bits.
            compressed = word(body) == 2;
            if (word(body) != 1 && !(compressed && adpcm && MsAdpcm::ReadLayout(bytes + body, chunk, *adpcm)))
                return false;
            format.channels = word(body + 2);
            format.rate = dword(body + 4);
            format.bits = compressed ? 16 : word(body + 14);
            have_format = true;
        }
        else if (!memcmp(bytes + at, "data", 4))
        {
            offset = body;
            size = chunk;
        }
        at = body + chunk + (chunk & 1);
    }
    if (adpcm && !compressed) *adpcm = MsAdpcm::Layout{};
    return have_format && size && Sound::Mixer::Supports(format);
}
} // namespace

void Application::ClosePlayedSound(void *opaque)
{
    auto &player = *(SoundPlayer *)opaque;
    if (player.source) Sound::Shared().Close(player.source);
    player = SoundPlayer{};
}

// The alias path of the sound schemes: the name is a key under AppEvents and
// its ".Current" subkey carries the file the user picked. That is where the
// sounds of the imported tree are registered. With SND_APPLICATION the event
// belongs to the calling program: WINMM looks under the base name of the
// process's executable first (Explorer keeps "Navigating" and
// "EmptyRecycleBin" under Apps\\Explorer) and under ".Default" after that.
bool Application::SoundOfAlias(const char *alias, char *path, unsigned capacity, bool application)
{
    char owner[64] = ".Default";
    for (unsigned round = application ? 0 : 1; round < 2; ++round)
    {
        if (!round)
        {
            const char *file = modules[0].path, *base = file;
            for (const char *p = file; *p; ++p)
                if (*p == '\\' || *p == '/' || *p == ':') base = p + 1;
            unsigned n = 0;
            while (base[n] && base[n] != '.' && n + 1 < sizeof owner) { owner[n] = base[n]; ++n; }
            owner[n] = 0;
            if (!n) continue;
        }
        else snprintf(owner, sizeof owner, ".Default");
        char key[320];
        const unsigned written = (unsigned)snprintf(
            key, sizeof key, "AppEvents\\Schemes\\Apps\\%s\\%s\\.Current", owner, alias);
        if (written >= sizeof key) return false;
        unsigned node = 0;
        bool created = false;
        if (registry.store.Open(registry.store.roots[1], key, false, node, created)) continue;
        const Registry::Value *value = nullptr;
        if (registry.store.Query(node, "", value) || !value || !value->data || !value->size) continue;
        unsigned length = value->size;
        if (length >= capacity) length = capacity - 1;
        memcpy(path, value->data, length);
        path[length] = 0;
        if (path[0]) return true;
    }
    return false;
}

// Reads a whole file of the guest file system into `into`. PlaySound has the
// sound in memory before it plays it, because the mixer copies it anyway.
unsigned Application::ReadSoundFile(const char *path, unsigned char *into, unsigned capacity)
{
    unsigned handle = 0, status = 0;
    if (files.Open(path, 1, 3, 3, 0, handle, status)) return 0;
    unsigned total = 0;
    while (total < capacity)
    {
        unsigned got = 0;
        if (files.Read(handle, into + total, capacity - total, got)) break;
        if (!got) break;
        total += got;
    }
    files.Close(handle);
    return total;
}

// The worker : everything between the flag test of PlaySoundA and the
// retry with the default sound. Zero is success.  only retries with
// the default sound when the worker answers 0FFFFFFFFh, which is the answer
// for a sound that was not found; every other refusal stands.
unsigned Application::PlaySoundWorker(unsigned sound, unsigned module, unsigned flags)
{
    // SND_LOOP without SND_ASYNC is MMSYSERR_INVALFLAG.
    if ((flags & 9) == 8) return InvalidFlag;
    SoundPlayer *player = nullptr, *free_slot = nullptr;
    for (auto &entry : players)
    {
        if (entry.process == this) { player = &entry; break; }
        if (!entry.process && !free_slot) free_slot = &entry;
    }
    // SND_NOSTOP keeps a running sound and refuses the new one.
    if (player && player->source && (flags & 0x10) && sound) return Allocated;
    //  No name, or SND_PURGE, means stop and succeed.
    if (player && (!sound || (flags & 0x40))) ClosePlayedSound(player);
    if (!sound || (flags & 0x40)) return NoError;
    if (!player) player = free_slot;
    if (!player) return Allocated;
    if (player->source) ClosePlayedSound(player);
    player->process = this;

    unsigned char *bytes = sound_bytes;
    unsigned length = 0;
    // SND_RESOURCE is 40004h and therefore carries the SND_MEMORY bit, so it
    // has to be looked at before SND_MEMORY.
    if (flags & 0x40000)
    {
        const unsigned type = String("WAVE");
        if (!type) return NotFound;
        const unsigned find[] = {module, sound, type};
        const unsigned resource = Api("FindResourceA", find);
        Free(type);
        if (!resource) return NotFound;
        const unsigned load[] = {module, resource};
        const unsigned handle = Api("LoadResource", load);
        const unsigned size = Api("SizeofResource", load);
        const unsigned lock[] = {handle};
        const unsigned address = Api("LockResource", lock);
        if (!handle || !address || !size || size > sizeof sound_bytes) return NotFound;
        if (!Read(address, bytes, size)) return NotFound;
        length = size;
    }
    else if (flags & 4)  // SND_MEMORY
    {
        // The caller owns the block and names no length; PlaySound reads the
        // RIFF header to learn how long it is, so the probe follows it.
        if (!virtual_memory.Access(sound, 12, false)) return InvalidParameter;
        unsigned char head[12] = {};
        if (!Read(sound, head, 12)) return InvalidParameter;
        const unsigned riff = (unsigned)head[4] | ((unsigned)head[5] << 8) |
                              ((unsigned)head[6] << 16) | ((unsigned)head[7] << 24);
        if (riff > sizeof sound_bytes - 8) return NotFound;
        length = riff + 8;
        if (!virtual_memory.Access(sound, length, false)) return InvalidParameter;
        if (!Read(sound, bytes, length)) return InvalidParameter;
    }
    else
    {
        //  probes the name for 90h characters.
        if (!virtual_memory.Access(sound, 1, false)) return InvalidParameter;
        const char *text = ReadString(sound);
        if (!text || !text[0] || strlen(text) >= 0x90) return InvalidParameter;
        char path[512];
        const bool alias = (flags & 0x10000) != 0, named = (flags & 0x20000) != 0;
        if (alias || !named)
        {
            // SND_ALIAS, and a name with neither flag, look in the sound
            // scheme of the user first. A name that is not an alias and not
            // SND_ALIAS is a file name, which is what Windows falls back to.
            if (!SoundOfAlias(text, path, sizeof path, (flags & 0x80) != 0))
            {
                if (alias) return NotFound;
                snprintf(path, sizeof path, "%s", text);
            }
        }
        else snprintf(path, sizeof path, "%s", text);
        length = ReadNamedSound(path, bytes, sizeof sound_bytes);
    }
    const unsigned submitted = SubmitSound(player, bytes, length, (flags & 8) != 0, sound ? sound : 1);
    if (submitted) return submitted;
    EnsureMultimediaThread();
    // Without SND_ASYNC the call does not return until the sound is over.
    if (!(flags & 1)) WaitForPlayedSound(player);
    return NoError;
}

// A file of a sound scheme or a file name given to PlaySound. A bare name is
// looked up in the media directory of the tree, with and without the
// extension, the way the shell registers it.
unsigned Application::ReadNamedSound(const char *path, unsigned char *into, unsigned capacity)
{
    unsigned length = ReadSoundFile(path, into, capacity);
    if (!length && !strchr(path, '\\') && !strchr(path, ':'))
    {
        char media[600];
        snprintf(media, sizeof media, "C:\\WINDOWS\\MEDIA\\%s", path);
        length = ReadSoundFile(media, into, capacity);
        if (!length)
        {
            snprintf(media, sizeof media, "C:\\WINDOWS\\MEDIA\\%s.WAV", path);
            length = ReadSoundFile(media, into, capacity);
        }
    }
    return length;
}

// The whole sound in one piece into a source of its own for `opaque`, a
// SoundPlayer: the mixer copies it anyway, and PlaySound has no streaming
// path. Microsoft ADPCM is decoded to 16 bits first.
unsigned Application::SubmitSound(void *opaque, const unsigned char *bytes, unsigned length, bool loop, unsigned tag)
{
    auto *player = (SoundPlayer *)opaque;
    Sound::Format format;
    unsigned offset = 0, size = 0;
    MsAdpcm::Layout adpcm;
    if (!length || !ParseWave(bytes, length, format, offset, size, &adpcm)) return NotFound;
    short *decoded = nullptr;
    size_t decoded_bytes = 0;
    if (adpcm.channels)
    {
        const size_t samples = MsAdpcm::DecodedSamples(adpcm, size);
        decoded = samples ? (short *)malloc(samples * sizeof(short)) : nullptr;
        if (!decoded) return samples ? Allocated : NotFound;
        MsAdpcm::Decode(adpcm, bytes + offset, size, decoded);
        decoded_bytes = samples * sizeof(short);
    }
    const unsigned source = Sound::Shared().Open(format);
    if (!source) { free(decoded); return Allocated; }
    player->source = source;
    player->loop = loop;
    player->tag = tag;
    Sound::Shared().Submit(source, decoded ? (const unsigned char *)decoded : bytes + offset,
                           decoded ? (unsigned)decoded_bytes : size, player->tag,
                           player->loop ? Sound::BeginLoop | Sound::EndLoop : 0,
                           player->loop ? 0xffffffff : 0);
    free(decoded); // the mixer has copied the block
    return NoError;
}

// The calling thread waits while the sound of `opaque` plays, then the source
// is closed: PlaySound without SND_ASYNC.
void Application::WaitForPlayedSound(void *opaque)
{
    auto *player = (SoundPlayer *)opaque;
    while (player->source && Sound::Shared().Busy(player->source) && state != Failed && state != Exited)
    {
        Sound::Shared().Advance(Now());
        auto &thread = threads[current_thread];
        thread.block = 3;
        thread.deadline = Now() + 5000;
        Suspend(Idle);
    }
    ClosePlayedSound(player);
}

// MSGSRV32.EXE plays the two sounds of the session through an MMTASK of its
// own (MMSYSTEM.900, mmTaskCreate) whose routine calls MMSYSTEM.3, PlaySound
// with SND_ALIAS | SND_NODEFAULT: "SystemStart" with SND_ASYNC,
// "SystemExit" synchronously, then it sets the word its caller waits on. So
// the sound belongs to no program: a PlaySound of the shell does not cut it
// off, and it does not end with a process. The alias is looked up under
// Apps\.Default with no default sound; an empty ".Current" plays nothing,
// which is what the sound scheme "No Sounds" and an empty entry of the
// Sounds page of the Control Panel mean.
unsigned Application::PlaySessionSound(const char *alias, bool wait)
{
    // One sound of the session at a time, as one task plays it.
    if (session_player.source) ClosePlayedSound(&session_player);
    char path[512];
    unsigned result = NotFound, length = 0;
    if (SoundOfAlias(alias, path, sizeof path))
    {
        length = ReadNamedSound(path, sound_bytes, sizeof sound_bytes);
        result = SubmitSound(&session_player, sound_bytes, length, false, 1);
    }
    else snprintf(path, sizeof path, "<none>");
    if (log)
    {
        char text[700];
        snprintf(text, sizeof text, "SOUND session name=%s file=%s bytes=%u wait=%u result=%u", alias, path,
                 length, wait ? 1u : 0u, result);
        log(text);
    }
    if (!result && wait) WaitForPlayedSound(&session_player);
    return result;
}

void Application::PlayStartupSound()
{
    PlaySessionSound("SystemStart", false);
}

unsigned Application::SoundApi(ApiName name, const unsigned *a)
{
    // SndPlaySoundA is PlaySoundA with the module of the process.
    const bool snd = API_IS(name, "sndPlaySoundA");
    const unsigned sound = a[0], module = snd ? 0 : a[1], flags = snd ? a[1] : a[2];
    //  asks for the device count first. This port has one device, so
    // the answer that stopped startup at a message box is gone.
    if (!WaveOutApi("waveOutGetNumDevs", a)) return 0;
    // every flag outside this mask is refused before anything else.
    if (flags & ~0x201720dfu) return 0;
    const unsigned answer = PlaySoundWorker(sound, module, flags);
    if (log)
    {
        // One line per request, so a host log says which event asked for which
        // sound and whether the scheme had a file for it.
        char text[256];
        const bool named = sound && !(flags & 0x40004) && virtual_memory.Access(sound, 1, false);
        snprintf(text, sizeof text, "SOUND request name=%s flags=%08x result=%u", named ? ReadString(sound) : "<memory or resource>", flags, answer);
        log(text);
    }
    if (!answer) return 1;
    //  Only a sound that was not found is retried, and
    // only when SND_NODEFAULT is clear. The default sound of the scheme is
    // played as an alias with the asynchronous bit of the caller.
    if (answer != NotFound || (flags & 2)) return 0;
    const unsigned name_address = String(".Default");
    if (!name_address) return 0;
    const unsigned played = PlaySoundWorker(name_address, 0, (flags & 1) | 0x10000);
    Free(name_address);
    return played ? 0 : 1;
}
} // namespace X86
