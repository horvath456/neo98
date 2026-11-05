/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Public DirectSound notification behavior, shared by the x86 and ARM64 tests. */
#define COBJMACROS
#include <windows.h>
#include <dsound.h>

static HANDLE report;
static void Row(DWORD key, DWORD value)
{
    DWORD row[2] = {key, value}, written;
    if (!WriteFile(report, row, sizeof row, &written, NULL) || written != sizeof row) ExitProcess(3);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    WNDCLASSA klass = {0};
    HWND window;
    IDirectSound *ds = NULL;
    IDirectSoundBuffer *buffer = NULL, *plain = NULL;
    IDirectSoundNotify *notify = NULL;
    IUnknown *a = NULL, *b = NULL;
    WAVEFORMATEX wave = {0};
    DSBUFFERDESC desc = {0};
    DSBPOSITIONNOTIFY positions[3];
    HANDLE events[3];
    HRESULT hr;
    DWORD status;
    unsigned i;
    (void)previous; (void)command; (void)show;
    klass.lpfnWndProc = DefWindowProcA;
    klass.hInstance = instance;
    klass.lpszClassName = "DsNotify";
    RegisterClassA(&klass);
    window = CreateWindowExA(0, klass.lpszClassName, "DsNotify", WS_POPUP,
                             100, 100, 100, 100, NULL, NULL, instance, NULL);
    if (!window) return 1;
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    report = CreateFileA("E:\\NOTIFY.BIN", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (report == INVALID_HANDLE_VALUE) return 2;
    hr = DirectSoundCreate(NULL, &ds, NULL);
    Row(0, hr);
    if (hr != DS_OK) goto done;
    Row(1, IDirectSound_SetCooperativeLevel(ds, window, DSSCL_NORMAL));
    wave.wFormatTag = WAVE_FORMAT_PCM;
    wave.nChannels = 1;
    wave.nSamplesPerSec = 22050;
    wave.wBitsPerSample = 16;
    wave.nBlockAlign = 2;
    wave.nAvgBytesPerSec = 44100;
    desc.dwSize = sizeof desc;
    desc.dwFlags = DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS;
    desc.dwBufferBytes = 44100;
    desc.lpwfxFormat = &wave;
    hr = IDirectSound_CreateSoundBuffer(ds, &desc, &plain, NULL);
    Row(2, hr);
    if (hr == DS_OK) {
        Row(3, IDirectSoundBuffer_QueryInterface(plain, &IID_IDirectSoundNotify, (void **)&notify));
        if (notify) { IDirectSoundNotify_Release(notify); notify = NULL; }
        IDirectSoundBuffer_Release(plain);
    }
    desc.dwFlags |= DSBCAPS_CTRLPOSITIONNOTIFY;
    hr = IDirectSound_CreateSoundBuffer(ds, &desc, &buffer, NULL);
    Row(4, hr);
    if (hr != DS_OK) goto done;
    hr = IDirectSoundBuffer_QueryInterface(buffer, &IID_IDirectSoundNotify, (void **)&notify);
    Row(5, hr);
    if (hr != DS_OK) goto done;
    IDirectSoundBuffer_QueryInterface(buffer, &IID_IUnknown, (void **)&a);
    IDirectSoundNotify_QueryInterface(notify, &IID_IUnknown, (void **)&b);
    Row(6, a == b && a != NULL);
    if (a) IUnknown_Release(a);
    if (b) IUnknown_Release(b);
    for (i = 0; i < 3; ++i) {
        events[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
        if (!events[i]) return 4;
        positions[i].hEventNotify = events[i];
    }
    positions[0].dwOffset = 0;
    positions[1].dwOffset = 22050;
    positions[2].dwOffset = DSBPN_OFFSETSTOP;
    Row(7, IDirectSoundNotify_SetNotificationPositions(notify, 0, NULL));
    positions[0].dwOffset = 44100;
    Row(8, IDirectSoundNotify_SetNotificationPositions(notify, 1, positions));
    positions[0].dwOffset = 0;
    Row(9, IDirectSoundNotify_SetNotificationPositions(notify, 3, positions));
    Row(10, IDirectSoundBuffer_Play(buffer, 0, 0, 0));
    Row(11, IDirectSoundNotify_SetNotificationPositions(notify, 3, positions));
    for (i = 0; i < 3; ++i) Row(12 + i, WaitForSingleObject(events[i], 3000));
    status = ~0u;
    IDirectSoundBuffer_GetStatus(buffer, &status);
    Row(15, status & DSBSTATUS_PLAYING);
    Row(16, IDirectSoundBuffer_SetCurrentPosition(buffer, 0));
    Row(17, IDirectSoundBuffer_Play(buffer, 0, 0, DSBPLAY_LOOPING));
    Row(18, WaitForSingleObject(events[1], 3000));
    Row(19, WaitForSingleObject(events[1], 3000));
    Row(20, IDirectSoundBuffer_Stop(buffer));
    Row(21, WaitForSingleObject(events[2], 3000));
    Row(22, IDirectSoundNotify_SetNotificationPositions(notify, 0, NULL));
    for (i = 0; i < 3; ++i) CloseHandle(events[i]);
done:
    if (notify) IDirectSoundNotify_Release(notify);
    if (buffer) IDirectSoundBuffer_Release(buffer);
    if (ds) IDirectSound_Release(ds);
    CloseHandle(report);
    DestroyWindow(window);
    return 0;
}

#ifndef _WIN64
void mainCRTStartup(void) { ExitProcess(WinMain(GetModuleHandleA(NULL), NULL, NULL, 0)); }
#endif
