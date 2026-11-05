// SPDX-License-Identifier: GPL-2.0-or-later
// The mixer device of the NEO98 sound driver: the lines, the controls and the
// rules of the mixer half of WINMM.
// Under Windows 98 the mixer entries of WINMM are a thin layer over the mixer
// driver of the sound card; MSMIXMGR finds it, and every line and every
// control comes from that driver. There is no card here, so this port is the
// driver, exactly as it is the wave driver of src/x86_wave_out.cpp and the
// MIDI driver of src/x86_midi_out.cpp. What this file describes is therefore
// not an implementation of a Windows file but the mixer of the one output this
// port has, written in the shape WINMM hands to its callers.
// The rule it follows: a line exists only where something can really be
// heard, and a control exists only where its value really changes the sound.
// The destination is the output itself and its volume scales the mixed sum
// (Sound::Mixer master volume); the two source lines are the wave stream group
// and the synthesizer group of the same mixer, and their volume is the device
// volume that waveOutSetVolume and midiOutSetVolume pass with a device number.
// Nothing is offered that answers without doing anything. Recording has a
// destination of its own, WAVEIN, with the microphone as its one source, and
// both exist only while Windows has the wave input device
// (Sound::Microphone::Present): their volumes scale the frames a recording
// stream reads. There is no line in and no CD line.
// What WINMM itself does before the driver is asked is the part that is read
// out of the original, and the addresses are in src/x86_mixer.cpp.
//
#pragma once
#include "neo98_hardware.h"
namespace Win98Mixer
{
using U = unsigned;

// MMSYSERR_ and MIXERR_ values the mixer entries answer.
enum : U
{
    NoError = 0, Error = 1, BadDeviceId = 2, InvalidHandle = 5, NoDriver = 6,
    NotSupported = 8, InvalidFlag = 10, InvalidParameter = 11,
    InvalidLine = 1024, InvalidControl = 1025, InvalidValue = 1026,
};

// Structure sizes of the ANSI forms, as WINMM probes them.
enum : U
{
    CapsBytes = 0x30,       // MIXERCAPSA
    LineBytes = 0xa8,       // MIXERLINEA
    ControlBytes = 0x94,    // MIXERCONTROLA
    LineControlsBytes = 0x18,
    ControlDetailsBytes = 0x18,
    ShortNameChars = 16,    // MIXER_SHORT_NAME_CHARS
    LongNameChars = 64,     // MIXER_LONG_NAME_CHARS
    PnameChars = 32,        // MAXPNAMELEN
};

// Field offsets of MIXERLINEA.
namespace Line
{
enum : U
{
    Size = 0x00, Destination = 0x04, Source = 0x08, LineId = 0x0c, Flags = 0x10,
    User = 0x14, ComponentType = 0x18, Channels = 0x1c, Connections = 0x20,
    Controls = 0x24, ShortName = 0x28, Name = 0x38, TargetType = 0x78,
    TargetDeviceId = 0x7c, TargetMid = 0x80, TargetPid = 0x82,
    TargetVersion = 0x84, TargetPname = 0x88,
};
} // namespace Line

// Field offsets of MIXERCONTROLA.
namespace Control
{
enum : U
{
    Size = 0x00, Id = 0x04, Type = 0x08, Flags = 0x0c, MultipleItems = 0x10,
    ShortName = 0x14, Name = 0x24, Minimum = 0x64, Maximum = 0x68, Steps = 0x7c,
};
} // namespace Control

// Field offsets of MIXERLINECONTROLS and MIXERCONTROLDETAILS.
namespace LineControls
{
enum : U { Size = 0x00, LineId = 0x04, ControlId = 0x08, Controls = 0x0c, ControlBytes = 0x10, Array = 0x14 };
} // namespace LineControls
namespace Details
{
enum : U { Size = 0x00, ControlId = 0x04, Channels = 0x08, Items = 0x0c, DetailBytes = 0x10, Array = 0x14 };
} // namespace Details

// The query flags.
enum : U
{
    QueryMask = 0x0f,
    QueryDestination = 0, QuerySource = 1, QueryLineId = 2, QueryComponentType = 3, QueryTargetType = 4,
    ControlsAll = 0, ControlsOfId = 1, ControlsOfType = 2,
    DetailsValue = 0, DetailsListText = 1,
    ObjectHandle = 0x80000000u,
    ObjectMixer = 0x00000000u, ObjectWaveOut = 0x10000000u, ObjectWaveIn = 0x20000000u,
    ObjectMidiOut = 0x30000000u, ObjectMidiIn = 0x40000000u, ObjectAux = 0x50000000u,
    ObjectTypeMask = 0xf0000000u,
};

// Component types and control types.
enum : U
{
    DstSpeakers = 0x00000004, DstWaveIn = 0x00000007, SrcWaveOut = 0x00001008, SrcSynthesizer = 0x00001004,
    SrcMicrophone = 0x00001003,
    ControlVolume = 0x50030001, ControlMute = 0x20010002,
    LineActive = 0x00000001, LineSource = 0x80000000u,
    ControlUniform = 0x00000001,
    TargetWaveOut = 1, TargetWaveIn = 2, TargetMidiOut = 3,
};

// The message a mixer with a window callback posts when a control changed.
enum : U { ControlChange = 0x03d1, LineChange = 0x03d0 };

// The groups of Sound::Mixer a source line belongs to. The wave provider tags
// every stream it opens with Wave, the MIDI synthesizer with Synth, so a
// group volume of the mixer reaches exactly the sources of that line.
enum Group : U { GroupWave = 0, GroupSynth = 1, GroupCount = 2 };

// One line of this mixer.
struct LineRecord
{
    U line_id, component_type, flags, connections, controls, target_type;
    const char *short_name, *name;
    U source;          // the source number, or ~0u for the destination
    U first_control;   // index into the control table
    U destination;     // the destination it is or belongs to
};

// One control of this mixer.
struct ControlRecord
{
    U id, type, line_id, flags, minimum, maximum, steps;
    const char *short_name, *name;
    // Which value it carries: the group it scales, Master for the output,
    // RecordMaster and RecordMicrophone for the recording.
    U group;
    bool mute;
};
enum : U { Master = 2, RecordMaster = 3, RecordMicrophone = 4 };

// Whether the recording destination exists: while Windows has the wave
// input device (src/x86_mixer.cpp asks Sound::Microphone::Present).
bool RecordingPresent();
inline U Destinations() { return RecordingPresent() ? 2 : 1; }

// The line table. Each destination first, then its sources, which is the
// order mixerGetLineInfo with MIXER_GETLINEINFOF_SOURCE counts in. A line
// identifier is the source number plus one in the high word and the
// destination in the low word.
inline const LineRecord *Lines(U &count)
{
    static const LineRecord table[] = {
        {0x00000000, DstSpeakers, LineActive, 2, 2, TargetWaveOut, "Volume", "Volume Control", ~0u, 0, 0},
        {0x00010000, SrcWaveOut, LineActive | LineSource, 0, 2, TargetWaveOut, "Wave", "Wave Out", 0, 2, 0},
        {0x00020000, SrcSynthesizer, LineActive | LineSource, 0, 2, TargetMidiOut, "MIDI", "MIDI Synth", 1, 4, 0},
        {0x00000001, DstWaveIn, LineActive, 1, 1, TargetWaveIn, "Recording", "Recording Control", ~0u, 6, 1},
        {0x00010001, SrcMicrophone, LineActive | LineSource, 0, 2, 0, "Microphone", "Microphone", 0, 7, 1},
    };
    count = U(sizeof table / sizeof table[0]);
    return table;
}
// A line of a destination Windows does not have is no line.
inline bool Visible(const LineRecord &line) { return line.destination < Destinations(); }

// The control table, grouped by line in the order of Lines.
inline const ControlRecord *Controls(U &count)
{
    static const ControlRecord table[] = {
        {1, ControlVolume, 0x00000000, 0, 0, 0xffff, 256, "Volume", "Volume", Master, false},
        {2, ControlMute, 0x00000000, ControlUniform, 0, 1, 0, "Mute", "Mute", Master, true},
        {3, ControlVolume, 0x00010000, 0, 0, 0xffff, 256, "Volume", "Wave Volume", GroupWave, false},
        {4, ControlMute, 0x00010000, ControlUniform, 0, 1, 0, "Mute", "Wave Mute", GroupWave, true},
        {5, ControlVolume, 0x00020000, 0, 0, 0xffff, 256, "Volume", "MIDI Volume", GroupSynth, false},
        {6, ControlMute, 0x00020000, ControlUniform, 0, 1, 0, "Mute", "MIDI Mute", GroupSynth, true},
        {7, ControlVolume, 0x00000001, 0, 0, 0xffff, 256, "Volume", "Recording Volume", RecordMaster, false},
        {8, ControlVolume, 0x00010001, 0, 0, 0xffff, 256, "Volume", "Microphone Volume", RecordMicrophone, false},
        {9, ControlMute, 0x00010001, ControlUniform, 0, 1, 0, "Mute", "Microphone Mute", RecordMicrophone, true},
    };
    count = U(sizeof table / sizeof table[0]);
    return table;
}

inline const LineRecord *LineOfId(U line_id)
{
    U count = 0;
    const LineRecord *lines = Lines(count);
    for (U i = 0; i < count; ++i)
        if (lines[i].line_id == line_id && Visible(lines[i])) return &lines[i];
    return nullptr;
}
inline const LineRecord *LineOfComponent(U component)
{
    U count = 0;
    const LineRecord *lines = Lines(count);
    for (U i = 0; i < count; ++i)
        if (lines[i].component_type == component && Visible(lines[i])) return &lines[i];
    return nullptr;
}
inline const LineRecord *LineOfSourceNumber(U destination, U source)
{
    U count = 0;
    const LineRecord *lines = Lines(count);
    for (U i = 0; i < count; ++i)
        if (lines[i].destination == destination && lines[i].source == source && Visible(lines[i]))
            return &lines[i];
    return nullptr;
}
inline const LineRecord *DestinationLine(U destination)
{
    U count = 0;
    const LineRecord *lines = Lines(count);
    for (U i = 0; i < count; ++i)
        if (lines[i].destination == destination && lines[i].source == ~0u && Visible(lines[i])) return &lines[i];
    return nullptr;
}
inline const ControlRecord *ControlOfId(U id)
{
    U count = 0;
    const ControlRecord *controls = Controls(count);
    for (U i = 0; i < count; ++i)
        if (controls[i].id == id && LineOfId(controls[i].line_id)) return &controls[i];
    return nullptr;
}
// The controls of one line, as a range of the control table.
inline bool ControlsOfLine(U line_id, U &first, U &number)
{
    const LineRecord *line = LineOfId(line_id);
    if (!line) return false;
    first = line->first_control;
    number = line->controls;
    return true;
}
// The one control of a line with that type, for MIXER_GETLINECONTROLSF_ONEBYTYPE.
inline const ControlRecord *ControlOfLineAndType(U line_id, U type)
{
    U count = 0;
    const ControlRecord *controls = Controls(count);
    for (U i = 0; i < count; ++i)
        if (controls[i].line_id == line_id && controls[i].type == type) return &controls[i];
    return nullptr;
}

// The name of the device, in the style of the wave and MIDI devices of this
// port. MM_MICROSOFT is 1; the product number is the one Windows uses for a
// driver that names no hardware of its own.
static constexpr auto &DeviceName = Neo98Hardware::MixerName;
enum : U { ManufacturerId = 1, ProductId = 105, DriverVersion = 0x0400 };

// A volume pair as waveOutSetVolume passes it: left in the low word, right in
// the high word. The mixer keeps the same shape, so one number travels
// between the mixer device, the wave device and Sound::Mixer unchanged.
inline U VolumePair(U left, U right) { return (left & 0xffff) | ((right & 0xffff) << 16); }
inline U Left(U pair) { return pair & 0xffff; }
inline U Right(U pair) { return (pair >> 16) & 0xffff; }
} // namespace Win98Mixer
