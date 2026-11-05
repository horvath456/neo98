// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_edit.h"
#if defined(__APPLE__) || defined(__linux__)
#include <stdlib.h>
#else
#include <circle/alloc.h>
#endif

// The buffer starts small and doubles, so a control that holds a line costs a
// line and one that holds a file of sixty kilobytes holds sixty kilobytes.
// Both buffers stay the same size, because Undo swaps them.
bool TextEdit::Reserve(unsigned needed)
{
    if (needed > limit) return false;
    if (capacity > needed) return true;
    unsigned want = capacity ? capacity : 256;
    while (want <= needed) want *= 2;
    if (want > Capacity) want = Capacity;
    if (want <= needed) return false;
    char *grown = static_cast<char *>(realloc(text, want));
    if (!grown) return false;
    text = grown;
    char *old = static_cast<char *>(realloc(previous, want));
    if (!old) return false;
    previous = old;
    for (unsigned i = capacity; i < want; ++i) { text[i] = 0; previous[i] = 0; }
    capacity = want;
    return true;
}
// One line record per line, and a line is at least one character, so the line
// table never needs more records than the text has characters plus one.
bool TextEdit::ReserveLines(unsigned needed)
{
    if (line_capacity > needed) return true;
    unsigned want = line_capacity ? line_capacity : 64;
    while (want <= needed) want *= 2;
    if (want > Capacity + 1) want = Capacity + 1;
    if (want <= needed) return false;
    auto *grown = static_cast<Line *>(realloc(lines, want * sizeof(Line)));
    if (!grown) return false;
    lines = grown;
    for (unsigned i = line_capacity; i < want; ++i) lines[i] = Line{0, 0, 0};
    line_capacity = want;
    return true;
}
TextEdit::~TextEdit()
{
    free(text);
    free(previous);
    free(lines);
}
void TextEdit::Reset(const char *source)
{
    ++generation;
    length = 0;
    unsigned wanted = 0;
    while (source[wanted] && wanted < limit) ++wanted;
    if (!Reserve(wanted)) { if (!Reserve(0)) return; wanted = capacity - 1; }
    while (length < wanted) { text[length] = source[length]; ++length; }
    text[length] = 0; cursor = anchor = 0; first_line = horizontal = 0; preferred_x = -1; can_undo = trailing_affinity = false;
    Layout();
}
void TextEdit::Metrics(const int values[256], int height, int width, int available, int viewport)
{
    ++generation;
    for (unsigned i = 0; i < 256; ++i) widths[i] = values[i] > 0 ? values[i] : 1;
    line_height = height > 0 ? height : 1; wrap_width = width > 0 ? width : 1;
    visible_lines = available / line_height; if (visible_lines < 1) visible_lines = 1;
    viewport_width = viewport > 0 ? viewport : wrap_width;
    preferred_x = -1; Layout(); Reveal();
}
void TextEdit::Remember()
{
    if (!text || !previous) return;
    __builtin_memcpy(previous, text, length + 1);
    old_length = length; old_cursor = cursor; old_anchor = anchor; can_undo = true;
    old_affinity = trailing_affinity;
}
bool TextEdit::Insert(const char *input, unsigned count)
{
    const unsigned a = SelectionStart(), b = SelectionEnd();
    const unsigned kept = length - (b - a);
    if ((!input && count) || count > limit - kept) return false;
    if (!count && a == b) return false;
    if (!Reserve(kept + count)) return false;
    Remember();
    __builtin_memmove(text + a + count, text + b, length - b + 1);
    if (count) __builtin_memcpy(text + a, input, count);
    length = length - (b - a) + count; cursor = anchor = a + count; ++generation;
    trailing_affinity = false;
    preferred_x = -1; Layout(); Reveal(); return true;
}
// Check the resulting text before changing the buffer or its undo record.
// A fixed-width single-line EDIT accepts the prefix that fits; a multiline
// EDIT without vertical scrolling refuses an insertion requiring extra rows.
unsigned TextEdit::FitInsertion(const char *input, unsigned count, bool multiline,
                               bool horizontal_scroll, bool vertical_scroll) const
{
    if ((!multiline && horizontal_scroll) || (multiline && vertical_scroll)) return count;
    const unsigned first=SelectionStart(), last=SelectionEnd(), kept=length-(last-first);
    if(count>Capacity-1-kept)count=Capacity-1-kept;
    if(!multiline){
        auto fits=[&](unsigned n){
            int pixels=0;
            for(unsigned i=0;i<first;++i)pixels+=Advance((unsigned char)text[i],pixels);
            for(unsigned i=0;i<n;++i)pixels+=Advance((unsigned char)input[i],pixels);
            for(unsigned i=last;i<length;++i)pixels+=Advance((unsigned char)text[i],pixels);
            return pixels<=viewport_width;
        };
        unsigned lo=0,hi=count;
        while(lo<hi){const unsigned mid=lo+(hi-lo+1)/2;if(fits(mid))lo=mid;else hi=mid-1;}
        return lo;
    }
    auto *combined=new char[kept+count+1];
    if(!combined)return 0;
    if(first)__builtin_memcpy(combined,Data(),first);
    if(count)__builtin_memcpy(combined+first,input,count);
    __builtin_memcpy(combined+first+count,Data()+last,length-last+1);
    TextEdit trial;
    trial.password=password;
    trial.tab_count=tab_count;
    for(unsigned i=0;i<tab_count;++i)trial.tab_stops[i]=tab_stops[i];
    trial.Metrics(widths,line_height,wrap_width,visible_lines*line_height,viewport_width);
    trial.Reset(combined);
    delete[] combined;
    return trial.LineCount()<=unsigned(visible_lines)?count:0;
}
bool TextEdit::Erase(bool forward)
{
    if (cursor == anchor) {
        if (forward) { if (cursor == length) return false; ++anchor; }
        else { if (!cursor) return false; --anchor; }
        // Keep the caret's original collapsed selection in the undo state.
        const unsigned before = cursor;
        const bool changed = Insert(nullptr, 0);
        old_cursor = old_anchor = before;
        return changed;
    }
    return Insert(nullptr, 0);
}
bool TextEdit::Undo()
{
    if (!can_undo || !text || !previous) return false;
    char *swapped = text; text = previous; previous = swapped;
    auto swap = [](unsigned &a, unsigned &b) { const unsigned t = a; a = b; b = t; };
    swap(length, old_length); swap(cursor, old_cursor); swap(anchor, old_anchor);
    const bool affinity = trailing_affinity; trailing_affinity = old_affinity; old_affinity = affinity;
    ++generation; preferred_x = -1; Layout(); Reveal(); return true;
}
void TextEdit::Layout()
{
    line_count = 0;
    if (!text && !Reserve(0)) { line_count = 1; return; }
    if (!ReserveLines(length + 1)) { line_count = 1; return; }
    unsigned start = 0;
    do {
        unsigned end = start, last_space = start;
        int width = 0, break_width = 0;
        while (end < length && text[end] != '\n') {
            const int next = width + Advance((unsigned char)text[end], width);
            if (next > wrap_width && end > start) {
                if (!word_break && last_space > start) { end = last_space; width = break_width; }
                break;
            }
            width = next; ++end;
            if (text[end - 1] == ' ') { last_space = end; break_width = width; }
        }
        if (end < length && text[end] != '\n' && word_break) {
            const unsigned before = generation;
            const unsigned custom = WordBoundary(end, 0);
            if (generation != before) return;
            if (custom > start && custom <= end) {
                end = custom;
                width = 0;
                for (unsigned i = start; i < end; ++i)
                    width += Advance((unsigned char)text[i], width);
            }
        }
        lines[line_count++] = {start, end - start, width};
        if (end == length) break;
        start = end + (text[end] == '\n' ? 1 : 0);
    } while (line_count < line_capacity);
    Scroll(0);
}
unsigned TextEdit::CursorLine() const
{
    unsigned i = 0;
    while (i + 1 < line_count && lines[i + 1].start <= cursor) ++i;
    if (trailing_affinity && i && lines[i].start == cursor && lines[i - 1].start + lines[i - 1].length == cursor) --i;
    return i;
}
int TextEdit::PrefixWidth(unsigned line, unsigned count) const
{
    const auto &l = lines[line]; if (count > l.length) count = l.length;
    int result = 0;
    for (unsigned i = 0; i < count; ++i) result += Advance((unsigned char)text[l.start + i], result);
    return result;
}
int TextEdit::LineOffset(unsigned line) const
{
    if (alignment == AlignLeft || line >= line_count) return 0;
    // The caret stands one pixel right of the last character, so a right
    // aligned line ends one pixel before the edge and the caret stays visible.
    const int room = viewport_width - 1 - lines[line].width;
    if (room <= 0) return 0;
    return alignment == AlignRight ? room : room / 2;
}
int TextEdit::CursorX() const { const unsigned line = CursorLine(); return LineOffset(line) + PrefixWidth(line, cursor - lines[line].start); }
unsigned TextEdit::AtX(unsigned line, int x, bool midpoint_back) const
{
    const auto &l = lines[line]; int position = 0;
    x -= LineOffset(line);
    for (unsigned i = 0; i < l.length; ++i) {
        const int width = Advance((unsigned char)text[l.start + i], position);
        if (x < position + (midpoint_back ? width / 2 + 1 : (width + 1) / 2)) return l.start + i;
        position += width;
    }
    return l.start + l.length;
}
int TextEdit::Advance(unsigned char ch, int x) const
{
    if (password) ch = password;
    if (ch != '\t') return widths[ch];
    for (unsigned i = 0; i < tab_count; ++i)
        if (tab_stops[i] > x) return tab_stops[i] - x;
    const int step = tab_count ? (tab_count > 1 ? tab_stops[tab_count - 1] - tab_stops[tab_count - 2] : tab_stops[0]) : 32;
    const int width = step > 0 ? step : 32;
    return ((x / width) + 1) * width - x;
}
unsigned TextEdit::WordBoundary(unsigned current, unsigned code) const
{
    if (current > length) current = length;
    if (word_break) {
        const int answer = word_break(word_context, text, current, length, code);
        return answer < 0 ? 0 : unsigned(answer) > length ? length : unsigned(answer);
    }
    auto delimiter = [&](unsigned at) { return at < length && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n'); };
    if (code == 0) {
        while (current && delimiter(current - 1)) --current;
        while (current && !delimiter(current - 1)) --current;
    } else {
        while (current < length && !delimiter(current)) ++current;
        while (current < length && delimiter(current)) ++current;
    }
    return current;
}
bool TextEdit::SetTabStops(const int *stops, unsigned count)
{
    if (count > 32 || (!stops && count)) return false;
    for (unsigned i = 0; i < count; ++i)
    {
        if (stops[i] <= 0 || (i && stops[i] <= stops[i - 1])) return false;
        tab_stops[i] = stops[i];
    }
    tab_count = count; ++generation;
    Layout();
    return true;
}
unsigned TextEdit::ScrollCommand(unsigned command)
{
    int rows = 0;
    switch (command)
    {
    case 0: rows = -1; break;
    case 1: rows = 1; break;
    case 2: rows = -visible_lines; break;
    case 3: rows = visible_lines; break;
    default: return 0;
    }
    const int before = first_line;
    Scroll(rows);
    return 0x10000u | ((first_line - before) & 0xffff);
}
void TextEdit::Reveal()
{
    const int row = CursorLine();
    if (row < first_line) first_line = row;
    if (row >= first_line + visible_lines) first_line = row - visible_lines + 1;
    const int x = CursorX();
    if (x < horizontal) horizontal = x;
    if (x >= horizontal + viewport_width) horizontal = x - viewport_width + 1;
    Scroll(0);
    ScrollColumns(0);
}
void TextEdit::Scroll(int rows)
{
    first_line += rows;
    const int maximum = (int)line_count > visible_lines ? line_count - visible_lines : 0;
    if (first_line > maximum) first_line = maximum;
    if (first_line < 0) first_line = 0;
}
void TextEdit::SetHorizontal(long long position)
{
    int maximum = 0;
    for (unsigned i = 0; i < line_count; ++i)
        if (lines[i].width - viewport_width > maximum) maximum = lines[i].width - viewport_width;
    horizontal = position < 0 ? 0 : position > maximum ? maximum : (int)position;
}
void TextEdit::ScrollColumns(int columns)
{
    SetHorizontal((long long)horizontal + (long long)columns * widths[(unsigned char)'0']);
}
void TextEdit::ScrollHorizontal(unsigned command, unsigned position)
{
    switch (command)
    {
    case 0: ScrollColumns(-1); break;
    case 1: ScrollColumns(1); break;
    case 2: SetHorizontal((long long)horizontal - viewport_width); break;
    case 3: SetHorizontal((long long)horizontal + viewport_width); break;
    case 4: case 5: SetHorizontal(position); break;
    case 6: horizontal = 0; break;
    case 7: SetHorizontal(0x7fffffff); break;
    default: break;
    }
}
void TextEdit::ScrollVertical(unsigned command, unsigned position)
{
    // USER.EXE: a page keeps one line in common; thumb messages
    // carry the absolute first line. SB_TOP/BOTTOM/ENDSCROLL are ignored.
    switch (command)
    {
    case 0: Scroll(-1); break;
    case 1: Scroll(1); break;
    case 2: Scroll(-(visible_lines - 1)); break;
    case 3: Scroll(visible_lines - 1); break;
    case 4: case 5: Scroll(int(position) - first_line); break;
    default: break;
    }
}
void TextEdit::Move(Key key, bool extend)
{
    const unsigned row = CursorLine();
    if (key == Up || key == Down || key == PageUp || key == PageDown) {
        if (preferred_x < 0) preferred_x = CursorX();
        int target = (int)row + (key == Up ? -1 : key == Down ? 1 : key == PageUp ? -visible_lines : visible_lines);
        if (target < 0) target = 0;
        if (target >= (int)line_count) target = line_count - 1;
        cursor = AtX(target, preferred_x);
        trailing_affinity = cursor == lines[target].start + lines[target].length;
    } else {
        preferred_x = -1;
        trailing_affinity = key == End;
        switch (key) {
        case Left: cursor = !extend && cursor != anchor ? SelectionStart() : cursor ? cursor - 1 : 0; break;
        case Right: cursor = !extend && cursor != anchor ? SelectionEnd() : cursor < length ? cursor + 1 : length; break;
        case WordLeft: cursor = !extend && cursor != anchor ? SelectionStart() : WordBoundary(cursor, 0); break;
        case WordRight: cursor = !extend && cursor != anchor ? SelectionEnd() : WordBoundary(cursor, 1); break;
        case Home: cursor = lines[row].start; break;
        case End: cursor = lines[row].start + lines[row].length; break;
        case DocumentHome: cursor = 0; break;
        case DocumentEnd: cursor = length; break;
        default: break;
        }
    }
    if (!extend) anchor = cursor;
    Reveal();
}
void TextEdit::SelectAll() { anchor = 0; cursor = length; preferred_x = -1; trailing_affinity = false; Reveal(); }
void TextEdit::SetSelection(unsigned start, unsigned end)
{
    anchor = start > length ? length : start; cursor = end > length ? length : end;
    preferred_x = -1; trailing_affinity = false; Reveal();
}
void TextEdit::SelectAt(int x, int y, bool extend, bool midpoint_back)
{
    x += horizontal;
    int row = first_line + (y >= 0 ? y / line_height : -1);
    if (row < 0) row = 0;
    if (row >= (int)line_count) row = line_count - 1;
    cursor = AtX(row, x, midpoint_back); if (!extend) anchor = cursor;
    trailing_affinity = cursor == lines[row].start + lines[row].length;
    preferred_x = -1; Reveal();
}
unsigned TextEdit::Copy(char *output) const
{
    const unsigned a = SelectionStart(), n = SelectionEnd() - a;
    __builtin_memcpy(output, text + a, n); output[n] = 0; return n;
}
