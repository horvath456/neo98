/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_mixer_guest.py: the mixer device of WINMM.

   It walks the mixer the way a program does that wants a volume: count the
   devices, read the capabilities, open the mixer, ask for the destination
   line and its source lines, list their controls, read the volume, set it and
   read it back, and check that the volume of the wave line and
   waveOutGetVolume of the device are the same value. Then it plays a square
   wave twice, once at full volume and once with the destination line at half,
   so the runner can see in the mixed samples that the control really moved
   the sound.

   Every answer goes out through the exit code of the step that failed; a run
   that finishes prints nothing and exits with zero. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U mixerGetNumDevs(void);
API U mixerGetDevCapsA(U, void *, U);
API U mixerOpen(U *, U, U, U, U);
API U mixerClose(U);
API U mixerGetID(U, U *, U);
API U mixerGetLineInfoA(U, void *, U);
API U mixerGetLineControlsA(U, void *, U);
API U mixerGetControlDetailsA(U, void *, U);
API U mixerSetControlDetails(U, void *, U);
API U mixerMessage(U, U, U, U);
API U waveOutGetVolume(U, U *);
API U waveOutSetVolume(U, U);
API U waveOutOpen(U *, U, const void *, U, U, U);
API U waveOutClose(U);
API U waveOutPrepareHeader(U, void *, U);
API U waveOutWrite(U, void *, U);
API U waveOutReset(U);

#define FAIL(n) ExitProcess(n)

static U caps[12];              /* MIXERCAPSA, 30h bytes */
static U line[42];              /* MIXERLINEA, 0A8h bytes */
static U controls[2][37];       /* two MIXERCONTROLA, 94h bytes each */
static U line_controls[6];      /* MIXERLINECONTROLS, 18h bytes */
static U details[6];            /* MIXERCONTROLDETAILS, 18h bytes */
static U values[2];
static unsigned char sound[8820];   /* 0.8 s of 8-bit mono at 11.025 kHz */
static U format[5];
static U header[8];

static const char *name_of(void *at) { return (const char *)at; }
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

/* One square wave period of 100 Hz at 11.025 kHz: 110 samples high, 110 low. */
static void build_sound(void)
{
    U i;
    for (i = 0; i < sizeof sound; ++i) sound[i] = (i % 220) < 110 ? 0xe0 : 0x20;
}

static U play(U volume_pair)
{
    U wave = 0, i;
    /* The destination line carries the volume of the output. */
    details[0] = 0x18;
    details[1] = 1;              /* dwControlID of the destination volume */
    details[2] = 2;              /* cChannels */
    details[3] = 0;
    details[4] = 4;
    details[5] = (U)values;
    values[0] = volume_pair & 0xffff;
    values[1] = (volume_pair >> 16) & 0xffff;
    if (mixerSetControlDetails(0, details, 0)) return 40;

    format[0] = 1 | (1u << 16);          /* wFormatTag PCM, nChannels 1 */
    format[1] = 11025;                   /* nSamplesPerSec */
    format[2] = 11025;                   /* nAvgBytesPerSec */
    format[3] = 1 | (8u << 16);          /* nBlockAlign 1, wBitsPerSample 8 */
    format[4] = 0;
    if (waveOutOpen(&wave, 0xffffffffu, format, 0, 0, 0)) return 41;
    for (i = 0; i < 8; ++i) header[i] = 0;
    header[0] = (U)sound;
    header[1] = sizeof sound;
    if (waveOutPrepareHeader(wave, header, 32)) return 42;
    if (waveOutWrite(wave, header, 32)) return 43;
    Sleep(1200);
    waveOutReset(wave);
    waveOutClose(wave);
    return 0;
}

void __stdcall mainCRTStartup(void)
{
    U mixer = 0, id = 0xffff, step;

    if (mixerGetNumDevs() != 1) FAIL(1);
    if (mixerGetDevCapsA(0, caps, 0x30)) FAIL(2);
    if (!same(name_of((unsigned char *)caps + 8), "NEO98 Mixer")) FAIL(3);
    if (caps[(8 + 32 + 4) / 4] != 1) FAIL(4);           /* cDestinations */
    if (mixerOpen(&mixer, 0, 0, 0, 0) || !mixer) FAIL(5);
    if (mixerGetID(mixer, &id, 0x80000000u) || id != 0) FAIL(6);

    /* The destination line: two controls and two source lines. */
    for (step = 0; step < 42; ++step) line[step] = 0;
    line[0] = 0xa8;
    line[1] = 0;                                        /* dwDestination */
    if (mixerGetLineInfoA(mixer, line, 0)) FAIL(7);
    if (line[6] != 0x00000004) FAIL(8);                 /* DST_SPEAKERS */
    if (line[7] != 2) FAIL(9);                          /* cChannels */
    if (line[8] != 2) FAIL(10);                         /* cConnections */
    if (line[9] != 2) FAIL(11);                         /* cControls */
    if (!same(name_of((unsigned char *)line + 0x38), "Volume Control")) FAIL(12);

    /* Its first source is the wave line. */
    for (step = 0; step < 42; ++step) line[step] = 0;
    line[0] = 0xa8;
    line[1] = 0;
    line[2] = 0;                                        /* dwSource */
    if (mixerGetLineInfoA(mixer, line, 1)) FAIL(13);
    if (line[6] != 0x00001008) FAIL(14);                /* SRC_WAVEOUT */
    if ((line[4] & 0x80000000u) == 0) FAIL(15);         /* MIXERLINE_LINEF_SOURCE */
    {
        const U wave_line = line[3];
        /* Its controls: a volume and a mute. */
        line_controls[0] = 0x18;
        line_controls[1] = wave_line;
        line_controls[2] = 0;
        line_controls[3] = 2;                           /* cControls */
        line_controls[4] = 0x94;
        line_controls[5] = (U)controls;
        if (mixerGetLineControlsA(mixer, line_controls, 0)) FAIL(16);
        if (controls[0][2] != 0x50030001) FAIL(17);     /* VOLUME */
        if (controls[1][2] != 0x20010002) FAIL(18);     /* MUTE */
        if (controls[0][25] != 0 || controls[0][26] != 0xffff) FAIL(19);  /* bounds */

        /* The volume of the wave line and waveOutGetVolume are one value. */
        details[0] = 0x18;
        details[1] = controls[0][1];
        details[2] = 2;
        details[3] = 0;
        details[4] = 4;
        details[5] = (U)values;
        values[0] = 0x4000;
        values[1] = 0x2000;
        if (mixerSetControlDetails(mixer, details, 0)) FAIL(20);
        values[0] = values[1] = 0;
        if (mixerGetControlDetailsA(mixer, details, 0)) FAIL(21);
        if (values[0] != 0x4000 || values[1] != 0x2000) FAIL(22);
        {
            U pair = 0;
            if (waveOutGetVolume(0, &pair)) FAIL(23);
            if (pair != (0x4000u | (0x2000u << 16))) FAIL(24);
            if (waveOutSetVolume(0, 0xffffffffu)) FAIL(25);
            values[0] = values[1] = 0;
            if (mixerGetControlDetailsA(mixer, details, 0)) FAIL(26);
            if (values[0] != 0xffff || values[1] != 0xffff) FAIL(27);
        }
        /* The mute of the wave line is one value, not two. */
        details[1] = controls[1][1];
        details[2] = 1;
        values[0] = 1;
        if (mixerSetControlDetails(mixer, details, 0)) FAIL(28);
        values[0] = 0;
        if (mixerGetControlDetailsA(mixer, details, 0)) FAIL(29);
        if (values[0] != 1) FAIL(30);
        values[0] = 0;
        if (mixerSetControlDetails(mixer, details, 0)) FAIL(31);
    }
    /* A line that does not exist, and a flag that does not. */
    for (step = 0; step < 42; ++step) line[step] = 0;
    line[0] = 0xa8;
    line[3] = 0x00090000;
    if (mixerGetLineInfoA(mixer, line, 2) != 1024) FAIL(32);   /* MIXERR_INVALLINE */
    if (mixerMessage(mixer, 1, 0, 0) != 8) FAIL(33);           /* MMSYSERR_NOTSUPPORTED */
    if (mixerClose(mixer)) FAIL(34);

    /* The sound itself, twice. The runner compares the two amplitudes. */
    build_sound();
    step = play(0xffffffffu);
    if (step) FAIL(step);
    step = play(0x40004000u);
    if (step) FAIL(step + 10);
    ExitProcess(0);
}
