/* SPDX-License-Identifier: GPL-2.0-or-later */
/* queued headers and complete notifications beyond 64 blocks. */
#include <windows.h>
#include <mmsystem.h>
#define COUNT 257
static WAVEHDR headers[COUNT];
static unsigned char samples[880];
static volatile DWORD completed, wrong_header, seen[COUNT];
static HANDLE report;
static DWORD failures;
static void record(DWORD id, DWORD value) {
    DWORD row[2] = {id, value}, written;
    WriteFile(report, row, sizeof row, &written, 0);
    if (value) ++failures;
}
static void CALLBACK callback(HWAVEOUT wave, UINT message, DWORD_PTR instance,
                              DWORD_PTR header, DWORD_PTR unused) {
    (void)wave; (void)instance; (void)unused;
    if (message == WOM_DONE) {
        DWORD offset = header - (DWORD_PTR)headers;
        if (offset >= sizeof headers || offset % sizeof headers[0]) ++wrong_header;
        else if (++seen[offset / sizeof headers[0]] != 1) ++wrong_header;
        ++completed;
    }
}
void mainCRTStartup(void) {
    HWAVEOUT wave = 0;
    WAVEFORMATEX format = {WAVE_FORMAT_PCM, 1, 44100, 44100, 1, 8, 0};
    report = CreateFileA("C:\\WAVEQ.BIN", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (report == INVALID_HANDLE_VALUE) ExitProcess(98);
    MMRESULT result = waveOutOpen(&wave, WAVE_MAPPER, &format, (DWORD_PTR)callback, 0, CALLBACK_FUNCTION);
    record(1, result);
    if (result) { CloseHandle(report); ExitProcess(1); }
    for (DWORD i = 0; i < sizeof samples; ++i) samples[i] = 192;
    for (DWORD i = 0; i < COUNT; ++i) {
        headers[i].lpData = (LPSTR)samples;
        headers[i].dwBufferLength = sizeof samples;
        record(1000+i, waveOutPrepareHeader(wave, &headers[i], sizeof headers[i]));
    }
    for (DWORD pass = 0; pass < 2; ++pass) {
        completed = wrong_header = 0;
        for (DWORD i = 0; i < COUNT; ++i) seen[i] = 0;
        record(2+pass*10, waveOutPause(wave));
        for (DWORD i = 0; i < COUNT; ++i) {
            record(2000+pass*1000+i, waveOutWrite(wave, &headers[i], sizeof headers[i]));
            record(4000+pass*1000+i, (headers[i].dwFlags & (WHDR_INQUEUE|WHDR_DONE)) != WHDR_INQUEUE);
        }
        if (!pass) record(3, waveOutReset(wave));
        else record(13, waveOutRestart(wave));
        DWORD start = GetTickCount();
        while (completed < COUNT && GetTickCount()-start < 10000) Sleep(10);
        record(4+pass*10, completed != COUNT);
        record(5+pass*10, wrong_header);
        for (DWORD i = 0; i < COUNT; ++i)
            record(6000+pass*1000+i, (headers[i].dwFlags & (WHDR_INQUEUE|WHDR_DONE)) != WHDR_DONE);
    }
    record(20, waveOutReset(wave));
    for (DWORD i = 0; i < COUNT; ++i)
        record(8000+i, waveOutUnprepareHeader(wave, &headers[i], sizeof headers[i]));
    record(21, waveOutClose(wave));
    CloseHandle(report);
    ExitProcess(failures ? 1 : 0);
}
