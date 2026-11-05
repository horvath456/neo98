// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

// Clipboard ownership, window validation,
// global-memory conversion, graphics conversion and message delivery are providers.
namespace Win98Clipboard {
using Word = uint16_t;
struct Entry { Word format, data; };
struct State {
    Entry *entries = nullptr;
    Word count = 0, open_task = 0;
    unsigned owner = 0, viewer = 0, open_window = 0;
    Word changed = 0, notifying = 0, saved_context = 0;
    unsigned sequence = 0;
    Word special_formats[2] = {}, special_data[2][2] = {};
};
struct Environment {
    virtual ~Environment() = default;
    virtual Word CurrentTask() = 0;
    virtual Word ExpectedVersion(Word task) = 0;
    virtual unsigned Locale(Word task) = 0;
    virtual unsigned WindowHandle(unsigned window) = 0;
    virtual Word Context() = 0;
    virtual void InputMode(bool enabled) = 0;
    // timeout=true is original SendMessageTimeout(flags=2, timeout=5000).
    virtual unsigned Send(unsigned window, Word message, unsigned wp, unsigned lp, bool timeout) = 0;
    // A stable table is required during synchronous rendering callbacks. The
    // original LocalReAlloc(flags=2) owns this property for the USER local heap.
    virtual Entry *ResizeTable(Entry *previous, Word bytes) = 0;
    virtual void FreeTable(Entry *) = 0;
    virtual void ReferenceAtom(Word format) = 0;
    virtual void DeleteAtom(Word format) = 0;
    virtual Word AtomName(Word format, unsigned char *out, Word capacity) = 0;
    virtual Word GlobalAlloc(Word flags, unsigned bytes) = 0;
    virtual unsigned GlobalSize(Word handle) = 0;
    virtual unsigned char *GlobalLock(Word handle) = 0;
    virtual unsigned char *GlobalData(Word handle) = 0; // directly addressable fixed allocation
    virtual void GlobalUnlock(Word handle) = 0;
    virtual Word GlobalReAlloc(Word handle, unsigned bytes, Word flags) = 0;
    virtual void GlobalFree(Word handle) = 0;
    virtual void SetObjectOwner(Word handle) = 0;
    virtual void DeleteObject(Word handle) = 0;
    virtual bool ValidMetaFile(Word handle) = 0;
    virtual Word CheckMetaFile(Word handle) = 0;
    virtual void DeleteMetaFile(Word handle, bool enhanced) = 0;
    virtual void DiscardMapping(Word handle) = 0; // KRNL386 ordinal542
    virtual unsigned To32(Word format, Word handle) = 0; // ordinal532
    virtual Word To16(Word format, unsigned handle, bool transfer) = 0; // ordinal531
    virtual unsigned ConvertSpecial(unsigned handle, bool to32) = 0; // USER
    virtual bool PaletteDisplay() = 0;
    virtual Word CreateSystemPalette() = 0; // USER graphics provider
    virtual Word ConvertGraphics(Word handle, Word from, Word to) = 0;
    virtual void TranslateText(const unsigned char *from, unsigned char *to, bool to_oem) = 0;
};
inline Word ReadWord(const unsigned char *p) { return Word(p[0] | unsigned(p[1]) << 8); }
inline void WriteWord(unsigned char *p, Word x) { p[0] = x; p[1] = x >> 8; }
inline int Find(const State &s, Word format) { //
    if (!format || !s.entries) return -1;
    for (int i = 0; i < int(int16_t(s.count)); ++i)
        if (s.entries[i].format == format) return i;
    return -1;
}
inline int Pair(const State &s, Word format) { //
    for (int i = 0; i < 2; ++i) if (s.special_formats[i] == format) return i;
    return -1;
}
inline unsigned Classify(const State &s, const Entry &v) { //
    if (v.data == 0xffff) return 5;
    if (v.data == 0xfffe) return 6;
    if (v.format == 2 || v.format == 9 || v.format == 0x82) return 1;
    if (v.format == 3 || v.format == 0x83) return 3;
    if (v.format == 14 || v.format == 0x8e) return 4;
    if (v.format == 0x80) return 0;
    return Pair(s, v.format) >= 0 ? 6 : 2;
}
inline void FreeEntry(Environment &e, State &s, Entry &v) { //
    if (!v.data) return;
    switch (Classify(s, v)) {
    case 1: e.DeleteObject(v.data); break;
    case 2: e.GlobalFree(v.data); break;
    case 3: {
        auto *p = e.GlobalLock(v.data);
        // Valid METAFILEPICT memory is the original caller/provider precondition.
        if (p && ReadWord(p + 6)) e.DeleteMetaFile(ReadWord(p + 6), false);
        e.GlobalUnlock(v.data); e.GlobalFree(v.data); break;
    }
    case 4: e.DeleteMetaFile(v.data, true); break;
    case 6: {
        const int pair = Pair(s, v.format);
        if (pair >= 0) for (auto &h : s.special_data[pair]) if (h) { e.GlobalFree(h); h = 0; }
        v.data = 0; break;
    }
    default: break;
    }
}
inline Word Set(Environment &e, State &s, Word format, Word data, Word flags, bool internal = false) { //
    if ((!internal && (data == 0xffff || data == 0xfffe)) || !s.open_task || !format) return 0;
    int at = Find(s, format);
    if (at < 0) {
        auto *table = e.ResizeTable(s.entries, Word((s.count + 1u) * 4u));
        if (!table) return 0;
        s.entries = table;
        if (format >= 0xbfff) e.ReferenceAtom(format);
        at = s.count++; s.entries[at].format = format;
    } else FreeEntry(e, s, s.entries[at]);
    auto &v = s.entries[at]; v.data = data;
    if (data) switch (Classify(s, v)) {
    case 0: case 2: e.GlobalReAlloc(v.data, 0, 0x2080); break;
    case 1: case 4: e.SetObjectOwner(v.data); break;
    case 3: {
        auto *p = e.GlobalLock(v.data);
        if (!p) data = v.data = 0;
        else {
            Word inner = ReadWord(p + 6);
            WriteWord(p + 6, e.ValidMetaFile(inner) ? e.GlobalReAlloc(inner, 0, 0x2080) : 0);
        }
        if (v.data) e.GlobalUnlock(v.data);
        e.GlobalReAlloc(v.data, 0, 0x2080); break;
    }
    case 6: {
        e.GlobalReAlloc(v.data, 0, 0x2080);
        const int pair = Pair(s, v.format);
        if (pair >= 0) { s.special_data[pair][flags & 1] = v.data; v.data = 0xfffe; }
        break;
    }
    default: break;
    }
    s.changed = 1; ++s.sequence; return data;
}
inline void BeginNotify(Environment &e, State &s) { s.saved_context = e.Context(); if (s.saved_context) e.InputMode(false); }
inline void EndNotify(Environment &e, State &s) { if (s.saved_context) e.InputMode(true); }
inline void NotifyOwner(Environment &e, State &s, Word message) { //
    BeginNotify(e, s); e.Send(s.owner, message, 0, 0, true); EndNotify(e, s);
}
inline void NotifyViewer(Environment &e, State &s) { //
    s.changed = 0;
    if (!s.notifying && s.viewer) {
        s.notifying = 1; BeginNotify(e, s);
        e.Send(s.viewer, 0x308, s.owner ? e.WindowHandle(s.owner) : 0, 0, false);
        EndNotify(e, s);
    }
    // A nested invocation also clears the shared guard in the original.
    s.notifying = 0;
}
inline unsigned Open(Environment &e, State &s, unsigned window) { //
    const Word task = e.CurrentTask();
    if (s.open_window != window && s.open_task) return 0;
    s.open_window = window; s.open_task = task; return 1;
}
inline unsigned Empty(Environment &e, State &s) { //
    if (e.CurrentTask() != s.open_task) return 0;
    if (s.owner) NotifyOwner(e, s, 0x307);
    if (s.entries) {
        for (int i = 0; i < int(int16_t(s.count)); ++i) {
            if (s.entries[i].format >= 17) e.DeleteAtom(s.entries[i].format);
            FreeEntry(e, s, s.entries[i]);
        }
        e.FreeTable(s.entries); s.entries = nullptr; s.count = 0;
    }
    s.owner = s.open_window; ++s.sequence; s.changed = 1; return 1;
}
// Format-conversion triples: conversion source, two automatically added formats.
inline constexpr unsigned char Related[17][3] = {
    {0,0,0},{7,7,0},{8,8,0},{14,14,0},{0,0,0},{0,0,0},{0,0,0},
    {1,1,0},{2,2,9},{8,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
    {3,3,0},{0,0,0},{0,0,0}
};
// SIGNALPROC32's exiting-task branch, USER. Empty intentionally
// executes after owner is cleared, suppressing WM_DESTROYCLIPBOARD delivery.
inline void TaskExited(Environment &e, State &s, Word task) {
    if (s.open_task != task) return;
    s.open_window = 0; s.owner = 0;
    Empty(e, s);
    s.open_task = 0;
}
inline void Synthesize(Environment &e, State &s) { //
    for (Word format = 1; format < 17; ++format) if (Find(s, format) >= 0) {
        if (format == 1 && Find(s, 16) < 0) {
            const Word data = e.GlobalAlloc(0x2042, 4);
            if (data) {
                // Original fixed handle is directly addressable; no GlobalLock call.
                auto *p = e.GlobalData(data);
                if (p) { const unsigned locale = e.Locale(s.open_task); for (unsigned n = 0; n < 4; ++n) p[n] = locale >> (8 * n); }
                Set(e, s, 16, data, 0);
            }
        }
        if (format == 2 && e.PaletteDisplay() && Find(s, 8) < 0 && Find(s, 9) < 0) {
            const Word palette = e.CreateSystemPalette();
            if (palette) Set(e, s, 9, palette, 0, true);
        }
        for (unsigned n = 1; n < 3; ++n) {
            const Word other = Related[format][n];
            if (!other) break;
            if (other == 9 && !e.PaletteDisplay()) continue;
            if (Find(s, other) < 0) Set(e, s, other, 0xffff, 0, true);
        }
    }
}
inline unsigned Close(Environment &e, State &s) { //
    const Word task = e.CurrentTask();
    if (task != s.open_task) return 0;
    if (s.changed) Synthesize(e, s);
    s.open_window = 0; s.open_task = 0;
    if (s.changed) NotifyViewer(e, s);
    if (s.entries && int16_t(e.ExpectedVersion(task)) >= 0x400)
        for (int i = 0; i < int(int16_t(s.count)); ++i) {
            auto &v = s.entries[i]; const unsigned kind = Classify(s, v);
            if ((kind == 2 || kind == 3) && v.data) e.DiscardMapping(v.data);
            else if (kind == 6) {
                const int pair = Pair(s, v.format);
                if (pair >= 0) for (Word h : s.special_data[pair]) if (h) e.DiscardMapping(h);
            }
        }
    return 1;
}
inline Word Convert(Environment &e, Word handle, Word from, Word to) { // text arm of
    if (!handle) return 0;
    if (to != 1 && to != 7) return e.ConvertGraphics(handle, from, to);
    const Word result = e.GlobalAlloc(0x2042, e.GlobalSize(handle));
    if (!result) return 0;
    auto *dst = e.GlobalLock(result);
    if (!dst) { e.GlobalFree(result); return 0; }
    auto *src = e.GlobalLock(handle);
    if (!src) { e.GlobalUnlock(result); e.GlobalFree(result); return 0; }
    e.TranslateText(src, dst, to == 7);
    e.GlobalUnlock(handle); e.GlobalUnlock(result); return result;
}
inline Word ResolveSpecial(Environment &e, State &s, const Entry &v, Word flags) { //
    const int pair = Pair(s, v.format);
    if (pair < 0) return 0; // invalid internal sentinel is outside the provider contract
    auto *h = s.special_data[pair];
    if (flags & 1) {
        if (!h[1]) {
            const unsigned converted = e.ConvertSpecial(h[0], true);
            if (converted) { h[1] = e.To16(0xfffe, converted, true); if (h[1]) e.GlobalReAlloc(h[1], 0, 0x2080); }
        }
        return h[1];
    }
    if (!h[0]) {
        const unsigned converted = e.To32(0xfffe, h[1]);
        if (converted) {
            h[0] = Word(e.ConvertSpecial(converted, false)); e.DiscardMapping(h[1]);
            if (h[0]) e.GlobalReAlloc(h[0], 0, 0x2080);
        }
    }
    return h[0];
}
inline Word Get(Environment &e, State &s, Word format, Word flags) { //
    if (e.CurrentTask() != s.open_task) return 0;
    const int at = Find(s, format);
    if (at < 0) return 0;
    Entry &v = s.entries[at]; // original saves the physical entry across SendMessage
    if (!v.data && s.owner) {
        BeginNotify(e, s); const Word changed = s.changed;
        e.Send(s.owner, 0x305, v.format, 0, false);
        s.changed = changed; EndNotify(e, s);
    }
    Word result = v.data;
    if (result == 0xffff) {
        const Word source = v.format < 17 ? Related[v.format][0] : 0;
        result = Get(e, s, source, flags);
        v.data = result ? Convert(e, result, source, v.format) : 0; result = v.data;
    } else if (result == 0xfffe) result = ResolveSpecial(e, s, v, flags);
    if (v.format == 3 && result) {
        auto *p = e.GlobalLock(v.data);
        if (p) {
            const Word inner = ReadWord(p + 6); e.GlobalLock(inner);
            const Word valid = e.CheckMetaFile(inner);
            e.GlobalUnlock(inner); e.GlobalUnlock(v.data);
            result = v.data; if (valid == 0xffff) result = v.data = 0;
        }
    }
    return result;
}
inline Word Enumerate(Environment &e, const State &s, Word format) { //
    if (e.CurrentTask() != s.open_task || !s.entries) return 0;
    if (!format) return s.entries[0].format;
    const int at = Find(s, format);
    return at < 0 || at + 1 == s.count ? 0 : s.entries[at + 1].format;
}
inline Word Name(Environment &e, Word format, unsigned char *out, Word capacity) { //
    if (format < 0xbfff) { *out = 0; return 0; }
    return e.AtomName(format, out, capacity);
}
inline int Priority(const State &s, const Word *formats, Word count) { // public signed-int result
    if (!s.count || !s.entries) return 0;
    for (int i = 0; i < int(int16_t(count)); ++i) if (Find(s, formats[i]) >= 0) return formats[i];
    return -1;
}
inline unsigned SetViewer(Environment &e, State &s, unsigned window) { //
    const unsigned old = s.viewer; s.viewer = window; NotifyViewer(e, s);
    return old ? e.WindowHandle(old) : 0;
}
inline Word ChangeChain(Environment &e, State &s, unsigned remove, unsigned next) { //
    if (!s.viewer) return 0;
    if (s.viewer == remove) { s.viewer = next; return 1; }
    BeginNotify(e, s);
    const Word result = Word(e.Send(s.viewer, 0x30d, e.WindowHandle(remove), next ? e.WindowHandle(next) : 0, false));
    EndNotify(e, s); return result;
}
inline void OwnerDestroyed(Environment &e, State &s) { // called by USER before WM_DESTROY
    NotifyOwner(e, s, 0x306);
    Word count = 0; bool owner_display = false;
    for (int i = 0; i < int(int16_t(s.count)); ++i) {
        const Entry v = s.entries[i]; bool keep = v.data != 0;
        if (v.format == 0x80) owner_display = true;
        if (v.data == 0xffff) {
            const int source = Find(s, v.format < 17 ? Related[v.format][0] : 0);
            keep = source >= 0 && s.entries[source].data;
        } else if (v.data == 0xfffe) {
            const int pair = Pair(s, v.format);
            keep = pair >= 0 && (s.special_data[pair][0] || s.special_data[pair][1]);
        }
        if (keep) s.entries[count++] = v;
    }
    s.owner = 0;
    if (s.count != count || owner_display) { s.count = count; s.changed = 1; NotifyViewer(e, s); }
}
// USER: conversion failure precedes Set's open/table checks.
inline unsigned Get32(Environment &e, State &s, Word format) {
    const Word data = Get(e, s, format, 1); return data ? e.To32(format, data) : 0;
}
inline unsigned Set32(Environment &e, State &s, Word format, unsigned data) {
    const Word converted = data ? e.To16(format, data, true) : 0;
    if (data && !converted) return 0;
    return Set(e, s, format, converted, 1) ? data : 0;
}
inline unsigned Available32(const State &s, unsigned format) { // USER32
    return format <= 0xffff && Find(s, Word(format)) >= 0 ? format : 0;
}
} // namespace Win98Clipboard
