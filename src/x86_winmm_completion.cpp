// SPDX-License-Identifier: GPL-2.0-or-later
// The WINMM exports that were still missing from the export contract
// after the import census of 23 September 2026.
// Most of them are the argument check of WINMM in front of a thunk into
// MMSYSTEM, and the 16-bit routine behind the thunk asks a device this port
// does not have: there is no wave input, no MIDI input, no auxiliary device
// and no joystick driver. Their answer is the one of that 16-bit routine on
// such a machine. Handle validation checks
// IsBadWritePtr over 4Ch bytes, the magic 12345678h at +4 and a device type at
// +0Ch inside the set the entry accepts. Unlike the capability queries of
// src/x86_mm_devices.cpp these entries have no pass-through for a device
// identifier, so zero and -1 fail the probe too.
#include "x86_app.h"
#include "host_devices.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_locale.h"
#else
#include "../build/winmm-message/winmm_messages.h"
#endif
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned InvalidHandle = 5, NoDriver = 6, NotSupported = 8, InvalidFlag = 10,
                   InvalidParameter = 11, BadDeviceId = 2;
const char *UnknownMciError()
{
    for (const auto &entry : OriginalMultimediaMessages)
        if (entry.id == 2001) return entry.text;
    return "";
}
} // namespace

// The handle test of the family. Zero or InvalidHandle.
unsigned Application::MultimediaRecord(unsigned handle, unsigned types, unsigned &device)
{
    device = 0;
    if (!handle || !virtual_memory.Access(handle, 0x4c, true)) return InvalidHandle;
    if (Read32(handle + 4) != 0x12345678) return InvalidHandle;
    if (Read32(handle + 0xc) & ~types) return InvalidHandle;
    device = Read32(handle);
    return 0;
}

// The device-identifier half of for the message entries: the
// probes of the four query messages 801h to 805h and the single message the
// family answers on an identifier. `expected` is that message: 3 for aux,
// 35h for MIDI input.
bool Application::MultimediaMessageArguments(unsigned message, unsigned first, unsigned second,
                                             unsigned expected)
{
    if (message >= 0x801 && message <= 0x803)
        return virtual_memory.Access(first, message == 0x802 ? 4 : second, true);
    if (message == 0x804)
    {
        if (!virtual_memory.Access(second, 4, true)) return false;
        return virtual_memory.Access(first, 2 * Read32(second), true);
    }
    if (message == 0x805) return true;
    return message == expected && virtual_memory.Access(first, 4, true);
}

// A tile of 64 KiB selectors over a linear block, KERNEL.613 behind
// WINMMTILEBUFFER. The answer is the 16:16 pointer of the
// first tile, or zero.
unsigned Application::MultimediaTile(unsigned linear, unsigned bytes)
{
    const unsigned tiles = bytes ? (bytes + 0xffff) / 0x10000 : 1;
    const unsigned selector = tiles == 1 ? Win16Segment(linear, bytes ? bytes - 1 : 0, false, false)
                                         : selectors.CreateRun(tiles);
    if (!selector) return 0;
    for (unsigned i = 0; i < tiles; ++i)
    {
        auto *entry = selectors.Find((selector + i * 8) << 16);
        if (!entry) return 0;
        entry->base = linear + i * 0x10000;
        entry->limit = bytes ? bytes - 1 - i * 0x10000 : 0;
        entry->present = true;
        if (!Win16Descriptor(selector + i * 8)) return 0;
    }
    for (auto &tile : multimedia_tiles)
        if (!tile.selector)
        {
            tile.selector = selector;
            tile.count = tiles;
            return selector << 16;
        }
    Fail("WINMMTILEBUFFER: the tile table of this port is full");
    return 0;
}

unsigned Application::WinmmCompletionApi(ApiName name, const unsigned *a)
{
    unsigned device = 0;
    switch (name.id)
    {
    // Ordinals 2 and 18 share : PlaySound is PlaySoundA.
    API_CASE("PlaySound")
        return Api("PlaySoundA", a);
    // Ordinal 4,  over : LoadLibraryA("MMSYS.CPL"),
    // GetProcAddress("ShowMMCPLPropertySheet"), the call with the same four
    // arguments, FreeLibrary. The answer is one whether or not any of it
    // worked.
    API_CASE("WINMM.ordinal_4")
    {
        const unsigned text = Allocate(64);
        if (!text)
        {
            Fail("WINMM ordinal 4: no guest memory for the library name");
            return 1;
        }
        Store(text, "MMSYS.CPL", 10);
        const unsigned library = Api("LoadLibraryA", &text);
        if (library)
        {
            Store(text, "ShowMMCPLPropertySheet", 23);
            const unsigned query[2] = {library, text};
            const unsigned procedure = Api("GetProcAddress", query);
            if (procedure)
                Call(procedure, a, 4);
            Api("FreeLibrary", &library);
        }
        Free(text);
        return 1;
    }
    //  probes the answer and goes to AUXGETVOLUME, which
    // probes again and asks mmDeviceFind for an aux device. There is
    // none, so the identifier is out of range.
    API_CASE("auxGetVolume")
        return virtual_memory.Access(a[1], 4, true) ? BadDeviceId : InvalidParameter;
        // CL 14h to AUXSETVOLUME without a check of its own.
    API_CASE("auxSetVolume")
        return BadDeviceId;
    //  through with the type set 0. A handle can never be an
    // aux record. On an identifier the only messages that pass are 3 and the
    // four queries, and the 16-bit side then finds no device: 5.
    API_CASE("auxOutMessage")
    {
        if (a[0] != ~0u && (a[0] >> 16)) return InvalidHandle;
        if (!MultimediaMessageArguments(a[1], a[2], a[3], 3)) return InvalidParameter;
        return InvalidHandle;
    }
    // CL 67h to JOYCONFIGCHANGED : flags must be zero, then
    // the notification goes to joystick 0, and without a joystick driver the
    // list at [132Eh] is empty.
    API_CASE("joyConfigChanged")
        return a[0] ? InvalidParameter : HostDevices::current ? 0 : NoDriver;
    // the answer is probed for four bytes, JOYGETTHRESHOLD
    // finds no joystick record, and the stub clears the high word of the
    // answer on every path.
    API_CASE("joyGetThreshold")
    {
        if (!virtual_memory.Access(a[1], 4, true)) return InvalidParameter;
        if(HostDevices::current&&a[0]<AH_MAX_JOYS)
        {Write32(a[1],HostDevices::thresholds[a[0]]);return 0;}
        const unsigned short zero = 0;
        Store(a[1] + 2, &zero, 2);
        return InvalidParameter;
    }
    // JOYSETTHRESHOLD, JOYSETCAPTURE and JOYRELEASECAPTURE
    // all start with the joystick record of the identifier.
    API_CASE("joySetThreshold")
        if(!HostDevices::current||a[0]>=AH_MAX_JOYS)return InvalidParameter;
        if(a[1]>65535)return 165;
        HostDevices::thresholds[a[0]]=a[1];return 0;
    API_CASE("joySetCapture")
    {
        if(!HostDevices::current)return InvalidParameter;
        if(a[1]>=2)return 165;
        if(!User::IsWindow(a[0])||User::WindowProcess(a[0])!=process_id)return InvalidParameter;
        AH_Status status{};if(HostDevices::current->Status(status)||a[1]>=status.joysticks)return 167;
        auto &capture=HostDevices::captures[a[1]];
        if(capture.window&&User::IsWindow(capture.window))return 165;
        capture={};capture.window=a[0];capture.period=a[2]<10?10:a[2]>1000?1000:a[2];capture.changed_only=a[3]!=0;
        return 0;
    }
    API_CASE("joyReleaseCapture")
        if(!HostDevices::current||a[0]>=2)return InvalidParameter;
        if(HostDevices::captures[a[0]].window&&User::WindowProcess(HostDevices::captures[a[0]].window)!=process_id)return 165;
        HostDevices::captures[a[0]]={};return 0;
    // IsBadStringPtrA(command, 100h) answers 11 as a nonzero BOOL.
    // Then MMSYSTEM: mciSendString without a return buffer; on an error
    // the text of the error, or string 2001, in a message box whose caption
    // is the second word of the command, the device name or alias.
    API_CASE("mciExecute")
    {
        if (!a[0] || !virtual_memory.Access(a[0], 1, false)) return InvalidParameter;
        const unsigned send[4] = {a[0], 0, 0, 0};
        const unsigned error = Api("mciSendStringA", send);
        if (!error) return 1;
        const unsigned block = Allocate(0x300);
        if (!block)
        {
            Fail("mciExecute: no guest memory for the error box");
            return 0;
        }
        const unsigned text = block, caption = block + 0x100;
        const unsigned query[3] = {error, text, 0x100};
        if (!Api("mciGetErrorStringA", query))
        {
            const char *fallback = UnknownMciError();
            Store(text, fallback, (unsigned)strlen(fallback) + 1);
        }
        // over the second word: blanks first, then the word, and a
        // quoted word keeps its blanks, two quotes stand for one. An
        // unbalanced quote leaves the caption null.
        char command[0x100] = {};
        const char *source = ReadString(a[0]);
        strncpy(command, source, sizeof command - 1);
        const char *p = command;
        while (*p == ' ') ++p;
        while (*p && *p != ' ') ++p;
        while (*p == ' ') ++p;
        char word[0x100] = {};
        unsigned length = 0;
        bool have_caption = *p != 0, open_quote = false;
        if (*p == '"')
        {
            open_quote = true;
            ++p;
            while (*p && length + 1 < sizeof word)
            {
                if (*p == '"')
                {
                    if (p[1] == '"') { word[length++] = '"'; p += 2; continue; }
                    open_quote = false;
                    break;
                }
                word[length++] = *p++;
            }
            if (open_quote) have_caption = false;
        }
        else
            while (*p && *p != ' ' && length + 1 < sizeof word) word[length++] = *p++;
        if (have_caption) Store(caption, word, length + 1);
        const unsigned box[4] = {0, text, have_caption ? caption : 0, 0x10};
        Api("MessageBoxA", box);
        Free(block);
        return 0;
    }
    // CL 6 to MCIGETCREATORTASK : the task that opened the
    // device, zero for an identifier without a device.
    API_CASE("mciGetCreatorTask")
        return MciCreatorTask(a[0] & 0xffff);
    // Windows 98 Win32 multimedia keeps no yield procedure. These entries
    // return zero without changing state.
    API_CASE("mciGetYieldProc") API_CASE("mciSetYieldProc")
        return 0;
    //  and over, then CL 4Fh to MMSYSTEM.
    // The source must be a MIDI record, the sink a MIDI output record. The
    // only MIDI device here is the synthesizer, and it has no thru, so the
    // driver answers DRVM_ADD_THRU and DRVM_REMOVE_THRU with 8; software thru
    // is only for a MIDI input source. Disconnecting a named sink that is not
    // the current thru target answers 11.
    API_CASE("midiConnect") API_CASE("midiDisconnect")
    {
        const bool connect = API_IS(name, "midiConnect");
        if (unsigned r = MultimediaRecord(a[0], 0x13, device)) return r;
        unsigned sink = 0;
        if (a[1])
            if (unsigned r = MultimediaRecord(a[1], 0x12, sink)) return r;
        void *opaque = nullptr;
        if (!MidiDeviceOfHandle(a[0], 0x12, opaque)) return InvalidHandle;
        if (connect)
        {
            if (!a[1] || !MidiDeviceOfHandle(a[1], 0x12, opaque)) return InvalidHandle;
            return NotSupported;
        }
        return a[1] ? InvalidParameter : NotSupported;
    }
    // The handle test with the MIDI input
    // (11h) and wave input (21h) type sets. No such record exists here.
    // Every record of this port has a type outside both sets, and a record
    // that passed would still meet the 16-bit routine, which refuses a handle
    // that is no input device with the same 5.
    API_CASE("midiInAddBuffer") API_CASE("midiInGetID")
        return InvalidHandle;
    //  through with the type set 11h. On an identifier the
    // only messages that pass are MIDM_GETNUMDEVS (35h) and the four
    // queries, and Windows 98 finds no MIDI input device.
    API_CASE("midiInMessage")
    {
        if (a[0] != ~0u && (a[0] >> 16)) return InvalidHandle;
        if (!MultimediaMessageArguments(a[1], a[2], a[3], 0x35)) return InvalidParameter;
        return InvalidHandle;
    }
    //  over the shared worker with CL 4Bh.
    API_CASE("midiInPrepareHeader") API_CASE("midiInUnprepareHeader")
        return MultimediaHeaderApi(name, a);
    //  The handle test with 16h, the patch array for
    // 100h bytes, and CL 5Dh or 5Ch to or, which check the
    // handle and the array again, refuse flags above bit 2 with 10 and send
    // MODM_CACHEPATCHES or MODM_CACHEDRUMPATCHES to the driver. The
    // synthesizer of this port keeps no patch cache and answers 8.
    API_CASE("midiOutCachePatches") API_CASE("midiOutCacheDrumPatches")
    {
        if (unsigned r = MultimediaRecord(a[0], 0x16, device)) return r;
        if (!virtual_memory.Access(a[2], 0x100, true)) return InvalidParameter;
        void *opaque = nullptr;
        if (!MidiDeviceOfHandle(a[0], 0x12, opaque) && !MidiDeviceOfHandle(a[0], 0x16, opaque))
            return InvalidHandle;
        if (a[3] & 0xfff8) return InvalidFlag;
        return NotSupported;
    }
    // Check the handle type against 12h and probe four output bytes.
    // The device query returns a word index, which the stub sign-extends.
    API_CASE("midiOutGetID")
    {
        if (unsigned r = MultimediaRecord(a[0], 0x12, device)) return r;
        if (!virtual_memory.Access(a[1], 4, true)) return InvalidParameter;
        void *opaque = nullptr;
        if (!MidiDeviceOfHandle(a[0], 0x12, opaque)) return InvalidHandle;
        Write32(a[1], unsigned(int(short(device & 0xffff))));
        return 0;
    }
    // WINMMTILEBUFFER allocates a run of
    // selectors over (linear, length); the answer is its 16:16 pointer.
    API_CASE("WINMM.ordinal_1")
        return MultimediaTile(a[0], a[1]);
    // WINMMUNTILEBUFFER releases the selector run. Callers ignore the result.
    API_CASE("WINMM.ordinal_3")
    {
        const unsigned selector = a[0] >> 16;
        for (auto &tile : multimedia_tiles)
            if (tile.selector && tile.selector == selector)
            {
                for (unsigned i = 0; i < tile.count; ++i)
                    selectors.Unmap((selector + i * 8) << 16);
                tile = {};
                break;
            }
        return 0;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}

// midiInPrepareHeader and midiInUnprepareHeader over the
// shared workers. The handle test takes every wave and
// MIDI record (77h); the header size and probes depend on the record type,
// and only then does the 16-bit routine, MIDIINPREPAREHEADER or
// MIDIINUNPREPAREHEADER, refuse a handle that is no MIDI input with 5.
unsigned Application::MultimediaHeaderApi(ApiName name, const unsigned *a)
{
    const bool prepare = API_IS(name, "midiInPrepareHeader");
    unsigned device = 0;
    if (unsigned r = MultimediaRecord(a[0], 0x77, device)) return r;
    const unsigned type = Read32(a[0] + 0xc), header = a[1], bytes = a[2];
    unsigned reserved = 0;
    if (type & 0x20)
    {
        if (bytes < 0x20 || !virtual_memory.Access(header, 0x20, true)) return InvalidParameter;
        reserved = header + 0x1c;
    }
    else if (type & 0x10)
    {
        if (!(bytes >= 0x40 && virtual_memory.Access(header, 0x40, true)) && !(type & 4))
            if (bytes < 0x1c || !virtual_memory.Access(header, 0x1c, true)) return InvalidParameter;
        reserved = header + 0x18;
    }
    else
        return InvalidHandle;
    const unsigned flags = Read32(header + 0x10);
    if (prepare)
    {
        // the data block, written by a recording device, read by a
        // playing one.
        const unsigned data = Read32(header), length = Read32(header + 4);
        if (length && !virtual_memory.Access(data, length, (type & 1) != 0)) return InvalidParameter;
        if (flags & 2)
        {
            // a header that says it is prepared must lead to the
            // shadow record of a prepare, 64h bytes. A shadow that is itself
            // prepared must name this header and this handle; one that is not
            // falls through to a fresh prepare.
            const unsigned shadow = Read32(reserved);
            if (!virtual_memory.Access(shadow, 0x64, true)) return InvalidParameter;
            if (Read32(shadow + 0x10) & 2)
                return Read32(shadow + 0xc) == header && Read32(shadow + 0x58) == a[0] ? 0
                                                                                        : InvalidParameter;
        }
        //  builds the shadow and thunks; MIDIINPREPAREHEADER refuses a
        // handle that is no MIDI input, and the shadow is freed again.
        return InvalidHandle;
    }
    // a header in a queue stays; otherwise PREPARED goes first, and
    // a header without a readable shadow is done at that point.
    if (type & 0x20) { if (flags & 0x10) return 0x21; }
    else if (flags & 4) return 0x41;
    Write32(header + 0x10, flags & ~2u);
    const unsigned shadow = Read32(reserved);
    if (!virtual_memory.Access(shadow, 0x64, true)) return 0;
    return InvalidHandle;
}
} // namespace X86
