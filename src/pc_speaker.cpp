// SPDX-License-Identifier: GPL-2.0-or-later
// PC speaker emulation. Programs set the 8253 channel 2 divisor through ports
// 43h and 42h, then enable the gate and speaker through bits 0 and 1 of port
// 61h. Both bits must be set to produce sound. The frequency is 1,193,182 Hz
// divided by the divisor; zero represents 65536.
// Neo98 mixes the resulting square wave as a PCM generator. Direct port
// access, MessageBeep(0xFFFFFFFF), KERNEL32 Beep and the MMSOUND.DRV note
// queue share this speaker. The note queue advances every five milliseconds
// on the mixer clock.
// The guest thread writes this state and the audio thread reads it. Hold
// Sound::Lock until Sound::Unlock for every change, as with other mixer
// sources.
#include "sound.h"

namespace Sound
{
namespace Speaker
{
namespace
{
// The 8253 input clock is 14.31818 MHz divided by 12.
constexpr unsigned long long TimerHertz = 1193182;
// Use one eighth of full amplitude so the square wave does not drown out
// other sources.
constexpr int Amplitude = 0x1000;
// Mute frequencies outside the audible range: smaller divisors produce
// ultrasound; larger ones produce isolated clicks.
constexpr unsigned LowestHertz = 20, HighestHertz = 16000;

struct State
{
    unsigned divisor = 0;      // Zaehler 2
    unsigned latch = 0;        // Partially written word on port 42h.
    bool have_latch = false;   // Whether the low byte has arrived.
    bool low_first = true;     // Byte order selected by the port 43h control word.
    unsigned port_b = 0;       // Last four bits written to port 61h.
    unsigned tone_hertz = 0;   // A tone with a fixed duration
    unsigned tone_frames = 0;  // Remaining output frames.
    unsigned phase = 0;        // Wave phase, from zero to OutputRate-1.
    unsigned source = 0;       // Mixer source handle.
} state;

// MMSOUND.DRV queue entries contain three words: channel 2 divisor, tone
// ticks and following rest ticks. The queue block is limited to 64 KB.
struct Note
{
    unsigned short divisor, sound, silence;
};
constexpr unsigned MaximumNotes = 0xffff / 6;
// Convert frequency to a divisor using 0x1234DC. A rest uses frequency
// 0xFFFF, producing divisor 18 and an inaudible 66 kHz tone. Stopping uses
// divisor 0x1D, also inaudible.
constexpr unsigned VoiceDividend = 0x1234dc, RestDivisor = 0x1234dc / 0xffff, IdleDivisor = 0x1d;
// Five milliseconds equals 220.5 output frames; count time in half-frames.
constexpr unsigned HalfFramesPerTick = OutputRate * 2 * 5 / 1000;
struct Voice
{
    bool active = false;                // Whether the queue callback is running.
    unsigned capacity = 0;              // Queue capacity in entries.
    unsigned head = 0, tail = 0;        // Queue entry indices.
    unsigned count = 0;                 // Pending entries.
    unsigned threshold = 0;             // DGROUP:16h
    unsigned status = 0;                // DGROUP:20h
    unsigned remaining = 0;             // Ticks in the current sound or silence section
    unsigned pending_silence = 0;       // Trailing silence of the previous note.
    unsigned divisor = IdleDivisor;     // Last divisor written by the callback to port 42h.
    unsigned clock = 0;                 // Half-frames since the last tick
    Note notes[MaximumNotes];
} voice;

unsigned CountedDivisor() { return state.divisor ? state.divisor : 65536; }

// A fixed-duration MessageBeep takes priority over the MMSOUND queue, which
// keeps the gate and speaker enabled while its callback runs. Direct port
// state has the lowest priority.
unsigned Current()
{
    if (state.tone_frames)
        return state.tone_hertz;
    if (voice.active)
        return (unsigned)(TimerHertz / voice.divisor);
    if ((state.port_b & 3) != 3)
        return 0;
    return (unsigned)(TimerHertz / CountedDivisor());
}

// Stop the callback, close the gate, set channel 2 to divisor 0x1D and empty
// the queue. Preserve the remaining rest duration.
void StopLocked()
{
    voice.active = false;
    voice.remaining = 0;
    voice.head = voice.tail = voice.count = 0;
    voice.divisor = IdleDivisor;
}

// One note-timer callback.
void Tick()
{
    // Decrement the current phase duration. Keep its state until the count
    // reaches zero.
    if (voice.remaining && --voice.remaining)
        return;
    // Stop the callback when the queue is empty, even if the last note still
    // has pending silence.
    if (!voice.count)
    {
        StopLocked();
        return;
    }
    // First play the previous note's silence using divisor 18.
    if (voice.pending_silence)
    {
        voice.remaining = voice.pending_silence;
        voice.pending_silence = 0;
        voice.divisor = RestDivisor;
        return;
    }
    const Note note = voice.notes[voice.head];
    voice.head = voice.head + 1 >= voice.capacity ? 0 : voice.head + 1;
    voice.pending_silence = note.silence;
    // Decrement pending notes and set the threshold flag when the count
    // reaches the configured threshold.
    --voice.count;
    if (voice.count == voice.threshold)
        voice.status |= 1;
    voice.divisor = note.divisor;
    voice.remaining = note.sound;
}

void RenderSquare(void *, short *out, unsigned frames)
{
    for (unsigned f = 0; f < frames; ++f)
    {
        if (voice.active)
        {
            voice.clock += 2;
            if (voice.clock >= HalfFramesPerTick)
            {
                voice.clock -= HalfFramesPerTick;
                Tick();
            }
        }
        const unsigned hertz = Current();
        short value = 0;
        if (hertz >= LowestHertz && hertz <= HighestHertz)
        {
            value = (short)(state.phase * 2 < OutputRate ? Amplitude : -Amplitude);
            state.phase += hertz;
            while (state.phase >= OutputRate) state.phase -= OutputRate;
        }
        out[f * 2] = out[f * 2 + 1] = value;
        if (state.tone_frames) --state.tone_frames;
    }
}

// Open the mixer source on the first sound and keep it open until ResetAll
// closes all sources.
void Ensure()
{
    if (state.source && Shared().Valid(state.source))
        return;
    state.source = Shared().OpenGenerator(RenderSquare, nullptr);
}
} // namespace

void Write(unsigned port, unsigned char value)
{
    Lock();
    switch (port)
    {
    case 0x43:
        // Control-word bits 7:6 select the channel and bits 5:4 select the
        // load order. The speaker uses channel 2 with either LSB then MSB
        // (11b) or MSB only (10b).
        if ((value >> 6) == 2)
        {
            state.low_first = ((value >> 4) & 3) != 2;
            state.have_latch = false;
        }
        break;
    case 0x42:
        if (!state.low_first)
        {
            state.divisor = (unsigned)value << 8;
            state.phase = 0;
        }
        else if (!state.have_latch)
        {
            state.latch = value;
            state.have_latch = true;
        }
        else
        {
            state.divisor = state.latch | ((unsigned)value << 8);
            state.have_latch = false;
            state.phase = 0;
        }
        break;
    case 0x61:
    {
        const unsigned before = state.port_b & 3;
        state.port_b = value & 0x0f;
        // Opening the gate restarts the waveform, as reloading the counter
        // does.
        if (before != 3 && (state.port_b & 3) == 3) state.phase = 0;
        break;
    }
    default:
        Unlock();
        return;
    }
    Unlock();
    if (Current()) Ensure();
}

unsigned PortB() { return state.port_b; }
unsigned Divisor() { return CountedDivisor(); }
unsigned Hertz() { return (unsigned)(TimerHertz / CountedDivisor()); }

// Port 61h bit 5 is channel 2 output. In mode 3 it is high for half the
// period and low for the other half. Closing the gate stops the counter and
// leaves output high. Guest busy loops depend on these transitions.
bool Output(unsigned long long now)
{
    if (!(state.port_b & 1))
        return true;
    const unsigned long long period = CountedDivisor();
    const unsigned long long ticks = now * TimerHertz / 1000000;
    return ticks % period < (period + 1) / 2;
}

void Tone(unsigned hertz, unsigned milliseconds)
{
    Lock();
    state.tone_hertz = hertz;
    state.tone_frames = (unsigned)((unsigned long long)milliseconds * OutputRate / 1000);
    state.phase = 0;
    Unlock();
    if (state.tone_frames) Ensure();
}

bool Sounding()
{
    const unsigned hertz = Current();
    return hertz >= LowestHertz && hertz <= HighestHertz;
}

bool Clocked() { return state.source != 0; }

void Reset()
{
    state = State();
    voice.active = false;
    voice.capacity = voice.head = voice.tail = voice.count = 0;
    voice.threshold = voice.status = voice.remaining = voice.pending_silence = voice.clock = 0;
    voice.divisor = IdleDivisor;
}

void VoiceReset(unsigned capacity)
{
    Lock();
    voice.capacity = capacity < MaximumNotes ? capacity : MaximumNotes;
    voice.head = voice.tail = voice.count = 0;
    Unlock();
}

unsigned VoiceQueue(unsigned hertz, unsigned sound, unsigned silence)
{
    Lock();
    unsigned answer = 0;
    // The queue is full when its read and write positions coincide with
    // entries pending. Treat a buffer shorter than one six-byte entry as
    // full; Windows 98 can fault for such an invalid buffer.
    if (!voice.capacity || voice.count >= voice.capacity)
        answer = 1;
    else
    {
        Note note{};
        if (!hertz || !sound)
        {
            // For a rest, halve only the rest duration; leave the tone
            // duration unchanged.
            note.divisor = (unsigned short)RestDivisor;
            note.sound = (unsigned short)sound;
            note.silence = (unsigned short)(((silence + 1) & 0xffff) >> 1);
        }
        else if (hertz < 0x25 || hertz > 0x7fff)
            answer = 2; //
        else
        {
            // Halve both durations, rounding up. One SetVoiceNote tick is 2.5
            // milliseconds.
            note.divisor = (unsigned short)(VoiceDividend / hertz);
            note.sound = (unsigned short)(((sound + 1) & 0xffff) >> 1);
            note.silence = (unsigned short)(((silence + 1) & 0xffff) >> 1);
        }
        if (!answer)
        {
            voice.notes[voice.tail] = note;
            voice.tail = voice.tail + 1 >= voice.capacity ? 0 : voice.tail + 1;
            ++voice.count;
        }
    }
    Unlock();
    return answer;
}

void VoiceStart()
{
    Lock();
    // An empty queue does nothing. Keep an existing callback; otherwise
    // schedule the first callback five milliseconds after timeSetEvent.
    const bool start = voice.count && !voice.active;
    if (start)
    {
        voice.active = true;
        voice.clock = 0;
    }
    Unlock();
    if (start) Ensure();
}

void VoiceStop()
{
    Lock();
    StopLocked();
    Unlock();
}

unsigned VoiceCount()
{
    Lock();
    const unsigned count = !voice.active ? 0u : voice.count ? voice.count : 1u;
    Unlock();
    return count;
}

void VoiceThreshold(unsigned notes)
{
    Lock();
    voice.threshold = notes;
    Unlock();
}

unsigned VoiceStatus(bool clear)
{
    Lock();
    const unsigned status = voice.status;
    if (clear) voice.status = 0;
    Unlock();
    return status;
}
} // namespace Speaker
} // namespace Sound
