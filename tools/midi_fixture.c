/* SPDX-License-Identifier: GPL-2.0-or-later
 * An x86 guest over the public midiOut and midiStream API. It sounds note 69,
 * which is 440 Hz, so the runner can count the zero crossings of what came out
 * of the synthesizer; that is the sample side of the pixel check a UI test
 * does.
 *
 * Every check carries its own exit code, so a failure names itself.
 * */
typedef unsigned U;
#define API __declspec(dllimport)

API void __stdcall ExitProcess(U);
API void __stdcall Sleep(U);
API U __stdcall GetTickCount(void);

API U __stdcall midiOutGetNumDevs(void);
API U __stdcall midiOutGetDevCapsA(U, void *, U);
API U __stdcall midiOutOpen(U *, U, U, U, U);
API U __stdcall midiOutClose(U);
API U __stdcall midiOutReset(U);
API U __stdcall midiOutShortMsg(U, U);
API U __stdcall midiOutLongMsg(U, void *, U);
API U __stdcall midiOutPrepareHeader(U, void *, U);
API U __stdcall midiOutUnprepareHeader(U, void *, U);
API U __stdcall midiOutGetVolume(U, U *);
API U __stdcall midiOutSetVolume(U, U);
API U __stdcall midiOutMessage(U, U, U, U);
API U __stdcall midiOutGetErrorTextA(U, char *, U);
API U __stdcall midiStreamOpen(U *, U *, U, U, U, U);
API U __stdcall midiStreamClose(U);
API U __stdcall midiStreamOut(U, void *, U);
API U __stdcall midiStreamPause(U);
API U __stdcall midiStreamRestart(U);
API U __stdcall midiStreamStop(U);
API U __stdcall midiStreamProperty(U, void *, U);
API U __stdcall midiStreamPosition(U, void *, U);

/* MIDIHDR, the whole 40h bytes a stream header needs. */
struct MidiHeader
{
    char *data;
    U length, recorded, user, flags;
    void *next;
    U reserved, offset, spare[8];
};
struct MidiOutCaps
{
    unsigned short mid, pid;
    U version;
    char name[32];
    unsigned short technology, voices, notes, channels;
    U support;
};
struct Property
{
    U size, value;
};
struct Time
{
    U type, value, rest;
};

static void check(int ok, U code)
{
    if (!ok) ExitProcess(code);
}

/* What the callback saw, in the order it saw it. */
static U opened, closed, done_count, position_count;
static void __stdcall Callback(U handle, U message, U instance, U first, U second)
{
    (void)handle;
    (void)first;
    (void)second;
    if (instance != 0x5ea50001) return;
    if (message == 0x3c7) ++opened;       /* MOM_OPEN */
    if (message == 0x3c8) ++closed;       /* MOM_CLOSE */
    if (message == 0x3c9) ++done_count;   /* MOM_DONE */
    if (message == 0x3ca) ++position_count; /* MOM_POSITIONCB */
}

static void wait(U milliseconds)
{
    U start = GetTickCount();
    while (GetTickCount() - start < milliseconds) Sleep(5);
}

/* The MIDIEVENT stream: delta time, stream identifier, event. */
static U stream_events[24];
static struct MidiHeader stream_header;
static struct MidiHeader long_header;
static unsigned char sysex[6] = {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7};

int mainCRTStartup(void)
{
    U handle = 0, value = 0, i = 0;
    struct MidiOutCaps caps;
    char text[64];

    /* The device count and the capabilities. */
    check(midiOutGetNumDevs() == 1, 2);
    check(midiOutGetDevCapsA(0, &caps, 0) == 0, 3);
    check(midiOutGetDevCapsA(0, (void *)0, sizeof caps) == 11, 4);
    check(midiOutGetDevCapsA(1, &caps, sizeof caps) == 2, 5);
    check(midiOutGetDevCapsA(0, &caps, sizeof caps) == 0, 6);
    check(caps.technology == 2, 7);          /* MOD_SYNTH */
    check(caps.voices >= 16, 8);
    check(caps.channels == 0xffff, 9);
    check((caps.support & 1) != 0, 10);      /* MIDICAPS_VOLUME */
    check((caps.support & 8) != 0, 11);      /* MIDICAPS_STREAM */
    check(caps.name[0] == 'A', 12);

    /* The error text of the MIDI block. */
    check(midiOutGetErrorTextA(0x40, text, sizeof text) == 0, 13);
    check(text[0] != 0, 14);

    /* Opening. */
    check(midiOutOpen((U *)0, 0, 0, 0, 0) == 11, 15);
    check(midiOutOpen(&handle, 1, 0, 0, 0) == 2, 16);
    check(midiOutOpen(&handle, 0, 0, 0, 0x60000) == 11, 17);
    check(midiOutOpen(&handle, 0, (U)&Callback, 0x5ea50001, 0x30000) == 0, 18);
    check(handle != 0, 19);
    check(opened == 1, 20);

    /* A note of 440 Hz on channel 1 with the piano program. */
    check(midiOutShortMsg(handle, 0xc0) == 0, 21);              /* program 0 */
    check(midiOutShortMsg(handle, 0x07b0 | (100u << 16)) == 0, 22); /* volume 100 */
    check(midiOutShortMsg(handle, 0x90 | (69u << 8) | (127u << 16)) == 0, 23);
    wait(400);
    check(midiOutShortMsg(handle, 0x80 | (69u << 8)) == 0, 24);
    wait(200);

    /* The volume pair works on the stream and on the device. */
    check(midiOutGetVolume(handle, &value) == 0, 25);
    check(midiOutSetVolume(handle, 0x80008000) == 0, 26);
    check(midiOutGetVolume(handle, &value) == 0, 27);
    check(value == 0x80008000, 28);
    check(midiOutSetVolume(handle, 0xffffffff) == 0, 29);
    check(midiOutGetVolume(0, &value) == 0, 30);
    check(midiOutGetVolume(0, (U *)0) == 11, 31);

    /* A system exclusive block over a prepared header. */
    long_header.data = (char *)sysex;
    long_header.length = sizeof sysex;
    check(midiOutLongMsg(handle, &long_header, sizeof long_header) == 0x40, 32);
    check(midiOutPrepareHeader(handle, &long_header, sizeof long_header) == 0, 33);
    check((long_header.flags & 2) != 0, 34);
    check(midiOutLongMsg(handle, &long_header, 8) == 11, 35);
    check(midiOutLongMsg(handle, &long_header, sizeof long_header) == 0, 36);
    wait(60);
    check(done_count == 1, 37);
    check((long_header.flags & 1) != 0, 38);
    check(midiOutUnprepareHeader(handle, &long_header, sizeof long_header) == 0, 39);

    /* This driver has no private messages. */
    check(midiOutMessage(handle, 0x805, 0, 0) == 8, 40);
    check(midiOutReset(handle) == 0, 41);
    check(midiOutClose(handle) == 0, 42);
    check(closed == 1, 43);
    check(midiOutShortMsg(handle, 0x90) == 5, 44);

    /* Silence between the two stretches the runner looks at. */
    wait(500);

    /* The stream. */
    {
        U stream = 0, device = 0;
        struct Property property;
        struct Time time;
        done_count = opened = closed = 0;
        check(midiStreamOpen(&stream, &device, 2, 0, 0, 0) == 8, 45);
        check(midiStreamOpen(&stream, &device, 1, (U)&Callback, 0x5ea50001, 0x30000) == 0, 46);
        check(opened == 1, 47);
        /* A stream handle refuses a short message outside its stream. */
        check(midiOutShortMsg(stream, 0x90) == 0x46, 48);

        property.size = sizeof property;
        property.value = 96;
        check(midiStreamProperty(stream, &property, 0x80000001u) == 0, 49);
        property.value = 0;
        check(midiStreamProperty(stream, &property, 0x40000001u) == 0, 50);
        check(property.value == 96, 51);
        property.value = 500000;
        check(midiStreamProperty(stream, &property, 0x80000002u) == 0, 52);
        property.value = 0;
        check(midiStreamProperty(stream, &property, 0x40000002u) == 0, 53);
        check(property.value == 500000, 54);
        check(midiStreamProperty(stream, &property, 0x40000004u) == 11, 55);
        check(midiStreamProperty(stream, &property, 0xc0000001u) == 11, 56);

        /* Four events: program, volume, note on at once, note off after a
         * quarter note, and a callback on the last one. At 96 ticks to the
         * quarter note and 500.000 microseconds a quarter note that is half a
         * second of sound. */
        i = 0;
        stream_events[i++] = 0;
        stream_events[i++] = 0;
        stream_events[i++] = 0x000000c0u;
        stream_events[i++] = 0;
        stream_events[i++] = 0;
        stream_events[i++] = 0x006407b0u;
        stream_events[i++] = 0;
        stream_events[i++] = 0;
        stream_events[i++] = 0x007f4590u;
        stream_events[i++] = 96;
        stream_events[i++] = 0;
        stream_events[i++] = 0x40000000u | 0x00004580u;
        stream_header.data = (char *)stream_events;
        stream_header.length = i * 4;
        stream_header.recorded = i * 4;
        check(midiStreamOut(stream, &stream_header, sizeof stream_header) == 0x40, 57);
        check(midiOutPrepareHeader(stream, &stream_header, 0x20) == 11, 58);
        check(midiOutPrepareHeader(stream, &stream_header, sizeof stream_header) == 0, 59);
        check(midiStreamOut(stream, &stream_header, sizeof stream_header) == 0, 60);
        check((stream_header.flags & 4) != 0, 61);
        check(midiStreamOut(stream, &stream_header, sizeof stream_header) == 0x41, 62);
        check(midiStreamRestart(stream) == 0, 63);
        wait(700);
        check(done_count == 1, 64);
        check(position_count == 1, 65);
        time.type = 1;  /* TIME_MS */
        check(midiStreamPosition(stream, &time, sizeof time) == 0, 66);
        check(time.type == 1, 67);
        check(time.value >= 400, 68);
        time.type = 0x20; /* TIME_TICKS */
        check(midiStreamPosition(stream, &time, sizeof time) == 0, 69);
        check(time.type == 0x20, 70);
        check(time.value >= 80, 71);
        check(midiStreamPause(stream) == 0, 72);
        check(midiStreamRestart(stream) == 0, 73);
        check(midiStreamStop(stream) == 0, 74);
        check(midiOutUnprepareHeader(stream, &stream_header, sizeof stream_header) == 0, 75);
        check(midiStreamClose(stream) == 0, 76);
        check(closed == 1, 77);
    }
    wait(200);
    ExitProcess(0);
    return 0;
}
