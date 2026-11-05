// SPDX-License-Identifier: GPL-2.0-or-later
// The recording side of the sound card: the ring the host microphone fills
// and the streams the wave input of WINMM reads from it. See src/sound.h for
// the recording interface.
// Three things happen here:
// the ring Mono signed 16-bit frames at InputRate, written by the host
// (Push, on its audio thread) or by the clock of this file
// (Advance) when nothing is pushed: the test device and the
// silence of a refused microphone. `written` counts every
// frame since the start; a slot is that number modulo the
// size of the ring.
// the streams Each one keeps its own place in the ring as a frame number
// and a fraction of its own rate, so one microphone feeds a
// program at 11.025 Hz and DirectSoundCapture at 44.100 Hz
// at once. A stream that falls more than a ring behind loses
// the oldest part, as a card loses what no buffer took.
// the device One reference per started stream. The first one opens the
// host device, the last Close shuts it.
#include "sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace Sound
{
namespace
{
constexpr unsigned RingFrames = 1u << 17;   // about three seconds
constexpr unsigned MaximumStreams = 16;
short ring[RingFrames];
unsigned long long written = 0;

struct Stream
{
    bool open = false, started = false, holds = false;
    unsigned long long position = 0;   // the next frame of the ring
    unsigned fraction = 0;             // 0 to rate-1, between position and position+1
};
Stream streams[MaximumStreams];

Input input;
bool enabled = true;
unsigned state = InputOff;
unsigned holders = 0;
unsigned level = 0xffffffffu, gain = 0xffffffffu;
bool muted = false;
bool told_denied = false, told_failed = false;

// The test device, NEO98_TEST_MICROPHONE.
enum Test : unsigned { NoTest, TestSine, TestSilence, TestDenied, TestNone };
unsigned test = NoTest;
bool test_read = false;
unsigned TestDevice()
{
    if (test_read) return test;
    test_read = true;
    test = NoTest;
    const char *value = getenv("NEO98_TEST_MICROPHONE");
    if (!value || !*value) return test;
    if (!strcmp(value, "sine")) test = TestSine;
    else if (!strcmp(value, "silence")) test = TestSilence;
    else if (!strcmp(value, "denied")) test = TestDenied;
    else test = TestNone;
    return test;
}

// The clock of the frames nobody pushes.
bool clock_started = false;
unsigned long long clock_start = 0, clock_frames = 0;
unsigned sine_phase = 0;
// A quarter of 441 Hz at 44.100 Hz, which is exactly a hundred frames a
// period: TestPeak * sin(2 pi i / 100) for i = 0 to 25.
constexpr short Quarter[26] = {0,     1607,  3209,  4797,  6366,  7911,  9424,  10900, 12333,
                               13717, 15047, 16318, 17524, 18662, 19725, 20711, 21615, 22433,
                               23164, 23802, 24347, 24796, 25147, 25398, 25549, 25600};
short Sine(unsigned phase)
{
    phase %= 100;
    if (phase <= 25) return Quarter[phase];
    if (phase <= 50) return Quarter[50 - phase];
    if (phase <= 75) return (short)-Quarter[phase - 50];
    return (short)-Quarter[100 - phase];
}

void InputLock() { if (input.Lock && !TestDevice()) input.Lock(input.owner); }
void InputUnlock() { if (input.Unlock && !TestDevice()) input.Unlock(input.owner); }

const char *StateName(unsigned value)
{
    switch (value)
    {
    case InputRunning: return "running";
    case InputWaiting: return "waiting";
    case InputDenied: return "denied";
    case InputFailed: return "failed";
    default: return "off";
    }
}
void Tell(unsigned value)
{
    fprintf(stderr, "NEO98 MICROPHONE start state=%s source=%s\n", StateName(value),
            TestDevice() ? "test" : "host");
    if (value == InputDenied && !told_denied)
    {
        told_denied = true;
        fprintf(stderr, "NEO98 MICROPHONE Neo98 does not have microphone permission; "
                        "Windows records silence.\n");
    }
    if (value == InputFailed && !told_failed)
    {
        told_failed = true;
        fprintf(stderr, "NEO98 MICROPHONE Could not open the microphone; "
                        "Windows records silence.\n");
    }
}

// Whether the clock of this file fills the ring: always for the test device,
// and for the host whenever it does not push.
bool ClockDriven() { return TestDevice() || state != InputRunning; }

void OpenHost()
{
    clock_started = false;
    switch (TestDevice())
    {
    case TestSine:
    case TestSilence: state = InputRunning; break;
    case TestDenied: state = InputDenied; break;
    case TestNone: state = InputFailed; break;
    default:
        state = enabled && input.Start ? input.Start(input.owner) : InputFailed;
        break;
    }
    Tell(state);
}
void CloseHost()
{
    if (!TestDevice() && input.Stop && (state == InputRunning || state == InputWaiting))
        input.Stop(input.owner);
    if (state != InputOff) fprintf(stderr, "NEO98 MICROPHONE stop\n");
    state = InputOff;
    clock_started = false;
}
void Acquire()
{
    if (holders++ == 0) OpenHost();
}
void Release()
{
    if (holders && --holders == 0) CloseHost();
}
Stream *Of(unsigned stream)
{
    return stream && stream <= MaximumStreams && streams[stream - 1].open ? &streams[stream - 1] : nullptr;
}
inline int Clamp(int value) { return value < -32768 ? -32768 : value > 32767 ? 32767 : value; }
} // namespace

void SetInput(const Input &value)
{
    if (holders) CloseHost();
    input = value;
    if (holders) OpenHost();
}

namespace Microphone
{
void SetEnabled(bool on)
{
    if (enabled == on) return;
    enabled = on;
    // A stream that is recording keeps recording, but without the host: it
    // hears silence until the switch is on again.
    if (holders)
    {
        if (on) OpenHost();
        else CloseHost();
    }
}
bool Enabled() { return enabled; }
bool Present()
{
    if (!enabled) return false;
    if (const unsigned kind = TestDevice()) return kind != TestNone;
    return input.Present && input.Present(input.owner);
}
unsigned State() { return state; }

unsigned Open()
{
    for (unsigned i = 0; i < MaximumStreams; ++i)
    {
        if (streams[i].open) continue;
        streams[i] = Stream{};
        streams[i].open = true;
        return i + 1;
    }
    return 0;
}
void Close(unsigned stream)
{
    Stream *s = Of(stream);
    if (!s) return;
    const bool held = s->holds;
    *s = Stream{};
    if (held) Release();
}
void Start(unsigned stream, unsigned long long now)
{
    Stream *s = Of(stream);
    if (!s || s->started) return;
    if (!s->holds)
    {
        s->holds = true;
        Acquire();
    }
    Advance(now);
    InputLock();
    s->position = written;
    s->fraction = 0;
    s->started = true;
    InputUnlock();
}
void Stop(unsigned stream)
{
    if (Stream *s = Of(stream)) s->started = false;
}
bool Started(unsigned stream)
{
    const Stream *s = Of(stream);
    return s && s->started;
}

unsigned Read(unsigned stream, unsigned char *out, unsigned bytes, const Format &format)
{
    Stream *s = Of(stream);
    if (!s || !s->started || !Mixer::Supports(format)) return 0;
    const unsigned channels = format.channels, bits = format.bits, rate = format.rate;
    const unsigned frame_bytes = channels * (bits / 8);
    const unsigned capacity = bytes / frame_bytes;
    const unsigned left_level = muted ? 0 : (level & 0xffff) * (gain & 0xffff) / 0xffff;
    const unsigned right_level = muted ? 0 : ((level >> 16) & 0xffff) * ((gain >> 16) & 0xffff) / 0xffff;
    InputLock();
    const unsigned long long end = written;
    // Too far behind: the ring has written over the oldest frames, so the
    // stream goes on from half a ring back. A stream below the full rate
    // steps several frames at once and may stand a frame or three beyond
    // the last one written; it waits there for the next.
    if (s->position < end && end - s->position > RingFrames - 2)
    {
        s->position = end - RingFrames / 2;
        s->fraction = 0;
    }
    unsigned frames = 0;
    while (frames < capacity && s->position + 1 < end)
    {
        const int a = ring[s->position % RingFrames], b = ring[(s->position + 1) % RingFrames];
        const int value = a + (int)((long long)(b - a) * s->fraction / rate);
        const int left = (int)((long long)value * left_level / 0xffff);
        const int right = (int)((long long)value * right_level / 0xffff);
        unsigned char *frame = out + frames * frame_bytes;
        const int lanes[2] = {channels == 1 ? (left + right) / 2 : left, right};
        for (unsigned c = 0; c < channels; ++c)
        {
            const int sample = Clamp(lanes[c]);
            if (bits == 8) frame[c] = (unsigned char)((sample >> 8) + 128);
            else
            {
                frame[c * 2] = (unsigned char)(sample & 0xff);
                frame[c * 2 + 1] = (unsigned char)((sample >> 8) & 0xff);
            }
        }
        ++frames;
        s->fraction += InputRate;
        while (s->fraction >= rate)
        {
            s->fraction -= rate;
            ++s->position;
        }
    }
    InputUnlock();
    return frames * frame_bytes;
}

void Advance(unsigned long long now)
{
    if (!holders) return;
    if (state == InputWaiting && !TestDevice() && input.Poll)
    {
        const unsigned answer = input.Poll(input.owner);
        if (answer != InputWaiting)
        {
            state = answer;
            Tell(state);
            clock_started = false;
        }
    }
    if (!ClockDriven()) return;
    if (!clock_started)
    {
        clock_started = true;
        clock_start = now;
        clock_frames = 0;
        return;
    }
    if (now <= clock_start) return;
    const unsigned long long target = (now - clock_start) * InputRate / 1000000;
    if (target <= clock_frames) return;
    unsigned long long due = target - clock_frames;
    // After a pause of the machine the clock does not catch up a whole
    // stretch at once; half a ring is what any stream can still take.
    if (due > RingFrames / 2) due = RingFrames / 2;
    clock_frames = target;
    const bool sine = TestDevice() == TestSine;
    InputLock();
    for (unsigned long long i = 0; i < due; ++i)
    {
        ring[written % RingFrames] = sine ? Sine(sine_phase++) : 0;
        ++written;
    }
    sine_phase %= 100;
    InputUnlock();
}

void Push(const short *frames, unsigned count)
{
    // Runs under the lock of the provider: SDL holds the device while it
    // calls the capture callback.
    if (!frames || ClockDriven()) return;
    for (unsigned i = 0; i < count; ++i)
    {
        ring[written % RingFrames] = frames[i];
        ++written;
    }
}

void SetLevel(unsigned pair) { level = pair; }
unsigned Level() { return level; }
void SetGain(unsigned pair) { gain = pair; }
unsigned Gain() { return gain; }
void SetMute(bool mute) { muted = mute; }
bool Muted() { return muted; }
unsigned OpenStreams()
{
    unsigned count = 0;
    for (const auto &s : streams) count += s.open;
    return count;
}
unsigned long long Received() { return written; }

void StateBind()
{
    // The state of the device describes the host that saved it.
    state = InputOff;
    clock_started = false;
    if (holders) OpenHost();
}

void ResetAll()
{
    for (auto &s : streams) s = Stream{};
    if (holders) CloseHost();
    holders = 0;
    state = InputOff;
    written = 0;
    clock_started = false;
    sine_phase = 0;
    level = gain = 0xffffffffu;
    muted = false;
    test_read = false;
    told_denied = told_failed = false;
    memset(ring, 0, sizeof ring);
}
} // namespace Microphone
} // namespace Sound
