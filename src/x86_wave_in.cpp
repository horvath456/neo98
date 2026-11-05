// SPDX-License-Identifier: GPL-2.0-or-later
// The WINMM wave input family on the microphone of src/sound.h.
// The same split as the output in src/x86_wave_out.cpp: WINMM.DLL is the
// argument check in front of the driver, and the driver is this port. The
// checks are read in the pinned WINMM.DLL, :
// waveInGetNumDevs is MOV CL,n and a jump into the thunk; the
// count is the driver's. One device while the host has
// a microphone and the switch "Mikrofon in Windows" is
// on, else none.
// waveInGetDevCapsA : a size of zero answers zero first, then the
// handle test with the type mask 77h, IsBadWritePtr on
// the buffer for 11, then 2Eh bytes from the driver and
// the word at 2Eh cleared when the caller has room.
// waveInOpen pushes the type 21h into the shared open
// worker that waveOutOpen uses with 22h; the
// record, the callback kinds and WAVE_FORMAT_QUERY are
// the ones of the output.
// waveInPrepareHeader and waveInUnprepareHeader go into
// the shared workers. For an input
// device (bit 0 of the type) the data buffer
// must pass IsBadWritePtr instead of IsBadReadPtr.
// waveInAddBuffer : the record (4Ch, magic, type mask
// 0FFFFFFDEh, which is 21h), a header of at least 20h
// bytes that IsBadWritePtr accepts (11), a block at
// +1Ch that IsBadWritePtr rejects for 64h bytes
// (WAVERR_UNPREPARED 22h), WHDR_INQUEUE set
// (WAVERR_STILLPLAYING 21h), a block without
// WHDR_PREPARED (22h). Then the block takes the flags
// without WHDR_DONE and WHDR_INQUEUE plus 1002h and the
// length, the header gets WHDR_INQUEUE and loses
// WHDR_DONE, and the driver is asked.
// the rest waveInStart, waveInStop,
// waveInReset, waveInClose,
// waveInGetPosition  (a size below 0Ch or a
// bad MMTIME is 11) and waveInGetID  (four
// bytes for the answer, 11): the record test with
// 0FFFFFFDEh and the driver.
// waveInMessage is the message worker with 21h; a
// device number or WAVE_MAPPER reaches the driver, and
// DRVM_MAPPER_PREFERRED_GET is the message of the mapper.
// What the driver does, which is this port: a started stream fills the
// buffers in the order they were added, one after the other, and a full one
// comes back with WHDR_DONE, dwBytesRecorded and WIM_DATA on the multimedia
// thread of the process. waveInStop hands back the buffer it was filling
// with what it holds, waveInReset every buffer, and the position counts the
// bytes recorded since the open or the last reset.
//
#include "neo98_hardware.h"
#include "sound.h"
#include "user.h"
#include "x86_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned NoError = 0, BadDeviceId = 2, Allocated = 4, InvalidHandle = 5;
constexpr unsigned NoDriver = 6, NoMemory = 7, NotSupported = 8, InvalidParameter = 11;
constexpr unsigned BadFormat = 32, StillPlaying = 33, Unprepared = 34;
constexpr unsigned WaveMapper = 0xffffffff;
constexpr unsigned RecordBytes = 0x4c, HeaderBlockBytes = 0x64;
constexpr unsigned RecordMagic = 0x12345678, WaveInType = 0x21;
constexpr unsigned HeaderDone = 1, HeaderPrepared = 2, HeaderInQueue = 0x10;
// MM_WIM_OPEN, MM_WIM_CLOSE and MM_WIM_DATA.
constexpr unsigned WimOpen = 0x3be, WimClose = 0x3bf, WimData = 0x3c0;
constexpr unsigned CallbackMask = 0x70000, CallbackWindow = 0x10000;
constexpr unsigned CallbackThread = 0x20000, CallbackFunction = 0x30000;
constexpr unsigned CallbackEvent = 0x50000;
constexpr unsigned FormatQuery = 1;
constexpr unsigned TimeMilliseconds = 1, TimeSamples = 2, TimeBytes = 4, TimeSmpte = 8;
constexpr unsigned MapperPreferredGet = 0x2015, MapperPreferredOnly = 1;
constexpr unsigned QueryMappable = 0x805, QueryDevNode = 0x802;
constexpr unsigned MaximumQueue = 64;

// One open wave input stream. The record in the shared handle store is what
// the program holds; the queue of its buffers lives here.
struct WaveInDevice
{
    Application *process = nullptr;
    unsigned record = 0, stream = 0;
    unsigned callback = 0, instance = 0, kind = 0;
    Sound::Format format;
    unsigned block_align = 1;
    unsigned queue[MaximumQueue] = {};
    unsigned head = 0, count = 0;
    unsigned filled = 0;                 // bytes in the buffer at the head
    unsigned long long position = 0;     // bytes recorded since open or reset
    bool started = false;
};
WaveInDevice devices[16];

// NEO98_TRACE_WAVEIN: one line per open, buffer and return, for the
// diagnosis of a program that records nothing.
bool Trace()
{
    static const bool on = getenv("NEO98_TRACE_WAVEIN") != nullptr;
    return on;
}
WaveInDevice *DeviceOfRecord(unsigned record)
{
    for (auto &device : devices)
        if (device.process && device.record == record) return &device;
    return nullptr;
}
} // namespace

bool Application::WaveInDeviceOfHandle(unsigned handle, void *&found)
{
    found = nullptr;
    JoinMultimediaHandles();
    if (!virtual_memory.Access(handle, RecordBytes, true)) return false;
    if (Read32(handle + 4) != RecordMagic) return false;
    if (Read32(handle + 0xc) & ~WaveInType) return false;
    found = DeviceOfRecord(handle);
    return found != nullptr;
}

// DriverCallback : a function gets the message itself, a window and
// a thread get it posted with the handle in wParam, an event is set for
// every message (DCB_EVENT).
void Application::WaveInNotify(void *opaque, unsigned message, unsigned parameter)
{
    auto &device = *(WaveInDevice *)opaque;
    if (device.process->Win16WaveInNotify(device.record, message, parameter)) return;
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
        ThreadApi("SetEvent", &device.callback);
        break;
    default:
        break;
    }
}

// The buffer at the head of the queue goes back to the program with what it
// holds: WHDR_DONE, WHDR_INQUEUE cleared, dwBytesRecorded, and WIM_DATA.
void Application::WaveInReturnHead(void *opaque)
{
    auto &device = *(WaveInDevice *)opaque;
    if (!device.count) return;
    const unsigned header = device.queue[device.head];
    device.head = (device.head + 1) % MaximumQueue;
    --device.count;
    const unsigned recorded = device.filled;
    device.filled = 0;
    if (Trace())
        fprintf(stderr, "NEO98 WAVEIN done record=%08x header=%08x bytes=%u queued=%u now=%llu\n", device.record,
                header, recorded, device.count, (unsigned long long)Now() / 1000);
    if (virtual_memory.Access(header, 0x20, true))
    {
        Write32(header + 8, recorded);
        Write32(header + 0x10, (Read32(header + 0x10) & ~HeaderInQueue) | HeaderDone);
        const unsigned block = Read32(header + 0x1c);
        if (block && virtual_memory.Access(block, HeaderBlockBytes, true))
        {
            Write32(block + 8, recorded);
            Write32(block + 0x10, (Read32(block + 0x10) & ~HeaderInQueue) | HeaderDone);
        }
    }
    WaveInNotify(&device, WimData, header);
}

// The driver half of a running stream: fill the buffers in their order with
// what the microphone has brought since the last round. Runs on the
// multimedia thread of the process.
void Application::FillWaveIn(void *opaque)
{
    auto &device = *(WaveInDevice *)opaque;
    if (!device.started) return;
    Sound::Microphone::Advance(Now());
    static unsigned char chunk[16384];
    while (device.count)
    {
        const unsigned header = device.queue[device.head];
        if (!virtual_memory.Access(header, 0x20, true)) { WaveInReturnHead(&device); continue; }
        const unsigned data = Read32(header), length = Read32(header + 4);
        const unsigned room = (length - device.filled) / device.block_align * device.block_align;
        if (!room)
        {
            WaveInReturnHead(&device);
            continue;
        }
        unsigned want = room < sizeof chunk ? room : sizeof chunk;
        want -= want % device.block_align;
        const unsigned got = Sound::Microphone::Read(device.stream, chunk, want, device.format);
        if (!got) return;
        if (!Store(data + device.filled, chunk, got))
        {
            device.filled = length;
            WaveInReturnHead(&device);
            continue;
        }
        device.filled += got;
        device.position += got;
        if (got < want) return;
    }
}

void Application::PumpWaveIn()
{
    bool running = false;
    for (auto &device : devices)
        running |= device.process == this && device.started;
    if (!running) return;
    for (auto &device : devices)
        if (device.process == this) FillWaveIn(&device);
}

bool Application::WaveInActive()
{
    for (const auto &device : devices)
        if (device.process == this) return true;
    return false;
}

void Application::FreeWaveIn()
{
    for (auto &device : devices)
    {
        if (device.process != this) continue;
        Sound::Microphone::Close(device.stream);
        if (device.record) FreeMultimediaHandle(device.record, RecordBytes);
        device = WaveInDevice{};
    }
}

// WAVEINCAPS of the one device: the name the preferred device of the Sound
// Mapper is matched against, the twelve PCM shapes of the three standard
// rates, two channels. The driver hands 2Eh bytes;  clears the word
// at 2Eh, wReserved1, when the caller's structure reaches that far.
unsigned Application::WaveInCapabilities(unsigned buffer, unsigned bytes)
{
    unsigned char caps[0x30] = {};
    const auto word = [&](unsigned at, unsigned value) {
        caps[at] = (unsigned char)value;
        caps[at + 1] = (unsigned char)(value >> 8);
    };
    word(0, 1);          // wMid, MM_MICROSOFT
    word(2, 0);          // wPid
    word(4, 0x0400);     // vDriverVersion 4.00
    memcpy(caps + 8, Neo98Hardware::WaveInName, sizeof Neo98Hardware::WaveInName - 1);
    word(0x28, 0xfff);   // dwFormats: WAVE_FORMAT_1M08 through 4S16
    word(0x2c, 2);       // wChannels
    const unsigned copy = bytes < sizeof caps ? bytes : (unsigned)sizeof caps;
    return Store(buffer, caps, copy) ? NoError : InvalidParameter;
}

// The preferred recording device of the wave mapper, "Record" under
// HKEY_CURRENT_USER\Software\Microsoft\Multimedia\Sound Mapper, the
// counterpart of PreferredWaveOutDevice. One device: a name that is not
// this driver's, like the "SB16 Wave In [220]" of the image, leaves device
// zero and drops PreferredOnly.
unsigned Application::PreferredWaveInDevice(unsigned &flags)
{
    flags = 0;
    unsigned node = 0;
    bool created = false;
    if (registry.store.Open(registry.store.roots[1], "Software\\Microsoft\\Multimedia\\Sound Mapper",
                            false, node, created))
        return 0;
    const Registry::Value *value = nullptr;
    if (!registry.store.Query(node, "PreferredOnly", value) && value && value->data && value->size >= 4)
    {
        const unsigned char *bytes = (const unsigned char *)value->data;
        if (bytes[0] | bytes[1] | bytes[2] | bytes[3]) flags |= MapperPreferredOnly;
    }
    const Registry::Value *record = nullptr;
    if (registry.store.Query(node, "Record", record) || !record || !record->data || !record->size)
        return 0;
    char named[64] = {};
    const unsigned length = record->size < sizeof named ? record->size : (unsigned)sizeof named - 1;
    memcpy(named, record->data, length);
    if (strcmp(named, Neo98Hardware::WaveInName) != 0) flags &= ~MapperPreferredOnly;
    return 0;
}

unsigned Application::WaveInApi(ApiName name, const unsigned *a)
{
    void *opaque = nullptr;
    const bool present = Sound::Microphone::Present();
#define API(n) case API_ID(#n):
    switch (name.id)
    {
    API(waveInGetNumDevs)
        return present ? 1 : 0;
    API(waveInGetDevCapsA)
    {
        if (!a[2]) return NoError;
        unsigned device = 0;
        if (!MultimediaDeviceOfHandle(a[0], 0x77, device)) return InvalidHandle;
        if (!a[1] || !virtual_memory.Access(a[1], a[2], true)) return InvalidParameter;
        if (!present) return device == WaveMapper ? NoDriver : BadDeviceId;
        if (device != 0 && device != WaveMapper) return BadDeviceId;
        return WaveInCapabilities(a[1], a[2]);
    }
    //  and the shared worker, in the order of waveOutOpen.
    API(waveInOpen)
    {
        if (!present)
        {
            if (!a[0] || !virtual_memory.Access(a[0], 4, true)) return InvalidParameter;
            return a[1] == WaveMapper ? NoDriver : BadDeviceId;
        }
        Sound::Format format;
        const unsigned bad = WaveFormatOf(a[2], format);
        if (bad == InvalidParameter) return InvalidParameter;
        if (a[1] != 0 && a[1] != WaveMapper) return BadDeviceId;
        const unsigned kind = a[5] & CallbackMask;
        if (kind && kind != CallbackWindow && kind != CallbackThread && kind != CallbackFunction &&
            kind != CallbackEvent)
            return InvalidParameter;
        if (!a[0]) return bad;
        if (!virtual_memory.Access(a[0], 4, true)) return InvalidParameter;
        if (kind == CallbackFunction && !virtual_memory.Access(a[3], 1, false)) return InvalidParameter;
        if (bad) return bad;
        if (a[5] & FormatQuery) return NoError;
        WaveInDevice *slot = nullptr;
        for (auto &device : devices)
            if (!device.process) { slot = &device; break; }
        if (!slot) return Allocated;
        const unsigned stream = Sound::Microphone::Open();
        if (!stream) return Allocated;
        const unsigned record = AllocateMultimediaHandle(RecordBytes);
        if (!record) { Sound::Microphone::Close(stream); return NoMemory; }
        Write32(record, 0);
        Write32(record + 4, RecordMagic);
        Write32(record + 0xc, WaveInType);
        Write32(record + 0x30, a[3]);
        Write32(record + 0x34, a[4]);
        Write32(record + 0x38, kind);
        *slot = WaveInDevice{};
        slot->process = this;
        slot->record = record;
        slot->stream = stream;
        slot->callback = a[3];
        slot->instance = a[4];
        slot->kind = kind;
        slot->format = format;
        slot->block_align = format.channels * (format.bits / 8);
        Write32(a[0], record);
        if (Trace())
            fprintf(stderr, "NEO98 WAVEIN open record=%08x rate=%u bits=%u channels=%u kind=%x callback=%08x\n",
                    record, format.rate, format.bits, format.channels, kind, a[3]);
        EnsureMultimediaThread();
        WaveInNotify(slot, WimOpen, 0);
        return NoError;
    }
    // A stream with buffers still queued stays open.
    API(waveInClose)
    {
        if (!WaveInDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        auto &device = *(WaveInDevice *)opaque;
        if (device.count) return StillPlaying;
        WaveInNotify(&device, WimClose, 0);
        Sound::Microphone::Close(device.stream);
        Write32(device.record + 4, 0);
        FreeMultimediaHandle(device.record, RecordBytes);
        device = WaveInDevice{};
        return NoError;
    }
    //  with the input branch: the data must be writable.
    API(waveInPrepareHeader)
    {
        if (!WaveInDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        if (a[2] < 0x20 || !a[1] || !virtual_memory.Access(a[1], 0x20, true)) return InvalidParameter;
        const unsigned data = Read32(a[1]), length = Read32(a[1] + 4);
        if (length && (!data || !virtual_memory.Access(data, length, true))) return InvalidParameter;
        if (Read32(a[1] + 0x10) & HeaderPrepared) return NoError;
        const unsigned block = Allocate(HeaderBlockBytes);
        if (!block) return NoMemory;
        unsigned char zero[HeaderBlockBytes] = {};
        if (!Store(block, zero, sizeof zero)) { Free(block); return NoMemory; }
        Write32(block + 0xc, a[1]);
        Write32(block + 0x10, HeaderPrepared);
        Write32(block + 0x58, a[0]);
        Write32(a[1] + 0x1c, block);
        Write32(a[1] + 0x10, Read32(a[1] + 0x10) | HeaderPrepared);
        return NoError;
    }
    API(waveInUnprepareHeader)
    {
        if (!WaveInDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        if (a[2] < 0x20 || !a[1] || !virtual_memory.Access(a[1], 0x20, true)) return InvalidParameter;
        if (Read32(a[1] + 0x10) & HeaderInQueue) return StillPlaying;
        if (!(Read32(a[1] + 0x10) & HeaderPrepared)) return NoError;
        const unsigned block = Read32(a[1] + 0x1c);
        if (block) Free(block);
        Write32(a[1] + 0x1c, 0);
        Write32(a[1] + 0x10, Read32(a[1] + 0x10) & ~HeaderPrepared);
        return NoError;
    }
    // in its order.
    API(waveInAddBuffer)
    {
        if (!WaveInDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        auto &device = *(WaveInDevice *)opaque;
        if (a[2] < 0x20 || !a[1] || !virtual_memory.Access(a[1], a[2], true)) return InvalidParameter;
        const unsigned block = Read32(a[1] + 0x1c);
        if (!block || !virtual_memory.Access(block, HeaderBlockBytes, true)) return Unprepared;
        const unsigned flags = Read32(a[1] + 0x10);
        if (flags & HeaderInQueue) return StillPlaying;
        if (!(Read32(block + 0x10) & HeaderPrepared)) return Unprepared;
        const unsigned data = Read32(a[1]), length = Read32(a[1] + 4);
        // The driver refuses a buffer it may not write.
        if (length && (!data || !virtual_memory.Access(data, length, true))) return InvalidParameter;
        if (device.count == MaximumQueue) return NoMemory;
        Write32(block + 0x10, (flags & ~(HeaderDone | HeaderInQueue)) | 0x1002);
        Write32(block + 4, length);
        Write32(a[1] + 8, 0);
        Write32(a[1] + 0x10, (flags | HeaderInQueue) & ~HeaderDone);
        device.queue[(device.head + device.count) % MaximumQueue] = a[1];
        ++device.count;
        if (Trace())
            fprintf(stderr, "NEO98 WAVEIN add record=%08x header=%08x length=%u queued=%u now=%llu\n",
                    device.record, a[1], length, device.count, (unsigned long long)Now() / 1000);
        return NoError;
    }
    API(waveInStart)
    {
        if (!WaveInDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        auto &device = *(WaveInDevice *)opaque;
        if (device.started) return NoError;
        device.started = true;
        Sound::Microphone::Start(device.stream, Now());
        return NoError;
    }
    // A stream that stops hands back the buffer it was filling, with what it
    // holds; the empty ones behind it stay queued for the next start.
    API(waveInStop)
    {
        if (!WaveInDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        auto &device = *(WaveInDevice *)opaque;
        if (device.started) FillWaveIn(&device);
        device.started = false;
        Sound::Microphone::Stop(device.stream);
        if (device.count && device.filled) WaveInReturnHead(&device);
        return NoError;
    }
    // Every buffer comes back before the call returns, and the position
    // starts again at zero.
    API(waveInReset)
    {
        if (!WaveInDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        auto &device = *(WaveInDevice *)opaque;
        if (device.started) FillWaveIn(&device);
        device.started = false;
        Sound::Microphone::Stop(device.stream);
        while (device.count) WaveInReturnHead(&device);
        device.position = 0;
        return NoError;
    }
    API(waveInGetPosition)
    {
        if (!WaveInDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        auto &device = *(WaveInDevice *)opaque;
        if (a[2] < 0xc || !a[1] || !virtual_memory.Access(a[1], a[2], true)) return InvalidParameter;
        if (device.started) FillWaveIn(&device);
        const unsigned long long bytes = device.position;
        const unsigned long long samples = device.block_align ? bytes / device.block_align : 0;
        const unsigned rate = device.format.rate;
        unsigned type = Read32(a[1]);
        if (type != TimeMilliseconds && type != TimeSamples && type != TimeSmpte) type = TimeBytes;
        Write32(a[1], type);
        if (type == TimeSmpte)
        {
            const unsigned long long seconds = rate ? samples / rate : 0;
            const unsigned long long frames = rate ? (samples % rate) * 30 / rate : 0;
            const unsigned char smpte[4] = {(unsigned char)(seconds / 3600), (unsigned char)(seconds / 60 % 60),
                                           (unsigned char)(seconds % 60), (unsigned char)frames};
            Store(a[1] + 4, smpte, 4);
            if (a[2] >= 0x10) Write32(a[1] + 8, 30);
            return NoError;
        }
        unsigned long long value = bytes;
        if (type == TimeSamples) value = samples;
        if (type == TimeMilliseconds) value = rate ? samples * 1000 / rate : 0;
        Write32(a[1] + 4, (unsigned)value);
        if (a[2] >= 0x10) Write32(a[1] + 8, 0);
        return NoError;
    }
    // the record test comes first, then the answer is probed.
    API(waveInGetID)
    {
        unsigned device = 0;
        if (MultimediaRecord(a[0], WaveInType, device)) return InvalidHandle;
        if (!a[1] || !virtual_memory.Access(a[1], 4, true)) return InvalidParameter;
        if (!WaveInDeviceOfHandle(a[0], opaque)) return InvalidHandle;
        Write32(a[1], 0);
        return NoError;
    }
    //  over. The mapper answers its preferred device and
    // its status; the device answers DRV_QUERYMAPPABLE and the devnode of
    // the card, like the output. Everything else is a private message this
    // driver does not have.
    API(waveInMessage)
    {
        if (WaveInDeviceOfHandle(a[0], opaque))
        {
            // DRVM_MAPPER_STATUS of a stream opened through the mapper:
            // device zero, nothing converted.
            if (a[1] == 0x2000 && (a[2] == 0 || a[2] == 1))
            {
                if (!a[3] || !virtual_memory.Access(a[3], 4, true)) return InvalidParameter;
                Write32(a[3], 0);
                return NoError;
            }
            return NotSupported;
        }
        unsigned device = 0;
        if (!MultimediaDeviceOfHandle(a[0], WaveInType, device)) return InvalidHandle;
        if (!present) return device == WaveMapper ? NoDriver : BadDeviceId;
        if (device != 0 && device != WaveMapper) return BadDeviceId;
        if (device == 0 && a[1] == QueryMappable) return NoError;
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
            Write32(a[2], PreferredWaveInDevice(flags));
            Write32(a[3], flags);
            return NoError;
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
} // namespace X86
