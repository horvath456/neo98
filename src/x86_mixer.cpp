// SPDX-License-Identifier: GPL-2.0-or-later
// WINMM mixer APIs validate object handles and output buffers before calling
// the mixer driver. Handle records carry a signature and type; mixerClose
// accepts only mixer handles. Invalid handles and buffers return MMSYSTEM
// errors. Neo98 provides master, wave, and synthesizer volume controls
// through Sound::Mixer and src/win98_mixer.h.
#include "win98_mixer.h"
#include "sound.h"
#include "x86_app.h"
#include <string.h>
// The recording destination follows the wave input device.
bool Win98Mixer::RecordingPresent() { return Sound::Microphone::Present(); }
namespace X86
{
namespace M = Win98Mixer;
namespace
{
// The mixer handle record and device type.
constexpr unsigned RecordBytes = 0x4c, RecordMagic = 0x12345678, MixerType = 0x41;
// Callback kinds, the same set every multimedia open accepts.
constexpr unsigned CallbackMask = 0x70000, CallbackWindow = 0x10000, CallbackFunction = 0x30000;
} // namespace

//  for every mixer entry: the object is a device number or one of our
// records, and a record only counts when its type has no bit outside 77h.
bool Application::MixerObjectDevice(unsigned object, unsigned &device)
{
    return MultimediaDeviceOfHandle(object, 0x77, device);
}

// Writes a name into a fixed field, padded with zeros, the way a driver fills
// szShortName and szName.
void Application::MixerName(unsigned at, const char *text, unsigned bytes)
{
    char field[M::LongNameChars] = {};
    unsigned length = 0;
    while (text[length] && length + 1 < bytes && length < sizeof field) { field[length] = text[length]; ++length; }
    Store(at, field, bytes < sizeof field ? bytes : sizeof field);
}

// The value of one control, as the pair the whole port passes volume in.
unsigned Application::MixerControlPair(const void *record)
{
    const auto &control = *(const M::ControlRecord *)record;
    auto &sound = Sound::Shared();
    // The recording: the microphone line and the destination behind it scale
    // what a recording stream reads (src/sound_microphone.cpp).
    if (control.group == M::RecordMicrophone)
        return control.mute ? (Sound::Microphone::Muted() ? 1 : 0) : Sound::Microphone::Level();
    if (control.group == M::RecordMaster) return Sound::Microphone::Gain();
    if (control.mute)
    {
        const bool muted = control.group == M::Master ? sound.MasterMute() : sound.GroupMute(control.group);
        return muted ? 1 : 0;
    }
    return control.group == M::Master ? sound.MasterVolume() : sound.GroupVolume(control.group);
}
void Application::MixerSetControlPair(const void *record, unsigned value)
{
    const auto &control = *(const M::ControlRecord *)record;
    auto &sound = Sound::Shared();
    if (control.group == M::RecordMicrophone)
    {
        if (control.mute) Sound::Microphone::SetMute(value != 0);
        else Sound::Microphone::SetLevel(value);
        return;
    }
    if (control.group == M::RecordMaster)
    {
        Sound::Microphone::SetGain(value);
        return;
    }
    if (control.mute)
    {
        if (control.group == M::Master) sound.SetMasterMute(value != 0);
        else sound.SetGroupMute(control.group, value != 0);
        return;
    }
    if (control.group == M::Master) sound.SetMasterVolume(value);
    else sound.SetGroupVolume(control.group, value);
}

// A control changed, so every mixer this process has open hears about it. A
// driver sends MM_MIXM_CONTROL_CHANGE to the window or the callback the open
// named, with the handle in wParam and the control in lParam.
void Application::MixerControlChanged(unsigned control_id)
{
    for (unsigned i = 0; i < mixer_record_count; ++i)
    {
        const unsigned record = mixer_records[i];
        if (!record) continue;
        const unsigned kind = Read32(record + 0x38) & CallbackMask;
        const unsigned callback = Read32(record + 0x30);
        if (!callback) continue;
        if (kind == CallbackWindow)
        {
            const unsigned post[] = {callback, M::ControlChange, record, control_id};
            Api("PostMessageA", post);
        }
        else if (kind == CallbackFunction)
        {
            const unsigned arguments[] = {record, M::ControlChange, Read32(record + 0x34), control_id, 0};
            Call(callback, arguments, 5);
        }
    }
}

unsigned Application::MixerApi(ApiName name, const unsigned *a)
{
#define API(n) case API_ID(#n):
    unsigned device = 0;
    switch (name.id)
    {
    // One mixer, because there is one output.
    API(mixerGetNumDevs)
        return 1;
    // A size of zero answers zero before anything else, then the
    // object test, then the buffer.
    API(mixerGetDevCapsA)
    {
        if (!a[2]) return M::NoError;
        if (!MixerObjectDevice(a[0], device)) return M::InvalidHandle;
        if (!a[1] || !virtual_memory.Access(a[1], a[2], true)) return M::InvalidParameter;
        if (device != 0) return M::BadDeviceId;
        unsigned char caps[M::CapsBytes] = {};
        auto put16 = [&](unsigned at, unsigned value) { caps[at] = value & 255; caps[at + 1] = (value >> 8) & 255; };
        auto put32 = [&](unsigned at, unsigned value) {
            caps[at] = value & 255; caps[at + 1] = (value >> 8) & 255;
            caps[at + 2] = (value >> 16) & 255; caps[at + 3] = value >> 24;
        };
        put16(0, M::ManufacturerId);
        put16(2, M::ProductId);
        put32(4, M::DriverVersion);
        for (unsigned i = 0; M::DeviceName[i] && i < M::PnameChars - 1; ++i) caps[8 + i] = M::DeviceName[i];
        put32(8 + M::PnameChars, 0);       // fdwSupport, no driver option
        put32(12 + M::PnameChars, M::Destinations());   // cDestinations
        const unsigned bytes = a[2] < M::CapsBytes ? a[2] : M::CapsBytes;
        return Store(a[1], caps, bytes) ? M::NoError : M::InvalidParameter;
    }

    API(mixerGetID)
    {
        if (!MixerObjectDevice(a[0], device)) return M::InvalidHandle;
        if (!a[1] || !virtual_memory.Access(a[1], 4, true)) return M::InvalidParameter;
        if (device != 0) return M::BadDeviceId;
        // A wave input device that Windows does not have has no mixer.
        if ((a[2] & M::ObjectTypeMask) == M::ObjectWaveIn && !M::RecordingPresent()) return M::BadDeviceId;
        Write32(a[1], 0);
        return M::NoError;
    }

    API(mixerOpen)
    {
        if (!a[0] || !virtual_memory.Access(a[0], 4, true)) return M::InvalidParameter;
        if (!MixerObjectDevice(a[1], device)) return M::InvalidHandle;
        if (device != 0) return M::BadDeviceId;
        const unsigned kind = a[4] & CallbackMask;
        if (kind && kind != CallbackWindow && kind != CallbackFunction && kind != 0x20000 && kind != 0x50000)
            return M::InvalidFlag;
        if (mixer_record_count >= MixerRecordCapacity) return M::Error;
        const unsigned record = Allocate(RecordBytes);
        if (!record) return M::Error;
        unsigned char empty[RecordBytes] = {};
        Store(record, empty, RecordBytes);
        Write32(record, 0);                 // the device number
        Write32(record + 4, RecordMagic);
        Write32(record + 0xc, MixerType);
        Write32(record + 0x30, a[2]);       // dwCallback
        Write32(record + 0x34, a[3]);       // dwInstance
        Write32(record + 0x38, a[4] & CallbackMask);
        mixer_records[mixer_record_count++] = record;
        Write32(a[0], record);
        return M::NoError;
    }
    // only a record of type 41h.
    API(mixerClose)
    {
        if (!virtual_memory.Access(a[0], RecordBytes, true)) return M::InvalidHandle;
        if (Read32(a[0] + 4) != RecordMagic || Read32(a[0] + 0xc) & ~MixerType) return M::InvalidHandle;
        for (unsigned i = 0; i < mixer_record_count; ++i)
            if (mixer_records[i] == a[0])
            {
                mixer_records[i] = mixer_records[--mixer_record_count];
                break;
            }
        Write32(a[0] + 4, 0);
        Free(a[0]);
        return M::NoError;
    }

    API(mixerGetLineInfoA)
    {
        if (!MixerObjectDevice(a[0], device)) return M::InvalidHandle;
        if (!a[1] || !virtual_memory.Access(a[1], M::LineBytes, true)) return M::InvalidParameter;
        if (device != 0) return M::BadDeviceId;
        if (Read32(a[1] + M::Line::Size) != M::LineBytes) return M::InvalidParameter;
        const M::LineRecord *line = nullptr;
        switch (a[2] & M::QueryMask)
        {
        case M::QueryDestination:
            line = M::DestinationLine(Read32(a[1] + M::Line::Destination));
            break;
        case M::QuerySource:
            line = M::LineOfSourceNumber(Read32(a[1] + M::Line::Destination), Read32(a[1] + M::Line::Source));
            break;
        case M::QueryLineId:
            line = M::LineOfId(Read32(a[1] + M::Line::LineId));
            break;
        case M::QueryComponentType:
            line = M::LineOfComponent(Read32(a[1] + M::Line::ComponentType));
            break;
        case M::QueryTargetType:
        {
            // The target names a wave or MIDI device of this driver; the line
            // that carries it is the one whose target type matches. A source
            // carries the output devices; the wave input device is the target
            // of the recording destination itself.
            unsigned count = 0;
            const M::LineRecord *lines = M::Lines(count);
            const unsigned type = Read32(a[1] + M::Line::TargetType);
            for (unsigned i = 0; i < count && !line; ++i)
                if (lines[i].target_type == type && lines[i].source != ~0u && M::Visible(lines[i])) line = &lines[i];
            for (unsigned i = 0; i < count && !line; ++i)
                if (lines[i].target_type == type && M::Visible(lines[i])) line = &lines[i];
            break;
        }
        default:
            return M::InvalidFlag;
        }
        if (!line) return M::InvalidLine;
        MixerWriteLine(a[1], line);
        return M::NoError;
    }

    API(mixerGetLineControlsA)
    {
        if (!MixerObjectDevice(a[0], device)) return M::InvalidHandle;
        if (!a[1] || !virtual_memory.Access(a[1], M::LineControlsBytes, true)) return M::InvalidParameter;
        if (device != 0) return M::BadDeviceId;
        if (Read32(a[1] + M::LineControls::Size) != M::LineControlsBytes) return M::InvalidParameter;
        const unsigned control_bytes = Read32(a[1] + M::LineControls::ControlBytes);
        const unsigned array = Read32(a[1] + M::LineControls::Array);
        if (control_bytes < M::ControlBytes) return M::InvalidParameter;
        unsigned count = 0;
        const M::ControlRecord *controls = M::Controls(count);
        unsigned first = 0, number = 0;
        switch (a[2] & M::QueryMask)
        {
        case M::ControlsAll:
            if (!M::ControlsOfLine(Read32(a[1] + M::LineControls::LineId), first, number)) return M::InvalidLine;
            if (Read32(a[1] + M::LineControls::Controls) != number) return M::InvalidParameter;
            break;
        case M::ControlsOfId:
        {
            const M::ControlRecord *control = M::ControlOfId(Read32(a[1] + M::LineControls::ControlId));
            if (!control) return M::InvalidControl;
            first = unsigned(control - controls);
            number = 1;
            Write32(a[1] + M::LineControls::LineId, control->line_id);
            break;
        }
        case M::ControlsOfType:
        {
            const unsigned line_id = Read32(a[1] + M::LineControls::LineId);
            if (!M::LineOfId(line_id)) return M::InvalidLine;
            const M::ControlRecord *control = M::ControlOfLineAndType(line_id, Read32(a[1] + M::LineControls::ControlId));
            if (!control) return M::InvalidControl;
            first = unsigned(control - controls);
            number = 1;
            break;
        }
        default:
            return M::InvalidFlag;
        }
        if (!array || !virtual_memory.Access(array, number * control_bytes, true)) return M::InvalidParameter;
        for (unsigned i = 0; i < number; ++i) MixerWriteControl(array + i * control_bytes, &controls[first + i]);
        Write32(a[1] + M::LineControls::Controls, number);
        return M::NoError;
    }
    //  and share their checks; only the direction differs.
    API(mixerGetControlDetailsA) API(mixerSetControlDetails)
    {
        const bool get = API_IS(name, "mixerGetControlDetailsA");
        if (!MixerObjectDevice(a[0], device)) return M::InvalidHandle;
        if (!a[1] || !virtual_memory.Access(a[1], M::ControlDetailsBytes, true)) return M::InvalidParameter;
        if (device != 0) return M::BadDeviceId;
        if (Read32(a[1] + M::Details::Size) != M::ControlDetailsBytes) return M::InvalidParameter;
        // Only the value form. A control of this mixer carries no list, so
        // MIXER_GETCONTROLDETAILSF_LISTTEXT has nothing to answer.
        if ((a[2] & M::QueryMask) == M::DetailsListText) return M::NotSupported;
        if ((a[2] & M::QueryMask) != M::DetailsValue) return M::InvalidFlag;
        const M::ControlRecord *control = M::ControlOfId(Read32(a[1] + M::Details::ControlId));
        if (!control) return M::InvalidControl;
        const unsigned channels = Read32(a[1] + M::Details::Channels);
        const unsigned detail_bytes = Read32(a[1] + M::Details::DetailBytes);
        const unsigned array = Read32(a[1] + M::Details::Array);
        if (detail_bytes != 4) return M::InvalidParameter;
        // A uniform control carries one value; a stereo fader one per channel,
        // and a caller that asks for one gets the pair as one number.
        const unsigned wanted = (control->flags & M::ControlUniform) ? 1 : 2;
        if (!channels || channels > wanted) return M::InvalidParameter;
        if (!array || !virtual_memory.Access(array, channels * 4, true)) return M::InvalidParameter;
        const unsigned pair = MixerControlPair(control);
        if (get)
        {
            if (channels == 1) Write32(array, control->mute ? pair : M::Left(pair));
            else { Write32(array, M::Left(pair)); Write32(array + 4, M::Right(pair)); }
            return M::NoError;
        }
        unsigned value = 0;
        if (channels == 1)
        {
            const unsigned one = Read32(array);
            if (one > control->maximum) return M::InvalidValue;
            value = control->mute ? (one ? 1u : 0u) : M::VolumePair(one, one);
        }
        else
        {
            const unsigned left = Read32(array), right = Read32(array + 4);
            if (left > control->maximum || right > control->maximum) return M::InvalidValue;
            value = M::VolumePair(left, right);
        }
        MixerSetControlPair(control, value);
        MixerControlChanged(control->id);
        return M::NoError;
    }
    // The message goes to the driver, and this driver has no private
    // message of its own.
    API(mixerMessage)
        if (!MixerObjectDevice(a[0], device)) return M::InvalidHandle;
        return M::NotSupported;
    default:
        break;
    }
    Fail(name);
    return 0;
#undef API
}

// One MIXERLINEA out of a line of the catalogue.
void Application::MixerWriteLine(unsigned at, const void *record)
{
    const auto &line = *(const M::LineRecord *)record;
    Write32(at + M::Line::Size, M::LineBytes);
    Write32(at + M::Line::Destination, line.destination);
    Write32(at + M::Line::Source, line.source == ~0u ? 0 : line.source);
    Write32(at + M::Line::LineId, line.line_id);
    Write32(at + M::Line::Flags, line.flags);
    Write32(at + M::Line::User, 0);
    Write32(at + M::Line::ComponentType, line.component_type);
    Write32(at + M::Line::Channels, 2);
    Write32(at + M::Line::Connections, line.connections);
    Write32(at + M::Line::Controls, line.controls);
    MixerName(at + M::Line::ShortName, line.short_name, M::ShortNameChars);
    MixerName(at + M::Line::Name, line.name, M::LongNameChars);
    Write32(at + M::Line::TargetType, line.target_type);
    Write32(at + M::Line::TargetDeviceId, 0);
    Write32(at + M::Line::TargetMid, M::ManufacturerId | (M::ProductId << 16));
    Write32(at + M::Line::TargetVersion, M::DriverVersion);
    // The wave input device names itself, as the szPname a program compares
    // with waveInGetDevCaps.
    MixerName(at + M::Line::TargetPname,
              line.target_type == M::TargetWaveIn ? Neo98Hardware::WaveInName : M::DeviceName, M::PnameChars);
}

// One MIXERCONTROLA out of a control of the catalogue.
void Application::MixerWriteControl(unsigned at, const void *record)
{
    const auto &control = *(const M::ControlRecord *)record;
    unsigned char empty[M::ControlBytes] = {};
    Store(at, empty, M::ControlBytes);
    Write32(at + M::Control::Size, M::ControlBytes);
    Write32(at + M::Control::Id, control.id);
    Write32(at + M::Control::Type, control.type);
    Write32(at + M::Control::Flags, control.flags);
    Write32(at + M::Control::MultipleItems, 0);
    MixerName(at + M::Control::ShortName, control.short_name, M::ShortNameChars);
    MixerName(at + M::Control::Name, control.name, M::LongNameChars);
    Write32(at + M::Control::Minimum, control.minimum);
    Write32(at + M::Control::Maximum, control.maximum);
    Write32(at + M::Control::Steps, control.steps);
}
} // namespace X86
