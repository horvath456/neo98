// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_critical.h"
#include "../src/win98_crt.h"
#include "../src/win98_tls.h"
#include <cassert>
#include <cstring>
namespace
{
constexpr unsigned Client = 0x102000, Object = 0x104000, NewObject = 0x104100;
struct Critical final : Win98Critical::Environment
{
    unsigned c[8], config, thread, trace[128]{}, count = 0;
    Win98Critical::Mutex m[2];
    void Event(unsigned kind, unsigned value = 0)
    {
        assert(count + 2 <= 128);
        trace[count++] = kind;
        trace[count++] = value;
    }
    Win98Critical::Client ReadClient(unsigned h) override
    {
        assert(h == Client);
        return {c[0], c[1]};
    }
    bool ProbeClient(unsigned h) override { return h == Client; }
    void TouchClient(unsigned h) override { assert(h == Client); }
    void WriteTag(unsigned h, unsigned char value) override
    {
        assert(h == Client);
        c[0] = (c[0] & ~255u) | value;
    }
    void WriteObject(unsigned h, unsigned value) override
    {
        assert(h == Client);
        c[1] = value;
    }
    Win98Critical::Mutex *FindMutex(unsigned id) override
    {
        return id == Object ? m : id == NewObject ? m + 1 : nullptr;
    }
    Win98Critical::Mutex &Data(unsigned id) override
    {
        auto *p = FindMutex(id);
        assert(p);
        return *p;
    }
    bool HeapContains(unsigned id) override
    {
        Event(13, id);
        return !(config & 16);
    }
    unsigned CurrentThread() override { return thread; }
    void GlobalLock() override { Event(1); }
    void GlobalUnlock() override { Event(2); }
    unsigned AllocateMutex() override
    {
        Event(3, 32);
        return config & 1 ? 0 : NewObject;
    }
    void InitializeMutex(unsigned id) override
    {
        Event(4, id);
        Data(id) = {};
    }
    bool ProcessPresent() override { return !(config & 8); }
    unsigned CreateProcessList() override
    {
        Event(5);
        return config & 2 ? 0 : 0x1234;
    }
    bool BindProcess(unsigned h) override
    {
        Event(6, h);
        return !(config & 4);
    }
    void DestroyProcessList(unsigned h) override { Event(7, h); }
    void DestroyMutex(unsigned id) override { Event(8, id); }
    void FreeMutex(unsigned id) override { Event(9, id); }
    void Wait(unsigned id) override
    {
        Event(10, id);
        if (config & 32)
        {
            Data(id).owner = thread;
            Data(id).recursion = 1;
        }
    }
    void Wake(unsigned id) override { Event(11, id); }
    bool Fault(unsigned code) override
    {
        Event(12, code);
        return true;
    }
};
struct Tls final : Win98Tls::Environment
{
    unsigned bits[3], data[3][80], error, trace[128]{}, count = 0;
    void Event(unsigned kind, unsigned value)
    {
        assert(count + 2 <= 128);
        trace[count++] = kind;
        trace[count++] = value;
    }
    void Lock(bool g) override { Event(1, g); }
    void Unlock(bool g) override { Event(2, g); }
    unsigned &Bitmap(unsigned word) override { return bits[word]; }
    unsigned Read(unsigned index) override { return data[0][index]; }
    void Write(unsigned index, unsigned value) override { data[0][index] = value; }
    void ClearThreads(unsigned index) override
    {
        Event(3, index);
        for (auto &t : data)
            t[index] = 0;
    }
    void Error(unsigned value) override { error = value; }
};
} // namespace
extern "C" unsigned critical(unsigned op, unsigned *state, unsigned thread, unsigned config, unsigned *trace)
{
    Critical e;
    memcpy(e.c, state, 32);
    memcpy(e.m, state + 8, 64);
    e.thread = thread;
    e.config = config;
    unsigned result = 0;
    switch (op)
    {
    case 0:
        Win98Critical::Enter(e, Client);
        break;
    case 1:
        Win98Critical::Leave(e, Client);
        break;
    case 2:
    case 5:
        Win98Critical::Initialize(e, Client);
        break;
    case 3:
        Win98Critical::Delete(e, Client);
        break;
    case 4:
        result = Win98Critical::Valid(e, Client);
        break;
    case 6:
        Win98Critical::EnterMutex(e, Object);
        break;
    case 7:
        Win98Critical::LeaveMutex(e, Object);
        break;
    default:
        assert(false);
    }
    memcpy(state, e.c, 32);
    memcpy(state + 8, e.m, 64);
    trace[0] = e.count;
    memcpy(trace + 1, e.trace, e.count * 4);
    return result;
}
extern "C" unsigned tls(unsigned op, unsigned *state, unsigned index, unsigned value, unsigned *trace)
{
    Tls e;
    memcpy(e.bits, state, 12);
    memcpy(e.data, state + 3, sizeof e.data);
    e.error = state[243];
    unsigned result = 0;
    switch (op)
    {
    case 0:
        result = Win98Tls::Alloc(e);
        break;
    case 1:
        result = Win98Tls::Free(e, index);
        break;
    case 2:
        result = Win98Tls::Get(e, index);
        break;
    case 3:
        result = Win98Tls::Set(e, index, value);
        break;
    default:
        assert(false);
    }
    memcpy(state, e.bits, 12);
    memcpy(state + 3, e.data, sizeof e.data);
    state[243] = e.error;
    trace[0] = e.count;
    memcpy(trace + 1, e.trace, e.count * 4);
    return result;
}
static unsigned register_cleanup(unsigned *state, unsigned size, unsigned fail, unsigned function,
                                 unsigned *trace, bool process)
{
    struct Environment
    {
        unsigned *state, size, fail, *trace, count = 0;
        unsigned char *Pointer(unsigned p)
        {
            if (p >= Client && p < Client + 16)
                return (unsigned char *)state + p - Client;
            assert(p >= Object && p + 4 <= Object + 512);
            return (unsigned char *)(state + 4) + p - Object;
        }
        unsigned Read(unsigned p)
        {
            unsigned v;
            memcpy(&v, Pointer(p), 4);
            return v;
        }
        void Write(unsigned p, unsigned v) { memcpy(Pointer(p), &v, 4); }
        void Event(unsigned kind, unsigned value)
        {
            trace[1 + count++] = kind;
            trace[1 + count++] = value;
        }
        void Lock() { Event(1, 13); }
        void Unlock() { Event(2, 13); }
        unsigned Size(unsigned p)
        {
            Event(3, p);
            return size;
        }
        unsigned Resize(unsigned p, unsigned n)
        {
            Event(4, n);
            if (fail)
                return 0;
            memcpy(Pointer(NewObject), Pointer(p), size);
            return NewObject;
        }
    } e{state, size, fail, trace};
    const auto result = process ? Win98Crt::OnExit(e, function, Client, Client + 4)
                                : Win98Crt::DllOnExit(e, function, Client, Client + 4);
    trace[0] = e.count;
    return result;
}

extern "C" unsigned dll_onexit(unsigned *state, unsigned size, unsigned fail, unsigned function,
                               unsigned *trace)
{
    return register_cleanup(state, size, fail, function, trace, false);
}
extern "C" unsigned process_onexit(unsigned *state, unsigned size, unsigned fail, unsigned function,
                                   unsigned *trace)
{
    return register_cleanup(state, size, fail, function, trace, true);
}
extern "C" unsigned process_cleanup(unsigned *table, unsigned count, unsigned *trace)
{
    struct Environment
    {
        unsigned *table, count, *trace, calls = 0;
        unsigned Read(unsigned p)
        {
            if (p == Client)
                return Object;
            if (p == Client + 4)
                return Object + count * 4;
            assert(p >= Object && p < Object + count * 4 && !(p & 3));
            return table[(p - Object) / 4];
        }
        bool Invoke(unsigned f)
        {
            trace[calls++] = f;
            return true;
        }
    } e{table, count, trace};
    Win98Crt::Cleanup(e, Client, Client + 4);
    return e.calls;
}
