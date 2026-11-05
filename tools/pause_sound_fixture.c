/* SPDX-License-Identifier: GPL-2.0-or-later
   The guests of tools/test_pause_sound_host.py : one sound
   path each, chosen when it is compiled, played across a pause of the
   machine.
     WAY 1 DirectSound: a looping secondary buffer of four seconds with a
            tone, and its play cursor
     WAY 2 CD audio: MCI cdaudio plays track 2, and its position in
            milliseconds
     WAY 3 MIDI: a stream with a note of three seconds, a rest, a short
            note, and its position in milliseconds
   The program reads the position and GetTickCount on a timer of 50 ms, keeps
   the largest step of the position between two timer messages, and after
   five seconds of its tick count writes to D:\PSOUND.TXT how far the position
   and the tick count went. */
typedef unsigned U;
typedef struct { U style; void *proc; int a, b; U instance, icon, cursor, brush; const char *menu, *name; } WNDCLASSA;
typedef struct { U hwnd, message, wparam, lparam, time; int x, y; } MSG;
#define I(r, n, ...) __declspec(dllimport) r __stdcall n(__VA_ARGS__)
I(U, RegisterClassA, const WNDCLASSA *);
I(U, CreateWindowExA, U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
I(U, DefWindowProcA, U, U, U, U);
I(int, GetMessageA, MSG *, U, U, U);
I(U, DispatchMessageA, const MSG *);
I(int, ShowWindow, U, int);
I(int, SetForegroundWindow, U);
I(int, SetWindowTextA, U, const char *);
I(U, SetTimer, U, U, U, void *);
I(int, KillTimer, U, U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);
I(U, GetStockObject, int);
I(U, GetModuleHandleA, const char *);
I(U, GetTickCount, void);
I(U, CreateFileA, const char *, U, U, void *, U, U, U);
I(int, WriteFile, U, const void *, U, U *, void *);
I(int, CloseHandle, U);
I(void, ExitProcess, U);

static U window, failed, tick0, position0, position_last, position_step, samples;
static int done;

#if WAY == 1
I(U, DirectSoundCreate, void *, void **, void *);
#define CALL(object, offset, ...) (((U(__stdcall *)())((*(U **)(object))[(offset) / 4]))(object, ##__VA_ARGS__))
#define TOTAL 176400u   /* four seconds of 16-bit mono at 22050 Hz */
static void *sound, *buffer;
static U cursor_last, cursor_sum;
static short format[9];
static U description[5];
static U Start(void)
{
    void *first = 0, *second = 0;
    U first_bytes = 0, second_bytes = 0, i;
    format[0] = 1; format[1] = 1;
    *(U *)(format + 2) = 22050; *(U *)(format + 4) = 44100;
    format[6] = 2; format[7] = 16;
    if (DirectSoundCreate(0, &sound, 0)) return 1;
    if (CALL(sound, 0x18, window, 2)) return 2;                           /* SetCooperativeLevel, DSSCL_PRIORITY */
    description[0] = sizeof description;
    description[1] = 0x10000 | 0x8000;                                    /* GETCURRENTPOSITION2 | GLOBALFOCUS */
    description[2] = TOTAL;
    description[4] = (U)format;
    if (CALL(sound, 0x0c, description, &buffer, 0)) return 3;             /* CreateSoundBuffer */
    if (CALL(buffer, 0x2c, 0, TOTAL, &first, &first_bytes, &second, &second_bytes, 0) || !first) return 4;   /* Lock */
    /* 441 Hz, a square of 50 frames. */
    for (i = 0; i < first_bytes / 2; ++i) ((short *)first)[i] = (short)(i % 50 < 25 ? 12000 : -12000);
    CALL(buffer, 0x4c, first, first_bytes, second, second_bytes);         /* Unlock */
    if (CALL(buffer, 0x30, 0, 0, 1)) return 5;                            /* Play, DSBPLAY_LOOPING */
    return 0;
}
/* The play cursor in milliseconds, counted on over the end of the buffer. */
static U Position(void)
{
    U play = 0, write = 0;
    if (CALL(buffer, 0x10, &play, &write)) { failed = 6; return 0; }      /* GetCurrentPosition */
    cursor_sum += (play + TOTAL - cursor_last) % TOTAL;
    cursor_last = play;
    return cursor_sum * 10 / 441;   /* 44.1 bytes a millisecond */
}
#elif WAY == 2
I(U, mciSendCommandA, U, U, U, U);
struct Open { U callback, device; const char *type, *element, *alias; };
struct Set { U callback, time_format, audio; };
struct Status { U callback, value, item, track; };
struct Play { U callback, from, to; };
static U device;
static U Start(void)
{
    struct Open open = {0};
    struct Set set = {0};
    struct Play play = {0};
    open.type = "cdaudio";
    if (mciSendCommandA(0, 0x803, 0x2000 | 2, (U)&open)) return 1;        /* MCI_OPEN, MCI_OPEN_TYPE | MCI_WAIT */
    device = open.device;
    set.time_format = 10;                                                 /* MCI_FORMAT_TMSF */
    if (mciSendCommandA(device, 0x80d, 0x400 | 2, (U)&set)) return 2;     /* MCI_SET, MCI_SET_TIME_FORMAT */
    play.from = 2;                                                        /* track 2, 0:00:00 */
    if (mciSendCommandA(device, 0x806, 4, (U)&play)) return 3;            /* MCI_PLAY, MCI_FROM */
    set.time_format = 0;                                                  /* MCI_FORMAT_MILLISECONDS */
    if (mciSendCommandA(device, 0x80d, 0x400 | 2, (U)&set)) return 4;
    return 0;
}
static U Position(void)
{
    struct Status status = {0};
    status.item = 2;                                                      /* MCI_STATUS_POSITION */
    if (mciSendCommandA(device, 0x814, 0x100 | 2, (U)&status)) { failed = 6; return 0; }
    return status.value;
}
#else
I(U, midiStreamOpen, U *, U *, U, U, U, U);
I(U, midiStreamProperty, U, void *, U);
I(U, midiStreamOut, U, void *, U);
I(U, midiStreamRestart, U);
I(U, midiStreamPosition, U, void *, U);
I(U, midiOutPrepareHeader, U, void *, U);
struct MidiHeader { char *data; U length, recorded, user, flags; void *next; U reserved, offset, spare[8]; };
struct Property { U size, value; };
struct Time { U type, value, rest; };
static U stream;
static struct MidiHeader header;
/* 96 ticks to the quarter note and 500.000 microseconds a quarter note: 192
   ticks a second. An organ note of three seconds, a rest of one, a note of
   0.3 seconds, and nothing (MEVT_NOP) at six seconds, so the stream runs on
   past the end of the measurement. */
static U events[] = {
    0, 0, 0x000013c0u,
    0, 0, 0x006407b0u,
    0, 0, 0x007f4590u,
    576, 0, 0x00004580u,
    192, 0, 0x007f4590u,
    58, 0, 0x00004580u,
    326, 0, 0x02000000u,
};
static U Start(void)
{
    struct Property property;
    U id = 0;
    if (midiStreamOpen(&stream, &id, 1, 0, 0, 0)) return 1;
    property.size = sizeof property;
    property.value = 96;
    if (midiStreamProperty(stream, &property, 0x80000001u)) return 2;      /* MIDIPROP_SET | MIDIPROP_TIMEDIV */
    property.value = 500000;
    if (midiStreamProperty(stream, &property, 0x80000002u)) return 3;      /* MIDIPROP_SET | MIDIPROP_TEMPO */
    header.data = (char *)events;
    header.length = header.recorded = sizeof events;
    if (midiOutPrepareHeader(stream, &header, sizeof header)) return 4;
    if (midiStreamOut(stream, &header, sizeof header)) return 5;
    if (midiStreamRestart(stream)) return 6;
    return 0;
}
static U Position(void)
{
    struct Time time = {0};
    time.type = 1;                                                        /* TIME_MS */
    if (midiStreamPosition(stream, &time, sizeof time) || time.type != 1) { failed = 7; return 0; }
    return time.value;
}
#endif

static void Finish(void)
{
    static char text[400];
    U file, written, n;
    const U position = Position();
    n = wsprintfA(text, "failed=%u\r\ntick_elapsed=%u\r\nposition_elapsed=%u\r\nposition_step=%u\r\nsamples=%u\r\n"
                        "position_first=%u\r\nposition_last=%u\r\n",
                  failed, GetTickCount() - tick0, position - position0, position_step, samples, position0, position);
    file = CreateFileA("D:\\PSOUND.TXT", 0x40000000, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, n, &written, 0);
    CloseHandle(file);
    SetWindowTextA(window, "Sound done");
}
static U __stdcall Procedure(U hwnd, U message, U wparam, U lparam)
{
    if (message == 0x113 && !done)   /* WM_TIMER */
    {
        const U position = Position();
        ++samples;
        if (position - position_last > position_step) position_step = position - position_last;
        position_last = position;
        if (GetTickCount() - tick0 >= 5000)
        {
            done = 1;
            KillTimer(hwnd, 1);
            Finish();
        }
        return 0;
    }
    if (message == 2) ExitProcess(0);
    return DefWindowProcA(hwnd, message, wparam, lparam);
}
void __stdcall mainCRTStartup(void)
{
    WNDCLASSA c = {0};
    MSG message;
    c.proc = Procedure;
    c.instance = GetModuleHandleA(0);
    c.brush = GetStockObject(0);
    c.name = "PauseSoundFixture";
    RegisterClassA(&c);
    window = CreateWindowExA(0, "PauseSoundFixture", "Sound", 0x00cf0000, 40, 40, 400, 300, 0, 0, c.instance, 0);
    ShowWindow(window, 1);
    SetForegroundWindow(window);
    failed = Start();
    if (failed)
    {
        char text[32];
        wsprintfA(text, "Sound failed %u", failed);
        SetWindowTextA(window, text);
    }
    else
    {
        tick0 = GetTickCount();
        position0 = position_last = Position();
        SetTimer(window, 1, 50, 0);
        SetWindowTextA(window, "Sound ready");
    }
    while (GetMessageA(&message, 0, 0, 0) > 0) DispatchMessageA(&message);
    ExitProcess(0);
}
