/* SPDX-License-Identifier: GPL-2.0-or-later
   For the fixtures that tools/test_pause_graphics_host.py runs across a pause
   of the machine, compiled in with FRAMETIMES: frame_time
   once a frame counts the frames and keeps the longest time between two of
   them by timeGetTime. While the key W is down it appends both to
   D:\PFRAME.TXT, once a press, and starts the longest time anew. */
API U timeGetTime(void);
API U CreateFileA(const char *, U, U, void *, U, U, U);
API U SetFilePointer(U, int, int *, U);
API int WriteFile(U, const void *, U, U *, void *);
API int CloseHandle(U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

static U frame_time_count, frame_time_last, frame_time_longest, frame_time_key;
static void frame_time(void)
{
    const U now = timeGetTime();
    if (frame_time_count && now - frame_time_last > frame_time_longest) frame_time_longest = now - frame_time_last;
    frame_time_last = now;
    ++frame_time_count;
    if (!(GetAsyncKeyState('W') & 0x8000)) { frame_time_key = 0; return; }
    if (!frame_time_key)
    {
        char text[64];
        U written, file;
        const U n = wsprintfA(text, "frames=%u longest=%u\r\n", frame_time_count, frame_time_longest);
        file = CreateFileA("D:\\PFRAME.TXT", 0x40000000, 0, 0, 4, 0x80, 0);   /* OPEN_ALWAYS */
        SetFilePointer(file, 0, 0, 2);
        WriteFile(file, text, n, &written, 0);
        CloseHandle(file);
        frame_time_key = 1;
        frame_time_longest = 0;
    }
}
