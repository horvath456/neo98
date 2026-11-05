/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* The MCI-string film device of the SDK (sdk/lib/moviemci.h) in an ARM64
   program: a film is opened, measured, played, paused, moved and closed
   through the strings a program would send to mciSendString. Writes one
   line per check into the result file. tools/test_arm64_player_host.py.

       MCIFILM.EXE C:\FILM.AVI C:\RESULT.TXT */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <moviemci.h>

static FILE *out;
static void Note(const char *name, long value) { fprintf(out, "%s=%ld\n", name, value); }
static long Ask(const char *command)
{
    char text[64] = "";
    return MovieMciSendString(command, text, sizeof text, NULL) ? -1 : atol(text);
}
static void Pump(DWORD milliseconds)
{
    const DWORD start = GetTickCount();
    MSG message;
    while (GetTickCount() - start < milliseconds)
    {
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&message);
        Sleep(5);
    }
}
static LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcA(window, message, wParam, lParam);
}
int main(int argc, char **argv)
{
    WNDCLASSA wc;
    HWND host, film;
    char command[400], text[96] = "";
    long length, before, after;
    if (argc < 3 || !(out = fopen(argv[2], "w"))) return 2;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = HostProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "MciFilmHost";
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "MciFilmHost", "film", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 400, 340, NULL, NULL, wc.hInstance, NULL);
    snprintf(command, sizeof command, "open \"%s\" alias film parent %ld style child", argv[1], (long)(INT_PTR)host);
    Note("open", (long)MovieMciSendString(command, NULL, 0, NULL));
    Note("frames", Ask("status film length")); /* the default time format is frames */
    Note("format", (long)MovieMciSendString("set film time format milliseconds", NULL, 0, NULL));
    length = Ask("status film length");
    Note("length", length);
    MovieMciSendString("where film source", text, sizeof text, NULL);
    fprintf(out, "source=%s\n", text);
    Note("put", (long)(MovieMciSendString("put film window at 10 20 320 240", NULL, 0, NULL) +
                       MovieMciSendString("put film destination at 0 0 320 240", NULL, 0, NULL)));
    film = FindWindowExA(host, NULL, "Neo98Movie", NULL);
    Note("child", film != NULL);
    MovieMciSendString("status film mode", text, sizeof text, NULL);
    fprintf(out, "mode_open=%s\n", text);
    Note("play", (long)MovieMciSendString("play film window", NULL, 0, NULL));
    Pump(1500);
    MovieMciSendString("status film mode", text, sizeof text, NULL);
    fprintf(out, "mode_playing=%s\n", text);
    Note("position_playing", Ask("status film position"));
    Note("visible", IsWindowVisible(film));
    Note("pause", (long)MovieMciSendString("pause film", NULL, 0, NULL));
    before = Ask("status film position");
    Pump(400);
    after = Ask("status film position");
    Note("paused_still", before == after && before > 0);
    MovieMciSendString("status film mode", text, sizeof text, NULL);
    fprintf(out, "mode_paused=%s\n", text);
    Note("seek", (long)MovieMciSendString("seek film to 500", NULL, 0, NULL));
    Note("position_seek", Ask("status film position"));
    Note("seek_beyond", (long)MovieMciSendString("seek film to 99999999", NULL, 0, NULL));
    Note("play_again", (long)MovieMciSendString("play film", NULL, 0, NULL));
    Pump((DWORD)length + 1500);
    MovieMciSendString("status film mode", text, sizeof text, NULL);
    fprintf(out, "mode_end=%s\n", text);
    Note("position_end", Ask("status film position"));
    Note("close", (long)MovieMciSendString("close film", NULL, 0, NULL));
    /* What is no film of this device goes to WINMM, which knows no such alias. */
    Note("foreign", MovieMciSendString("status nosuchdevice mode", text, sizeof text, NULL) != 0);
    fprintf(out, "done=1\n");
    fclose(out);
    DestroyWindow(host);
    return 0;
}
