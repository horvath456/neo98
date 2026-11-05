// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_change_notification.h"
namespace
{
constexpr unsigned MemoryBase = 0x10fef4, Buffer = 0x10fef4, Object = 0x300200;
struct E
{
    const unsigned *c;
    const char *text;
    unsigned char *m;
    unsigned *object, *o;
    unsigned error = 0;
    bool cleared = false;
    void Event(unsigned op, unsigned x = 0, unsigned y = 0, unsigned z = 0, unsigned w = 0)
    {
        unsigned at = 3 + o[1] * 5;
        o[at] = op;
        o[at + 1] = x;
        o[at + 2] = y;
        o[at + 3] = z;
        o[at + 4] = w;
        ++o[1];
    }
    unsigned Read8(unsigned p) const { return p - MemoryBase < 0x104 ? m[p - MemoryBase] : 0; }
    void Write8(unsigned p, unsigned v)
    {
        if (p - MemoryBase < 0x104)
            m[p - MemoryBase] = (unsigned char)v;
    }
    unsigned Scratch() const { return Buffer; }
    void Lock() { Event(1); }
    void Unlock() { Event(2); }
    void LockHeap() { Event(3); }
    void UnlockHeap() { Event(4); }
    void Error(unsigned e)
    {
        error = e;
        Event(5, e);
    }
    unsigned LastError() const { return error; }
    unsigned FileApiMode(unsigned mode)
    {
        Event(6, mode);
        return c[2];
    }
    // The result flag represents clearing the two notification-state bits
    // observed by the reference test.
    void ClearFileApiState(unsigned) { cleared = true; }
    unsigned FullPath(unsigned path, unsigned capacity, unsigned buffer, unsigned part)
    {
        Event(8, path, capacity, buffer, part);
        if (c[0] == 0)
            return 0;
        if (c[0] == 1)
            return 0x104;
        // The original resolver never writes past the capacity it was given.
        unsigned n = 0;
        while (text[n])
        {
            if (n < capacity)
                Write8(buffer + n, (unsigned char)text[n]);
            ++n;
        }
        if (n < capacity)
            Write8(buffer + n, 0);
        return n;
    }
    unsigned AllocateObject(unsigned size, unsigned type)
    {
        Event(9, size, type);
        if (c[3])
            return 0;
        object[0] = 0;
        object[1] = 0x1234abcd;
        object[2] = 0;
        return Object;
    }
    void FreeObject(unsigned p) { Event(10, p); }
    unsigned SignalObject(unsigned p) const { return object[(p - Object) / 4 + 1]; }
    unsigned DosHandle(unsigned p) const { return object[(p - Object) / 4 + 2]; }
    void SetDosHandle(unsigned p, unsigned v) { object[(p - Object) / 4 + 2] = v; }
    bool IfsOpen(unsigned path, unsigned subtree, unsigned filter, unsigned low, unsigned high,
                 unsigned &result)
    {
        Event(0x71a3, path, subtree, filter, low | (high << 16));
        if (c[4])
        {
            result = c[4];
            return false;
        }
        result = 0x7788;
        return true;
    }
    bool IfsNext(unsigned handle, unsigned &result)
    {
        Event(0x71a4, handle);
        if (c[5])
        {
            result = c[5];
            return false;
        }
        return true;
    }
    bool IfsClose(unsigned handle, unsigned &result)
    {
        Event(0x71a5, handle);
        if (c[6])
        {
            result = c[6];
            return false;
        }
        return true;
    }
    unsigned CreateHandle(unsigned p)
    {
        Event(11, p);
        return c[7] ? ~0u : 0x60000123u;
    }
    unsigned Resolve(unsigned handle, unsigned mask)
    {
        Event(12, handle, mask);
        if (c[8])
        {
            Error(6);
            return 0;
        }
        return Object;
    }
    void ReleaseReference(unsigned p) { Event(13, p); }
};
} // namespace
extern "C" unsigned change_notification_run(unsigned op, const unsigned *a, const unsigned *config,
                                            const char *text, unsigned char *memory, unsigned *object,
                                            unsigned *out)
{
    E e{config, text, memory, object, out};
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    unsigned result = 0;
    if (op == 0)
        result = Win98ChangeNotification::First(e, a[0], a[1], a[2]);
    else if (op == 1)
        result = Win98ChangeNotification::Next(e, a[0]);
    else if (op == 2)
    {
        Win98ChangeNotification::StripTrailingSeparators(e, a[0]);
        result = a[0]; // The original leaves its buffer argument in EAX.
    }
    else
        result = Win98ChangeNotification::FirstWide(e);
    out[0] = e.error;
    out[2] = e.cleared;
    return result;
}
