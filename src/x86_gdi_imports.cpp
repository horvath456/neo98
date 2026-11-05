// SPDX-License-Identifier: GPL-2.0-or-later
// GDI32 APIs implemented by the native graphics layer. Unsupported Windows 98
// exports retain their failure behavior in src/win98_gdi_missing_exports.h.
#include "x86_app.h"
namespace X86 {
// Windows 98 GdiFlush returns TRUE because there is no pending batch.
// GetGraphicsMode returns GM_COMPATIBLE for every DC, including invalid
// handles.
unsigned Application::GdiImportsApi(ApiName name, const unsigned *a)
{
    if (API_IS(name, "GdiFlush"))
        return 1;
    if (API_IS(name, "GetGraphicsMode"))
        return 1;
    struct Memory final : Gdi::BitmapIO
    {
        Application &app;
        explicit Memory(Application &a) : app(a) {}
        bool Read(unsigned p, void *data, unsigned size) override { return app.Read(p, data, size); }
        bool Write(unsigned p, const void *data, unsigned size) override { return app.Store(p, data, size); }
        bool Probe(unsigned p, bool write) override { return app.virtual_memory.Access(p, 1, write); }
        void Error(unsigned code) override { app.last_error = code; }
        void Fault(const char *why) override { app.Fail(why); }
    } memory(*this);
    const unsigned result = Gdi::RegionBrushApi(name, a, memory);
    if (!result)
        return 0;
    // The creators hand out a new object; the four region drawing calls have
    // touched the surface and the compositor has to learn about it.
    if (API_IS(name, "CreateEllipticRgn") || API_IS(name, "CreateEllipticRgnIndirect") ||
        API_IS(name, "CreateRoundRectRgn") || API_IS(name, "CreateHatchBrush") ||
        API_IS(name, "CreateBrushIndirect"))
        return OwnGdi(result, 1);
    if (API_IS(name, "FillRgn") || API_IS(name, "FrameRgn") ||
        API_IS(name, "InvertRgn") || API_IS(name, "PaintRgn"))
        User::DrawingChanged(a[0]);
    return result;
}
} // namespace X86
