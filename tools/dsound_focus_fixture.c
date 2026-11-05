/* SPDX-License-Identifier: GPL-2.0-or-later */
#define COBJMACROS
#include <windows.h>
#include <dsound.h>
#include <stdio.h>

static unsigned failures;
#define CHECK(c) do { if (!(c)) { ++failures; printf("FAIL line=%u\n", __LINE__); } } while (0)

int main(void)
{
    WNDCLASSA wc = {0};
    IDirectSound *ds = NULL;
    WAVEFORMATEX wave = {WAVE_FORMAT_PCM, 1, 22050, 44100, 2, 16, 0};
    DWORD flags[3] = {0, DSBCAPS_GLOBALFOCUS, DSBCAPS_STICKYFOCUS};
    HWND window;
    unsigned mode, i;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "DsFocus";
    RegisterClassA(&wc);
    window = CreateWindowExA(0, wc.lpszClassName, "DsFocus", WS_OVERLAPPEDWINDOW,
                             20, 20, 300, 200, NULL, NULL, wc.hInstance, NULL);
    CHECK(window != NULL);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    CHECK(DirectSoundCreate(NULL, &ds, NULL) == DS_OK);
    if (!ds) return 1;
    CHECK(IDirectSound_SetCooperativeLevel(ds, window, DSSCL_NORMAL) == DS_OK);
    for (mode = 0; mode < 3; ++mode) {
        DSBUFFERDESC desc = {0};
        IDirectSoundBuffer *buffer = NULL;
        void *memory = NULL;
        DWORD bytes = 0, before, after, status;
        desc.dwSize = sizeof desc;
        desc.dwFlags = flags[mode] | DSBCAPS_CTRLVOLUME | DSBCAPS_LOCSOFTWARE;
        desc.dwBufferBytes = 44100 * 2;
        desc.lpwfxFormat = &wave;
        CHECK(IDirectSound_CreateSoundBuffer(ds, &desc, &buffer, NULL) == DS_OK);
        if (!buffer) return 2;
        CHECK(IDirectSoundBuffer_Lock(buffer, 0, desc.dwBufferBytes, &memory, &bytes, NULL, NULL, 0) == DS_OK);
        if (!memory) return 3;
        for (i = 0; i < bytes / 2; ++i) ((short *)memory)[i] = i % 100 < 50 ? 12000 : -12000;
        CHECK(IDirectSoundBuffer_Unlock(buffer, memory, bytes, NULL, 0) == DS_OK);
        CHECK(IDirectSoundBuffer_Play(buffer, 0, 0, DSBPLAY_LOOPING) == DS_OK);
        printf("FOCUS %u front\n", mode); fflush(stdout); Sleep(600);
        CHECK(IDirectSoundBuffer_GetCurrentPosition(buffer, &before, NULL) == DS_OK);
        printf("FOCUS %u back\n", mode); fflush(stdout); Sleep(600);
        CHECK(IDirectSoundBuffer_GetCurrentPosition(buffer, &after, NULL) == DS_OK);
        CHECK(before != after);
        CHECK(IDirectSoundBuffer_GetStatus(buffer, &status) == DS_OK && (status & DSBSTATUS_PLAYING));
        CHECK(IDirectSoundBuffer_SetVolume(buffer, -600) == DS_OK);
        printf("FOCUS %u quietback\n", mode); fflush(stdout); Sleep(600);
        printf("FOCUS %u return\n", mode); fflush(stdout); Sleep(600);
        CHECK(IDirectSoundBuffer_Stop(buffer) == DS_OK);
        printf("FOCUS %u stop\n", mode); fflush(stdout); Sleep(100);
        IDirectSoundBuffer_Release(buffer);
    }
    IDirectSound_Release(ds);
    DestroyWindow(window);
    printf("DSFOCUS failures=%u\n", failures);
    return failures ? 1 : 0;
}
