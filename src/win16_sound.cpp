// SPDX-License-Identifier: GPL-2.0-or-later
// Win16 SOUND imports bind to MMSOUND.DRV. The driver controls the PC speaker
// with a five-millisecond note timer. Neo98 schedules it on the mixer clock
// in src/pc_speaker.cpp. Only the owning task may operate the device.
// OpenSound allocates the queue and sets defaults; CloseSound stops playback
// and releases ownership. Notes split into sound and silence according to
// articulation. Accent changes apply tempo before mode and pitch validation.
// Queue, frequency, and wait errors follow Windows 98; unsupported voice
// features return zero.
#include "win16_machine.h"
#include "win98_ne_task.h"
#include "win98_win16_user.h"
#include "sound.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
// The dividend of the note length. A tick of that arithmetic is
// half a callback of five milliseconds, because halves every part.
constexpr unsigned LengthDividend = 0x17700;
// The block OpenSound allocates is 0xC0 bytes and a note is six of
// them, so the queue starts with thirty-two notes.
constexpr unsigned DefaultQueueBytes = 0xc0, NoteBytes = 6;
constexpr unsigned DefaultTempo = 0x78;
// DGROUP of MMSOUND.DRV as far as the entries read it. The driver is loaded
// once for the whole system, so these words are one for every task, not one
// per process. `owner` is the word at DGROUP:0, the task that opened
// the sound; tempo, mode and pitch are the bytes at DGROUP:6..8.
struct Driver
{
    const void *owner = nullptr;
    unsigned tempo = 0, mode = 0, pitch = 0;
} driver;
// the code of the enqueue in AL picks a word of the table.
unsigned EnqueueAnswer(unsigned code)
{
    static constexpr unsigned short table[] = {0xfffc, 0xfff3, 0xfff7, 0xfff5, 0xfff2, 0xfff1};
    return code && code <= 6 ? table[code - 1] : 0;
}
// Top-octave note frequencies, C8 through B8 (4186 Hz for C8, 7040 Hz for
// A8). SetVoiceNote scales by 6 minus the octave and rounds using the last
// shifted-out bit.
constexpr unsigned NoteHertz[12] = {4186, 4435, 4699, 4978, 5274, 5588,
                                    5920, 6272, 6645, 7040, 7459, 7902};
// For notes 1..84, add the SetVoiceAccent pitch offset and wrap overflow by
// 84.
unsigned NoteFrequency(unsigned note, unsigned pitch)
{
    unsigned value = (note + pitch) & 0xff;
    if (value > 0x54)
        value -= 0x54;
    --value;
    const unsigned octave = value / 12, step = value % 12;
    const unsigned shift = octave < 6 ? 6 - octave : 0;
    const unsigned base = NoteHertz[step];
    return (base >> shift) + (shift && ((base >> (shift - 1)) & 1) ? 1u : 0u);
}
} // namespace

// The mixer thread sets the MMSOUND threshold flag. Pump copies its state
// into the guest word exposed by GetThresholdEvent.
void Application::Win16SoundServe()
{
    if (!win16.state || driver.owner != this || !win16.state->sound_event)
        return;
    Win16Word(Win16Linear(win16.state->sound_event >> 16, 0), Sound::Speaker::VoiceStatus(false));
}
// A task that ends holding the sound gives the driver back, so the next
// task's OpenSound is not refused for good, and the speaker falls
// silent. Under Win98 nothing in MMSOUND notices the end of the task; the
// queue would run out on its own or, once MMSYSTEM drops the timers of the
// task, the gate would stay open on the last divisor. This port stops the
// queue as StopSound does instead, when the task is marked as ended
// (Pump) and again when it is torn down.
void Application::Win16SoundRelease()
{
    if (driver.owner != this)
        return;
    Sound::Speaker::VoiceStop();
    Sound::Speaker::VoiceReset(0);
    driver.owner = nullptr;
}
bool Application::Win16SoundCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                 unsigned &words)
{
    // The owner check compares GetCurrentTask against DGROUP:0. A caller
    // that is not the task which opened the sound leaves every entry without
    // doing anything; the answer is zero, which is what the tails,
    // and carry.
    const bool owner = driver.owner == this;
    result = 0;
    switch (ordinal)
    {
    case 1: // OpenSound
        words = 0;
        // refuses with -1 while DGROUP:0 names a task, this one or
        // another one.
        if (driver.owner)
        {
            result = 0xffff;
            return true;
        }
        driver.owner = this;
        driver.tempo = DefaultTempo;
        driver.mode = driver.pitch = 0;
        Sound::Speaker::VoiceReset(DefaultQueueBytes / NoteBytes);
        Sound::Speaker::VoiceThreshold(0);
        result = 1; // one voice, the count Windows 98 answers
        return true;
    case 2: // CloseSound
        words = 0;
        if (owner)
            Win16SoundRelease(); // StopSound, DGROUP:0, the queue
        return true;
    case 3: // SetVoiceQueueSize(voice, bytes)
    {
        words = 2;
        if (!owner || frame.Word(1) != 1)
            return true;
        if (Sound::Speaker::VoiceCount())
        {
            result = 0xfffd; // the queue is still playing,
            return true;
        }
        Sound::Speaker::VoiceReset(frame.Word(0) / NoteBytes);
        return true;
    }
    case 4: // SetVoiceNote(voice, value, length, cdots)
    {
        words = 4;
        if (!owner || frame.Word(3) != 1)
            return true;
        const unsigned note = frame.Word(2), length = frame.Word(1);
        const unsigned divisor = (driver.tempo & 0xff) * (length & 0xff);
        // without a divisor the pop leaves the note in AX.
        if (!divisor)
        {
            result = note;
            return true;
        }
        const unsigned ticks = LengthDividend / divisor;
        if (!ticks)
            return true;
        if (!note)
        {
            // a rest is pause only.
            result = EnqueueAnswer(Sound::Speaker::VoiceQueue(0, 0, ticks));
            return true;
        }
        unsigned sound = ticks;
        if (driver.mode != 1)
        {
            sound = driver.mode == 2 ? ((ticks * 3) & 0xffff) >> 2 : ((ticks * 7) & 0xffff) >> 3;
            if (!sound)
                sound = 1;
        }
        if (note > 0x54)
        {
            result = 0xfffb; //
            return true;
        }
        result = EnqueueAnswer(
            Sound::Speaker::VoiceQueue(NoteFrequency(note, driver.pitch), sound, ticks - sound));
        return true;
    }
    case 5: // SetVoiceAccent(voice, tempo, volume, mode, pitch)
    {
        words = 5;
        if (!owner || frame.Word(4) != 1)
            return true;
        const unsigned tempo = frame.Word(3), mode = frame.Word(1), pitch = frame.Word(0);
        if (tempo > 0xff || tempo < 0x20)
        {
            result = 0xfff8;
            return true;
        }
        driver.tempo = tempo;
        if (mode > 2)
        {
            result = 0xfff6;
            return true;
        }
        driver.mode = mode;
        if (pitch > 0x53)
        {
            result = 0xfff4;
            return true;
        }
        driver.pitch = pitch;
        return true;
    }
    case 6: // SetVoiceEnvelope(voice, shape, repeat): is two instructions.
        words = 3;
        return true;
    case 7: // SetSoundNoise(source, duration): is the same two.
        words = 2;
        return true;
    case 8: // SetVoiceSound(voice, frequency, duration)
    {
        words = 4;
        if (!owner || frame.Word(3) != 1)
            return true;
        // The four words are the voice, the LONG frequency and the duration, so
        // the frequency begins at word one and its whole number half, which
        // Windows 98 reads, is word two. Word one is the fractional half; 440 Hz is
        // 01B80000h, whose low word is zero. A frequency of zero makes the
        // duration zero.
        const unsigned hertz = frame.Word(2);
        const unsigned duration = hertz ? frame.Word(0) : 0;
        result = EnqueueAnswer(Sound::Speaker::VoiceQueue(hertz, duration, 0));
        return true;
    }
    case 9: // StartSound
        words = 0;
        if (owner)
            Sound::Speaker::VoiceStart();
        return true;
    case 10: // StopSound
        words = 0;
        if (owner)
            Sound::Speaker::VoiceStop();
        return true;
    case 11: // WaitSoundState(state)
    {
        words = 1;
        if (!owner)
            return true;
        const unsigned wanted = frame.Word(0);
        if (wanted > 2)
        {
            result = 0xfff0;
            return true;
        }
        // The original yields and before every look, until
        // the queue has run down or the threshold was reached. Yield is the
        // scheduler rule of this port too, so the wait is the same wait; the
        // guard is the queue, not a count of turns (a limit of 20000
        // turns answered "done" early).
        const unsigned nothing[] = {0};
        while (state != Failed && state != Exited)
        {
            Api("Sleep", nothing); // KERNEL.29 Yield takes the same wait
            if (wanted == 0 ? !Sound::Speaker::VoiceCount() : Sound::Speaker::VoiceStatus(true) != 0)
                break;
        }
        return true;
    }
    case 12: // SyncAllVoices: is `xor ax, ax` and `retf`.
        words = 0;
        return true;
    case 13: // CountVoiceNotes(voice)
        words = 1;
        if (owner && frame.Word(0) == 1)
            result = Sound::Speaker::VoiceCount();
        return true;
    case 14: // GetThresholdEvent: the far pointer to the threshold event.
        words = 0;
        if (owner)
        {
            if (!win16.state->sound_event)
                win16.state->sound_event = Win16SoundEvent();
            result = win16.state->sound_event;
            Win16SoundServe();
        }
        return true;
    case 15: // GetThresholdStatus: the word at DGROUP:0x20, read and cleared.
        words = 0;
        if (owner)
        {
            result = Sound::Speaker::VoiceStatus(true);
            Win16SoundServe();
        }
        return true;
    case 16: // SetVoiceThreshold(voice, notes)
        words = 2;
        if (owner && frame.Word(1) == 1)
            Sound::Speaker::VoiceThreshold(frame.Word(0));
        return true;
    default:
        break;
    }
    char text[200];
    snprintf(text, sizeof text,
             "Win16 SOUND ordinal %u is not one of the eighteen entries of MMSOUND.DRV", ordinal);
    Fail(text);
    return false;
}
// MMSYSTEM.402, WAVEOUTGETDEVCAPS, at segment. Its Pascal frame is
// cbwoc at BP+6, the far WAVEOUTCAPS pointer at BP+8 and the device word at
// BP+0Ch. The driver hands MMSYSTEM a 32h-byte Win16 structure. WINMM's
// 32-bit wrapper later inserts the reserved word at 2Eh and moves dwSupport
// from 2Eh to 30h, so the inverse conversion removes that word here.
bool Application::Win16WaveOutGetDevCaps(const Win16Frame &frame, unsigned &result)
{
    const unsigned bytes = frame.Word(0);
    if (!bytes)
    {
        result = 0;
        return true;
    }
    const unsigned target = frame.Pointer(1);
    if (!target)
    {
        result = 11; // MMSYSERR_INVALPARAM
        return true;
    }
    const unsigned scratch = Win16Scratch();
    if (!scratch)
        return false;
    const unsigned args[] = {frame.Word(3), scratch, 52};
    result = Api("waveOutGetDevCapsA", args);
    if (result)
        return true;
    unsigned char caps32[52] = {}, caps16[50] = {};
    if (!Read(scratch, caps32, sizeof caps32))
        return false;
    memcpy(caps16, caps32, 46);
    memcpy(caps16 + 46, caps32 + 48, 4);
    const unsigned copy = bytes < sizeof caps16 ? bytes : (unsigned)sizeof caps16;
    if (!Store(target, caps16, copy))
        result = 11;
    return true;
}

// MMSYSTEM.502, WAVEINGETDEVCAPS: the frame of 402. WAVEINCAPS of this side
// is the 2Eh bytes the driver hands over, the same ones the 32-bit
// waveInGetDevCapsA copies before it clears wReserved1, so the
// conversion is a copy of the first 2Eh bytes.
bool Application::Win16WaveInGetDevCaps(const Win16Frame &frame, unsigned &result)
{
    const unsigned bytes = frame.Word(0);
    if (!bytes) { result = 0; return true; }
    const unsigned target = frame.Pointer(1);
    if (!target) { result = 11; return true; }
    const unsigned scratch = Win16Scratch();
    if (!scratch) return false;
    const unsigned device = frame.Word(3) == 0xffff ? 0xffffffffu : frame.Word(3);
    const unsigned args[] = {device, scratch, 0x30};
    result = Api("waveInGetDevCapsA", args);
    if (result) return true;
    unsigned char caps[0x2e] = {};
    if (!Read(scratch, caps, sizeof caps)) return false;
    const unsigned copy = bytes < sizeof caps ? bytes : (unsigned)sizeof caps;
    if (!Store(target, caps, copy)) result = 11;
    return true;
}

// The Win16 wave family keeps WORD handles and 20h-byte WAVEHDR16 records on
// its side of MMSYSTEM. Native WINMM uses its shared 32-bit handle records and
// 20h-byte headers. These tables keep both identities alive until unprepare or
// close; in particular WOM_DONE must name the caller's original FAR header.
// The input family 501 to 514 has the same frames as the output family 401
// to 412 a hundred below it (waveInAddBuffer where waveOutWrite is,
// waveInStart and waveInStop where waveOutPause and waveOutRestart are), so
// one bridge serves both, and WIM_DATA names the caller's header the way
// WOM_DONE does. 513 waveInGetID and 514 waveInMessage have frames of their
// own.
bool Application::Win16WaveOutCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                   unsigned &words)
{
    auto &machine = *win16.state;
    const bool input = ordinal >= 500;
    const auto api = [&](const char *output, const char *recording) { return input ? recording : output; };
    if (input) ordinal -= 100;
    constexpr unsigned CallbackMask = 0x70000, CallbackWindow = 0x10000, CallbackTask = 0x20000,
                       CallbackFunction = 0x30000;
    constexpr unsigned WomOpen = 0x3bb, WimOpen = 0x3be;
    constexpr unsigned InvalidHandle = 5, InvalidParameter = 11;
    const auto stream_of = [&](unsigned word) -> Win16Machine::WaveStream * {
        if (!word) return nullptr;
        for (auto &stream : machine.wave_streams)
            if (stream.word == word) return &stream;
        return nullptr;
    };
    const auto header_of = [&](unsigned stream, unsigned flat) -> Win16Machine::WaveHeader * {
        for (auto &header : machine.wave_headers)
            if (header.stream == stream && header.flat == flat) return &header;
        return nullptr;
    };
    const auto copy_out = [&](Win16Machine::WaveHeader &header) {
        Win16Word(header.flat + 8, Read32(header.native + 8));
        Win16Word(header.flat + 10, Read32(header.native + 8) >> 16);
        Win16Word(header.flat + 16, Read32(header.native + 16));
        Win16Word(header.flat + 18, Read32(header.native + 16) >> 16);
    };
    if (ordinal == 404)
    {
        words = 11;
        const unsigned flags = frame.Long(0), kind = flags & CallbackMask;
        // MMSYSTEM checks the callback with before anything
        // else of the call and answers MMSYSERR_INVALPARAM for a bad one;
        // waveInOpen goes through the same worker.
        const unsigned checked = Win16CallbackCheck(flags >> 16, frame.Long(4));
        if (checked == 1) { result = InvalidParameter; return true; }
        if (checked == 2)
        {
            // Kinds 4 to 6 are the bridges of WINMM's 32-bit side (DriverCallback); a 16-bit program has no business with them.
            char text[160];
            snprintf(text, sizeof text, "Win16 waveOutOpen with callback kind %u, a bridge of the 32-bit side of WINMM",
                     (flags >> 16) & 7);
            Fail(text);
            return false;
        }
        const unsigned output = frame.Pointer(9);
        // WAVE_MAPPER is the UINT -1, FFFFh on this side.
        const unsigned device = frame.Word(8) == 0xffff ? 0xffffffffu : frame.Word(8);
        // Query validates the common packed PCM format but neither creates a
        // mapping nor writes the caller's output word.
        if (flags & 1)
        {
            const unsigned args[] = {0, device, frame.Pointer(6), 0, 0, flags};
            result = Api(api("waveOutOpen", "waveInOpen"), args);
            return true;
        }
        if (!output || !virtual_memory.Access(output, 2, true))
        { result = InvalidParameter; return true; }
        Win16Machine::WaveStream *slot = nullptr;
        for (auto &stream : machine.wave_streams)
            if (!stream.word) { slot = &stream; break; }
        if (!slot) { result = 4; return true; }
        const unsigned native_output = Win16Scratch();
        if (!native_output) return false;
        Write32(native_output, 0);
        // Native callback dispatch is suppressed. WaveOutNotify below still
        // intercepts completion and close and translates them; OPEN is posted
        // here once the Win16 mapping exists.
        const unsigned args[] = {native_output, device, frame.Pointer(6), 0,
                                 frame.Long(2), flags & ~CallbackMask};
        result = Api(api("waveOutOpen", "waveInOpen"), args);
        if (result) return true;
        const unsigned native = Read32(native_output);
        unsigned word = 1;
        for (;; ++word)
        {
            bool used = false;
            for (const auto &stream : machine.wave_streams) used |= stream.word == word;
            if (!used) break;
        }
        *slot = Win16Machine::WaveStream{word, native, frame.Long(4), frame.Long(2), kind};
        Win16Word(output, word);
        const unsigned opened = input ? WimOpen : WomOpen;
        if (kind == CallbackWindow || kind == CallbackTask)
            Win16PostCallback(kind, slot->callback, opened, word, 0);
        else if (kind == CallbackFunction)
            Win16WaveOutCallback(slot->callback, slot->word, slot->instance, opened, 0);
        return true;
    }
    if (input && ordinal == 414) // waveInMessage(hWaveIn, uMsg, dw1, dw2)
    {
        words = 6;
        auto *stream = stream_of(frame.Word(5));
        if (!stream) { result = InvalidHandle; return true; }
        const unsigned message = frame.Word(4);
        if (message != 0x2000)
        {
            char text[160];
            snprintf(text, sizeof text, "Win16 waveInMessage %04xh: its parameters are not converted", message);
            Fail(text);
            return false;
        }
        const unsigned args[] = {stream->native, message, frame.Long(2), frame.Pointer(0)};
        result = Api("waveInMessage", args);
        return true;
    }
    if (input && ordinal == 413) // waveInGetID(hWaveIn, lpuDeviceID): a word on this side
    {
        words = 3;
        auto *stream = stream_of(frame.Word(2));
        if (!stream) { result = InvalidHandle; return true; }
        const unsigned target = frame.Pointer(0);
        if (!target || !virtual_memory.Access(target, 2, true)) { result = InvalidParameter; return true; }
        const unsigned native = Win16Scratch();
        if (!native) return false;
        const unsigned args[] = {stream->native, native};
        result = Api("waveInGetID", args);
        if (!result) Win16Word(target, Read32(native));
        return true;
    }
    if (input && (ordinal == 415 || ordinal == 421 || ordinal > 414))
    {
        char text[120];
        snprintf(text, sizeof text, "Win16 MMSYSTEM.%u is not a wave input entry", ordinal + 100);
        Fail(text);
        return false;
    }

    if (ordinal == 421) // waveOutMessage(hWaveOut, uMsg, dw1, dw2)
    {
        words = 6;
        auto *stream = stream_of(frame.Word(5));
        if (!stream) { result = InvalidHandle; return true; }
        const unsigned message = frame.Word(4);
        // Only messages whose parameters are known are passed on: the mapper
        // status takes a pointer to a DWORD in dw2, WODM_GETVOLUME (10h) one
        // in dw1, WODM_SETVOLUME (11h) the volume itself.
        if (message == 0x10 || message == 0x11)
        {
            const unsigned args[] = {stream->native, message == 0x10 ? frame.Pointer(2) : frame.Long(2)};
            result = Api(message == 0x10 ? "waveOutGetVolume" : "waveOutSetVolume", args);
            return true;
        }
        if (message != 0x2000)
        {
            char text[160];
            snprintf(text, sizeof text, "Win16 waveOutMessage %04xh: its parameters are not converted", message);
            Fail(text);
            return false;
        }
        const unsigned args[] = {stream->native, message, frame.Long(2), frame.Pointer(0)};
        result = Api("waveOutMessage", args);
        return true;
    }
    if (ordinal == 415) // waveOutGetVolume(uDeviceID, lpdwVolume): a device or a handle
    {
        words = 3;
        const unsigned given = frame.Word(2);
        auto *open_stream = stream_of(given);
        const unsigned args[] = {open_stream ? open_stream->native : given, frame.Pointer(0)};
        result = Api("waveOutGetVolume", args);
        return true;
    }
    if (ordinal == 409 || ordinal == 410) // waveOutPause, waveOutRestart; waveInStart, waveInStop
    {
        words = 1;
        auto *stream = stream_of(frame.Word(0));
        if (!stream) { result = InvalidHandle; return true; }
        const unsigned args[] = {stream->native};
        result = Api(ordinal == 409 ? api("waveOutPause", "waveInStart") : api("waveOutRestart", "waveInStop"), args);
        return true;
    }
    words = (ordinal >= 406 && ordinal <= 408) || ordinal == 412 ? 4 : 1;
    const unsigned word = frame.Word(words - 1);
    auto *stream = stream_of(word);
    if (!stream) { result = InvalidHandle; return true; }
    if (ordinal == 412)
    {
        const unsigned target = frame.Pointer(1), bytes = frame.Word(0);
        if (!target || bytes < 6 || !virtual_memory.Access(target, 6, true))
        { result = InvalidParameter; return true; }
        const unsigned native = Win16Scratch();
        if (!native) return false;
        Write32(native, Win16Word(target));
        Write32(native + 4, 0);
        Write32(native + 8, 0);
        const unsigned args[] = {stream->native, native, 12};
        result = Api(api("waveOutGetPosition", "waveInGetPosition"), args);
        if (!result)
        {
            Win16Word(target, Read32(native));
            Win16Word(target + 2, Read32(native + 4));
            Win16Word(target + 4, Read32(native + 4) >> 16);
        }
        return true;
    }
    if (ordinal == 411)
    {
        const unsigned args[] = {stream->native};
        result = Api(api("waveOutReset", "waveInReset"), args);
        return true;
    }
    if (ordinal == 405)
    {
        const unsigned args[] = {stream->native};
        result = Api(api("waveOutClose", "waveInClose"), args);
        if (!result)
        {
            for (auto &header : machine.wave_headers)
                if (header.stream == word)
                {
                    // The native handle is gone, so no later entry can use
                    // this provider-private preparation block.
                    const unsigned block = Read32(header.native + 0x1c);
                    if (block) Free(block);
                    Free(header.native);
                    header = Win16Machine::WaveHeader{};
                }
            *stream = Win16Machine::WaveStream{};
        }
        return true;
    }

    const unsigned flat = frame.Pointer(1), far_pointer = frame.Long(1), bytes = frame.Word(0);
    if (!flat || bytes < 32 || !virtual_memory.Access(flat, 32, true))
    { result = InvalidParameter; return true; }
    auto *header = header_of(word, flat);
    bool created = false;
    if (ordinal == 406 && !header)
    {
        for (auto &candidate : machine.wave_headers)
            if (!candidate.stream) { header = &candidate; break; }
        if (!header) { result = 7; return true; }
        const unsigned native = Allocate(32);
        if (!native) { result = 7; return true; }
        unsigned char zero[32] = {};
        Store(native, zero, sizeof zero);
        const unsigned data_offset = Win16Word(flat), data_selector = Win16Word(flat + 2);
        Write32(native, data_selector ? Win16Linear(data_selector, data_offset) : 0);
        for (unsigned at = 4; at <= 20; at += 4) Write32(native + at, Read32(flat + at));
        *header = Win16Machine::WaveHeader{word, far_pointer, flat, native};
        created = true;
    }
    if (!header) { result = ordinal == 407 ? 0 : 34; return true; }
    const char *entry = ordinal == 406 ? api("waveOutPrepareHeader", "waveInPrepareHeader")
                        : ordinal == 407 ? api("waveOutUnprepareHeader", "waveInUnprepareHeader")
                                         : api("waveOutWrite", "waveInAddBuffer");
    if (ordinal == 408)
    {
        const unsigned data_offset = Win16Word(flat), data_selector = Win16Word(flat + 2);
        Write32(header->native, data_selector ? Win16Linear(data_selector, data_offset) : 0);
        for (unsigned at = 4; at <= 12; at += 4)
            Write32(header->native + at, Read32(flat + at));
        const unsigned managed = 1 | 2 | 0x10;
        Write32(header->native + 16, (Read32(flat + 16) & ~managed) |
                                            (Read32(header->native + 16) & managed));
        Write32(header->native + 20, Read32(flat + 20));
    }
    const unsigned args[] = {stream->native, header->native, 32};
    result = Api(entry, args);
    copy_out(*header);
    if (created && result)
    {
        Free(header->native);
        *header = Win16Machine::WaveHeader{};
        return true;
    }
    if (ordinal == 407 && !result)
    {
        Free(header->native);
        *header = Win16Machine::WaveHeader{};
    }
    return true;
}

// WIM_DATA and WIM_CLOSE of a stream a 16-bit program opened: the same
// translation as the output, by the native handle.
bool Application::Win16WaveInNotify(unsigned handle, unsigned message, unsigned parameter)
{
    return Win16WaveOutNotify(handle, message, parameter);
}
bool Application::Win16WaveOutNotify(unsigned handle, unsigned message, unsigned parameter)
{
    if (!win16.state) return false;
    auto &machine = *win16.state;
    Win16Machine::WaveStream *stream = nullptr;
    for (auto &candidate : machine.wave_streams)
        if (candidate.native == handle) { stream = &candidate; break; }
    if (!stream) return false;
    unsigned translated = 0;
    bool known_header = !parameter;
    if (parameter)
        for (auto &header : machine.wave_headers)
            if (header.stream == stream->word && header.native == parameter)
            {
                Win16Word(header.flat + 8, Read32(header.native + 8));
                Win16Word(header.flat + 10, Read32(header.native + 8) >> 16);
                Win16Word(header.flat + 16, Read32(header.native + 16));
                Win16Word(header.flat + 18, Read32(header.native + 16) >> 16);
                translated = header.far_pointer;
                known_header = true;
                break;
            }
    // DriverCallback : a window or a task gets WOM_CLOSE with the
    // caller's dwInstance in lParam.
    if ((stream->kind == 0x10000 || stream->kind == 0x20000) && known_header)
        Win16PostCallback(stream->kind, stream->callback, message, stream->word,
                          message == 0x3bc || message == 0x3bf ? stream->instance : translated);
    else if (stream->kind == 0x30000 && known_header)
        Win16WaveOutCallback(stream->callback, stream->word, stream->instance, message, translated);
    return true;
}
// The callback check of MMSYSTEM, on the high word of the open flags
// and the callback: 0 no callback, 1 a window (IsWindow, the high word of the
// handle zero), 2 a task (IsTask, KERNEL.320: the selector of a task data
// with "TD" at FAh), 3 a far procedure (LAR: a present, readable code
// segment; LSL: the offset below its limit), 4 to 6 without a check. The
// answer is 0 for a good callback, 1 for a bad one (MMSYSERR_INVALPARAM at
// the call site) and 2 for kinds 4 to 6.
unsigned Application::Win16CallbackCheck(unsigned kind, unsigned callback)
{
    switch (kind & 7)
    {
    case 0:
        return 0;
    case 1:
        return !(callback >> 16) && User::IsWindow(callback & 0xffff) ? 0 : 1;
    case 2:
    {
        const unsigned task = callback & 0xffff;
        const auto *entry = !(callback >> 16) && task ? selectors.At(task) : nullptr;
        return entry && entry->limit >= Win98NeTask::TaskSignature + 1 &&
                       Win16Word(entry->base + Win98NeTask::TaskSignature) == Win98NeTask::TaskSignatureValue
                   ? 0
                   : 1;
    }
    case 3:
    {
        const auto *entry = selectors.At(callback >> 16);
        return entry && entry->present && entry->code && (callback & 0xffff) < entry->limit ? 0 : 1;
    }
    case 4:
    case 5:
    case 6:
        return 2;
    default:
        return 1;
    }
}
// The thread whose queue belongs to a task word. GetCurrentTask answers the
// selector of the task data, a thread of mmThreadCreate is a task of its
// own, and GetWindowTask answers the thread itself (src/win16_user.cpp), so
// a word that is no task database of this process passes unchanged.
unsigned Application::Win16TaskThread(unsigned task)
{
    if (!win16.state || !task)
        return task;
    if (task == win16.state->task && win16.state->task_thread)
        return win16.state->task_thread;
    for (const auto &record : win16.state->hosted_threads)
        if (record.thread && record.task == task)
            return record.thread;
    return task;
}
// DriverCallback for a window (USER.110 PostMessage) and for a
// task (USER.116 PostAppMessage, the same entry the gate of USER.116 takes).
void Application::Win16PostCallback(unsigned kind, unsigned callback, unsigned message, unsigned word,
                                    unsigned parameter)
{
    if (kind == 0x10000)
    {
        User::PostMessage((User::HWND)callback, message, word, parameter);
        return;
    }
    const unsigned a[] = {Win16TaskThread(callback & 0xffff), message, word, parameter};
    Api("PostThreadMessageA", a);
}
// The DriverCallback of MMSYSTEM for DCB_FUNCTION: the caller's procedure
// waveOutProc(hWaveOut, uMsg, dwInstance, dwParam1, dwParam2), Pascal order,
// with the 16-bit handle and the far address of the header.
void Application::Win16WaveOutCallback(unsigned callback, unsigned word, unsigned instance, unsigned message,
                                       unsigned first)
{
    if (!callback)
        return;
    const unsigned push[] = {word, message & 0xffff, instance >> 16, instance & 0xffff, first >> 16, first & 0xffff, 0, 0};
    if (log)
    {
        char text[120];
        snprintf(text, sizeof text, "WIN16 waveOut callback %04x:%04x message=%03x", callback >> 16, callback & 0xffff,
                 message);
        log(text);
    }
    bool returned = false;
    Win16HostedCall(callback, push, 8, 0, nullptr, &returned);
}

void Application::Win16FreeWaveOut()
{
    // A task that ends holding the sound gives the driver back, so the next
    // task's OpenSound is not refused for good.
    Win16SoundRelease();
    if (!win16.state) return;
    for (auto &header : win16.state->wave_headers)
    {
        if (header.native)
        {
            const unsigned block = Read32(header.native + 0x1c);
            if (block) Free(block);
            Free(header.native);
        }
        header = Win16Machine::WaveHeader{};
    }
    for (auto &stream : win16.state->wave_streams)
        stream = Win16Machine::WaveStream{};
}
// The word GetThresholdEvent hands out as a far pointer. The original hands
// out DGROUP:0x20 of the driver; here it is a word of the arena behind its own
// descriptor, because this port has no driver data segment, and Pump keeps
// the status of the callback in it (Win16SoundServe).
unsigned Application::Win16SoundEvent()
{
    const unsigned block = Allocate(4);
    if (!block)
        return 0;
    const unsigned selector = Win16Segment(block, 3, false, false);
    if (!selector)
        return 0;
    Win16Word(block, 0);
    return selector << 16;
}
} // namespace X86
