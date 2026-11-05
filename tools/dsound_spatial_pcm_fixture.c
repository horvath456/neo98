/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#define COBJMACROS
#include <windows.h>
#include <dsound.h>
#include <math.h>
#include <stdio.h>
static unsigned failures;
#define CHECK(c)                                                                                                                           \
    do                                                                                                                                     \
    {                                                                                                                                      \
        if (!(c))                                                                                                                          \
        {                                                                                                                                  \
            ++failures;                                                                                                                    \
            printf("FAIL line=%u\n", __LINE__);                                                                                            \
        }                                                                                                                                  \
    } while (0)
int main(void)
{
    IDirectSound *ds = NULL;
    IDirectSoundBuffer *primary = NULL, *buffer = NULL;
    IDirectSound3DBuffer *spatial = NULL;
    IDirectSound3DListener *listener = NULL;
    WAVEFORMATEX wave = {WAVE_FORMAT_PCM, 1, 44100, 88200, 2, 16, 0};
    DSBUFFERDESC desc = {0};
    void *memory = NULL;
    DWORD size = 0;
    DS3DBUFFER b = {0};
    DS3DLISTENER l = {0};
    CHECK(DirectSoundCreate(NULL, &ds, NULL) == DS_OK);
    if (!ds)
        return 1;
    CHECK(IDirectSound_SetCooperativeLevel(ds, GetDesktopWindow(), DSSCL_PRIORITY) == DS_OK);
    desc.dwSize = sizeof desc;
    desc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
    CHECK(IDirectSound_CreateSoundBuffer(ds, &desc, &primary, NULL) == DS_OK);
    if (!primary)
        return 2;
    {
        WAVEFORMATEX format = {WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0};
        CHECK(IDirectSoundBuffer_SetFormat(primary, &format) == DS_OK);
    }
    CHECK(IDirectSoundBuffer_QueryInterface(primary, &IID_IDirectSound3DListener, (void **)&listener) == DS_OK);
    if (!listener)
        return 3;
    desc.dwFlags = DSBCAPS_CTRL3D | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY | DSBCAPS_GLOBALFOCUS | DSBCAPS_MUTE3DATMAXDISTANCE;
    desc.dwBufferBytes = 88200;
    desc.lpwfxFormat = &wave;
    CHECK(IDirectSound_CreateSoundBuffer(ds, &desc, &buffer, NULL) == DS_OK);
    if (!buffer)
        return 4;
    CHECK(IDirectSoundBuffer_QueryInterface(buffer, &IID_IDirectSound3DBuffer, (void **)&spatial) == DS_OK);
    if (!spatial)
        return 5;
    CHECK(IDirectSoundBuffer_Lock(buffer, 0, 88200, &memory, &size, NULL, NULL, 0) == DS_OK);
    for (unsigned n = 0; n < size / 2; ++n)
        ((short *)memory)[n] = (short)(16000 * sin(n * 6.283185307179586 / 100));
    CHECK(IDirectSoundBuffer_Unlock(buffer, memory, size, NULL, 0) == DS_OK);
    CHECK(IDirectSoundBuffer_Play(buffer, 0, 0, DSBPLAY_LOOPING) == DS_OK);
    for (unsigned mode = 0; mode < 16; ++mode)
    {
        b.dwSize = sizeof b;
        IDirectSound3DBuffer_GetAllParameters(spatial, &b);
        b.vPosition.x = b.vPosition.y = 0;
        b.vPosition.z = 1;
        b.vVelocity.x = b.vVelocity.y = b.vVelocity.z = 0;
        b.dwInsideConeAngle = b.dwOutsideConeAngle = 360;
        b.vConeOrientation.x = b.vConeOrientation.y = 0;
        b.vConeOrientation.z = 1;
        b.lConeOutsideVolume = 0;
        b.flMinDistance = 1;
        b.flMaxDistance = 1000000000;
        b.dwMode = DS3DMODE_NORMAL;
        l.dwSize = sizeof l;
        IDirectSound3DListener_GetAllParameters(listener, &l);
        l.vPosition.x = l.vPosition.y = l.vPosition.z = 0;
        l.vVelocity.x = l.vVelocity.y = l.vVelocity.z = 0;
        l.vOrientFront.x = l.vOrientFront.y = 0;
        l.vOrientFront.z = 1;
        l.vOrientTop.x = l.vOrientTop.z = 0;
        l.vOrientTop.y = 1;
        l.flDistanceFactor = l.flRolloffFactor = l.flDopplerFactor = 1;
        if (mode == 1 || mode == 5)
            b.vPosition.z = 2;
        if (mode == 2)
            b.vPosition.z = 20;
        if (mode == 3 || mode == 4)
        {
            b.vPosition.x = mode == 3 ? -2 : 2;
            b.vPosition.z = 0;
        }
        if (mode == 5)
            b.vVelocity.z = -30;
        if (mode == 6)
        {
            b.dwInsideConeAngle = 30;
            b.dwOutsideConeAngle = 60;
            b.lConeOutsideVolume = -2000;
        }
        if (mode == 7)
        {
            b.vPosition.z = 20;
            l.flRolloffFactor = 0;
        }
        if (mode == 8)
        {
            b.vPosition.z = 20;
            b.dwMode = DS3DMODE_DISABLE;
        }
        if (mode == 9)
        {
            b.vPosition.z = 20;
            b.flMaxDistance = 10;
        }
        if (mode == 10 || mode == 11)
        {
            b.vPosition.z = 2;
            l.vPosition.z = 1;
            if (mode == 11)
                b.dwMode = DS3DMODE_HEADRELATIVE;
        }
        if (mode == 12)
        {
            b.vPosition.x = 2;
            b.vPosition.z = 0;
            l.vOrientFront.x = 1;
            l.vOrientFront.z = 0;
        }
        CHECK(IDirectSound3DBuffer_SetAllParameters(spatial, &b, DS3D_IMMEDIATE) == DS_OK);
        CHECK(IDirectSound3DListener_SetAllParameters(listener, &l, DS3D_IMMEDIATE) == DS_OK);
        if (mode == 13 || mode == 14)
        {
            CHECK(IDirectSound3DBuffer_SetPosition(spatial, 0, 0, 20, DS3D_DEFERRED) == DS_OK);
            if (mode == 14)
                CHECK(IDirectSound3DListener_CommitDeferredSettings(listener) == DS_OK);
        }
        if (mode == 15)
        {
            b.vPosition.x = 1;
            b.vPosition.z = 1;
            CHECK(IDirectSound3DBuffer_SetAllParameters(spatial, &b, 0) == DS_OK);
        }
        printf("FOCUS %u front\n", mode);
        fflush(stdout);
        Sleep(600);
        printf("FOCUS %u stop\n", mode);
        fflush(stdout);
        Sleep(20);
    }
    IDirectSoundBuffer_Stop(buffer);
    IDirectSound3DBuffer_Release(spatial);
    IDirectSoundBuffer_Release(buffer);
    IDirectSound3DListener_Release(listener);
    IDirectSoundBuffer_Release(primary);
    IDirectSound_Release(ds);
    printf("DS3DPCM failures=%u\n", failures);
    return failures != 0;
}
