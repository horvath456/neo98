/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_dsound_capture_guest.py: DirectSoundCapture over
   the original DSOUND.DLL of the tree, on the test microphone.

   The fixture loads the module by hand and walks the public path a program
   walks: DirectSoundCaptureEnumerateA, DirectSoundCaptureCreate, GetCaps,
   CreateCaptureBuffer, Start with DSCBSTART_LOOPING, GetCurrentPosition until half a second is in,
   Lock and Unlock, Stop and Release, in 8-bit mono at 11.025 Hz and in
   16-bit stereo at 44.100 Hz. The interfaces are called through their
   vtables; the offsets are the ones of IDirectSoundCapture and
   IDirectSoundCaptureBuffer.

   Every observation goes out through OutputDebugStringA as "DSCAPTURE
   name=value" in hexadecimal, and the runner checks them; the samples the
   buffer holds are measured here, peak and zero crossings of the 441 Hz of
   the test microphone. The last word of the command line is the kind of
   the test device. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API void Sleep(U);
API U GetTickCount(void);
API const char *GetCommandLineA(void);

typedef U(__stdcall *Create)(void *, void **, void *);
typedef U(__stdcall *Enumerate)(void *, void *);
typedef U(__stdcall *Two)(void *, void *);
typedef U(__stdcall *Word)(void *, U);
typedef U(__stdcall *Nothing)(void *);
typedef U(__stdcall *MakeBuffer)(void *, void *, void **, void *);
typedef U(__stdcall *Cursor)(void *, U *, U *);
typedef U(__stdcall *Lock)(void *, U, U, void **, U *, void **, U *, U);
typedef U(__stdcall *Unlock)(void *, void *, U, void *, U);
typedef U(__stdcall *Format)(void *, void *, U, U *);

static char line[256];
static void number(const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    const char *head = "DSCAPTURE ";
    U at = 0, i = 0;
    while (head[i]) line[at++] = head[i++];
    i = 0;
    while (label[i]) line[at++] = label[i++];
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

static U devices, first_guid_null;
static U __stdcall Found(void *guid, const char *description, const char *module, void *context)
{
    (void)description;
    (void)module;
    (void)context;
    if (!devices) first_guid_null = guid == 0;
    ++devices;
    return 1;
}

static int sample(const unsigned char *frame, U bits, U lane)
{
    if (bits == 8) return ((int)frame[lane] - 128) * 256;
    return (short)(frame[lane * 2] | (frame[lane * 2 + 1] << 8));
}

/* One capture buffer of a second, looping, read after about seven tenths. */
static void record(void *capture, U rate, U bits, U channels, const char *name)
{
    /* DSCBUFFERDESC of DirectX 7: dwSize, dwFlags, dwBufferBytes,
       dwReserved, lpwfxFormat. */
    U description[5], caps[4], done = 0, read = 0, position = 0, status = 0, start, i;
    void *buffer = 0, *one = 0, *two = 0;
    U one_bytes = 0, two_bytes = 0, frame_bytes = channels * bits / 8;
    U crossings = 0, mismatched = 0;
    int peak = 0, previous = 0;
    char label[32];
    U at = 0;
    while (name[at]) label[at] = name[at], ++at;
#define LABEL(text)                                                                                \
    do                                                                                             \
    {                                                                                              \
        U k = 0;                                                                                   \
        while ((text)[k]) label[at + k] = (text)[k], ++k;                                          \
        label[at + k] = 0;                                                                         \
    } while (0)
    wave(rate, bits, channels);
    description[0] = sizeof description;
    description[1] = 0;
    description[2] = rate * frame_bytes;
    description[3] = 0;
    description[4] = (U)format;
    LABEL("_buffer");
    number(label, ((MakeBuffer)entry(capture, 0x0c))(capture, description, &buffer, 0));
    LABEL("_buffer_object");
    number(label, buffer != 0);
    if (!buffer) return;
    caps[0] = sizeof caps;
    LABEL("_caps");
    number(label, ((Two)entry(buffer, 0x0c))(buffer, caps));
    LABEL("_caps_bytes");
    number(label, caps[2]);
    LABEL("_start");
    number(label, ((Word)entry(buffer, 0x24))(buffer, 1));
    LABEL("_status");
    ((Two)entry(buffer, 0x18))(buffer, &status);
    number(label, status);
    /* Until the read cursor is past half a second, at most five seconds; a
       loaded host may give the guest less than its share. */
    done = rate / 2 * frame_bytes;
    start = GetTickCount();
    do
    {
        Sleep(20);
        LABEL("_position");
        status = ((Cursor)entry(buffer, 0x10))(buffer, &position, &read);
    } while (!status && read < done && GetTickCount() - start < 5000);
    number(label, status);
    LABEL("_read");
    number(label, read);
    /* Half a second from the start of the buffer, which the device has
       written by now. */
    LABEL("_lock");
    number(label, ((Lock)entry(buffer, 0x20))(buffer, 0, done, &one, &one_bytes, &two, &two_bytes, 0));
    LABEL("_lock_bytes");
    number(label, one_bytes);
    if (one && one_bytes)
    {
        const unsigned char *bytes = (const unsigned char *)one;
        previous = sample(bytes, bits, 0);
        for (i = 0; i < one_bytes / frame_bytes; ++i)
        {
            const int left = sample(bytes + i * frame_bytes, bits, 0);
            if (channels == 2 && sample(bytes + i * frame_bytes, bits, 1) != left) ++mismatched;
            if (left > peak) peak = left;
            if ((previous < 0) != (left < 0)) ++crossings;
            previous = left;
        }
    }
    LABEL("_peak");
    number(label, (U)peak);
    LABEL("_crossings");
    number(label, crossings);
    LABEL("_mismatched");
    number(label, mismatched);
    LABEL("_unlock");
    number(label, ((Unlock)entry(buffer, 0x2c))(buffer, one, one_bytes, two, two_bytes));
    LABEL("_stop");
    number(label, ((Nothing)entry(buffer, 0x28))(buffer));
    ((Two)entry(buffer, 0x18))(buffer, &status);
    LABEL("_status_end");
    number(label, status);
    LABEL("_release");
    number(label, ((Nothing)entry(buffer, 0x08))(buffer));
#undef LABEL
}

int mainCRTStartup(void)
{
    U module, caps[4], i;
    void *capture = 0;
    Create create;
    Enumerate enumerate;
    module = LoadLibraryA("DSOUND.DLL");
    number("loaded", module != 0);
    if (!module) ExitProcess(1);
    create = (Create)GetProcAddress(module, "DirectSoundCaptureCreate");
    enumerate = (Enumerate)GetProcAddress(module, "DirectSoundCaptureEnumerateA");
    number("found", create != 0 && enumerate != 0);
    if (!create || !enumerate) ExitProcess(2);
    number("enumerate", enumerate((void *)Found, 0));
    number("devices", devices);
    number("first_null", first_guid_null);
    number("create", create(0, &capture, 0));
    number("object", capture != 0);
    if (!capture)
    {
        number("done", 1);
        ExitProcess(0);
    }
    for (i = 0; i < 4; ++i) caps[i] = 0;
    caps[0] = sizeof caps;
    number("getcaps", ((Two)entry(capture, 0x10))(capture, caps));
    number("caps_flags", caps[1]);
    number("caps_formats", caps[2]);
    number("caps_channels", caps[3]);
    record(capture, 11025, 8, 1, "mono8");
    record(capture, 44100, 16, 2, "stereo16");
    number("release", ((Nothing)entry(capture, 0x08))(capture));
    number("done", 1);
    ExitProcess(0);
    return 0;
}
