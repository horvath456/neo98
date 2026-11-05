// SPDX-License-Identifier: GPL-2.0-or-later
// The shared PCM mixer of NEO98. Everything above it, waveOut and PlaySound in
// src/x86_wave_out.cpp and later MIDI and MCI, hands it blocks of sample data;
// below it one host provider carries the mixed frames to a speaker.
// The mixer is the place where the formats of the guest meet the one format
// the host plays: 44.100 frames a second, two channels, signed 16 bit. A
// source may be 8 or 16 bit, mono or stereo, and any rate between 100 Hz and
// 100 kHz; the mixer converts while it renders, so a block costs its own bytes
// and nothing more.
// Nothing in this file touches the host. tools/host_audio.h opens the SDL
// audio device and registers itself as the output; without a provider the
// mixer runs on its own clock and writes the mixed frames into the capture
// buffer a test runner hands it. That is the silent provider: a test checks
// samples there the way a UI test checks pixels.
//
#pragma once
#include <vector>
namespace Sound
{
// The one output format. Windows 98 sound cards resampled in the driver; this
// port resamples in the mixer and keeps the host device at a fixed rate, so a
// second source never reopens the device.
constexpr unsigned OutputRate = 44100;
constexpr unsigned OutputChannels = 2;
// How many sources the mixer carries at once. A playing DirectSound buffer
// is a source too (src/x86_dsound_vxd.cpp). Wave block queues grow separately
// on submission; the audio thread never allocates their storage.
constexpr unsigned MaximumSources = 64;
constexpr unsigned InitialBlocks = 64;
// The byte pool a submitted block is copied into. The mixer may not read guest
// memory while it renders, because it renders on the host audio thread, so
// every block is copied once on submission. A block that does not fit is
// refused; the caller turns that into MMSYSERR_NOMEM.
constexpr unsigned PoolBytes = 4u << 20;

// The format of a source, in the terms WAVEFORMATEX uses.
struct Format
{
    unsigned rate = OutputRate;
    unsigned channels = OutputChannels;
    unsigned bits = 16;
};

// What the mixer needs from a host that owns a speaker. Lock and Unlock keep
// Render out of the mixer while a guest thread changes its state; with SDL
// they are SDL_LockAudioDevice and SDL_UnlockAudioDevice.
struct Output
{
    void *owner = nullptr;
    bool (*Start)(void *owner) = nullptr;
    void (*Stop)(void *owner) = nullptr;
    void (*Lock)(void *owner) = nullptr;
    void (*Unlock)(void *owner) = nullptr;
    // How many frames the host takes out of the mixer in one call of Render.
    // A source that plays out of memory its owner writes while it plays (a
    // DirectSound buffer) is read that far ahead in one piece, so this is the
    // stretch in front of its play cursor that can no longer be changed.
    unsigned block = 0;
};

// The flags of a submitted block. They are the WHDR_ values of the wave API,
// because that is the only caller that sets them today.
enum : unsigned
{
    BeginLoop = 4,
    EndLoop = 8
};

class Mixer
{
  public:
    // Whether a WAVEFORMATEX with this shape can be played. The answer is the
    // one waveOutOpen gives for WAVE_FORMAT_QUERY.
    static bool Supports(const Format &format);
    // What the output has been given since the start: frames rendered, and
    // how many of them were not silence. For measurements without a
    // loudspeaker (tools/games_arm64_proof.py).
    unsigned long long RenderedFrames() const { return rendered_frames; }
    unsigned long long AudibleFrames() const { return audible_frames; }
    // Opens a source and answers its identifier, or zero when none is free.
    unsigned Open(const Format &format);
    // Opens a source that has no queue: the mixer asks `generator` for frames
    // whenever it renders. The MIDI synthesizer of src/sound_synth.cpp is the
    // one caller; a generator runs on the host audio thread and must not call
    // back into the mixer.
    using Generator = void (*)(void *owner, short *out, unsigned frames);
    unsigned OpenGenerator(Generator generator, void *owner);
    // Opens a source that plays out of memory its owner keeps: `bytes` of
    // sample data at `memory` in `format`. It has no queue. The mixer reads
    // the bytes at its play cursor at the moment it renders them, so what the
    // owner writes in front of the cursor is heard and nothing is copied; that
    // is a sound buffer of DirectSound on a card that mixes it in hardware.
    // The memory must stay where it is until Close, and Render reads it on
    // the thread of the host.
    unsigned OpenRing(const unsigned char *memory, unsigned bytes, const Format &format);
    // Starts a ring at its play cursor. A looping ring turns over at its end,
    // any other one stops there with the cursor back at zero.
    void PlayRing(unsigned source, bool looping);
    void StopRing(unsigned source);
    bool RingPlaying(unsigned source) const;
    // The play cursor of a ring as a byte offset into its memory, always on a
    // frame boundary.
    unsigned RingCursor(unsigned source) const;
    void SetRingCursor(unsigned source, unsigned offset);
    // The rate a ring is played at; DirectSound changes it while it plays.
    void SetRingRate(unsigned source, unsigned rate);
    // The format of the bytes of a ring; the cursor goes back to zero.
    void SetRingFormat(unsigned source, const Format &format);
    // The stretch in front of the play cursor of a ring that the next Render
    // may take in one piece, in bytes of the ring: the block of the host
    // provider at the rate of the ring.
    unsigned RingLead(unsigned source) const;
    // How many rings are playing; they need the clock of the mixer.
    unsigned RingsPlaying() const;
    void Close(unsigned source);
    // Copies `bytes` of sample data into the pool and queues it. `tag` is what
    // the owner recognises the block by and what Complete hands back; it must
    // not be zero. `loops` is the repeat count of a block that starts a loop.
    // Answers 0 on success, 1 for an invalid source/tag and 2 for no memory.
    unsigned Submit(unsigned source, const unsigned char *data, unsigned bytes,
                    unsigned tag, unsigned flags, unsigned loops);
    // Queue live PCM while retaining only the newest max_frames. Discarding
    // excess old frames keeps latency bounded without emptying the queue.
    unsigned SubmitLive(unsigned source, const unsigned char *data, unsigned bytes,
                        unsigned tag, unsigned max_frames);
    // The same, for a caller whose bytes are not reachable as a pointer. The
    // wave API is one: guest memory is only readable through a copy, and the
    // copy may as well land in the pool directly. `fill` runs while the mixer
    // is locked and must not call back into it.
    using Fill = bool (*)(void *owner, unsigned char *into, unsigned bytes);
    unsigned Submit(unsigned source, unsigned bytes, unsigned tag, unsigned flags,
                    unsigned loops, Fill fill, void *owner, unsigned max_frames = 0);
    // The tag of the oldest block that has finished playing, or zero. Every
    // block is handed back exactly once, in the order it was submitted.
    unsigned Complete(unsigned source);
    void Pause(unsigned source);
    void Restart(unsigned source);
    // Drops the whole queue. Every queued block finishes at once, which is
    // what waveOutReset promises.
    void Reset(unsigned source);
    // Ends the innermost loop after the block that is playing now.
    void BreakLoop(unsigned source);
    // Volume as waveOutSetVolume passes it: left in the low word, right in the
    // high word, 0 to 0FFFFh each.
    void SetVolume(unsigned source, unsigned volume);
    unsigned Volume(unsigned source) const;
    // Which group of the mixer device a source belongs to: the wave streams or
    // the synthesizer. A group carries the device volume that
    // waveOutSetVolume and midiOutSetVolume set with a device number, and
    // that the mixer device offers as the volume of its source line.
    // src/win98_mixer.h.
    void SetSourceGroup(unsigned source, unsigned group);
    void SetGroupVolume(unsigned group, unsigned volume);
    unsigned GroupVolume(unsigned group) const;
    void SetGroupMute(unsigned group, bool mute);
    bool GroupMute(unsigned group) const;
    static constexpr unsigned Groups = 2;
    // The volume of the output itself, the one the mixer device of
    // src/win98_mixer.h calls the destination line. It scales the sum of
    // every source, the way the volume control of a sound card sits behind
    // its mixing stage; the mute switch silences it without losing the value.
    void SetMasterVolume(unsigned volume);
    unsigned MasterVolume() const;
    void SetMasterMute(bool mute);
    bool MasterMute() const;
    // How many bytes of source data have left the mixer since the last reset.
    // This is the number waveOutGetPosition reports as TIME_BYTES.
    unsigned long long Played(unsigned source) const;
    Format SourceFormat(unsigned source) const;
    bool Paused(unsigned source) const;
    bool Busy(unsigned source) const;
    bool Valid(unsigned source) const;

    // The output provider calls this with room for `frames` stereo frames. It
    // overwrites, it does not add, and it is the only entry that may run on a
    // foreign thread.
    void Render(short *out, unsigned frames);
    // Without an output provider the mixer follows this clock instead: it
    // renders every frame that has come due since the last call. `now` is the
    // same microsecond clock the rest of the port uses.
    void Advance(unsigned long long now);
    // The pause of the machine : while held Render answers
    // silence and moves nothing, so every source goes on at the frame it
    // stopped at. The last block before and the first one after are faded,
    // which keeps the cut from clicking.
    void Hold(bool hold);
    bool Held() const { return held; }
    // Whether a host provider carries the frames away.
    bool Live() const;
    // Starts the host provider now instead of with the first source: a card
    // whose driver is opened (DirectSoundCreate) runs before its first buffer
    // plays. True without a provider as well; false when the device fails.
    bool StartOutput();
    // After a saved state was loaded: the host device runs again when a
    // source is open.
    void StateBind();

  private:
    struct Block
    {
        unsigned offset = 0, bytes = 0, tag = 0, flags = 0, loops = 0;
    };
    struct Source
    {
        bool open = false, paused = false, break_loop = false;
        Generator generator = nullptr;
        void *generator_owner = nullptr;
        // A ring: memory of the owner instead of a queue. `frame` and
        // `fraction` are its play cursor.
        const unsigned char *ring = nullptr;
        unsigned ring_bytes = 0;
        bool ring_playing = false, ring_looping = false;
        Format format;
        unsigned volume = 0xffffffff;
        unsigned group = 0;
        std::vector<Block> queue;
        // Sequence numbers, not ring indices: `first` is the oldest block
        // that has not been handed back, `cursor` the one being rendered and
        // `tail` the next free place. The ring index is the number modulo
        // queue.size; capacity grows while the output thread is locked out.
        unsigned first = 0, cursor = 0, tail = 0;
        unsigned frame = 0;         // source frame inside the cursor block
        unsigned fraction = 0;      // 0 to OutputRate-1, the position between two frames
        bool looping = false;
        unsigned loop_start = 0, loop_left = 0;
        unsigned long long played = 0;
        std::vector<unsigned> done;
        unsigned done_head = 0, done_count = 0;
    };
    Source sources[MaximumSources];
    unsigned master_volume = 0xffffffff;
    bool master_mute = false;
    unsigned long long rendered_frames = 0, audible_frames = 0;   // RenderedFrames, AudibleFrames
    bool held = false, faded = false;   // Hold; faded once the last block went out
    unsigned group_volume[Groups] = {0xffffffff, 0xffffffff};
    bool group_mute[Groups] = {};

    void Lock() const;
    void Unlock() const;
    void Retire(Source &source, unsigned index);
    void RenderRing(Source &source, short *out, unsigned frames, unsigned left_volume, unsigned right_volume);
};

Mixer &Shared();
// Registers the host provider. An empty Output takes the mixer back to its own
// clock.
void SetOutput(const Output &output);
// The capture buffer of the silent provider: `frames` stereo frames. A runner
// hands one in and checks the samples afterwards.
void SetCapture(short *buffer, unsigned frames);
// How many frames the capture buffer holds.
unsigned Captured();
// The block of the output: what the host provider says, and for the silent
// provider the stretch of one tick of its clock.
unsigned OutputBlock();
// The lock of the host provider, for a caller that keeps state a generator
// reads. Everything between Lock and Unlock is safe against Render.
void Lock();
void Unlock();
// Drops every source and the capture buffer. A runner calls this between two
// guests; the mixer is one object for the whole process.
void ResetAll();

// ---------------------------------------------------------------------------
// Recording: the microphone of the host as the one wave input device.
// The host delivers what its microphone hears in one format, mono signed 16
// bit at InputRate, into a ring here; every open recording stream of the
// guest reads the ring from its own position and converts to the format it
// opened, the way the wave driver of a Windows 98 card converted from what
// its codec sampled. src/ stays free of SDL: tools/host_audio.h opens the
// SDL capture device and registers itself as the Input.
// The host device is opened at the first Start of a stream and closed when
// the last stream that started closes, so macOS asks for the microphone only
// when a program really records. A host that refuses (no permission, a
// device that does not open) keeps the device for Windows; it records
// silence and the log says so once.
// NEO98_TEST_MICROPHONE replaces the host by a test device, for tests that
// must never open the real microphone: `sine` is 441 Hz with the peak
// TestPeak, `silence` records zeros, `denied` behaves like a host without
// permission, `none` is a host without a microphone.
constexpr unsigned InputRate = 44100;
constexpr unsigned TestPeak = 25600;
// What the host device does. Off until the first stream starts.
enum InputState : unsigned
{
    InputOff = 0,
    InputRunning = 1,
    InputWaiting = 2,   // macOS asks the user; silence until the answer
    InputDenied = 3,    // no permission
    InputFailed = 4,    // the device did not open
};
struct Input
{
    void *owner = nullptr;
    // Whether the host has a microphone. Asked when Windows counts devices.
    bool (*Present)(void *owner) = nullptr;
    // Opens the microphone and answers an InputState. From InputRunning on
    // the host hands its frames to Microphone::Push.
    unsigned (*Start)(void *owner) = nullptr;
    // Asked while the state is InputWaiting: the state now.
    unsigned (*Poll)(void *owner) = nullptr;
    void (*Stop)(void *owner) = nullptr;
    // Keep Push out of the ring while a guest thread reads it.
    void (*Lock)(void *owner) = nullptr;
    void (*Unlock)(void *owner) = nullptr;
};
// Registers the host provider; an empty Input is a host without one.
void SetInput(const Input &input);

namespace Microphone
{
// The switch "Mikrofon in Windows" (audio.microphone): off, Windows sees no
// recording device and the host is never asked for its microphone.
void SetEnabled(bool on);
bool Enabled();
// Whether there is a wave input device for Windows.
bool Present();
// The state of the host device (InputState).
unsigned State();
// A recording stream. Zero when none is free.
unsigned Open();
void Close(unsigned stream);
// Start takes what arrives from now on; the first started stream opens the
// host device. Stop keeps the stream and its device reference.
void Start(unsigned stream, unsigned long long now);
void Stop(unsigned stream);
bool Started(unsigned stream);
// Converts what has arrived since the last read into `format` and answers
// the bytes written, always whole frames.
unsigned Read(unsigned stream, unsigned char *out, unsigned bytes, const Format &format);
// The clock of the sources the host does not push: the test device, and
// the silence of a refused device. `now` is the microsecond clock of the
// port.
void Advance(unsigned long long now);
// The host side: `frames` mono frames at InputRate. May run on any thread.
void Push(const short *frames, unsigned count);
// The recording level of the mixer device, as volume pairs (left in the low
// word): Level is the microphone line, Gain the recording destination behind
// it, and every frame a stream reads is scaled by both. Mute records silence.
void SetLevel(unsigned pair);
unsigned Level();
void SetGain(unsigned pair);
unsigned Gain();
void SetMute(bool mute);
bool Muted();
// How many streams are open and how many frames have come in, for tests.
unsigned OpenStreams();
unsigned long long Received();
// Drops every stream and closes the host device. Runners call it between
// two guests.
void ResetAll();
// After a saved state was loaded: the streams came with the state, the
// device of the host did not. A stream that was recording opens it again.
void StateBind();
} // namespace Microphone

// PC speaker emulation. Windows 98 programs use 8253 channel 2 through ports
// 42h and 43h and the gate/speaker bits of port 61h. MessageBeep and KERNEL32
// Beep use the same speaker. Neo98 mixes its square wave as a PCM generator.
namespace Speaker
{
// Handle guest writes to ports 42h, 43h and 61h. Ignore other ports.
void Write(unsigned port, unsigned char value);
// Return the four stored port 61h bits. The caller supplies clock-dependent
// refresh bit 4 and channel 2 output bit 5.
unsigned PortB();
// Channel 2 divisor and resulting frequency. As on the hardware, a zero
// divisor represents 65536.
unsigned Divisor();
unsigned Hertz();
// Counter 2 output at `now` in microseconds, bit 5 of port 61h.
bool Output(unsigned long long now);
// Fixed-duration sound for MessageBeep(0xFFFFFFFF) and KERNEL32 Beep. A zero
// duration stops it.
void Tone(unsigned hertz, unsigned milliseconds);
// Whether the speaker is sounding through the gate bit or a timed tone.
bool Sounding();
// Whether the speaker has a mixer source. Once it has sounded, continue
// advancing the mixer clock during silence so the next tone starts at the
// correct time.
bool Clocked();
// Release the generator source and stop all tones.
void Reset();

// MMSOUND.DRV note queue for the 16-bit SOUND module. Windows 98 StartSound
// schedules a callback every five milliseconds to advance the queue allocated
// by OpenSound and reload channel 2. Here the callback follows the mixer
// clock, independently of guest scheduling. win16_sound.cpp converts notes
// and queries queue state. Each function acquires the mixer lock itself.
// Empty the queue and set its capacity in six-byte entries. Leave an active
// callback running, matching Windows 98.
void VoiceReset(unsigned capacity);
// Append a note: frequency in Hz (zero for a rest), and tone/rest lengths in
// SetVoiceNote ticks. Return 0 for success, 1 for a full queue, or 2 for a
// frequency outside 37..32767 Hz.
unsigned VoiceQueue(unsigned hertz, unsigned sound, unsigned silence);
// StartSound starts the callback and enables the gate/speaker when a note is
// pending and playback is not already active.
void VoiceStart();
// Stop the callback, close the gate and empty the queue. Preserve any
// remaining rest duration, matching Windows 98.
void VoiceStop();
// Return zero without an active callback; otherwise return the number of
// notes not yet started, with a minimum of one.
unsigned VoiceCount();
// The threshold set by SetVoiceThreshold.
void VoiceThreshold(unsigned notes);
// The callback sets this flag when pending notes reach the threshold. With
// clear set, read and reset it.
unsigned VoiceStatus(bool clear);
} // namespace Speaker
} // namespace Sound
