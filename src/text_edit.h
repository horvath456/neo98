// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// New native editing model. Rendering/measurement are supplied by the ported
// DIB text engine; this is not the Windows EDIT control implementation.
// The buffer grows with the text instead of standing at a fixed size. USER.EXE
// holds the text of an edit control in a movable block of a local heap, so the
// only limit is the one EM_LIMITTEXT sets and the room the heap has: 32 KiB for
// a single line control and 64 KiB for a multi line one on the 16-bit side, and
// on the 32-bit side what EM_SETLIMITTEXT names. `Capacity` is the largest a
// buffer of this port becomes, which is the 64 KiB of the 16-bit contract.
class TextEdit {
public:
    using WordBreak = int (*)(void *, const char *, unsigned current, unsigned length, unsigned code);
    static constexpr unsigned Capacity = 65536;
    enum Key { Left, Right, WordLeft, WordRight, Up, Down, Home, End, DocumentHome, DocumentEnd, PageUp, PageDown };
    struct Line { unsigned start, length; int width; };
    void Reset(const char *);
    void Metrics(const int widths[256], int line_height, int width, int height, int viewport_width = 0);
    bool Insert(const char *, unsigned count);
    unsigned FitInsertion(const char *, unsigned count, bool multiline, bool horizontal_scroll, bool vertical_scroll) const;
    bool Erase(bool forward);
    bool Undo();
    bool CanUndo() const { return can_undo; }
    void ClearUndo() { can_undo = false; }
    void Move(Key, bool extend = false);
    void SelectAll();
    void SetSelection(unsigned start, unsigned end);
    void SelectAt(int x, int y, bool extend, bool midpoint_back = false);
    void Scroll(int rows);
    void ScrollColumns(int columns);
    void ScrollHorizontal(unsigned command, unsigned position);
    void ScrollVertical(unsigned command, unsigned position);
    unsigned ScrollCommand(unsigned command);
    unsigned ScrollPosition() const { return first_line; }
    bool SetTabStops(const int *stops, unsigned count);
    void ScrollCaret() { Reveal(); }
    unsigned Copy(char *output) const;
    unsigned SelectionStart() const { return cursor < anchor ? cursor : anchor; }
    unsigned SelectionEnd() const { return cursor > anchor ? cursor : anchor; }
    unsigned Cursor() const { return cursor; }
    unsigned Length() const { return length; }
    unsigned LineCount() const { return line_count; }
    unsigned CursorLine() const;
    int CursorX() const;
    int FirstLine() const { return first_line; }
    int HorizontalPosition() const { return horizontal; }
    int VisibleLines() const { return visible_lines; }
    int LineHeight() const { return line_height; }
    int PrefixWidth(unsigned line, unsigned count) const;
    // ES_CENTER (1) and ES_RIGHT (2) of the EDIT style. USER aligns each
    // line inside the format rectangle as long as it is narrower than the
    // rectangle; a wider line starts at the left edge and scrolls as usual.
    enum Alignment { AlignLeft = 0, AlignCenter = 1, AlignRight = 2 };
    void SetAlignment(unsigned value) { alignment = value == AlignRight ? AlignRight : value == AlignCenter ? AlignCenter : AlignLeft; ++generation; preferred_x = -1; Reveal(); }
    unsigned GetAlignment() const { return alignment; }
    // Where line `line` starts inside the format rectangle.
    int LineOffset(unsigned line) const;
    int CharacterAdvance(unsigned char ch, int x) const { return Advance(ch, x); }
    void SetPasswordCharacter(unsigned char ch) { password = ch; ++generation; preferred_x = -1; Layout(); Reveal(); }
    unsigned char PasswordCharacter() const { return password; }
    void SetWordBreak(void *context, WordBreak callback) { word_context = context; word_break = callback; ++generation; preferred_x = -1; Layout(); Reveal(); }
    const char *Data() const { return text ? text : ""; }
    // The largest text this control takes, as EM_LIMITTEXT sets it. Zero means
    // the maximum of this port.
    void SetLimit(unsigned characters) { limit = characters && characters < Capacity ? characters : Capacity - 1; }
    unsigned Limit() const { return limit; }
    TextEdit() = default;
    ~TextEdit();
    TextEdit(const TextEdit &) = delete;
    TextEdit &operator=(const TextEdit &) = delete;
    const Line &GetLine(unsigned index) const { return lines[index]; }
private:
    void Layout();
    void Reveal();
    void SetHorizontal(long long position);
    void Remember();
    unsigned AtX(unsigned line, int x, bool midpoint_back = false) const;
    int Advance(unsigned char ch, int x) const;
    unsigned WordBoundary(unsigned current, unsigned code) const;
    // Makes room for `needed` characters and the terminator in both buffers.
    // False when the room is not there; the text then stays what it was.
    bool Reserve(unsigned needed);
    bool ReserveLines(unsigned needed);
    char *text = nullptr, *previous = nullptr;
    unsigned capacity = 0;
    unsigned length = 0, cursor = 0, anchor = 0, old_length = 0, old_cursor = 0, old_anchor = 0;
    bool can_undo = false, trailing_affinity = false, old_affinity = false;
    int widths[256] = {}, line_height = 13, wrap_width = 600, visible_lines = 20, first_line = 0, preferred_x = -1;
    int viewport_width = 600, horizontal = 0;
    Line *lines = nullptr;
    unsigned line_capacity = 0;
    unsigned line_count = 1;
    unsigned limit = Capacity - 1;
    unsigned generation = 0;
    unsigned char password = 0;
    unsigned alignment = AlignLeft;
    void *word_context = nullptr;
    WordBreak word_break = nullptr;
    int tab_stops[32] = {};
    unsigned tab_count = 0;
};
