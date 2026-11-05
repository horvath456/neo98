// SPDX-License-Identifier: GPL-2.0-or-later
// Local mailslot queues, message delivery, and handle validation. Native
// providers supply storage, timing, and synchronization.
#pragma once
namespace Win98Mailslot
{
typedef unsigned U;
enum { RingSize = 0x1000, DefaultMaximum = 0x1000, NameCapacity = 128 };
struct Slot
{
    bool used = false, default_size = false;
    char name[NameCapacity] = {};
    U maximum = 0, timeout = 0, head = 0, tail = 0, pending = 0;
    unsigned char ring[RingSize] = {};
};
inline U Upper(U c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
// 0x140dc43..0x140dcb1: "\\." then "MAILSLOT\" then a non-empty rest.
inline U ParseName(const char *path, char (&out)[NameCapacity])
{
    if (!path || path[0] != '\\' || path[1] != '\\') return 123;
    const char *p = path + 2;
    if (p[0] != '.' || (p[1] && p[1] != '\\')) return 123;
    if (!p[1]) return 3;
    p += 2;
    static const char component[] = "MAILSLOT";
    for (U i = 0; component[i]; ++i, ++p) if (Upper((unsigned char)*p) != (U)component[i]) return 3;
    if (*p != '\\') return 3;
    ++p;
    if (!*p) return 3;
    U n = 0;
    for (; p[n]; ++n) { if (n + 1 >= NameCapacity) return 123; out[n] = (char)Upper((unsigned char)p[n]); }
    out[n] = 0;
    return 0;
}
inline bool SameName(const char *a, const char *b)
{
    for (;; ++a, ++b) { if (Upper((unsigned char)*a) != Upper((unsigned char)*b)) return false; if (!*a) return true; }
}
inline U Free(const Slot &s)   // 0x140ea21, 16-bit arithmetic
{
    if (s.head > s.tail) return (s.head - s.tail - 1) & 0xffff;
    return (RingSize - s.tail + s.head - 1) & 0xffff;
}
inline U Message(const Slot &s, U at) { return s.ring[at % RingSize] | (s.ring[(at + 1) % RingSize] << 8); }
template <U N> Slot *Find(Slot (&slots)[N], const char *name)
{
    for (auto &s : slots) if (s.used && SameName(s.name, name)) return &s;
    return nullptr;
}
template <U N> U Create(Slot (&slots)[N], const char *path, U maximum, U timeout, Slot *&created)
{
    char name[NameCapacity];
    const U error = ParseName(path, name);
    if (error) return error;
    if (Find(slots, name)) return 183;
    for (auto &s : slots)
        if (!s.used)
        {
            s = Slot();
            s.used = true;
            for (U i = 0;; ++i) { s.name[i] = name[i]; if (!name[i]) break; }
            s.default_size = maximum == 0;
            s.maximum = maximum ? maximum : DefaultMaximum;
            s.timeout = timeout;
            created = &s;
            return 0;
        }
    return 4;
}
// Client write; the slot was already found by name (2 when absent).
inline U Write(Slot &s, const unsigned char *data, U length)
{
    if (s.maximum && length > s.maximum) return s.maximum;   // 0x140e6ea: the size itself travels as the error
    if (Free(s) < length + 2) return 121;
    U at = s.tail;
    s.ring[at % RingSize] = (unsigned char)length; s.ring[(at + 1) % RingSize] = (unsigned char)(length >> 8);
    at += 2;
    for (U i = 0; i < length; ++i) s.ring[(at + i) % RingSize] = data[i];
    s.tail = (at + length) % RingSize;
    return 0;
}
enum ReadResult { Copied = 0, Empty = 1 };
// Server read of the next message: 0 copied, 1 empty, 111 short buffer.
inline U Read(Slot &s, unsigned char *out, U capacity, U &length)
{
    length = 0;
    if (s.head == s.tail) return Empty;
    const U size = Message(s, s.head);
    if (capacity < size) return 111;
    for (U i = 0; i < size; ++i) out[i] = s.ring[(s.head + 2 + i) % RingSize];
    s.head = (s.head + 2 + size) % RingSize;
    length = size;
    return Copied;
}
inline void Info(const Slot &s, U &maximum, U &next, U &count, U &timeout)
{
    maximum = s.default_size ? 0 : s.maximum;
    timeout = s.timeout;
    count = 0;
    next = ~0u;
    for (U at = s.head; at != s.tail; ++count)
    {
        const U size = Message(s, at);
        if (!count) next = size;
        at = (at + 2 + size) % RingSize;
    }
}
}
