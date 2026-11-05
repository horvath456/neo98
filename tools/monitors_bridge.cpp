// SPDX-License-Identifier: GPL-2.0-or-later
// Drives the ported Win98 monitor core from the original-comparison harness.
#include "../src/win98_monitors.h"
#include <string.h>
using namespace Win98Monitors;
namespace {
Rect At(const U *a, U base) { return {short(a[base]), short(a[base + 1]), short(a[base + 2]), short(a[base + 3])}; }
struct E {
    const U *a;
    U seq = 0;
    U Count() const { return a[1]; }
    Rect Monitor(U i) const { return At(a, 2 + i * 8); }
    Rect Work(U i) const { return At(a, 6 + i * 8); }
    Rect VirtualScreen() const { return At(a, 34); }
    // Index 0 takes the display-driver name, every other record its own field.
    void Name(U i, char (&out)[32]) const {
        memset(out, 0, sizeof out);
        if (!i) { memcpy(out, "\\\\.\\DISPLAY1", 12); return; }
        out[0] = 'M'; out[1] = 'O'; out[2] = 'N'; out[3] = char('0' + i);
    }
    U Handle(U i) const { return 0x11a2 + i * 0x3e; }
    U Sequence() const { return seq; }
    U Validate(U h) const { return (h && (a[45] & 1)) ? h : 0; }
    bool HasProperties(U) const { return (a[45] >> 1) & 1; }
    bool MonitorProperty(U, Rect &r) const { if (!((a[45] >> 2) & 1)) return false; r = At(a, 46); return true; }
    bool ParentIsDesktop(U) const { return (a[45] >> 3) & 1; }
    Rect WindowRect(U) const { return At(a, 50); }
};
void Put(U *out, U base, const Rect &r) { out[base] = U(r.left); out[base + 1] = U(r.top); out[base + 2] = U(r.right); out[base + 3] = U(r.bottom); }
} // namespace
extern "C" U monitors_run(const U *a, U *out) {
    E e{a};
    for (U i = 0; i < 40; ++i) out[i] = 0;
    switch (a[0]) {
    case 0: return FromPoint(e, short(a[39]), short(a[40]), a[38]);
    case 1: return FromRect(e, At(a, 39), a[38]);
    case 2: return FromWindow(e, a[43], a[38]);
    case 3: {
        Info info;
        info.size = a[44];
        const U result = Query(e, a[43], info);
        Put(out, 1, info.monitor); Put(out, 5, info.work); out[9] = info.flags;
        memcpy(out + 10, info.device, 32);
        return result;
    }
    default: {
        const Rect clip = At(a, 55);
        U calls = 0;
        const U result = Enumerate(e, a[54] ? &clip : nullptr, [&](U handle, const Rect &area) -> U {
            if (calls < 4) { out[2 + calls * 5] = handle; Put(out, 3 + calls * 5, area); }
            ++calls;
            if (calls == a[61]) ++e.seq;
            return calls == a[59] ? a[60] & 65535 : 1;
        });
        out[1] = calls;
        return U(int(short(result))); // USER32 sign-extends the 16-bit result.
    }
    }
}
