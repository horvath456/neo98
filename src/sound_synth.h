// SPDX-License-Identifier: GPL-2.0-or-later
// The General MIDI software synthesizer of NEO98. It is the lower half of the
// MIDI output device in src/x86_midi_out.cpp, the way the mixer is the lower
// half of the wave output device: WINMM checks arguments, this file makes the
// sound, and the mixer carries it to the speaker.
// Windows 98 without a MIDI driver counts zero MIDI devices and every program
// with music stays silent. A Windows 98 with a sound card but without a synth
// on it reports an external MIDI port and is silent too, because the notes go
// out of a cable. This port has neither a cable nor a wavetable, so it answers
// with what it is: a synthesizer, with voices of its own.
// What it is not: it has no sample set. Every voice is a square, a triangle or
// a noise generator with an envelope, picked by the General MIDI family of the
// program. That is enough to hear the music of a game and its melody, and it
// is not enough to sound like an instrument. Soundfont playback is not implemented.
#pragma once
namespace Sound
{
// How many notes sound at once. A voice that is needed while all of them are
// busy takes the oldest one, which is what a card of 1998 did.
constexpr unsigned SynthVoices = 32;
constexpr unsigned MidiChannels = 16;

class Synth
{
  public:
    // A channel message in the packing of midiOutShortMsg: status in the low
    // byte, the two data bytes above it. A running status is not allowed here,
    // because midiOutShortMsg never carries one.
    void ShortMessage(unsigned message);
    // A system exclusive block. Only the two messages that change the sound
    // are followed: the General MIDI reset and the master volume of the
    // universal real time block.
    void LongMessage(const unsigned char *bytes, unsigned length);
    // Silences everything and puts every channel back to its power-on state.
    void Reset();
    void AllNotesOff();
    // Writes `frames` stereo frames. This runs on the host audio thread.
    void Render(short *out, unsigned frames);
    // Whether any voice is still sounding. midiOutReset and the MCI sequencer
    // ask, because a file is over when the last note has died away.
    bool Sounding() const;

  private:
    struct Voice
    {
        bool used = false, held = false;
        unsigned channel = 0, key = 0, velocity = 0;
        unsigned wave = 0;          // 0 square, 1 triangle, 2 noise
        unsigned phase = 0, step = 0;
        unsigned noise = 0x1234abcdu;
        unsigned envelope = 0;      // 0 attack, 1 decay, 2 sustain, 3 release
        unsigned level = 0;         // 0 to 0x10000
        unsigned age = 0;
        unsigned attack = 0, decay = 0, sustain = 0, release = 0;
    };
    struct Channel
    {
        unsigned program = 0;
        unsigned volume = 100, expression = 127, pan = 64;
        unsigned bend = 8192;
        unsigned bend_range = 2;    // semitones, the General MIDI default
        bool sustain = false;
    };
    Voice voices[SynthVoices];
    Channel channels[MidiChannels];
    unsigned master = 0x4000;       // 0 to 0x4000, the universal master volume
    unsigned clock = 0;

    void NoteOn(unsigned channel, unsigned key, unsigned velocity);
    void NoteOff(unsigned channel, unsigned key);
    void Controller(unsigned channel, unsigned number, unsigned value);
    void ReleaseHeld(unsigned channel);
    unsigned StepOf(const Channel &channel, unsigned key) const;
    void Retune(unsigned channel);
};
} // namespace Sound
