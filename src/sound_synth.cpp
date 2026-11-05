// SPDX-License-Identifier: GPL-2.0-or-later
// The General MIDI software synthesizer. See src/sound_synth.h for its capabilities and limits.
// Three things happen here and nothing else happens here:
// the channels Sixteen of them, with the power-on state of General MIDI:
// program 0, volume 100, expression 127, pan 64, no bend,
// a bend range of two semitones. Channel 10, the tenth, is
// the percussion channel: its key is an instrument and not a
// pitch.
// the voices Thirty-two, each a square, a triangle or a noise generator
// with an attack, a decay, a sustain and a release. Which of
// the three a note takes comes from the General MIDI family
// of the program, which is the program number divided by
// eight.
// the render One pass over the voices per output frame, in the output
// format of the mixer. Everything is integer arithmetic; the
// phase is a 32 bit accumulator and the step of a key is a
// table entry, so a note costs an addition and a shift.
#include "sound_synth.h"
#include "sound.h"
namespace Sound
{
namespace
{
// The phase step of every MIDI key at the output rate of the mixer:
// round(440 * 2^((key - 69) / 12) * 2^32 / 44100). Key 69 is 440 Hz, and the
// table is the only place where a frequency is written down.
const unsigned NoteStep[128] = {
    796254u, 843601u, 893765u, 946911u, 1003217u, 1062871u,
    1126073u, 1193033u, 1263974u, 1339134u, 1418763u, 1503127u,
    1592507u, 1687203u, 1787529u, 1893821u, 2006434u, 2125742u,
    2252146u, 2386065u, 2527948u, 2678268u, 2837526u, 3006254u,
    3185015u, 3374406u, 3575058u, 3787642u, 4012867u, 4251485u,
    4504291u, 4772130u, 5055896u, 5356535u, 5675051u, 6012507u,
    6370030u, 6748811u, 7150117u, 7575285u, 8025735u, 8502970u,
    9008582u, 9544261u, 10111792u, 10713070u, 11350103u, 12025015u,
    12740059u, 13497623u, 14300233u, 15150569u, 16051469u, 17005939u,
    18017165u, 19088521u, 20223584u, 21426141u, 22700205u, 24050030u,
    25480119u, 26995246u, 28600467u, 30301139u, 32102938u, 34011878u,
    36034330u, 38177043u, 40447168u, 42852281u, 45400411u, 48100060u,
    50960238u, 53990491u, 57200933u, 60602278u, 64205876u, 68023757u,
    72068660u, 76354085u, 80894335u, 85704563u, 90800821u, 96200119u,
    101920476u, 107980983u, 114401866u, 121204555u, 128411753u, 136047513u,
    144137319u, 152708170u, 161788671u, 171409126u, 181601643u, 192400238u,
    203840952u, 215961966u, 228803732u, 242409110u, 256823506u, 272095026u,
    288274639u, 305416341u, 323577341u, 342818251u, 363203285u, 384800477u,
    407681904u, 431923931u, 457607465u, 484818220u, 513647012u, 544190053u,
    576549277u, 610832681u, 647154683u, 685636503u, 726406571u, 769600953u,
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u};
// round(2^(n / 12) * 65536) for n from -2 to 2. A bend lands between two of
// them and is interpolated; over one semitone the error of the straight line
// is under a fiftieth of a percent, which is below what an ear resolves.
const unsigned SemitoneRatio[5] = {58386u, 61858u, 65536u, 69433u, 73562u};

// 0 square, 1 triangle, 2 noise. One entry per General MIDI family, and a
// family is eight programs: piano, chromatic percussion, organ, guitar, bass,
// strings, ensemble, brass, reed, pipe, synth lead, synth pad, synth effects,
// ethnic, percussive, sound effects.
const unsigned char FamilyWave[16] = {1, 1, 0, 1, 1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 2, 2};
// Whether the family holds its note while the key is down. An organ does, a
// piano does not; the ones that do not fade away on their own.
const unsigned char FamilyHolds[16] = {0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0};

// An envelope step: how much of the full level 10000h one frame covers. A
// time of zero is a step of the whole level, so the stage passes at once.
unsigned Step(unsigned milliseconds)
{
    const unsigned frames = milliseconds * OutputRate / 1000;
    return frames ? (0x10000u / frames ? 0x10000u / frames : 1u) : 0x10000u;
}
inline unsigned Clip(unsigned value, unsigned limit) { return value > limit ? limit : value; }
inline int Clamp(int value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return value;
}
} // namespace

void Synth::Reset()
{
    for (auto &voice : voices) voice = Voice{};
    for (auto &channel : channels) channel = Channel{};
    master = 0x4000;
    clock = 0;
}
void Synth::AllNotesOff()
{
    for (auto &voice : voices)
    {
        if (!voice.used) continue;
        voice.envelope = 3;
        voice.held = false;
    }
}
bool Synth::Sounding() const
{
    for (const auto &voice : voices)
        if (voice.used) return true;
    return false;
}

// The step of a key under the bend of its channel. The bend is fourteen bits
// around 2000h and covers the bend range in both directions, so the distance
// from the centre, scaled to the range, is the number of semitones.
unsigned Synth::StepOf(const Channel &channel, unsigned key) const
{
    const unsigned base = NoteStep[Clip(key, 127)];
    const int offset = (int)channel.bend - 8192;
    if (!offset || !channel.bend_range) return base;
    // In units of a twelfth of a semitone times 4096, so the whole part is the
    // semitone and the rest is the fraction.
    long long units = (long long)offset * (int)channel.bend_range * 4096 / 8192;
    if (units < -2 * 4096) units = -2 * 4096;
    if (units > 2 * 4096 - 1) units = 2 * 4096 - 1;
    int whole = (int)(units >> 12);
    const unsigned fraction = (unsigned)(units & 4095);
    const unsigned low = SemitoneRatio[whole + 2], high = SemitoneRatio[whole + 3];
    const unsigned ratio = low + (unsigned)((long long)(high - low) * fraction / 4096);
    return (unsigned)((unsigned long long)base * ratio >> 16);
}
void Synth::Retune(unsigned channel)
{
    for (auto &voice : voices)
    {
        if (!voice.used || voice.channel != channel || channel == 9) continue;
        voice.step = StepOf(channels[channel], voice.key);
    }
}

// The percussion map of channel 10. A key there names an instrument, so it
// picks the generator, the pitch and how long the sound lasts instead of a
// note. The groups are the ones General MIDI lays down.
namespace
{
struct Percussion
{
    unsigned wave, step, decay;
};
Percussion PercussionOf(unsigned key)
{
    if (key <= 36) return {1, NoteStep[28], 160};           // bass drum
    if (key == 37) return {2, 0x40000000u, 70};             // rim shot
    if (key == 38 || key == 40) return {2, 0x40000000u, 170}; // snare
    if (key == 39) return {2, 0x30000000u, 200};            // hand clap
    if (key == 42 || key == 44) return {2, 0x70000000u, 60};  // closed hi-hat
    if (key == 46) return {2, 0x70000000u, 320};            // open hi-hat
    if (key == 49 || key == 52 || key == 55 || key == 57 || key == 59)
        return {2, 0x60000000u, 700};                       // cymbals
    if (key >= 41 && key <= 50) return {1, NoteStep[36 + (key - 41)], 240}; // toms
    if (key >= 60 && key <= 69) return {1, NoteStep[60], 130}; // bongos, congas
    return {2, 0x50000000u, 130};
}
} // namespace

void Synth::NoteOn(unsigned channel, unsigned key, unsigned velocity)
{
    if (!velocity) { NoteOff(channel, key); return; }
    key = Clip(key, 127);
    // A second note on the same key of the same channel takes over its voice,
    // as a driver does, so a repeated note does not stack up.
    Voice *slot = nullptr;
    for (auto &voice : voices)
        if (voice.used && voice.channel == channel && voice.key == key) slot = &voice;
    if (!slot)
        for (auto &voice : voices)
            if (!voice.used) { slot = &voice; break; }
    if (!slot)
    {
        unsigned oldest = 0;
        for (auto &voice : voices)
            if (!slot || voice.age < oldest) { slot = &voice; oldest = voice.age; }
    }
    const Channel &state = channels[channel];
    const unsigned family = Clip(state.program, 127) / 8;
    *slot = Voice{};
    slot->used = true;
    slot->held = true;
    slot->channel = channel;
    slot->key = key;
    slot->velocity = velocity;
    slot->age = ++clock;
    slot->noise = 0x1234abcdu + key * 2654435761u;
    if (channel == 9)
    {
        const Percussion hit = PercussionOf(key);
        slot->wave = hit.wave;
        slot->step = hit.step;
        slot->attack = Step(2);
        slot->decay = Step(hit.decay);
        slot->sustain = 0;
        slot->release = Step(40);
    }
    else
    {
        slot->wave = FamilyWave[family];
        slot->step = StepOf(state, key);
        if (FamilyHolds[family])
        {
            slot->attack = Step(15);
            slot->decay = Step(60);
            slot->sustain = 0xc000;
            slot->release = Step(120);
        }
        else
        {
            slot->attack = Step(4);
            slot->decay = Step(900);
            slot->sustain = 0;
            slot->release = Step(80);
        }
    }
}
void Synth::NoteOff(unsigned channel, unsigned key)
{
    key = Clip(key, 127);
    for (auto &voice : voices)
    {
        if (!voice.used || voice.channel != channel || voice.key != key || !voice.held) continue;
        voice.held = false;
        if (!channels[channel].sustain) voice.envelope = 3;
    }
}
void Synth::ReleaseHeld(unsigned channel)
{
    for (auto &voice : voices)
        if (voice.used && voice.channel == channel && !voice.held) voice.envelope = 3;
}

void Synth::Controller(unsigned channel, unsigned number, unsigned value)
{
    Channel &state = channels[channel];
    switch (number)
    {
    case 7: state.volume = value; break;
    case 10: state.pan = value; break;
    case 11: state.expression = value; break;
    case 64:
        state.sustain = value >= 64;
        if (!state.sustain) ReleaseHeld(channel);
        break;
    case 120: // all sound off: no release, the voices stop
        for (auto &voice : voices)
            if (voice.channel == channel) voice = Voice{};
        break;
    case 121: // reset all controllers
        state.volume = 100;
        state.expression = 127;
        state.pan = 64;
        state.bend = 8192;
        state.sustain = false;
        Retune(channel);
        break;
    case 123: // all notes off
    case 124:
    case 125:
    case 126:
    case 127:
        for (auto &voice : voices)
            if (voice.used && voice.channel == channel) { voice.held = false; voice.envelope = 3; }
        break;
    default:
        break;
    }
}

void Synth::ShortMessage(unsigned message)
{
    const unsigned status = message & 0xff;
    if (status < 0x80) return;
    const unsigned data1 = (message >> 8) & 0x7f, data2 = (message >> 16) & 0x7f;
    if (status >= 0xf0)
    {
        if (status == 0xff) Reset();
        return;
    }
    const unsigned channel = status & 0xf;
    switch (status & 0xf0)
    {
    case 0x80: NoteOff(channel, data1); break;
    case 0x90: NoteOn(channel, data1, data2); break;
    case 0xa0: break; // polyphonic aftertouch changes nothing in this synth
    case 0xb0: Controller(channel, data1, data2); break;
    case 0xc0: channels[channel].program = data1; break;
    case 0xd0: break; // channel aftertouch, likewise
    case 0xe0:
        channels[channel].bend = data1 | (data2 << 7);
        Retune(channel);
        break;
    default: break;
    }
}

// The two system exclusive messages that change what is heard. Everything
// else is addressed to a device this synthesizer is not.
void Synth::LongMessage(const unsigned char *bytes, unsigned length)
{
    if (!bytes || length < 4 || bytes[0] != 0xf0) return;
    // F0 7E <device> 09 01 F7, the General MIDI system on reset.
    if (bytes[1] == 0x7e && length >= 6 && bytes[3] == 0x09) { Reset(); return; }
    // F0 7F <device> 04 01 <lsb> <msb> F7, the universal master volume.
    if (bytes[1] == 0x7f && length >= 8 && bytes[3] == 0x04 && bytes[4] == 0x01)
        master = (bytes[5] & 0x7f) | ((unsigned)(bytes[6] & 0x7f) << 7);
}

void Synth::Render(short *out, unsigned frames)
{
    for (unsigned i = 0; i < frames * OutputChannels; ++i) out[i] = 0;
    for (auto &voice : voices)
    {
        if (!voice.used) continue;
        const Channel &state = channels[voice.channel];
        // The gain of this voice, in units of 1/16384: the velocity, the
        // channel volume, the expression controller and the master volume.
        unsigned gain = 6000;
        gain = (unsigned)((unsigned long long)gain * voice.velocity / 127);
        gain = (unsigned)((unsigned long long)gain * Clip(state.volume, 127) / 127);
        gain = (unsigned)((unsigned long long)gain * Clip(state.expression, 127) / 127);
        gain = (unsigned)((unsigned long long)gain * Clip(master, 0x4000) / 0x4000);
        const unsigned pan = Clip(state.pan, 127);
        const unsigned left_gain = pan <= 64 ? 256u : 256u * (127 - pan) / 63;
        const unsigned right_gain = pan >= 64 ? 256u : 256u * pan / 64;
        for (unsigned f = 0; f < frames; ++f)
        {
            // The envelope, one stage at a time.
            switch (voice.envelope)
            {
            case 0:
                voice.level += voice.attack;
                if (voice.level >= 0x10000) { voice.level = 0x10000; voice.envelope = 1; }
                break;
            case 1:
                if (voice.level <= voice.sustain + voice.decay)
                {
                    voice.level = voice.sustain;
                    voice.envelope = 2;
                }
                else voice.level -= voice.decay;
                if (!voice.level) { voice.used = false; }
                break;
            case 2:
                break;
            default:
                if (voice.level <= voice.release) { voice.level = 0; voice.used = false; }
                else voice.level -= voice.release;
                break;
            }
            if (!voice.used) break;
            if (voice.envelope == 2 && !voice.level) { voice.used = false; break; }
            const unsigned previous = voice.phase;
            voice.phase += voice.step;
            int value = 0;
            if (voice.wave == 0) value = voice.phase < 0x80000000u ? 32767 : -32768;
            else if (voice.wave == 1)
            {
                // A triangle over the phase: up over the first half, down over
                // the second, from -8000h to 7FFFh.
                const unsigned quarter = voice.phase >> 15;   // 0.. 1FFFFh
                value = quarter < 0x10000 ? (int)quarter - 0x8000
                                          : 0x8000 - (int)(quarter - 0x10000);
                if (value > 32767) value = 32767;
            }
            else
            {
                if (voice.phase < previous)
                {
                    // A maximal length shift register over 32 bits; the taps
                    // are the standard ones, so the sequence does not repeat
                    // inside any note.
                    voice.noise = (voice.noise >> 1) ^ (voice.noise & 1 ? 0xd0000001u : 0);
                }
                value = (int)(short)(voice.noise & 0xffff);
            }
            const int sample = (int)((long long)value * gain / 32768 * voice.level / 0x10000);
            const int left = out[f * 2] + sample * (int)left_gain / 256;
            const int right = out[f * 2 + 1] + sample * (int)right_gain / 256;
            out[f * 2] = (short)Clamp(left);
            out[f * 2 + 1] = (short)Clamp(right);
        }
    }
}
} // namespace Sound
