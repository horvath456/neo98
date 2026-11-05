// SPDX-License-Identifier: GPL-2.0-or-later
// A stand-in for the game behind a Win32 front end, the fixture of
// tools/test_arm64_dx_host.py :
// two windows whose every pixel the test can name. The games themselves are
// built and run by tools/games_arm64_proof.py.
// The front end of Doodle Jump (games/doodle-jump-dos/win32/main_win.cpp in
// ~/dos-ports) is compiled unchanged; it includes "../desktop/game_adapter.h"
// and finds this file in place of the game's. The interface is the game's
// `port::` adapter. Behind it is no game: a test picture that moves with the
// tick count, and a square wave for the mixer. The stand-in counts what the
// front end asks of it and writes PROOF.TXT beside the program when it ends,
// which says: pictures presented, ticks, time, bytes mixed.
// The same file builds for ARM64 with the SDK and for x86 with mingw-w64, so
// one front end runs both ways, over the ARM64 DDRAW.DLL and over the
// original one.
#pragma once
#include <cstring>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>
namespace port
{
constexpr int PW = 320, PH = 240, SW = PW, SH = PH;
enum { K_LEFT = 0x125, K_RIGHT = 0x127, K_UP = 0x126, K_DOWN = 0x128, K_CTRL = 0x111, K_SPACE = 32, K_ENTER = 13, K_F1 = 0x130 };
struct Input { enum Type { KeyDown, KeyUp, MouseMove, MouseDown, MouseUp } type; int key = 0, x = 0, y = 0; };
struct Canvas { uint8_t px[PW * PH]; };
// The colours of the test picture: index i is (i, 255 - i, 64 + i / 2).
inline const uint8_t *palette()
{
    static uint8_t table[768];
    if (!table[4])
        for (int i = 0; i < 256; ++i)
        {
            table[i * 3] = uint8_t(i);
            table[i * 3 + 1] = uint8_t(255 - i);
            table[i * 3 + 2] = uint8_t(64 + i / 2);
        }
    return table;
}
struct Audio
{
    bool musicOn = true, sfxOn = true;
    int rate = 22050;
    unsigned long mixed = 0, calls = 0;
    unsigned phase = 0;
    static void (*lock)();
    static void (*unlock)();
    void setRate(int value) { rate = value; }
    // Eight bit unsigned mono: a square wave of 441 Hz, 50 samples a period.
    void mix8(uint8_t *out, int count)
    {
        for (int i = 0; i < count; ++i, ++phase) out[i] = musicOn ? ((phase / 25) & 1 ? 0xc0 : 0x40) : 0x80;
        mixed += unsigned(count);
        ++calls;
    }
};
inline void (*Audio::lock)() = nullptr;
inline void (*Audio::unlock)() = nullptr;
// How long the stand-in runs before it asks to quit, in ticks of a sixtieth of a second.
#ifndef PROOF_TICKS
#define PROOF_TICKS 600
#endif
class Game
{
    Audio audio_;
    bool loaded_ = false, reported_ = false;
    unsigned ticks_ = 0, pictures_ = 0, inputs_ = 0;
    DWORD first_ = 0, last_ = 0;
    int pointer_x_ = -1, pointer_y_ = -1;
    void Report()
    {
        if (reported_) return;
        reported_ = true;
        FILE *file = fopen("PROOF.TXT", "w");
        if (!file) return;
        fprintf(file, "ticks=%u\npictures=%u\nmilliseconds=%lu\nmixed=%lu\nmix_calls=%lu\ninputs=%u\npointer=%d,%d\ndone=1\n", ticks_,
                pictures_, (unsigned long)(last_ - first_), audio_.mixed, audio_.calls, inputs_, pointer_x_, pointer_y_);
        fclose(file);
    }
  public:
    void init(const char *, uint32_t, const char *) { loaded_ = true; }
    bool original() const { return loaded_; }
    bool quitRequested()
    {
        const bool quit = ticks_ >= PROOF_TICKS;
        if (quit) Report();
        return quit;
    }
    void audioReady(int rate) { audio_.setRate(rate); }
    Audio &audio() { return audio_; }
    void handle(const Input &e)
    {
        ++inputs_;
        if (e.type != Input::KeyDown && e.type != Input::KeyUp) { pointer_x_ = e.x; pointer_y_ = e.y; }
    }
    void update()
    {
        if (!ticks_) first_ = timeGetTime();
        ++ticks_;
        last_ = timeGetTime();
    }
    // The picture of tick t: pixel (x, y) is index (x + 2 * y + 3 * t) mod 256;
    // a block of sixteen pixels in the corner holds t mod 256.
    void render(Canvas &canvas)
    {
        ++pictures_;
        for (int y = 0; y < PH; ++y)
            for (int x = 0; x < PW; ++x) canvas.px[y * PW + x] = uint8_t(x + 2 * y + 3 * int(ticks_));
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) canvas.px[y * PW + x] = uint8_t(ticks_);
    }
    void autoInput() {}
    void startScene(int) {}
    ~Game() { Report(); }
};
} // namespace port
