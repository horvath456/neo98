// SPDX-License-Identifier: GPL-2.0-or-later
// An x86 program that decompresses every frame of a film through the video
// compression manager of the tree (MSVFW32) and the installed codec, as fast
// as it goes: for Cinepak the original ICCVID.DLL, x86 code under FEXCore.
// The counterpart of `PLAYER.EXE /bench` for tools/measure_arm64_player.py.
//
// C:\FRAMES.BIN: DWORD fccHandler, DWORD frames, DWORD format bytes, the
// BITMAPINFOHEADER of the stream (padded to a DWORD), then per frame DWORD
// bytes and the compressed data (padded to a DWORD). C:\CVID.TXT gets what
// was measured.
typedef unsigned long DWORD;
typedef void *HANDLE;
typedef struct
{
    DWORD biSize;
    long biWidth, biHeight;
    unsigned short biPlanes, biBitCount;
    DWORD biCompression, biSizeImage;
    long biXPelsPerMeter, biYPelsPerMeter;
    DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;
typedef struct
{
    DWORD dwFlags;
    BITMAPINFOHEADER *lpbiInput;
    void *lpInput;
    BITMAPINFOHEADER *lpbiOutput;
    void *lpOutput;
    DWORD ckid;
} ICDECOMPRESS;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(unsigned);
API HANDLE CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
API int ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
API int WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
API int CloseHandle(HANDLE);
API DWORD GetFileSize(HANDLE, DWORD *);
API void *VirtualAlloc(void *, DWORD, DWORD, DWORD);
API int QueryPerformanceCounter(long long *);
API int QueryPerformanceFrequency(long long *);
API int lstrlenA(const char *);
API HANDLE ICOpen(DWORD, DWORD, unsigned);
API long ICClose(HANDLE);
API long ICSendMessage(HANDLE, unsigned, DWORD, DWORD);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);
int _fltused;
void *memset(void *to, int value, unsigned bytes)
{
    unsigned char *d = to;
    while (bytes--)
        *d++ = (unsigned char)value;
    return to;
}

#define ICM_DECOMPRESS_QUERY 0x400b
#define ICM_DECOMPRESS_BEGIN 0x400c
#define ICM_DECOMPRESS 0x400d
#define ICM_DECOMPRESS_END 0x400e
#define ICTYPE_VIDEO 0x63646976 // 'vidc'

static void Report(const char *text)
{
    DWORD written = 0;
    HANDLE out = CreateFileA("C:\\CVID.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(out, text, (DWORD)lstrlenA(text), &written, 0);
    CloseHandle(out);
}

void mainCRTStartup(void)
{
    char text[400];
    HANDLE in = CreateFileA("C:\\FRAMES.BIN", 0x80000000u, 1, 0, 3, 0, 0);
    const DWORD size = in != (HANDLE)-1 ? GetFileSize(in, 0) : 0;
    unsigned char *data = size ? VirtualAlloc(0, size, 0x3000, 4) : 0;
    DWORD read = 0, bits = 32, failed = 0, at;
    long long frequency = 1, before = 0, after = 0;
    long state = -1;
    if (!data || !ReadFile(in, data, size, &read, 0) || read != size)
    {
        Report("error=frames\r\n");
        ExitProcess(1);
    }
    CloseHandle(in);
    const DWORD handler = *(DWORD *)data, frames = *(DWORD *)(data + 4), format = *(DWORD *)(data + 8);
    BITMAPINFOHEADER *input = (BITMAPINFOHEADER *)(data + 12);
    BITMAPINFOHEADER output = {40, input->biWidth, input->biHeight, 1, 32, 0, 0, 0, 0, 0, 0};
    unsigned char *picture = VirtualAlloc(0, (DWORD)(input->biWidth * input->biHeight * 4 + 4096), 0x3000, 4);
    HANDLE codec = ICOpen(ICTYPE_VIDEO, handler, 2); // ICMODE_DECOMPRESS
    if (!codec || !picture)
    {
        Report("error=codec\r\n");
        ExitProcess(2);
    }
    // 32 bits for each pixel, as the ARM64 player decodes; 24 if the codec
    // does not offer that.
    for (;; bits = 24)
    {
        output.biBitCount = (unsigned short)bits;
        output.biSizeImage = (DWORD)((((input->biWidth * (long)bits + 31) & ~31) / 8) * input->biHeight);
        state = ICSendMessage(codec, ICM_DECOMPRESS_QUERY, (DWORD)input, (DWORD)&output);
        if (!state || bits == 24) break;
    }
    if (!state) state = ICSendMessage(codec, ICM_DECOMPRESS_BEGIN, (DWORD)input, (DWORD)&output);
    if (state)
    {
        wsprintfA(text, "error=begin %ld\r\n", state);
        Report(text);
        ExitProcess(3);
    }
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&before);
    at = 12 + ((format + 3) & ~3u);
    for (DWORD frame = 0; frame < frames; ++frame)
    {
        const DWORD bytes = *(DWORD *)(data + at);
        unsigned char *compressed = data + at + 4;
        at += 4 + ((bytes + 3) & ~3u);
        input->biSizeImage = bytes;
        ICDECOMPRESS job = {bytes ? 0 : 0x10000000u, input, compressed, &output, picture, 0};
        if (ICSendMessage(codec, ICM_DECOMPRESS, (DWORD)&job, sizeof job)) ++failed;
    }
    QueryPerformanceCounter(&after);
    ICSendMessage(codec, ICM_DECOMPRESS_END, 0, 0);
    ICClose(codec);
    // The counter values as they are: a 64-bit division would need a runtime
    // this fixture does not link. The measuring tool divides.
    const unsigned long long ticks = (unsigned long long)(after - before);
    DWORD sum = 0;
    for (DWORD i = 0; i < output.biSizeImage; i += 4099) sum += picture[i];
    wsprintfA(text, "frames=%lu\r\nfailed=%lu\r\nbits=%lu\r\nticks_high=%lu\r\nticks_low=%lu\r\nfrequency_high=%lu\r\nfrequency_low=%lu\r\nlast_picture_sample=%lu\r\ndone=1\r\n",
              frames, failed, bits, (DWORD)(ticks >> 32), (DWORD)ticks, (DWORD)((unsigned long long)frequency >> 32),
              (DWORD)frequency, sum);
    Report(text);
    ExitProcess(0);
}
