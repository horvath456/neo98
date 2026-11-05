// SPDX-License-Identifier: GPL-2.0-or-later
// The x86 side of the GDI32 path family, the three curve entries and the two
// flood fills.
#include "x86_app.h"
#define PATH_MEMORY(name)                                                      \
    struct Memory final : Gdi::BitmapIO                                        \
    {                                                                          \
        Application &app;                                                      \
        explicit Memory(Application &a) : app(a) {}                            \
        bool Read(unsigned p, void *d, unsigned n) override { return app.Read(p, d, n); }         \
        bool Write(unsigned p, const void *d, unsigned n) override { return app.Store(p, d, n); } \
        bool Probe(unsigned p, bool w) override { return app.virtual_memory.Access(p, 1, w); }    \
        void Error(unsigned code) override { app.last_error = code; }          \
        void Fault(const char *why) override { app.Fail(why); }                \
    } name(*this)
namespace X86 {
// Every drawing entry of GDI.EXE tests the open bit of DC+7Ah before it does
// anything. The four recorders are (MoveTo), (the line and
// curve runs), (the polygon runs) and (text); everything else
// answers FALSE, as shows for RECTANGLE.
bool Application::PathRecord(ApiName name, const unsigned *a, unsigned &result)
{
    if (!a || !Gdi::PathRecording(a[0]))
        return false;
    PATH_MEMORY(memory);
    return Gdi::PathRecord(name, a, memory, result);
}
unsigned Application::GdiPathsApi(ApiName name, const unsigned *a)
{
    PATH_MEMORY(memory);
    const unsigned result = Gdi::PathApi(name, a, memory);
    if (!result)
        return 0;
    // PathToRegion hands out a new object; the painting entries and the flood
    // fills have touched the surface.
    if (API_IS(name, "PathToRegion"))
        return OwnGdi(result, 1);
    switch (name.id)
    {
    case API_ID("FillPath"):
    case API_ID("StrokePath"):
    case API_ID("StrokeAndFillPath"):
    case API_ID("PolyBezier"):
    case API_ID("PolyBezierTo"):
    case API_ID("PolylineTo"):
    case API_ID("PolyPolyline"):
    case API_ID("FloodFill"):
    case API_ID("ExtFloodFill"):
        User::DrawingChanged(a[0]);
        break;
    default:
        break;
    }
    return result;
}
} // namespace X86
