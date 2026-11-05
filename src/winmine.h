// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace Winmine {
// Recovered from WINMINE.EXE 4.0.0.950. Coordinates are one-based.
// Each cell keeps the original state bits, including its sentinel border.
class Game {
public:
    unsigned char cells[864] = {};
    short queue_y[100] = {}, queue_x[100] = {};
    int chord = 0, phase = 24, face = 0, ticking = 0;
    int cursor_x = -1, cursor_y = -1;
    unsigned random = 1;
    int safe = 0, rows = 0, seconds = 0, mines = 0, columns = 0, revealed = 0;
    int tail = 0, level = 0, configured_mines = 0, configured_rows = 0, configured_columns = 0;
    int questions = 0, remaining = 0;

    bool Configure(int width, int height, int count, unsigned seed);
    void Reset(); // Configure must have succeeded before the first Reset.
    void Preview(int y, int x);
    void Release();
    void Mark(int y, int x);
    void Tick();
    bool Active() const { return (phase & 1) != 0; }
    bool Inside(int y, int x) const { return x >= 1 && y >= 1 && x <= columns && y <= rows; }
    unsigned char Cell(int y, int x) const { return cells[y * 32 + x]; }
    unsigned Fingerprint() const;

private:
    void InitializeBoard();
    int RandomBelow(int limit);
    int NeighbourMines(int y, int x) const;
    int NeighbourFlags(int y, int x) const;
    void SetCell(int state, int y, int x);
    void Expose(int state);
    void Finish(bool won);
    void RevealOne(int y, int x);
    void Flood(int y, int x);
    void Open(int y, int x);
    void OpenNeighbours(int y, int x);
    void PressCell(int y, int x);
    void RestoreCell(int y, int x);
};

unsigned OriginalSelfTest();
unsigned OriginalTestCount();
}
