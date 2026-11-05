// SPDX-License-Identifier: GPL-2.0-or-later
// NEO98DX.DLL, the sound half: the DirectSound driver below the ARM64
// DSOUND.DLL (sdk/dx/dsound).
// The card behind it is the mixer of src/sound.h, and a buffer is a ring of
// that mixer: memory the mixer reads at its play cursor at the moment the
// host takes the frames. Nothing is copied and nothing is mixed ahead, so
// what a program writes in front of the cursor is heard. That is the way the
// native DSOUND.VXD plays the buffers of the original DSOUND.DLL
// (src/x86_dsound_vxd.cpp); here the DLL is ARM64 code of the port and calls
// the mixer through these functions without a DeviceIoControl in between.
// The memory of a buffer is a block in the address space of the process that
// the host sees at a fixed pointer (AllocateDibLease). The audio thread of
// the host reads it until the source is closed, which happens before the
// block is released and when the process ends (FreeDxSound).
#include "../sdk/dx/neo98dx_abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gdi.h"
#include "sound.h"
#include "user.h"
#include "win98_mixer.h"
#include "x86_app.h"
namespace X86
{
namespace
{
enum : unsigned
{
    DsOk = 0,
    DsInvalidParam = 0x80070057u,
    DsOutOfMemory = 0x8007000eu,
    DsAllocated = 0x8878000au,
    DsBadFormat = 0x88780064u,
    DsGeneric = 0x80004005u,
};
constexpr unsigned BufferCapacity = 256;
// DSBSIZE_MIN and DSBSIZE_MAX.
constexpr unsigned MinimumBytes = 4, MaximumBytes = 0x0fffffff;
// What DSOUND.DLL adds to the write cursor of a driver's buffer :
// the time a program gets to write behind it.
constexpr unsigned WriteMarginMilliseconds = 10;
} // namespace

// The bytes of a buffer; a duplicate shares them with its original.
struct Application::DxSoundMemory
{
    Gdi::DibLease lease{};
    unsigned references = 0;
};
struct Application::DxSoundVoice
{
    bool used = false, primary = false, looping = false, background = false;
    unsigned handle = 0, bytes = 0, source = 0, cursor = 0, volume = 0xffffffff;
    unsigned original_rate = 0, effective_volume = 0xffffffff;
    Sound::Format format;
    DxSoundMemory *memory = nullptr;
    NEO98DX_SOUNDNOTIFY *notifications = nullptr;
    unsigned notification_count = 0, last_position = 0;
};
struct Application::DxSound
{
    DxSoundVoice buffers[BufferCapacity];
    unsigned next_handle = 0x2000;
};

Application::DxSoundVoice *Application::DxSoundBuffer(unsigned handle)
{
    if (!dx_sound || !handle) return nullptr;
    for (auto &buffer : dx_sound->buffers)
        if (buffer.used && buffer.handle == handle) return &buffer;
    return nullptr;
}

namespace
{
unsigned FrameBytes(const Sound::Format &format) { return format.channels * (format.bits / 8); }
} // namespace

// The original software mixer signals offsets in [last, current), including
// a wrap, and signals OFFSETSTOP when playback ends (DsoundVxdNotify).
void Application::DxSoundNotify(DxSoundVoice &buffer, unsigned position, bool stopped)
{
    const unsigned from = buffer.last_position;
    for (unsigned i = 0; i < buffer.notification_count; ++i)
    {
        const auto &note = buffer.notifications[i];
        const bool passed = note.offset != 0xffffffffu &&
            (from <= position ? note.offset >= from && note.offset < position
                              : note.offset >= from || note.offset < position);
        if (passed || (stopped && note.offset == 0xffffffffu))
        {
            const unsigned saved_error = last_error;
            ThreadApi("SetEvent", &note.event);
            last_error = saved_error;
        }
    }
    buffer.last_position = position;
}

// Service also runs while the application waits for a notification event.
void Application::DxSoundServe()
{
    Sound::Shared().Advance(Now());
    for (auto &buffer : dx_sound->buffers)
        if (buffer.used) { DxSoundVolume(buffer); DxSoundSettle(buffer); }
}

// Muting changes the mixer's gain, not the application's stored volume or
// the ring cursor. Global/sticky buffers retain their background playback.
void Application::DxSoundVolume(DxSoundVoice &buffer, bool force)
{
    const bool muted = !buffer.primary && !buffer.background &&
        User::WindowProcess(User::GetForegroundWindow()) != process_id;
    const unsigned effective = muted ? 0 : buffer.volume;
    if (buffer.source && (force || effective != buffer.effective_volume))
        Sound::Shared().SetVolume(buffer.source, effective);
    buffer.effective_volume = effective;
}

// A buffer that played to its end gives its voice back and stands at zero.
void Application::DxSoundSettle(DxSoundVoice &buffer)
{
    auto &mixer = Sound::Shared();
    if (!buffer.source) return;
    if (mixer.RingPlaying(buffer.source))
    {
        DxSoundNotify(buffer, mixer.RingCursor(buffer.source), false);
        return;
    }
    DxSoundNotify(buffer, buffer.bytes, true);
    mixer.Close(buffer.source);
    buffer.source = 0;
    buffer.cursor = buffer.last_position = 0;
}

// `unmap` is false when the process ends: its address space goes as a whole.
void Application::DxSoundRelease(DxSoundVoice &buffer, bool unmap)
{
    if (buffer.source) Sound::Shared().Close(buffer.source);
    if (buffer.memory && !--buffer.memory->references)
    {
        auto &lease = buffer.memory->lease;
        if (unmap && lease.release) lease.release(lease.context, lease);
        delete buffer.memory;
    }
    delete[] buffer.notifications;
    buffer = DxSoundVoice{};
}

unsigned Application::DxSoundCreate(unsigned address, unsigned bytes)
{
    NEO98DX_SOUND s{};
    if (bytes != sizeof s || !virtual_memory.Access(address, sizeof s, true) || !Read(address, &s, sizeof s) || s.size != sizeof s)
        return DsInvalidParam;
    Sound::Format format{s.rate, s.channels, s.bitsPerSample};
    if (!Sound::Mixer::Supports(format)) return DsBadFormat;
    const unsigned frame = FrameBytes(format);
    if (s.bytes < MinimumBytes || s.bytes > MaximumBytes || s.bytes % frame) return DsInvalidParam;
    if (!dx_sound) dx_sound = new DxSound;
    DxSoundVoice *slot = nullptr;
    for (auto &buffer : dx_sound->buffers)
        if (!buffer.used) { slot = &buffer; break; }
    if (!slot) return DsAllocated;
    auto *memory = new DxSoundMemory;
    if (!memory) return DsOutOfMemory;
    const unsigned rounded = (s.bytes + 4095) & ~4095u;
    if (!AllocateDibLease(rounded, memory->lease) || !memory->lease.host)
    {
        delete memory;
        return DsOutOfMemory;
    }
    // Silence: 80h for eight bits, zero for sixteen.
    memset(memory->lease.host, format.bits == 8 ? 0x80 : 0, rounded);
    memory->references = 1;
    *slot = DxSoundVoice{};
    slot->used = true;
    slot->primary = (s.flags & NEO98DX_SOUND_PRIMARY) != 0;
    slot->background = (s.flags & NEO98DX_SOUND_BACKGROUND) != 0;
    slot->handle = dx_sound->next_handle++;
    slot->bytes = s.bytes;
    slot->format = format;
    slot->original_rate = format.rate;
    slot->memory = memory;
    s.handle = slot->handle;
    s.memory = memory->lease.guest;
    if (!Store(address, &s, sizeof s))
    {
        DxSoundRelease(*slot, true);
        return DsInvalidParam;
    }
    return DsOk;
}

// A duplicate plays the same bytes with a cursor, a rate and a volume of its own.
unsigned Application::DxSoundDuplicate(unsigned handle, unsigned out)
{
    auto *original = DxSoundBuffer(handle);
    if (!original || original->primary || !out || !virtual_memory.Access(out, 4, true)) return DsInvalidParam;
    DxSoundVoice *slot = nullptr;
    for (auto &buffer : dx_sound->buffers)
        if (!buffer.used) { slot = &buffer; break; }
    if (!slot) return DsAllocated;
    *slot = DxSoundVoice{};
    slot->used = true;
    slot->handle = dx_sound->next_handle++;
    slot->bytes = original->bytes;
    slot->format = original->format;
    slot->original_rate = original->original_rate;
    slot->volume = original->volume;
    slot->background = original->background;
    slot->memory = original->memory;
    ++slot->memory->references;
    Write32(out, slot->handle);
    return DsOk;
}

unsigned Application::DxSoundApi(ApiName name, const unsigned *a)
{
    auto &mixer = Sound::Shared();
    switch (name.id)
    {
    API_CASE("DsDrvGetCaps")
    {
        NEO98DX_SOUNDCAPS caps{};
        if (a[1] != sizeof caps || !virtual_memory.Access(a[0], sizeof caps, true)) return DsInvalidParam;
        caps.size = sizeof caps;
        caps.voices = Sound::MaximumSources;
        unsigned playing = 0;
        if (dx_sound)
            for (const auto &buffer : dx_sound->buffers)
                if (buffer.used && buffer.source) ++playing;
        caps.voicesFree = Sound::MaximumSources - playing;
        caps.outputRate = Sound::OutputRate;
        caps.outputChannels = Sound::OutputChannels;
        caps.outputBits = 16;
        caps.lead = Sound::OutputBlock();
        caps.live = mixer.Live() ? 1 : 0;
        return Store(a[0], &caps, sizeof caps) ? DsOk : DsInvalidParam;
    }
    API_CASE("DsDrvOpen")
        // The card is opened: its output runs from now on, not from the
        // first buffer that plays. Without a device there is no driver.
        return mixer.StartOutput() ? DsOk : 0x88780078u;   // DSERR_NODRIVER
    API_CASE("DsDrvCreateBuffer") return DxSoundCreate(a[0], a[1]);
    API_CASE("DsDrvDuplicateBuffer") return DxSoundDuplicate(a[0], a[1]);
    API_CASE("DsDrvDestroyBuffer")
    {
        auto *buffer = DxSoundBuffer(a[0]);
        if (!buffer) return DsInvalidParam;
        DxSoundRelease(*buffer, true);
        return DsOk;
    }
    API_CASE("DsDrvPlay")
    {
        auto *buffer = DxSoundBuffer(a[0]);
        if (!buffer) return DsInvalidParam;
        // Without a host device the mixer runs on the clock of the port.
        mixer.Advance(Now());
        DxSoundSettle(*buffer);
        buffer->looping = (a[1] & 1) != 0;   // DSBPLAY_LOOPING
        if (!buffer->source)
        {
            buffer->source = mixer.OpenRing(buffer->memory->lease.host, buffer->bytes, buffer->format);
            // No voice left: every source of the mixer is playing.
            if (!buffer->source) return DsAllocated;
            mixer.SetSourceGroup(buffer->source, Win98Mixer::GroupWave);
            DxSoundVolume(*buffer, true);
            mixer.SetRingCursor(buffer->source, buffer->cursor);
        }
        mixer.PlayRing(buffer->source, buffer->looping);
        return DsOk;
    }
    API_CASE("DsDrvStop")
    {
        auto *buffer = DxSoundBuffer(a[0]);
        if (!buffer) return DsInvalidParam;
        mixer.Advance(Now());
        DxSoundSettle(*buffer);
        if (buffer->source)
        {
            buffer->cursor = mixer.RingCursor(buffer->source);
            DxSoundNotify(*buffer, buffer->cursor, true);
            mixer.Close(buffer->source);
            buffer->source = 0;
        }
        return DsOk;
    }
    API_CASE("DsDrvGetPosition")
    {
        // The play cursor is where the mixer reads next. The write cursor is
        // ahead of it by what the host takes in one piece, which may be gone
        // the next moment, and by the margin DSOUND gives a program to write.
        auto *buffer = DxSoundBuffer(a[0]);
        if (!buffer) return DsInvalidParam;
        mixer.Advance(Now());
        DxSoundSettle(*buffer);
        unsigned play = buffer->cursor, write = buffer->cursor;
        if (buffer->source)
        {
            play = mixer.RingCursor(buffer->source);
            const unsigned frame = FrameBytes(buffer->format);
            unsigned lead = mixer.RingLead(buffer->source) + buffer->format.rate * WriteMarginMilliseconds / 1000 * frame;
            if (lead >= buffer->bytes) lead = 0;
            write = (play + lead) % buffer->bytes;
        }
        if (a[1] && virtual_memory.Access(a[1], 4, true)) Write32(a[1], play);
        if (a[2] && virtual_memory.Access(a[2], 4, true)) Write32(a[2], write);
        return DsOk;
    }
    API_CASE("DsDrvSetPosition")
    {
        auto *buffer = DxSoundBuffer(a[0]);
        if (!buffer || a[1] >= buffer->bytes) return DsInvalidParam;
        mixer.Advance(Now());
        DxSoundSettle(*buffer);
        const unsigned frame = FrameBytes(buffer->format);
        buffer->cursor = buffer->last_position = a[1] - a[1] % frame;
        if (buffer->source) mixer.SetRingCursor(buffer->source, buffer->cursor);
        return DsOk;
    }
    API_CASE("DsDrvGetStatus")
    {
        auto *buffer = DxSoundBuffer(a[0]);
        if (!buffer || !a[1] || !virtual_memory.Access(a[1], 4, true)) return DsInvalidParam;
        mixer.Advance(Now());
        DxSoundSettle(*buffer);
        Write32(a[1], buffer->source ? NEO98DX_STATUS_PLAYING | (buffer->looping ? NEO98DX_STATUS_LOOPING : 0) : 0);
        return DsOk;
    }
    API_CASE("DsDrvSetNotifications")
    {
        auto *buffer = DxSoundBuffer(a[0]);
        const unsigned count = a[2];
        if (!buffer || count > 100000 || (count && !virtual_memory.Access(a[1], count * 8, false)))
            return DsInvalidParam;
        mixer.Advance(Now());
        DxSoundSettle(*buffer);
        if (buffer->source) return 0x88780032u; // DSERR_INVALIDCALL
        NEO98DX_SOUNDNOTIFY *notes = count ? new NEO98DX_SOUNDNOTIFY[count] : nullptr;
        if (count && !notes) return DsOutOfMemory;
        if (count && !Read(a[1], notes, count * sizeof *notes))
        {
            delete[] notes;
            return DsInvalidParam;
        }
        for (unsigned i = 0; i < count; ++i)
        {
            const auto *event = FindKernelHandle(notes[i].event);
            if ((notes[i].offset >= buffer->bytes && notes[i].offset != 0xffffffffu) ||
                !event || event->kind != 1 || !(event->access & 2))
            {
                delete[] notes;
                return DsInvalidParam;
            }
        }
        delete[] buffer->notifications;
        buffer->notifications = notes;
        buffer->notification_count = count;
        buffer->last_position = buffer->cursor;
        return DsOk;
    }
    API_CASE("DsDrvSetFormat")
    {
        // The format of the bytes of a buffer; DirectSound allows it for the
        // primary buffer only, and the DLL sees to that.
        auto *buffer = DxSoundBuffer(a[0]);
        if (!buffer) return DsInvalidParam;
        Sound::Format format{a[1], a[2], a[3]};
        if (!Sound::Mixer::Supports(format)) return DsBadFormat;
        buffer->format = format;
        buffer->original_rate = format.rate;
        buffer->cursor = 0;
        buffer->bytes -= buffer->bytes % FrameBytes(format);
        if (buffer->source) mixer.SetRingFormat(buffer->source, format);
        return DsOk;
    }
    API_CASE("DsDrvSetFrequency")
    {
        auto *buffer = DxSoundBuffer(a[0]);
        if (!buffer) return DsInvalidParam;
        const unsigned rate = a[1] ? a[1] : buffer->original_rate;   // DSBFREQUENCY_ORIGINAL
        if (rate < 100 || rate > 100000) return DsInvalidParam;
        buffer->format.rate = rate;
        if (buffer->source) mixer.SetRingRate(buffer->source, rate);
        return DsOk;
    }
    API_CASE("DsDrvSetVolumePan")
    {
        // The amplitude factors of the left and the right channel, 0 to FFFFh.
        auto *buffer = DxSoundBuffer(a[0]);
        if (!buffer) return DsInvalidParam;
        buffer->volume = (a[1] > 0xffff ? 0xffff : a[1]) | (a[2] > 0xffff ? 0xffff : a[2]) << 16;
        DxSoundVolume(*buffer, true);
        return DsOk;
    }
    default:
        Fail(name);
        return 0;
    }
}

// The process ends: every voice closes before the memory it reads goes.
void Application::FreeDxSound()
{
    if (!dx_sound) return;
    for (auto &buffer : dx_sound->buffers)
        if (buffer.used) DxSoundRelease(buffer, false);
    delete dx_sound;
    dx_sound = nullptr;
}
} // namespace X86
