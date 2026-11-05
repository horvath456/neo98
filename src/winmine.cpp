// SPDX-License-Identifier: GPL-2.0-or-later
#include "winmine.h"

namespace Winmine {
// Original near-call offsets are kept beside the corresponding functions.
bool Game::Configure(int width, int height, int count, unsigned seed)
{
    if (width < 8 || width > 30 || height < 8 || height > 24 ||
        count < 10 || count > (width - 1) * (height - 1)) return false;
    configured_columns = width; configured_rows = height; configured_mines = count;
    questions = 1; level = 3;
    random = seed & 65535; // 22e6: srand(unsigned int), with a 16-bit int.
    Reset();
    return true;
}

// 22f8 / 1552: original Microsoft C runtime PRNG and modulo selection.
int Game::RandomBelow(int limit)
{
    random = random * 0x343fdu + 0x269ec3u;
    return ((random >> 16) & 0x7fff) % (unsigned)limit;
}

// 0be6
void Game::InitializeBoard()
{
    for (auto &cell : cells) cell = 0x0f;
    for (int x = 0; x < columns + 2; ++x)
        cells[x] = cells[(rows + 1) * 32 + x] = 0x10;
    for (int y = 0; y < rows + 2; ++y)
        cells[y * 32] = cells[y * 32 + columns + 1] = 0x10;
}

// 101a. Queue contents and mouse state intentionally survive a new game.
void Game::Reset()
{
    ticking = 0;
    columns = configured_columns; rows = configured_rows;
    InitializeBoard();
    face = 0; mines = configured_mines;
    do {
        int x, y;
        do { x = RandomBelow(columns) + 1; y = RandomBelow(rows) + 1; }
        while (Cell(y, x) & 0x80);
        cells[y * 32 + x] |= 0x80;
    } while (--mines);
    mines = remaining = configured_mines;
    safe = rows * columns - mines;
    phase = 1; seconds = 0; revealed = 0;
}

// 0bc0
void Game::SetCell(int state, int y, int x)
{
    cells[y * 32 + x] = (Cell(y, x) & 0xe0) | state;
}

// 0c3e
int Game::NeighbourMines(int y, int x) const
{
    int count = 0;
    for (int yy = y - 1; yy <= y + 1; ++yy)
        for (int xx = x - 1; xx <= x + 1; ++xx)
            if (Cell(yy, xx) & 0x80) ++count;
    return count;
}

// 0f30
int Game::NeighbourFlags(int y, int x) const
{
    int count = 0;
    for (int yy = y - 1; yy <= y + 1; ++yy)
        for (int xx = x - 1; xx <= x + 1; ++xx)
            if ((Cell(yy, xx) & 0x1f) == 0x0e) ++count;
    return count;
}

// 0c82: mines, incorrect flags and already exposed cells have distinct states.
void Game::Expose(int state)
{
    for (int y = 1; y <= rows; ++y) for (int x = 1; x <= columns; ++x) {
        const unsigned char cell = Cell(y, x);
        if (cell & 0x40) continue;
        if (cell & 0x80) {
            if ((cell & 0x1f) != 0x0e) SetCell(state, y, x);
        } else if ((cell & 0x1f) == 0x0e) {
            cells[y * 32 + x] = (cell & 0xeb) | 0x0b;
        }
    }
}

// 0cea. Sound, record-entry and record-display dialogs are platform services.
void Game::Finish(bool won)
{
    ticking = 0; face = won ? 3 : 2;
    Expose(won ? 0x0e : 0x0a);
    if (won) remaining = 0;
    phase = 0x10;
}

// 0d84
void Game::RevealOne(int y, int x)
{
    const auto cell = Cell(y, x);
    if ((cell & 0x40) || (cell & 0x1f) == 0x10 || (cell & 0x1f) == 0x0e) return;
    ++revealed;
    const int count = NeighbourMines(y, x);
    cells[y * 32 + x] = count | 0x40;
    if (!count) {
        queue_x[tail] = x; queue_y[tail] = y;
        if (++tail == 100) tail = 0;
    }
}

// 0dfa: retain the original 100-entry ring and traversal order.
void Game::Flood(int y, int x)
{
    tail = 1;
    RevealOne(y, x);
    int head = 1;
    while (head != tail) {
        const int xx = queue_x[head], yy = queue_y[head];
        RevealOne(yy - 1, xx - 1); RevealOne(yy - 1, xx); RevealOne(yy - 1, xx + 1);
        RevealOne(yy, xx - 1); RevealOne(yy, xx + 1);
        RevealOne(yy + 1, xx - 1); RevealOne(yy + 1, xx); RevealOne(yy + 1, xx + 1);
        if (++head == 100) head = 0;
    }
}

// 0e98: first-click relocation deliberately excludes the last row and column.
void Game::Open(int y, int x)
{
    if (Cell(y, x) & 0x80) {
        if (!revealed) {
            for (int yy = 1; yy < rows; ++yy) for (int xx = 1; xx < columns; ++xx)
                if (!(Cell(yy, xx) & 0x80)) {
                    cells[y * 32 + x] = 0x0f;
                    cells[yy * 32 + xx] |= 0x80;
                    Flood(y, x);
                    return;
                }
            return;
        }
        SetCell(0x4c, y, x);
        Finish(false);
    } else {
        Flood(y, x);
        if (revealed == safe) Finish(true);
    }
}

// 0f76
void Game::OpenNeighbours(int y, int x)
{
    if (!(Cell(y, x) & 0x40) || (Cell(y, x) & 0x1f) != NeighbourFlags(y, x)) {
        Preview(-2, -2);
        return;
    }
    bool exploded = false;
    for (int yy = y - 1; yy <= y + 1; ++yy) for (int xx = x - 1; xx <= x + 1; ++xx) {
        const auto cell = Cell(yy, xx);
        if ((cell & 0x1f) == 0x0e || !(cell & 0x80)) Flood(yy, xx);
        else { exploded = true; SetCell(0x4c, yy, xx); }
    }
    if (exploded) Finish(false);
    else if (revealed == safe) Finish(true);
}

// 10c2 / 110c
void Game::PressCell(int y, int x)
{
    int state = Cell(y, x) & 0x1f;
    if (state == 0x0d) state = 9;
    else if (state == 0x0f) state = 0;
    SetCell(state, y, x);
}
void Game::RestoreCell(int y, int x)
{
    int state = Cell(y, x) & 0x1f;
    if (state == 9) state = 0x0d;
    else if (state == 0) state = 0x0f;
    SetCell(state, y, x);
}

// 1154: change the depressed tile(s) when the pointer moves while held.
void Game::Preview(int y, int x)
{
    if (cursor_y == y && cursor_x == x) return;
    const int old_y = cursor_y, old_x = cursor_x;
    cursor_y = y; cursor_x = x;
    if (!chord) {
        if (Inside(old_y, old_x) && !(Cell(old_y, old_x) & 0x40)) RestoreCell(old_y, old_x);
        if (Inside(y, x) && !(Cell(y, x) & 0x40) && (Cell(y, x) & 0x1f) != 0x0e) PressCell(y, x);
    } else {
        if (Inside(old_y, old_x))
            for (int yy = old_y - 1; yy <= old_y + 1; ++yy)
                for (int xx = old_x - 1; xx <= old_x + 1; ++xx)
                    if (Inside(yy, xx) && !(Cell(yy, xx) & 0x40)) RestoreCell(yy, xx);
        if (Inside(y, x))
            for (int yy = y - 1; yy <= y + 1; ++yy)
                for (int xx = x - 1; xx <= x + 1; ++xx)
                    if (Inside(yy, xx) && !(Cell(yy, xx) & 0x40)) PressCell(yy, xx);
    }
}

// 13d8 / 1540
void Game::Mark(int y, int x)
{
    if (!Inside(y, x) || (Cell(y, x) & 0x40)) return;
    const int old = Cell(y, x) & 0x1f;
    int state;
    if (old == 0x0e) { state = questions ? 0x0d : 0x0f; ++remaining; }
    else if (old == 0x0d) state = 0x0f;
    else { state = 0x0e; --remaining; }
    SetCell(state, y, x);
    if ((Cell(y, x) & 0x1f) == 0x0e && revealed == safe) Finish(true);
}

// 1482. The caller, as in the original window procedure, rejects ended games.
void Game::Release()
{
    if (!Inside(cursor_y, cursor_x)) return;
    if (!revealed && !seconds) { ++seconds; ticking = 1; }
    if (!Active()) { cursor_x = cursor_y = -2; return; }
    if (chord) OpenNeighbours(cursor_y, cursor_x);
    else if (!(Cell(cursor_y, cursor_x) & 0x40) && (Cell(cursor_y, cursor_x) & 0x1f) != 0x0e)
        Open(cursor_y, cursor_x);
}

// 0d68
void Game::Tick() { if (ticking && seconds < 999) ++seconds; }

unsigned Game::Fingerprint() const
{
    unsigned hash = 2166136261u;
    const auto byte = [&hash](unsigned value) { hash = (hash ^ (value & 255)) * 16777619u; };
    const int words[] = {chord, phase, face, ticking, cursor_x, cursor_y,
        (int)(random & 65535), (int)(random >> 16), safe, rows, seconds, mines, columns,
        revealed, tail, level, configured_mines, configured_rows, configured_columns,
        questions, remaining};
    for (int value : words) { byte(value); byte((unsigned)value >> 8); }
    for (short value : queue_y) { byte(value); byte((unsigned)value >> 8); }
    for (short value : queue_x) { byte(value); byte((unsigned)value >> 8); }
    for (auto value : cells) byte(value);
    return hash;
}

// The replay of the original needs the generated steps; a program that only
// plays (sdk/samples/mines.cpp) builds without them.
#ifndef WINMINE_NO_SELF_TEST
struct Replay { unsigned op; int a, b, c; unsigned seed, hash; };
#include "../build/generated/winmine_replay.h"
unsigned OriginalTestCount() { return sizeof(Replays) / sizeof(Replays[0]); }
unsigned OriginalSelfTest()
{
    Game game;
    for (unsigned i = 0; i < OriginalTestCount(); ++i) {
        const auto &r = Replays[i];
        switch (r.op) {
        case 0: game.Configure(r.a, r.b, r.c, r.seed); break;
        case 1: game.Preview(r.a, r.b); break;
        case 2: game.Release(); break;
        case 3: game.Mark(r.a, r.b); break;
        case 4: game.Tick(); break;
        case 5: game.chord = r.a; break;
        case 6: game.questions = r.a; break;
        }
        if (game.Fingerprint() != r.hash) return i + 1;
    }
    return 0;
}
#endif
}
