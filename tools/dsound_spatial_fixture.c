/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* identical public-interface probe for original Win98 and ARM64. */
#define COBJMACROS
#include <windows.h>
#include <dsound.h>
#include <string.h>
static HANDLE output;
static unsigned row;
static void Word(DWORD value)
{
    DWORD n;
    WriteFile(output, &value, 4, &n, NULL);
    ++row;
}
static void Bytes(const void *p, unsigned size)
{
    const DWORD *w = p;
    for (unsigned n = 0; n < size / 4; ++n)
        Word(w[n]);
}
static void Snapshot(LPDIRECTSOUND3DBUFFER b)
{
    DS3DBUFFER p;
    memset(&p, 0xcc, sizeof p);
    p.dwSize = sizeof p;
    Word(IDirectSound3DBuffer_GetAllParameters(b, &p));
    Bytes(&p, sizeof p);
}
static void ListenerSnapshot(LPDIRECTSOUND3DLISTENER b)
{
    DS3DLISTENER p;
    memset(&p, 0xcc, sizeof p);
    p.dwSize = sizeof p;
    Word(IDirectSound3DListener_GetAllParameters(b, &p));
    Bytes(&p, sizeof p);
}
void mainCRTStartup(void)
{
    LPDIRECTSOUND ds = NULL;
    LPDIRECTSOUNDBUFFER primary = NULL, buffer = NULL, duplicate = NULL;
    LPDIRECTSOUND3DBUFFER b = NULL, copy = NULL;
    LPDIRECTSOUND3DLISTENER l = NULL;
    WAVEFORMATEX wave = {WAVE_FORMAT_PCM, 1, 44100, 88200, 2, 16, 0};
    DSBUFFERDESC desc;
    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    output = CreateFileA("E:\\SPATIAL.BIN", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    Word(DirectSoundCreate(NULL, &ds, NULL));
    if (!ds)
        ExitProcess(1);
    Word(IDirectSound_SetCooperativeLevel(ds, GetDesktopWindow(), DSSCL_NORMAL));
    desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
    Word(IDirectSound_CreateSoundBuffer(ds, &desc, &primary, NULL));
    if (!primary)
        ExitProcess(2);
    Word(IDirectSoundBuffer_QueryInterface(primary, &IID_IDirectSound3DListener, (void **)&l));
    if (!l)
        ExitProcess(3);
    ListenerSnapshot(l);
    desc.dwFlags = DSBCAPS_CTRL3D | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY | DSBCAPS_GLOBALFOCUS;
    desc.dwBufferBytes = 8820;
    desc.lpwfxFormat = &wave;
    Word(IDirectSound_CreateSoundBuffer(ds, &desc, &buffer, NULL));
    if (!buffer)
        ExitProcess(4);
    Word(IDirectSoundBuffer_QueryInterface(buffer, &IID_IDirectSound3DBuffer, (void **)&b));
    if (!b)
        ExitProcess(5);
    Snapshot(b);
    IUnknown *unknown = NULL;
    Word(IDirectSound3DBuffer_QueryInterface(b, &IID_IUnknown, (void **)&unknown));
    Word((void *)unknown == (void *)buffer);
    Word((void *)unknown == (void *)b);
    IUnknown *base = NULL;
    IDirectSoundBuffer_QueryInterface(buffer, &IID_IUnknown, (void **)&base);
    Word(base == unknown);
    if (base)
        IUnknown_Release(base);
    IUnknown_Release(unknown);
    Word(IDirectSound3DBuffer_SetPosition(b, 2, 3, 4, DS3D_DEFERRED));
    Snapshot(b);
    Word(IDirectSound3DBuffer_SetVelocity(b, 5, 6, 7, DS3D_IMMEDIATE));
    Snapshot(b);
    Word(IDirectSound3DListener_SetPosition(l, 10, 20, 30, DS3D_DEFERRED));
    ListenerSnapshot(l);
    Word(IDirectSound3DListener_SetDistanceFactor(l, 0.5f, DS3D_IMMEDIATE));
    ListenerSnapshot(l);
    Word(IDirectSound3DListener_CommitDeferredSettings(l));
    Snapshot(b);
    ListenerSnapshot(l);
    Word(IDirectSound3DBuffer_SetConeAngles(b, 30, 90, 0));
    Word(IDirectSound3DBuffer_SetConeOrientation(b, 1, 2, 3, 0));
    Word(IDirectSound3DBuffer_SetConeOutsideVolume(b, -3000, 0));
    Word(IDirectSound3DBuffer_SetMinDistance(b, 2, 0));
    Word(IDirectSound3DBuffer_SetMaxDistance(b, 9, 0));
    Word(IDirectSound3DBuffer_SetMode(b, DS3DMODE_HEADRELATIVE, 0));
    Snapshot(b);
    Word(IDirectSound_DuplicateSoundBuffer(ds, buffer, &duplicate));
    if (duplicate)
    {
        Word(IDirectSoundBuffer_QueryInterface(duplicate, &IID_IDirectSound3DBuffer, (void **)&copy));
        if (copy)
        {
            Snapshot(copy);
            IDirectSound3DBuffer_Release(copy);
        }
        IDirectSoundBuffer_Release(duplicate);
    }
    Word(IDirectSound3DBuffer_SetConeAngles(b, 361, 360, 0));
    Word(IDirectSound3DBuffer_SetConeAngles(b, 90, 30, 0));
    Word(IDirectSound3DBuffer_SetConeOrientation(b, 0, 0, 0, 0));
    Word(IDirectSound3DBuffer_SetConeOutsideVolume(b, 1, 0));
    Word(IDirectSound3DBuffer_SetMinDistance(b, 0, 0));
    Word(IDirectSound3DBuffer_SetMaxDistance(b, 0, 0));
    Word(IDirectSound3DBuffer_SetMode(b, 3, 0));
    Word(IDirectSound3DBuffer_SetPosition(b, 1, 2, 3, 2));
    Word(IDirectSound3DListener_SetDistanceFactor(l, 0, 0));
    Word(IDirectSound3DListener_SetRolloffFactor(l, 11, 0));
    Word(IDirectSound3DListener_SetDopplerFactor(l, -1, 0));
    Word(IDirectSound3DListener_SetOrientation(l, 0, 0, 0, 0, 0, 0, 0));
    Word(IDirectSound3DBuffer_GetAllParameters(b, NULL));
    Word(IDirectSound3DListener_GetAllParameters(l, NULL));
    Word(IDirectSound3DBuffer_SetAllParameters(b, NULL, 0));
    Word(IDirectSound3DListener_SetAllParameters(l, NULL, 0));
    IDirectSound3DBuffer_Release(b);
    IDirectSoundBuffer_Release(buffer);
    IDirectSound3DListener_Release(l);
    IDirectSoundBuffer_Release(primary);
    for (unsigned i = 0; i < 3; ++i)
    {
        buffer = NULL;
        desc.dwFlags = DSBCAPS_CTRL3D | (i == 0 ? DSBCAPS_CTRLPAN : i == 1 ? DSBCAPS_LOCHARDWARE : 0);
        wave.nChannels = i == 2 ? 2 : 1;
        wave.nBlockAlign = wave.nChannels * 2;
        wave.nAvgBytesPerSec = 44100 * wave.nBlockAlign;
        Word(IDirectSound_CreateSoundBuffer(ds, &desc, &buffer, NULL));
        if (buffer)
            IDirectSoundBuffer_Release(buffer);
    }
    IDirectSound_Release(ds);
    CloseHandle(output);
    ExitProcess(0);
}
