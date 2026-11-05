/* SPDX-License-Identifier: GPL-2.0-or-later
 * DirectMusic from an x86 guest through its legacy MIDI port: the original
 * DMUSIC.DLL enumerates the ports, the output port of the WinMM driver class
 * is DMUSIC32.DLL's emulated port, and its 16-bit half DMUSIC16.DLL is ours
 * (src/x86_dmusic16.cpp). The fixture plays half a second of middle C
 * through IDirectMusicPort::PlayBuffer; tools/test_dmusic_guest.py checks the
 * samples the mixer captured.
 *
 * The exit code is 0 or the number of the check that failed. */
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef long HRESULT;
typedef unsigned long long REFERENCE_TIME;
typedef struct { DWORD a; unsigned short b, c; unsigned char d[8]; } GUID;
#define WINAPI __stdcall
__declspec(dllimport) void WINAPI ExitProcess(UINT);
__declspec(dllimport) void WINAPI Sleep(DWORD);
__declspec(dllimport) void WINAPI OutputDebugStringA(const char *);
__declspec(dllimport) HRESULT WINAPI CoInitialize(void *);
__declspec(dllimport) void WINAPI CoUninitialize(void);
__declspec(dllimport) HRESULT WINAPI CoCreateInstance(const GUID *, void *, DWORD, const GUID *, void **);

static const GUID CLSID_DirectMusic = {0x636b9f10, 0x0c7d, 0x11d1, {0x95, 0xb2, 0x00, 0x20, 0xaf, 0xdc, 0x74, 0x21}};
static const GUID IID_IDirectMusic = {0x6536115a, 0x7b2d, 0x11d2, {0xba, 0x18, 0x00, 0x00, 0xf8, 0x75, 0xac, 0x12}};

typedef void **Object;
#define METHOD(object, index) (((void **)*(object))[index])

static char line[160];
static void put(const char *text)
{
    unsigned i = 0;
    while (text[i] && i + 1 < sizeof line) { line[i] = text[i]; ++i; }
    line[i] = 0;
    OutputDebugStringA(line);
}
static void number(const char *label, DWORD value)
{
    static const char digits[] = "0123456789abcdef";
    unsigned i = 0, shift;
    while (label[i] && i < 100) { line[i] = label[i]; ++i; }
    line[i++] = '=';
    for (shift = 28;; shift -= 4) { line[i++] = digits[(value >> shift) & 15]; if (!shift) break; }
    line[i] = 0;
    OutputDebugStringA(line);
}
static void check(int ok, UINT code)
{
    if (!ok) { number("DMUSIC failed check", code); ExitProcess(code); }
}

/* DMUS_PORTCAPS of DirectX 6.1: dwSize, dwFlags, guidPort, dwClass, dwType,
   dwMemorySize, dwMaxChannelGroups, dwMaxVoices, dwMaxAudioChannels,
   dwEffectFlags, wszDescription[128]. */
typedef struct { DWORD size, flags; GUID port; DWORD klass, type, memory, groups, voices, audio, effects;
                 unsigned short description[128]; } PortCaps;
typedef struct { DWORD size, valid, voices, groups, audio, rate, effects, share; } PortParams;
typedef struct { DWORD size, flags; GUID format; DWORD bytes; } BufferDesc;

void mainCRTStartup(void)
{
    Object music = 0, port = 0, buffer = 0, clock = 0;
    PortCaps caps;
    PortParams params;
    BufferDesc desc;
    REFERENCE_TIME now = 0;
    DWORD index, found = 0;
    HRESULT hr;
    unsigned i;
    check(CoInitialize(0) >= 0, 1);
    hr = CoCreateInstance(&CLSID_DirectMusic, 0, 1 /* CLSCTX_INPROC_SERVER */, &IID_IDirectMusic, (void **)&music);
    number("DMUSIC create", hr);
    check(hr >= 0 && music, 2);
    /* IDirectMusic::SetDirectSound (11) with neither object nor window: the
       way applications start, DirectMusic makes its own when it needs one. */
    hr = ((HRESULT(WINAPI *)(Object, void *, void *))METHOD(music, 11))(music, 0, 0);
    number("DMUSIC setdirectsound", hr);
    check(hr >= 0, 12);
    /* IDirectMusic::EnumPort (3): the first output port of the WinMM class. */
    for (index = 0; index < 16 && !found; ++index)
    {
        char *p = (char *)&caps;
        for (i = 0; i < sizeof caps; ++i) p[i] = 0;
        caps.size = sizeof caps;
        hr = ((HRESULT(WINAPI *)(Object, DWORD, PortCaps *))METHOD(music, 3))(music, index, &caps);
        if (hr != 0) break;
        for (i = 0; i < 60 && caps.description[i]; ++i) line[i] = (char)caps.description[i];
        line[i] = 0;
        OutputDebugStringA(line);
        number("DMUSIC port class", caps.klass);
        number("DMUSIC port type", caps.type);
        if (caps.klass == 1 /* DMUS_PC_OUTPUTCLASS */ && caps.type == 0 /* DMUS_PORT_WINMM_DRIVER */) found = 1;
    }
    check(found, 3);
    {
        char *p = (char *)&params;
        for (i = 0; i < sizeof params; ++i) p[i] = 0;
        params.size = sizeof params;
    }
    /* IDirectMusic::CreatePort (5). */
    hr = ((HRESULT(WINAPI *)(Object, const GUID *, PortParams *, Object *, void *))METHOD(music, 5))(
        music, &caps.port, &params, &port, 0);
    number("DMUSIC createport", hr);
    check(hr >= 0 && port, 4);
    /* IDirectMusicPort::Activate (15). */
    hr = ((HRESULT(WINAPI *)(Object, int))METHOD(port, 15))(port, 1);
    number("DMUSIC activate", hr);
    check(hr >= 0, 5);
    {
        char *p = (char *)&desc;
        for (i = 0; i < sizeof desc; ++i) p[i] = 0;
        desc.size = sizeof desc;
        desc.bytes = 256;
    }
    /* IDirectMusic::CreateMusicBuffer (4). */
    hr = ((HRESULT(WINAPI *)(Object, BufferDesc *, Object *, void *))METHOD(music, 4))(music, &desc, &buffer, 0);
    number("DMUSIC buffer", hr);
    check(hr >= 0 && buffer, 6);
    /* IDirectMusicPort::GetLatencyClock (8), IReferenceClock::GetTime (3). */
    hr = ((HRESULT(WINAPI *)(Object, Object *))METHOD(port, 8))(port, &clock);
    check(hr >= 0 && clock, 7);
    hr = ((HRESULT(WINAPI *)(Object, REFERENCE_TIME *))METHOD(clock, 3))(clock, &now);
    check(hr >= 0, 8);
    /* IDirectMusicBuffer::PackStructured (5): note on, and off half a second
       later, middle C on channel 1 of group 1. */
    hr = ((HRESULT(WINAPI *)(Object, REFERENCE_TIME, DWORD, DWORD))METHOD(buffer, 5))(buffer, now, 1, 0x7f3c90);
    check(hr >= 0, 9);
    hr = ((HRESULT(WINAPI *)(Object, REFERENCE_TIME, DWORD, DWORD))METHOD(buffer, 5))(buffer, now + 5000000, 1,
                                                                                        0x003c80);
    check(hr >= 0, 10);
    /* IDirectMusicPort::PlayBuffer (3). */
    hr = ((HRESULT(WINAPI *)(Object, Object))METHOD(port, 3))(port, buffer);
    number("DMUSIC play", hr);
    check(hr >= 0, 11);
    Sleep(1500);
    ((DWORD(WINAPI *)(Object))METHOD(clock, 2))(clock);
    ((DWORD(WINAPI *)(Object))METHOD(buffer, 2))(buffer);
    ((HRESULT(WINAPI *)(Object, int))METHOD(port, 15))(port, 0);
    ((DWORD(WINAPI *)(Object))METHOD(port, 2))(port);
    ((DWORD(WINAPI *)(Object))METHOD(music, 2))(music);
    CoUninitialize();
    put("DMUSIC done");
    ExitProcess(0);
}
