// SPDX-License-Identifier: GPL-2.0-or-later
// Guest of tools/test_video_codecs.py: decompresses frames through the video
// compression manager of the tree (MSVFW32) and the original codecs of
// [drivers32] in SYSTEM.INI.
// C:\FRAMES.BIN holds one record per stream:
// DWORD fccHandler, DWORD frames, DWORD format bytes, DWORD bits of the
// output (24, or 8 with the stream's own colour table), the stream's
// BITMAPINFO (header and colour table, padded to a DWORD), then per frame
// DWORD bytes and the compressed data (padded to a DWORD).
// C:\DECODED.BIN gets per stream the DWORD result of ICOpen (nonzero or not),
// the answer of ICM_DECOMPRESS_QUERY and _BEGIN, and per frame the DWORD
// answer of ICM_DECOMPRESS and the bottom-up picture. The exit code of a
// failed run is the number of the failed check.
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
API void OutputDebugStringA(const char *);
API HANDLE CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
API int ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
API int WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
API int CloseHandle(HANDLE);
API DWORD GetFileSize(HANDLE, DWORD *);
API void *VirtualAlloc(void *, DWORD, DWORD, DWORD);
API HANDLE ICOpen(DWORD, DWORD, unsigned);
API long ICClose(HANDLE);
API long ICSendMessage(HANDLE, unsigned, DWORD, DWORD);
int _fltused;
// The compiler may turn the copies below into calls; the fixture links no C
// runtime, so it brings its own.
void *memcpy(void *to, const void *from, unsigned bytes)
{
    unsigned char *d = to;
    const unsigned char *f = from;
    while (bytes--)
        *d++ = *f++;
    return to;
}
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

static void check(int ok, unsigned code)
{
    if (!ok)
    {
        char text[32] = "CODECFIX fail ";
        text[14] = (char)('0' + code / 10);
        text[15] = (char)('0' + code % 10);
        text[16] = 0;
        OutputDebugStringA(text);
        ExitProcess(code);
    }
}

static HANDLE out;
static void put(const void *data, DWORD bytes)
{
    DWORD written = 0;
    check(WriteFile(out, data, bytes, &written, 0) && written == bytes, 10);
}

void mainCRTStartup(void)
{
    HANDLE in = CreateFileA("C:\\FRAMES.BIN", 0x80000000u, 1, 0, 3, 0, 0);
    check(in != (HANDLE)-1, 1);
    const DWORD size = GetFileSize(in, 0);
    unsigned char *data = VirtualAlloc(0, size, 0x3000, 4);
    check(data != 0, 2);
    DWORD read = 0;
    check(ReadFile(in, data, size, &read, 0) && read == size, 3);
    CloseHandle(in);
    out = CreateFileA("C:\\DECODED.BIN", 0x40000000u, 0, 0, 2, 0, 0);
    check(out != (HANDLE)-1, 4);
    unsigned char *picture = VirtualAlloc(0, 4 << 20, 0x3000, 4);
    check(picture != 0, 5);
    DWORD at = 0;
    while (at < size)
    {
        const DWORD handler = *(DWORD *)(data + at), frames = *(DWORD *)(data + at + 4);
        const DWORD format = *(DWORD *)(data + at + 8), bits = *(DWORD *)(data + at + 12);
        BITMAPINFOHEADER *input = (BITMAPINFOHEADER *)(data + at + 16);
        at += 16 + ((format + 3) & ~3u);
        // The output format: a header, and for 8 bits the colour table of the
        // stream behind it.
        static struct
        {
            BITMAPINFOHEADER header;
            DWORD colours[256];
        } out_format;
        BITMAPINFOHEADER output = {40, input->biWidth, input->biHeight, 1, (unsigned short)bits, 0, 0, 0, 0, 0, 0};
        output.biSizeImage = (DWORD)((((input->biWidth * bits + 31) & ~31) / 8) * input->biHeight);
        out_format.header = output;
        if (bits == 8)
        {
            const DWORD count = input->biClrUsed ? input->biClrUsed : 256;
            out_format.header.biClrUsed = count;
            for (DWORD i = 0; i < count; ++i)
                out_format.colours[i] = ((DWORD *)((unsigned char *)input + input->biSize))[i];
        }
        HANDLE codec = ICOpen(ICTYPE_VIDEO, handler, 2); // ICMODE_DECOMPRESS
        const DWORD opened = codec != 0;
        put(&opened, 4);
        long state = -1;
        if (codec)
        {
            state = ICSendMessage(codec, ICM_DECOMPRESS_QUERY, (DWORD)input, (DWORD)&out_format);
            if (!state)
                state = ICSendMessage(codec, ICM_DECOMPRESS_BEGIN, (DWORD)input, (DWORD)&out_format);
        }
        put(&state, 4);
        for (DWORD frame = 0; frame < frames; ++frame)
        {
            const DWORD bytes = *(DWORD *)(data + at);
            unsigned char *compressed = data + at + 4;
            at += 4 + ((bytes + 3) & ~3u);
            long answer = -1;
            if (codec && !state)
            {
                input->biSizeImage = bytes;
                ICDECOMPRESS job = {bytes ? 0 : 0x10000000u, input, compressed, &out_format.header, picture, 0};
                answer = ICSendMessage(codec, ICM_DECOMPRESS, (DWORD)&job, sizeof job);
            }
            put(&answer, 4);
            put(picture, out_format.header.biSizeImage);
        }
        if (codec)
        {
            if (!state)
                ICSendMessage(codec, ICM_DECOMPRESS_END, 0, 0);
            ICClose(codec);
        }
    }
    CloseHandle(out);
    OutputDebugStringA("CODECFIX done");
    ExitProcess(0);
}
