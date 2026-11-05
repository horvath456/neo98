/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_dsound_3d_guest.py: one mono 3D buffer of
   DirectSound, moved around the listener, over the original DSOUND.DLL.

   The buffer is a second of a 441 Hz sine and loops. The listener stays where
   DirectSound puts it, at the origin and looking along +z. Each phase lasts a
   second and starts with a mark in the log: in front, to the left, to the
   right, far away, and coming closer at 30 m/s. The runner measures the level
   of each channel, the delay between the channels and the pitch in what the
   mixer rendered; tools/test_dsound_3d_guest.py compares the run over the
   DirectSound driver with the run over the wave device.

   A second pass asks for the HRTF algorithm of DirectX 7 in the buffer
   description; a DirectSound that does not know the longer description
   refuses it, and the pass is left out. A third pass repeats the first with
   the speaker configuration set to headphones. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API void Sleep(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, U);
API U SetForegroundWindow(U);

typedef U(__stdcall *Create)(void *, void **, void *);
typedef U(__stdcall *Buffer2)(void *, void *, void **, void *);
typedef U(__stdcall *Level)(void *, U, U);
typedef U(__stdcall *Lock)(void *, U, U, void **, U *, void **, U *, U);
typedef U(__stdcall *Unlock)(void *, void *, U, void *, U);
typedef U(__stdcall *Play)(void *, U, U, U);
typedef U(__stdcall *Nothing)(void *);
typedef U(__stdcall *Word)(void *, U);
typedef U(__stdcall *Format)(void *, void *);
typedef U(__stdcall *Query)(void *, const void *, void **);
typedef U(__stdcall *Vector)(void *, float, float, float, U);

/* The compiler asks for this symbol of the C runtime in a program with floats. */
int _fltused = 0;

static char line[128];
static void number(const char *prefix, const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    U at = 0, i;
    const char *head = "D3D ";
    while (*head) line[at++] = *head++;
    while (*prefix) line[at++] = *prefix++;
    while (*label) line[at++] = *label++;
    line[at++] = '=';
    for (i = 0; i < 8; ++i) line[at++] = digits[(value >> (28 - i * 4)) & 15];
    line[at] = 0;
    OutputDebugStringA(line);
}
static void *entry(void *object, U offset)
{
    U *vtable = *(U **)object;
    return (void *)vtable[offset / 4];
}
static short format[9];
static void wave(U rate, U bits, U channels)
{
    const U align = channels * (bits / 8);
    format[0] = 1;
    format[1] = (short)channels;
    *(U *)(format + 2) = rate;
    *(U *)(format + 4) = rate * align;
    format[6] = (short)align;
    format[7] = (short)bits;
    format[8] = 0;
}
/* DSBUFFERDESC of DirectX 7: the five dwords of the first one and the GUID of
   the 3D algorithm. */
static U description[9];
static U window_class[10];
static U __stdcall procedure(U window, U message, U first, U second)
{
    return DefWindowProcA(window, message, first, second);
}
/* 441 Hz at 11.025 kHz, 25 samples a period. */
static const int period[25] = {0,   25,  48,  68,  84,  95,  100, 98,  90,  77,  59, 37, 13,
                               -13, -37, -59, -77, -90, -98, -100, -95, -84, -68, -48, -25};
/* IID_IDirectSound3DBuffer, 279AFA86-4981-11CE-A521-0020AF0BE560. */
static const unsigned char buffer_3d[16] = {0x86, 0xfa, 0x9a, 0x27, 0x81, 0x49, 0xce, 0x11,
                                            0xa5, 0x21, 0x00, 0x20, 0xaf, 0x0b, 0xe5, 0x60};
/* DS3DALG_HRTF_LIGHT, C2413342-1C1B-11D2-94F5-00C04FC28ACA. */
static const unsigned char hrtf_light[16] = {0x42, 0x33, 0x41, 0xc2, 0x1b, 0x1c, 0xd2, 0x11,
                                             0x94, 0xf5, 0x00, 0xc0, 0x4f, 0xc2, 0x8a, 0xca};

static void pass(void *sound, const char *prefix, const unsigned char *algorithm)
{
    void *buffer = 0, *space = 0, *first = 0, *second = 0;
    U first_bytes = 0, second_bytes = 0, value, i;
    const U bytes = 11025 * 2;
    wave(11025, 16, 1);
    description[0] = algorithm ? 36 : 20;
    description[1] = 0x10 | 0x8000; /* DSBCAPS_CTRL3D, DSBCAPS_GLOBALFOCUS */
    description[2] = bytes;
    description[3] = 0;
    description[4] = (U)format;
    for (i = 0; i < 16; ++i) ((unsigned char *)(description + 5))[i] = algorithm ? algorithm[i] : 0;
    value = ((Buffer2)entry(sound, 0x0c))(sound, description, &buffer, 0);
    number(prefix, "buffer", value);
    if ((int)value < 0 || !buffer) return;
    value = ((Query)entry(buffer, 0))(buffer, buffer_3d, &space);
    number(prefix, "space", value);
    if (value || !space) ExitProcess(20);
    value = ((Lock)entry(buffer, 0x2c))(buffer, 0, bytes, &first, &first_bytes, &second, &second_bytes, 0);
    if (value || !first) ExitProcess(21);
    for (i = 0; i < first_bytes / 2; ++i) ((short *)first)[i] = (short)(period[i % 25] * 160);
    ((Unlock)entry(buffer, 0x4c))(buffer, first, first_bytes, second, second_bytes);

    /* In front before the sound starts, so the first phase is steady too. */
    ((Vector)entry(space, 0x4c))(space, 0.0f, 0.0f, 1.0f, 0);
    value = ((Play)entry(buffer, 0x30))(buffer, 0, 0, 1);
    number(prefix, "play", value);
    if (value) ExitProcess(22);
    number(prefix, "mark", 0);
    Sleep(1000);
    ((Vector)entry(space, 0x4c))(space, -2.0f, 0.0f, 0.0f, 0);
    number(prefix, "mark", 1);
    Sleep(1000);
    ((Vector)entry(space, 0x4c))(space, 2.0f, 0.0f, 0.0f, 0);
    number(prefix, "mark", 2);
    Sleep(1000);
    ((Vector)entry(space, 0x4c))(space, 0.0f, 0.0f, 20.0f, 0);
    number(prefix, "mark", 3);
    Sleep(1000);
    /* Two metres in front and coming closer at 30 m/s: only the pitch moves,
       the position stays. */
    ((Vector)entry(space, 0x4c))(space, 0.0f, 0.0f, 2.0f, 0);
    ((Vector)entry(space, 0x50))(space, 0.0f, 0.0f, -30.0f, 0);
    number(prefix, "mark", 4);
    Sleep(1000);
    ((Nothing)entry(buffer, 0x48))(buffer);
    number(prefix, "mark", 5);
    ((Nothing)entry(space, 8))(space);
    ((Nothing)entry(buffer, 8))(buffer);
    Sleep(500);
}

void __stdcall mainCRTStartup(void)
{
    U library, value, window;
    void *sound = 0, *primary = 0;
    Create create;
    library = LoadLibraryA("DSOUND.DLL");
    if (!library) ExitProcess(1);
    create = (Create)GetProcAddress(library, "DirectSoundCreate");
    if (!create) ExitProcess(2);
    value = create(0, &sound, 0);
    number("", "create", value);
    if (value || !sound) ExitProcess(3);
    window_class[1] = (U)procedure;
    window_class[9] = (U) "NEO98DirectSound3D";
    RegisterClassA(window_class);
    window = CreateWindowExA(0, "NEO98DirectSound3D", "NEO98 DirectSound 3D", 0x10000000, 0, 0, 320, 200, 0, 0, 0, 0);
    if (!window) ExitProcess(4);
    ShowWindow(window, 1);
    SetForegroundWindow(window);
    value = ((Level)entry(sound, 0x18))(sound, window, 2);
    if (value) ExitProcess(5);
    /* The primary buffer with DSBCAPS_CTRL3D, which is where a game gets its
       listener from; it stays for the whole run. */
    description[0] = 20;
    description[1] = 1 | 0x10;
    value = ((Buffer2)entry(sound, 0x0c))(sound, description, &primary, 0);
    if (value || !primary) ExitProcess(6);
    wave(44100, 16, 2);
    value = ((Format)entry(primary, 0x38))(primary, format);
    number("", "primary_format", value);

    pass(sound, "plain_", 0);
    pass(sound, "hrtf_", hrtf_light);
    /* DSSPEAKER_HEADPHONE: the configuration for which a DirectSound may
       choose a delay between the ears instead of the balance. */
    value = ((Word)entry(sound, 0x24))(sound, 1);
    number("", "headphone", value);
    pass(sound, "phones_", 0);
    ((Word)entry(sound, 0x24))(sound, 4); /* DSSPEAKER_STEREO again */
    ((Nothing)entry(primary, 8))(primary);
    ((Nothing)entry(sound, 8))(sound);
    number("", "done", 1);
    ExitProcess(0);
}
