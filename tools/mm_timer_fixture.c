/* SPDX-License-Identifier: GPL-2.0-or-later
 * The WINMM timer family, the device queries without a device and the error
 * texts, all over the public API. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateEventA(void *, U, U, const char *);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API void Sleep(U);
API U GetTickCount(void);
API U timeGetDevCaps(U *, U);
API U timeBeginPeriod(U);
API U timeEndPeriod(U);
API U timeSetEvent(U, U, void *, U, U);
API U timeKillEvent(U);
API U timeGetTime(void);
API U waveInGetNumDevs(void);
API U midiOutGetNumDevs(void);
API U midiInGetNumDevs(void);
API U mixerGetNumDevs(void);
API U auxGetNumDevs(void);
API U joyGetNumDevs(void);
API U waveInGetDevCapsA(U, void *, U);
API U midiOutGetDevCapsA(U, void *, U);
API U mixerGetDevCapsA(U, void *, U);
API U joyGetDevCapsA(U, void *, U);
API U joyGetPos(U, void *);
API U joyGetPosEx(U, void *);
API U waveOutGetVolume(U, U *);
API U waveOutSetVolume(U, U);
API U midiOutSetVolume(U, U);
API U midiOutGetVolume(U, U *);
API U waveOutGetErrorTextA(U, char *, U);
API U midiOutGetErrorTextA(U, char *, U);
API U mciGetErrorStringA(U, char *, U);
API U waveInOpen(U *, U, const void *, U, U, U);
API U mixerOpen(U *, U, U, U, U);
API U mixerClose(U);
API U OpenDriver(const char *, const char *, U);
API U OpenDriverA(const char *, const char *, U);
API U DrvOpen(const char *, const char *, U);
API U CloseDriver(U, U, U);
API U DrvClose(U, U, U);
API U SendDriverMessage(U, U, U, U);
API U DefDriverProc(U, U, U, U, U);
API U DrvDefDriverProc(U, U, U, U, U);
API U GetDriverModuleHandle(U);
API U DrvGetModuleHandle(U);
API U GetDriverFlags(U);
API U PlaySoundA(const char *, U, U);
API U mciSendStringA(const char *, char *, U, U);
API U mmioWrite(U, const char *, U);
#define TIME_ONESHOT 0u
#define TIME_PERIODIC 1u
#define TIME_CALLBACK_EVENT_SET 0x10u
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U once_calls = 0, once_id = 0, once_user = 0, once_extra = 0;
static U period_calls = 0, period_id = 0;
static void __stdcall one_shot(U id, U message, U user, U first, U second)
{
    ++once_calls;
    once_id = id;
    once_user = user;
    once_extra = message | first | second;
}
static void __stdcall periodic(U id, U message, U user, U first, U second)
{
    (void)message; (void)user; (void)first; (void)second;
    ++period_calls;
    period_id = id;
}
static U length(const char *text)
{
    U n = 0;
    while (text[n])
        ++n;
    return n;
}
void mainCRTStartup(void)
{
    U caps[4], id, event, volume, before, spent;
    char text[256];
    unsigned char block[512];

    /* timeGetDevCaps: the size test comes before the write probe, so a size
     * below eight answers 11 even for a good pointer, and a pointer the probe
     * rejects answers 129. */
    check(timeGetDevCaps(caps, 4) == 11, 43600);
    check(timeGetDevCaps(0, 8) == 129, 43601);
    caps[0] = caps[1] = 0xffffffff;
    check(timeGetDevCaps(caps, 8) == 0, 43602);
    check(caps[0] >= 1 && caps[0] <= caps[1], 43603);

    /* The resolution request goes to VTDAPI : a period
     * of zero is 97, and so is an EndPeriod VTD has nothing to release for.
     * From 36h up VTDAPI answers success and asks nothing, because 54 ms is
     * the slowest interrupt anyway, so even a period above the capabilities
     * succeeds. */
    check(timeBeginPeriod(0) == 97, 43610);
    check(timeBeginPeriod(caps[1] + 1) == 0, 43611);
    check(timeEndPeriod(caps[0]) == 97, 43612);
    check(timeBeginPeriod(caps[0]) == 0, 43613);
    check(timeBeginPeriod(caps[0]) == 0, 43614);
    check(timeEndPeriod(caps[0]) == 0, 43615);
    check(timeEndPeriod(caps[0]) == 0, 43616);
    check(timeEndPeriod(caps[0]) == 97, 43617);

    /* Flags outside 0F1h answer with the identifier zero and arm nothing. */
    check(timeSetEvent(10, 1, (void *)one_shot, 0, 0x100) == 0, 43620);
    check(timeSetEvent(10, 1, (void *)one_shot, 0, 2) == 0, 43621);
    check(timeSetEvent(10, 1, 0, 0, TIME_ONESHOT) == 0, 43622);

    /* One shot with a function callback. The identifier is the record, so its
     * high word is not zero, and the callback sees it back with dwUser. */
    id = timeSetEvent(20, 1, (void *)one_shot, 0x1234abcd, TIME_ONESHOT);
    check(id != 0 && (id >> 16) != 0, 43630);
    for (spent = 0; spent < 200 && once_calls == 0; ++spent)
        Sleep(5);
    check(once_calls == 1, 43631);
    check(once_id == id, 43632);
    check(once_user == 0x1234abcd, 43633);
    check(once_extra == 0, 43634);
    /* The record is gone with the call, so killing it again is 11. */
    check(timeKillEvent(id) == 11, 43635);
    Sleep(60);
    check(once_calls == 1, 43636);

    /* Periodic with a function callback, measured over its own clock. */
    before = timeGetTime();
    id = timeSetEvent(20, 1, (void *)periodic, 0, TIME_PERIODIC);
    check(id != 0, 43640);
    for (spent = 0; spent < 400 && period_calls < 5; ++spent)
        Sleep(5);
    check(period_calls >= 5, 43641);
    check(period_id == id, 43642);
    spent = timeGetTime() - before;
    /* Five periods of 20 ms cannot have passed in less than 80 ms. */
    check(spent >= 80, 43643);
    check(timeKillEvent(id) == 0, 43644);
    before = period_calls;
    Sleep(120);
    check(period_calls == before, 43645);

    /* The event callback type. Its identifier is a driver number, so the high
     * word is zero, and the driver signals the handle without a thread. */
    event = CreateEventA(0, 0, 0, 0);
    check(event != 0, 43650);
    id = timeSetEvent(20, 1, (void *)event, 0, TIME_CALLBACK_EVENT_SET);
    check(id != 0 && (id >> 16) == 0, 43651);
    check(WaitForSingleObject(event, 2000) == 0, 43652);
    check(CloseHandle(event) != 0, 43653);

    /* Identifiers that name nothing. */
    check(timeKillEvent(0) == 11, 43660);
    check(timeKillEvent(0x00ff0001) == 11, 43661);

    /* Recording, aux and the joystick have no device; the wave output and the
     * MIDI output and the mixer each have one. */
    check(waveInGetNumDevs() == 0, 43670);
    check(midiOutGetNumDevs() == 1, 43671);
    check(midiInGetNumDevs() == 0, 43672);
    check(mixerGetNumDevs() == 1, 43673);
    check(auxGetNumDevs() == 0, 43674);
    check(joyGetNumDevs() == 0, 43675);

    /* The capability queries answer a size of zero with success before
     * anything else, then reject the device. The mapper has no driver. */
    check(waveInGetDevCapsA(0, 0, 0) == 0, 43680);
    check(waveInGetDevCapsA(0, block, 50) == 2, 43681);
    check(waveInGetDevCapsA(~0u, block, 50) == 6, 43682);
    check(midiOutGetDevCapsA(1, block, 50) == 2, 43683);
    check(midiOutGetDevCapsA(0, block, 50) == 0, 43686);
    check(mixerGetDevCapsA(0, block, 84) == 0, 43684);
    check(waveInGetDevCapsA(0, 0, 50) == 11, 43685);

    /* Without VJOYD the joystick calls take the original's failure answer. */
    check(joyGetDevCapsA(0, block, 404) == 6, 43690);
    check(joyGetDevCapsA(0, block, 0) == 0, 43691);
    check(joyGetPos(0, block) == 6, 43692);
    *(U *)block = 0x34;
    check(joyGetPosEx(0, block) == 6, 43693);
    *(U *)block = 4;
    check(joyGetPosEx(0, block) == 11, 43694);

    /* A value that is not one of our handles fails the record test with 5; a
     * value with a zero high word is a device number, and since the mixer of
     * PLAN.md step 11 there is exactly one, so 1 is out of range and the wave
     * mapper answers for device 0. */
    check(waveOutGetVolume(1, &volume) == 2, 43700);
    check(waveOutGetVolume(~0u, &volume) == 0, 43701);
    check(waveOutGetVolume(0x00ff0001, &volume) == 5, 43702);
    check(waveOutSetVolume(0x00ff0001, 0) == 5, 43703);
    check(midiOutSetVolume(0x00ff0001, 0) == 5, 43704);
    check(midiOutGetVolume(1, &volume) == 2, 43706);
    check(midiOutGetVolume(~0u, &volume) == 0, 43707);
    check(waveOutGetVolume(1, 0) == 11, 43705);

    /* The error texts come out of the MMSYSTEM string table. */
    text[0] = 0;
    check(waveOutGetErrorTextA(6, text, sizeof text) == 0, 43710);
    check(length(text) > 10, 43711);
    check(waveOutGetErrorTextA(32, text, sizeof text) == 0, 43712);
    check(waveOutGetErrorTextA(64, text, sizeof text) == 9, 43713);
    check(midiOutGetErrorTextA(64, text, sizeof text) == 0, 43714);
    check(midiOutGetErrorTextA(32, text, sizeof text) == 9, 43715);
    check(waveOutGetErrorTextA(6, text, 0) == 11, 43716);
    /* A short buffer truncates and still terminates. */
    check(waveOutGetErrorTextA(6, text, 5) == 0, 43717);
    check(length(text) == 4, 43718);
    check(mciGetErrorStringA(287, text, sizeof text) == 1, 43719);
    check(length(text) > 10, 43720);
    check(mciGetErrorStringA(1, text, sizeof text) == 0, 43721);

    /* The parts PLAN.md holds back answer what the original answers without a
     * device or a driver. None of them pretends to succeed. */
    check(waveInOpen((U *)block, 0, block, 0, 0, 0) == 2, 43730);
    check(waveInOpen((U *)block, ~0u, block, 0, 0, 0) == 6, 43731);
    /* The mixer opens device 0 and closes the handle again. */
    *(U *)block = 0;
    check(mixerOpen((U *)block, 0, 0, 0, 0) == 0 && *(U *)block != 0, 43732);
    check(mixerClose(*(U *)block) == 0, 43737);
    check(OpenDriver("nosuchdriver", 0, 0) == 0, 43733);
    /* The installable driver family, WINMM and its neighbours. No
     * driver is registered, so opening finds nothing, every entry that takes a
     * driver handle rejects the zero handle, and the table walks answer zero.
     * The linker folded the Drv forms onto the same bodies. */
    check(OpenDriverA("nosuchdriver", 0, 0) == 0, 43740);
    check(DrvOpen("nosuchdriver", 0, 0) == 0, 43741);
    check(CloseDriver(0, 0, 0) == 0, 43742);
    check(DrvClose(0, 0, 0) == 0, 43743);
    check(SendDriverMessage(0, 1, 0, 0) == 0, 43744);
    check(GetDriverModuleHandle(0) == 0, 43745);
    check(DrvGetModuleHandle(0x1234) == 0, 43746);
    check(GetDriverFlags(0) == 0, 43747);
    /* DefDriverProc needs no driver at all: it answers out of the message
     * alone. One for DRV_LOAD, DRV_ENABLE, DRV_CLOSE, DRV_DISABLE,
     * DRV_FREE, DRV_INSTALL and DRV_REMOVE, zero for everything else. */
    check(DefDriverProc(0, 0, 1, 0, 0) == 1, 43750);
    check(DefDriverProc(0, 0, 2, 0, 0) == 1, 43751);
    check(DefDriverProc(0, 0, 3, 0, 0) == 0, 43752);
    check(DefDriverProc(0, 0, 4, 0, 0) == 1, 43753);
    check(DefDriverProc(0, 0, 5, 0, 0) == 1, 43754);
    check(DefDriverProc(0, 0, 6, 0, 0) == 1, 43755);
    check(DefDriverProc(0, 0, 7, 0, 0) == 0, 43756);
    check(DefDriverProc(0, 0, 8, 0, 0) == 0, 43757);
    check(DefDriverProc(0, 0, 9, 0, 0) == 1, 43758);
    check(DefDriverProc(0, 0, 10, 0, 0) == 1, 43759);
    check(DefDriverProc(0, 0, 11, 0, 0) == 0, 43760);
    check(DrvDefDriverProc(0, 0, 1, 0, 0) == 1, 43761);
    check(DefDriverProc(0, 0, 0, 0, 0) == 0, 43762);
    /* PlaySound has a device now; a name that is neither an alias nor a file
     * falls through to the default sound of the scheme, and the tree of the
     * test runner registers none, so the answer stays FALSE. */
    check(PlaySoundA("nosuchsound.wav", 0, 0) == 0, 43734);
    check(mciSendStringA("open nosuchdevice", text, sizeof text, 0) == 287, 43735);
    check(mmioWrite(0, "x", 1) == ~0u, 43736);

    ExitProcess(0);
}
