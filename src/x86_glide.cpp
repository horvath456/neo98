// SPDX-License-Identifier: GPL-2.0-or-later
// GLIDE2X.DLL, the Glide 2 driver of a 3dfx card, as a native provider on the
// one rasterizer of the port.
// Glide is no part of Windows 98 but the driver a card vendor shipped, like
// the HAL in a display driver, so it has the route Native in
// src/dll_routes.h. Every export is a gate of the pseudo module GLIDE2X.DLL
// with the name, ordinal and argument bytes of the driver 2.61.00.0658 of a
// Voodoo 3 (src/win98_glide_exports.h). A program's own glide2x.dll, wherever
// it lies, is a driver for real hardware and cannot run here; the loader
// takes the native one by the name alone (src/x86_modules.cpp).
// The gates end in Glide::Context (src/glide_context.cpp), which knows
// neither Unicorn nor the guest; this file is its Glide::Host: guest memory,
// the display mode of grSstWinOpen, the picture on the screen and the
// vertical blank. There is a driver only when a rasterizer that draws Glide
// is installed; NEO98_NO_GLIDE=1 hides it, and a program then finds no
// glide2x.dll, as on a machine without a 3dfx card.
#include "x86_app.h"
#include <stdio.h>
#include "win98_glide_exports.h"
// The Pi kernel is built without a C++ library and without a rasterizer, so
// it gets the driver that is never there (the end of this file).
#if __has_include(<vector>)
#include "glide_context.h"
#include "user.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace X86
{
const char *Application::GlideModule() { return "GLIDE2X.DLL"; }

unsigned Application::GlideEntry(const char *name)
{
    for (const auto &e : Win98Glide::Exports)
        if (!strcmp(e.name, name)) return e.ordinal;
    return ~0u;
}

unsigned Application::GlideArgumentCount(const char *name)
{
    for (const auto &e : Win98Glide::Exports)
        if (!strcmp(e.name, name)) return e.argument_bytes / 4;
    return ~0u;
}

const char *Application::GlideExportName(unsigned ordinal)
{
    for (const auto &e : Win98Glide::Exports)
        if (e.ordinal == ordinal) return e.name;
    return nullptr;
}

// A string value of the driver's settings key, the one where the Glide
// driver of the Voodoo 3 kept its own settings (the string in GLIDE2X.DLL
// 2.61): HKLM System\CurrentControlSet\Services\3Dfx\Device0\glide.
bool Application::GlideRegistryString(const char *value, char *out, unsigned capacity)
{
    out[0] = 0;
    const unsigned text = Allocate(600);
    if (!text) return false;
    PutString(text, "System\\CurrentControlSet\\Services\\3Dfx\\Device0\\glide");
    PutString(text + 300, value);
    const unsigned open[] = {0x80000002u, text, 0, 0x20019, text + 400};
    bool found = false;
    if (!Api("RegOpenKeyExA", open))
    {
        const unsigned key = Read32(text + 400);
        Write32(text + 404, 64);
        const unsigned query[] = {key, text + 300, 0, 0, text + 408, text + 404};
        if (!Api("RegQueryValueExA", query))
        {
            snprintf(out, capacity, "%s", ReadString(text + 408));
            found = true;
        }
        const unsigned close[] = {key};
        Api("RegCloseKey", close);
    }
    Free(text);
    return found;
}

bool Application::GlideAvailable()
{
    return gpu_backend != nullptr && gpu_backend->Glide() && !getenv("NEO98_NO_GLIDE");
}

// The calls that may wait, change the display mode or return on the FPU
// stack take the ordinary way; every other one is served from the gate.
bool Application::GlideFastEntry(unsigned ordinal)
{
    using namespace Win98Glide;
    switch (ordinal)
    {
    case grSstWinOpen: case grSstWinClose: case grGlideShutdown: case grBufferSwap:
    case guFogTableIndexToW: case grLfbLock: case grLfbUnlock:
        return false;
    default:
        // The entries of GLIDEPAG.DLL call back into the property sheet.
        return ordinal < 0x1000;
    }
}

struct Application::GlideState
{
    std::unique_ptr<Glide::Context> context;
    std::vector<unsigned short> scanout;   // the picture the monitor shows
    int previous_width = 0, previous_height = 0, previous_depth = 0;
    bool screen = false;
    // GLIDEPAG.DLL: the method tables, the class factory and the page's
    // dialog template in guest memory (src/x86_glide_page.inc).
    unsigned page_tables = 0, page_factory = 0, page_template = 0;
};

class GlideHost final : public Glide::Host
{
  public:
    GlideHost(Application &app, Application::GlideState &state) : app(app), state(state) {}
    bool Read(unsigned address, void *out, unsigned bytes) override { return !bytes || app.Read(address, out, bytes); }
    bool Write(unsigned address, const void *in, unsigned bytes) override { return !bytes || app.Store(address, in, bytes); }
    unsigned Allocate(unsigned bytes) override { return app.Allocate(bytes); }
    // grSstWinOpen takes the screen as an exclusive DirectDraw program does:
    // the 16-bit mode of its resolution, and back at grSstWinClose.
    bool OpenScreen(int width, int height) override
    {
        if (!state.screen) User::ScreenMode(state.previous_width, state.previous_height, state.previous_depth);
        if (app.ApplyDisplayMode(width, height, 16, 0) != 0) return false;
        state.screen = true;
        state.scanout.assign(size_t(width) * size_t(height), 0);
        User::SetScanout(reinterpret_cast<const unsigned char *>(state.scanout.data()), width * 2);
        User::ScreenChanged();
        return true;
    }
    void CloseScreen() override
    {
        if (!state.screen) return;
        state.screen = false;
        User::ResetScanout();
        User::ClearOverlay();
        // A process on its way out has no thread left to answer the
        // WM_DISPLAYCHANGE broadcast; the mode goes back without it.
        if (quiet) User::SetScreenMode(state.previous_width, state.previous_height, state.previous_depth);
        else app.ApplyDisplayMode(state.previous_width, state.previous_height, state.previous_depth, 0);
        User::ScreenChanged();
    }
    bool quiet = false;
    // NEO98_GLIDE_SCALE, then the value NEO98_SCALE the display properties
    // keep where the 3dfx driver kept its settings, then the largest whole
    // multiple of the program's resolution that fits the host's output.
    // Edge smoothing (NEO98_GLIDE_ANTIALIAS or NEO98_ANTIALIAS) doubles the
    // multiple; the host shrinks the picture to its window with a linear
    // filter, which at half the size is the mean of four samples.
    int Scale(int width, int height) override
    {
        int scale = Setting("NEO98_GLIDE_SCALE", "NEO98_SCALE");
        if (scale < 1)
        {
            int host_width = 0, host_height = 0;
            User::HostOutput(host_width, host_height);
            scale = host_width > 0 && host_height > 0 && width > 0 && height > 0
                        ? std::min(host_width / width, host_height / height) : 1;
        }
        if (scale < 1) scale = 1;
        if (Setting("NEO98_GLIDE_ANTIALIAS", "NEO98_ANTIALIAS") > 0) scale *= 2;
        return scale > 8 ? 8 : scale;
    }
    void Present(const unsigned *bits, int width, int height, const unsigned *full, int full_width, int full_height) override
    {
        if (!state.screen) return;
        // The card shows its own front buffer; nothing GDI draws is seen
        // while the window is open, as with the pass-through of a Voodoo.
        // The 16-bit mode of the display driver is 555, as the DIB engine
        // of Windows 98 has it (src/win98_display_modes.h).
        const size_t count = size_t(width) * size_t(height);
        if (state.scanout.size() < count) state.scanout.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            const unsigned c = bits[i];
            state.scanout[i] = (unsigned short)(((c >> 9) & 0x7c00) | ((c >> 6) & 0x03e0) | ((c >> 3) & 0x001f));
        }
        User::SetScanout(reinterpret_cast<const unsigned char *>(state.scanout.data()), width * 2);
        // The picture at the resolution the driver renders at goes to the
        // host's window as it is.
        if (full && (full_width != width || full_height != height)) User::SetOverlay(full, full_width, full_height);
        else User::ClearOverlay();
        User::ScreenChanged();
    }
    void Pace() override { app.PaceFrame(); }
    unsigned long long Microseconds() override { return Application::Now(); }
    void Fail(const char *message) override { app.Fail(message); }
    void Trace(const char *line) override { if (app.log) app.log(line); }

  private:
    // The number in the environment variable, else in the registry value;
    // 0 when neither is there.
    int Setting(const char *variable, const char *value)
    {
        if (const char *text = getenv(variable)) return atoi(text);
        char text[32];
        return app.GlideRegistryString(value, text, sizeof text) ? atoi(text) : 0;
    }
    Application &app;
    Application::GlideState &state;
};

// A saved state carries the buffers of the card as bytes; the rasterizer
// holds the newer picture (src/x86_state_machine.cpp).
void Application::GlideStateSync()
{
    if (glide && glide->context) glide->context->StateSync();
}
void Application::GlideRelease()
{
    if (!glide) return;
    if (glide->context && glide->context->Open())
    {
        GlideHost host(*this, *glide);
        host.quiet = true;
        glide->context->Close(host);
    }
    delete glide;
    glide = nullptr;
}

unsigned Application::GlideApi(unsigned ordinal, const unsigned *a)
{
    if (!GlideAvailable()) return 0;
    if (!glide) glide = new GlideState;
    if (ordinal >= 0x1000) return GlidePageApi(ordinal, a);
    // The owner keeps the driver's textures apart from the Direct3D surfaces
    // and the OpenGL objects of the same process in the backend's caches.
    if (!glide->context) glide->context.reset(new Glide::Context(*gpu_backend, process_id | 0x40000000u));
    GlideHost host(*this, *glide);
    if (ordinal == Win98Glide::guFogTableIndexToW)
    {
        // float guFogTableIndexToW(int i) returns in ST(0): 2^(3 + i/4) / (8 - i%4).
        const int i = int(a[0]);
        const float w = float(std::pow(2.0, 3.0 + double(i >> 2)) / double(8 - (i & 3)));
        const double wide = w;
        unsigned long long bits;
        memcpy(&bits, &wide, 8);
        ReturnDoubleBits(bits);
        return 0;
    }
    return glide->context->Call(ordinal, a, host);
}

#include "x86_glide_page.inc"
} // namespace X86
#else
namespace X86
{
struct Application::GlideState {};
const char *Application::GlideModule() { return "GLIDE2X.DLL"; }
unsigned Application::GlideEntry(const char *) { return ~0u; }
unsigned Application::GlideArgumentCount(const char *) { return ~0u; }
const char *Application::GlideExportName(unsigned) { return nullptr; }
bool Application::GlideAvailable() { return false; }
bool Application::GlideFastEntry(unsigned) { return false; }
void Application::GlideRelease() {}
void Application::GlideStateSync() {}
unsigned Application::GlideApi(unsigned, const unsigned *) { return 0; }
bool Application::GlideRegistryString(const char *, char *, unsigned) { return false; }
const char *Application::GlidePageModule() { return "GLIDEPAG.DLL"; }
unsigned Application::GlidePageEntry(const char *) { return ~0u; }
unsigned Application::GlidePageArgumentCount(const char *) { return ~0u; }
} // namespace X86
#endif
