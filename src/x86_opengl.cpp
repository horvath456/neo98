// SPDX-License-Identifier: GPL-2.0-or-later
// The installable client driver (ICD) of the NEO98 display driver: OpenGL
// on the one rasterizer of the port.
// The original OPENGL32.DLL of Windows 98 looks for a driver in
// wglCreateContext's helper:
// 1. ExtEscape(hdc, QUERYESCSUPPORT, OPENGL_GETINFO) has to be positive.
// 2. ExtEscape(hdc, OPENGL_GETINFO, 4, {0}, 0x214, info) answers
// an OPENGL_INFO: ulVersion 2, the driver version for DrvValidateVersion
// and, on Windows 9x, the ANSI name of the driver at offset 8.
// 3. That name is a REG_SZ value under HKLM\Software\Microsoft\Windows\
// CurrentVersion\OpenGLDrivers, and its data is the DLL.
// 4. LoadLibraryA of that DLL, GetProcAddress of DrvValidateVersion,
// DrvSetCallbackProcs and the sixteen driver entry points. The driver
// needs DrvCreateContext or DrvCreateLayerContext, DrvDeleteContext,
// DrvSetContext, DrvReleaseContext, DrvDescribePixelFormat,
// DrvSetPixelFormat and DrvSwapBuffers.
// 5. wglMakeCurrent calls DrvSetContext(hdc, dhglrc, pfnSetProcTable)
// and takes the table it returns when its count is 0x132 or
// 0x150; every gl* export of OPENGL32 then jumps through that table.
// The display driver answers the escape (src/x86_gdi_device.cpp), the INF
// value is installed with the registry (src/win98_opengl_registration.h), and
// NEO98GL32.DLL is a native provider like NEO98DD32.DLL: every export and
// every entry of the dispatch table is a gate. The gl* gates end in
// OpenGL::Context (src/opengl_context.cpp). There is a driver only when a
// rasterizer is installed; NEO98_NO_OPENGL=1 hides it, and OPENGL32 then keeps
// its own software implementation.
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
// Standalone builds without the rendering context use the stub below.
#if __has_include(<vector>)
#include "opengl_context.h"
#include "opengl_batch.h"
#include "render_profile.h"
#include "user.h"
#include "win98_opengl_dispatch.h"
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>

namespace X86
{
namespace
{
constexpr unsigned OpenGLGetInfo = 0x1101;
constexpr unsigned IcdVersion = 2, IcdDriverVersion = 0x00010000;
constexpr const char *IcdName = "NEO98";
// The entries of NEO98GL32.DLL besides the dispatch table.
enum Entry : unsigned
{
    DrvCreateContext = 1000, DrvCreateLayerContext, DrvDeleteContext, DrvSetContext, DrvReleaseContext,
    DrvCopyContext, DrvShareLists, DrvGetProcAddress, DrvDescribeLayerPlane, DrvSetLayerPaletteEntries,
    DrvGetLayerPaletteEntries, DrvRealizeLayerPalette, DrvSwapLayerBuffers, DrvDescribePixelFormat,
    DrvSetPixelFormat, DrvSwapBuffers, DrvValidateVersion, DrvSetCallbackProcs
};
// The exports with their stack words, as the Windows NT DDK declares them.
constexpr struct { const char *name; Entry entry; unsigned words; } Entries[] = {
    {"DrvCreateContext", DrvCreateContext, 1}, {"DrvCreateLayerContext", DrvCreateLayerContext, 2},
    {"DrvDeleteContext", DrvDeleteContext, 1}, {"DrvSetContext", DrvSetContext, 3},
    {"DrvReleaseContext", DrvReleaseContext, 1}, {"DrvCopyContext", DrvCopyContext, 3},
    {"DrvShareLists", DrvShareLists, 2}, {"DrvGetProcAddress", DrvGetProcAddress, 1},
    {"DrvDescribeLayerPlane", DrvDescribeLayerPlane, 5}, {"DrvSetLayerPaletteEntries", DrvSetLayerPaletteEntries, 5},
    {"DrvGetLayerPaletteEntries", DrvGetLayerPaletteEntries, 5}, {"DrvRealizeLayerPalette", DrvRealizeLayerPalette, 3},
    {"DrvSwapLayerBuffers", DrvSwapLayerBuffers, 2}, {"DrvDescribePixelFormat", DrvDescribePixelFormat, 4},
    {"DrvSetPixelFormat", DrvSetPixelFormat, 2}, {"DrvSwapBuffers", DrvSwapBuffers, 1},
    {"DrvValidateVersion", DrvValidateVersion, 1}, {"DrvSetCallbackProcs", DrvSetCallbackProcs, 2},
};
constexpr auto &Extensions = OpenGL::Extensions;
// The pixel formats of the driver. OPENGL32 numbers them first, before its
// own 24 generic formats, so ChoosePixelFormat prefers them when they fit.
struct IcdFormat
{
    bool double_buffer;
    int depth_bits, stencil_bits;
};
constexpr IcdFormat Formats[] = {{true, 24, 8}, {true, 16, 0}, {false, 24, 8}, {false, 16, 0}};
constexpr unsigned FormatCount = sizeof Formats / sizeof Formats[0];
} // namespace

const char *Application::OpenGLIcdModule() { return "NEO98GL32.DLL"; }

// The name of an NEO98GL32.DLL export as a route argument: the slot of a gl*
// function, or one of the Drv entries.
unsigned Application::OpenGLEntry(const char *name)
{
    if (!strcmp(name, "NEO98BatchFlush")) return OpenGLBatch::FlushEntry;
    for (const auto &e : Extensions) if (!strcmp(e.name, name)) return e.entry;
    for (const auto &e : Entries)
        if (!strcmp(e.name, name)) return e.entry;
    for (unsigned i = 0; i < Win98OpenGL::DispatchCount; ++i)
        if (!strcmp(Win98OpenGL::Dispatch[i].name, name)) return i;
    return ~0u;
}

unsigned Application::OpenGLArgumentCount(const char *name)
{
    if (!strcmp(name, "NEO98BatchFlush")) return 0;
    for (const auto &e : Extensions) if (!strcmp(e.name, name)) return e.words;
    for (const auto &e : Entries)
        if (!strcmp(e.name, name)) return e.words;
    for (unsigned i = 0; i < Win98OpenGL::DispatchCount; ++i)
        if (!strcmp(Win98OpenGL::Dispatch[i].name, name)) return Win98OpenGL::Dispatch[i].argument_bytes / 4;
    return ~0u;
}

bool Application::OpenGLAvailable() { return gpu_backend != nullptr && !getenv("NEO98_NO_OPENGL"); }

struct Application::OpenGLState
{
    unsigned table = 0;                  // the GLCLTPROCTABLE in guest memory
    unsigned strings[4] = {};            // GL_VENDOR, GL_RENDERER, GL_VERSION, GL_EXTENSIONS
    std::vector<std::unique_ptr<OpenGL::Context>> contexts;   // dhglrc - 1
    std::vector<unsigned> context_windows;
    std::map<unsigned, unsigned> current;    // thread id -> dhglrc
    std::map<unsigned, unsigned> window_formats;   // hwnd -> pixel format
    unsigned callbacks = 0;
    const unsigned char *scanout = nullptr;
    struct Batch { unsigned queue = 0, table = 0, code = 0; };
    std::map<unsigned, Batch> batches;
    unsigned batch_common = 0;
};

// A saved state carries the buffers of every context as bytes
// (src/x86_state_machine.cpp).
void Application::OpenGLRelease()
{
    if (opengl && opengl->scanout && User::ScanoutBits() == opengl->scanout) User::SettleScanout();
    delete opengl;
    opengl = nullptr;
}

// ExtEscape(OPENGL_GETINFO), the display driver's half of the handshake.
unsigned Application::OpenGLEscape(unsigned in_bytes, unsigned in, unsigned out_bytes, unsigned out)
{
    if (!OpenGLAvailable() || !out || out_bytes < 12) return 0;
    unsigned request = 0;
    if (in_bytes >= 4 && in) Read(in, &request, 4);
    if (request != 0) return 0;   // OPENGL_GETINFO_DRVNAME is the only request
    unsigned char info[0x214] = {};
    const unsigned version = IcdVersion, driver = IcdDriverVersion;
    memcpy(info, &version, 4);
    memcpy(info + 4, &driver, 4);
    memcpy(info + 8, IcdName, strlen(IcdName));
    const unsigned n = out_bytes < sizeof info ? out_bytes : unsigned(sizeof info);
    return Store(out, info, n) ? 1 : 0;
}

bool Application::OpenGLEscapeSupported(unsigned escape) { return escape == OpenGLGetInfo && OpenGLAvailable(); }

// Guest access for one context call.
class OpenGLGuest final : public OpenGL::Guest
{
  public:
    OpenGLGuest(Application &app, unsigned *strings) : app(app), strings(strings) {}
    bool Read(unsigned address, void *out, unsigned bytes) override
    {
        if (!bytes) return true;
        if (app.Read(address, out, bytes)) return true;
        // A partly mapped range: byte by byte, zero where nothing is.
        auto *p = static_cast<unsigned char *>(out);
        for (unsigned i = 0; i < bytes; ++i)
            if (!app.Read(address + i, p + i, 1)) p[i] = 0;
        return false;
    }
    bool Write(unsigned address, const void *in, unsigned bytes) override
    {
        return bytes ? app.Store(address, in, bytes) : true;
    }
    const unsigned char *Span(unsigned address, unsigned bytes) override
    {
        if (!bytes || uint64_t(address) + bytes > 0x100000000ull) return nullptr;
        // Read uses this same per-page table. Private array data needs no
        // region search or copying once the whole range is readable.
        if (const auto *readable = app.cpu->page_read)
        {
            bool direct = true;
            const uint64_t last = (uint64_t(address) + bytes - 1) >> 12;
            for (uint64_t page = address >> 12; page <= last; ++page)
                if (!readable[page]) { direct = false; break; }
            if (direct) return app.cpu->memory + address;
        }
        if (!app.virtual_memory.Access(address, bytes, false)) return nullptr;
        return static_cast<const unsigned char *>(app.cpu->HostPointer(address, bytes));
    }
    void Drain(unsigned thread)
    {
        auto &st = *app.opengl;
        auto batch = st.batches.find(thread);
        if (batch == st.batches.end()) return;
        const unsigned count = app.Read32(batch->second.queue);
        if (!count) return;
        if (count > OpenGLBatch::Capacity) { app.Fail("Corrupt OpenGL command batch"); return; }
        std::vector<OpenGLBatch::Command> commands(count);
        if (!app.Read(batch->second.queue + OpenGLBatch::HeaderBytes, commands.data(), count * sizeof(commands[0]))) return;
        app.Write32(batch->second.queue, 0);
        auto current = st.current.find(thread);
        if (current == st.current.end()) return;
        auto *context = app.OpenGLContext(current->second);
        if (!context) return;
        for (const auto &command : commands)
        {
            if (!OpenGLBatch::Eligible(command.slot)) { app.Fail("Invalid OpenGL batch entry"); return; }
            const auto &entry = *OpenGLBatch::Entry(command.slot);
            ++app.api_calls;
            ++app.fast_gate_calls;
            const unsigned token = app.api_ring.Begin(app.OpenGLIcdModule(), entry.name, thread, 0,
                                                      command.args, entry.argument_bytes / 4);
            context->Call(command.slot, command.args, *this);
            app.api_ring.Finish(token, 0);
        }
    }
    unsigned BatchTable(unsigned thread)
    {
        auto &st = *app.opengl;
        const char *setting = getenv("NEO98_OPENGL_BATCH");
        if (app.cpu->Backend() != CpuBackend::Native || !app.fast_gates || app.trace_calls || app.export_census ||
            (setting && !strcmp(setting, "0"))) return st.table;
        auto found = st.batches.find(thread);
        if (found != st.batches.end()) return found->second.table;
        if (!st.batch_common)
        {
            st.batch_common = app.Allocate(sizeof Win98OpenGLBatch::Record);
            if (!st.batch_common || !app.Store(st.batch_common, Win98OpenGLBatch::Record, sizeof Win98OpenGLBatch::Record))
                return st.table;
            app.cpu->InvalidateCode(st.batch_common, sizeof Win98OpenGLBatch::Record);
        }
        Application::OpenGLState::Batch batch;
        batch.queue = app.Allocate(OpenGLBatch::HeaderBytes + OpenGLBatch::Capacity * sizeof(OpenGLBatch::Command));
        batch.table = app.Allocate((Win98OpenGL::DispatchCount + 1) * 4);
        batch.code = app.Allocate(OpenGLBatch::Slots * 32);
        if (!batch.queue || !batch.table || !batch.code)
        {
            if (batch.queue) app.Free(batch.queue);
            if (batch.table) app.Free(batch.table);
            if (batch.code) app.Free(batch.code);
            return st.table;
        }
        std::vector<unsigned> table(Win98OpenGL::DispatchCount + 1);
        if (!app.Read(st.table, table.data(), unsigned(table.size() * 4))) return st.table;
        const unsigned header[] = {0, app.NewImport(app.OpenGLIcdModule(), "NEO98BatchFlush", 0), 0, 0};
        if (!header[1] || !app.Store(batch.queue, header, sizeof header)) return st.table;
        std::vector<unsigned char> code(OpenGLBatch::Slots * 32, 0xcc);
        for (unsigned index = 0; index < OpenGLBatch::Slots; ++index)
        {
            const unsigned slot = OpenGLBatch::Slot(index);
            if (!OpenGLBatch::Eligible(slot)) continue;
            const unsigned words = OpenGLBatch::Entry(slot)->argument_bytes / 4;
            const unsigned at = index * 32;
            unsigned pos = at;
            auto byte = [&](unsigned char b) { code[pos++] = b; };
            auto word = [&](unsigned w) { memcpy(code.data() + pos, &w, 4); pos += 4; };
            // lea eax,[esp+4]; push eax; push argc; push slot; push queue;
            // call Record; ret argument_bytes. Record pops its four arguments.
            for (unsigned char b : {0x8d, 0x44, 0x24, 0x04, 0x50, 0x6a}) byte(b);
            byte(static_cast<unsigned char>(words));
            byte(0x68); word(slot);
            byte(0x68); word(batch.queue);
            byte(0xe8); word(st.batch_common - (batch.code + pos + 4));
            byte(0xc2); byte(static_cast<unsigned char>(words * 4)); byte(0);
            if (slot < Win98OpenGL::DispatchCount) table[slot + 1] = batch.code + at;
        }
        if (!app.Store(batch.code, code.data(), unsigned(code.size())) ||
            !app.Store(batch.table, table.data(), unsigned(table.size() * 4))) return st.table;
        app.cpu->InvalidateCode(batch.code, unsigned(code.size()));
        st.batches.emplace(thread, batch);
        return batch.table;
    }
    unsigned String(unsigned name) override
    {
        if (name < 0x1f00 || name > 0x1f03) return 0;
        unsigned &slot = strings[name - 0x1f00];
        if (!slot)
        {
            static const char *const texts[] = {"NEO98", "NEO98 ICD", "1.1.0", "GL_EXT_bgra GL_EXT_compiled_vertex_array GL_ARB_multitexture"};
            char renderer[160];
            const char *text = texts[name - 0x1f00];
            if (name == 0x1f01 && Application::gpu_backend)
            {
                snprintf(renderer, sizeof renderer, "NEO98 ICD on %s", Application::gpu_backend->Name());
                text = renderer;
            }
            slot = app.String(text);
        }
        return slot;
    }
  private:
    Application &app;
    unsigned *strings;
};

OpenGL::Context *Application::OpenGLContext(unsigned dhglrc)
{
    if (!opengl || !dhglrc || dhglrc > opengl->contexts.size()) return nullptr;
    return opengl->contexts[dhglrc - 1].get();
}

void Application::OpenGLStateSync()
{
    if (!opengl) return;
    OpenGLGuest guest(*this, opengl->strings);
    for (const auto &batch : opengl->batches) guest.Drain(batch.first);
    for (auto &context : opengl->contexts)
        if (context) context->StateSync();
}

// The client area of a window, the size the buffers of a context follow.
static void ClientSize(unsigned hwnd, int &width, int &height)
{
    Gdi::RECT r{};
    width = height = 1;
    if (hwnd && User::GetClientRect(hwnd, &r))
    {
        width = r.right - r.left;
        height = r.bottom - r.top;
    }
}

// Shows a picture of a context in its window: into the window's surface as a
// display driver's blit would put it, then the window has drawn.
void Application::OpenGLPresent(unsigned hwnd, const OpenGL::Picture &picture)
{
    RenderProfile::Scope profile(RenderProfile::GlPresent);
    if (!hwnd || !picture.bits) return;
    const Gdi::HDC dc = User::GetDC(hwnd);
    // NEO98_TRACE_OPENGL=1: one line per picture, with the number of pixels
    // that are not black, for a run that shows nothing.
    static const bool trace = getenv("NEO98_TRACE_OPENGL") != nullptr;
    if (trace && log)
    {
        Gpu::Surface source;
        source.bits = const_cast<unsigned char *>(picture.bits);
        gpu_backend->Sync(source, false);
        unsigned lit = 0;
        for (int y = 0; y < picture.height; ++y)
            for (int x = 0; x < picture.width; ++x)
                if (reinterpret_cast<const unsigned *>(picture.bits + y * picture.pitch)[x] & 0xffffff) ++lit;
        char text[160];
        snprintf(text, sizeof text, "OPENGL present hwnd=%u dc=%u size=%dx%d lit=%u", hwnd, dc, picture.width, picture.height, lit);
        log(text);
    }
    if (!dc) return;
    // A fully visible window covering the current 32-bit screen can publish
    // its resident image through the same presenter as DirectDraw flips.
    int screen_width = 0, screen_height = 0, screen_depth = 0;
    User::ScreenMode(screen_width, screen_height, screen_depth);
    Gdi::RECT client{}, clip{};
    const bool fullscreen = screen_depth == 32 &&
        picture.width == screen_width && picture.height == screen_height &&
        User::IsVisible(hwnd) && User::ClientScreenRect(hwnd, &client) &&
        client.left == 0 && client.top == 0 && client.right == screen_width && client.bottom == screen_height &&
        Gdi::GetClipBox(dc, &clip) == 2 && clip.left == 0 && clip.top == 0 &&
        clip.right == picture.width && clip.bottom == picture.height;
    if (fullscreen)
    {
        User::SetScanout(picture.bits, picture.pitch);
        if (User::DeferScreenRead())
        {
            opengl->scanout = picture.bits;
            User::ScreenChanged();
            User::ReleaseDC(hwnd, dc);
            return;
        }
        User::ResetScanout();
    }
    if (opengl->scanout)
    {
        if (User::ScanoutBits() == opengl->scanout) User::SettleScanout();
        opengl->scanout = nullptr;
    }
    Gpu::Surface source;
    source.bits = const_cast<unsigned char *>(picture.bits);
    gpu_backend->Sync(source, false);
    const DibEng::Bitmap bitmap{const_cast<unsigned char *>(picture.bits), picture.width, picture.height, picture.pitch, 32};
    Gdi::CopySurface(dc, 0, 0, bitmap);
    User::DrawingChanged(dc);
    User::ReleaseDC(hwnd, dc);
}

unsigned Application::OpenGLApi(unsigned entry, const unsigned *a)
{
    if (!OpenGLAvailable()) return 0;
    if (!opengl) opengl = new OpenGLState;
    auto &st = *opengl;
    const unsigned thread = threads[current_thread].id;
    OpenGLGuest queued(*this, st.strings);
    queued.Drain(thread);
    if (entry == OpenGLBatch::FlushEntry || state == Failed) return 0;
    // A gl* function: the context current on this thread.
    if (entry < Win98OpenGL::DispatchCount || OpenGL::ExtensionFor(entry))
    {
        auto found = st.current.find(thread);
        if (found == st.current.end()) return 0;
        OpenGL::Context *context = OpenGLContext(found->second);
        if (!context) return 0;
        OpenGLGuest guest(*this, st.strings);
        const unsigned value = context->Call(entry, a, guest);
        if (context->TakePresent())
        {
            OpenGL::Picture picture;
            if (context->Front(picture)) OpenGLPresent(st.context_windows[found->second - 1], picture);
        }
        return value;
    }
    switch (entry)
    {
    case DrvValidateVersion:
        return a[0] == IcdDriverVersion ? 1 : 0;
    case DrvSetCallbackProcs:
        st.callbacks = a[1];
        return 0;
    case DrvDescribePixelFormat:
    {
        // (hdc, iPixelFormat, cjpfd, ppfd): the number of formats, and the
        // descriptor of one of them when there is room for it.
        const unsigned index = a[1], bytes = a[2], out = a[3];
        if (out && index >= 1 && index <= FormatCount && bytes >= 40)
        {
            const IcdFormat &f = Formats[index - 1];
            int width = 0, height = 0, depth = 32;
            User::ScreenMode(width, height, depth);
            unsigned char pfd[40] = {};
            pfd[0] = 40;
            pfd[2] = 1;
            unsigned flags = 0x4 | 0x20;   // PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL
            if (f.double_buffer) flags |= 0x1 | 0x400;   // PFD_DOUBLEBUFFER | PFD_SWAP_COPY
            memcpy(pfd + 4, &flags, 4);
            pfd[8] = 0;   // PFD_TYPE_RGBA
            // The colour of the screen mode, as a card's driver describes it.
            if (depth == 16)
            {
                pfd[9] = 16;
                pfd[10] = 5; pfd[11] = 11; pfd[12] = 6; pfd[13] = 5; pfd[14] = 5; pfd[15] = 0;
            }
            else
            {
                pfd[9] = depth == 24 ? 24 : 32;
                pfd[10] = 8; pfd[11] = 16; pfd[12] = 8; pfd[13] = 8; pfd[14] = 8; pfd[15] = 0;
            }
            pfd[23] = (unsigned char)f.depth_bits;
            pfd[24] = (unsigned char)f.stencil_bits;
            Store(out, pfd, 40);
        }
        return FormatCount;
    }
    case DrvSetPixelFormat:
    {
        const unsigned hwnd = User::WindowFromDC(a[0]);
        if (!hwnd || a[1] < 1 || a[1] > FormatCount) return 0;
        st.window_formats[hwnd] = a[1];
        return 1;
    }
    case DrvCreateContext:
    case DrvCreateLayerContext:
    {
        if (entry == DrvCreateLayerContext && a[1]) return 0;   // no overlay planes
        const unsigned hwnd = User::WindowFromDC(a[0]);
        auto found = st.window_formats.find(hwnd);
        if (!hwnd || found == st.window_formats.end()) return 0;
        const IcdFormat &f = Formats[found->second - 1];
        OpenGL::Format format;
        format.double_buffer = f.double_buffer;
        format.depth_bits = f.depth_bits;
        format.stencil_bits = f.stencil_bits;
        // The owner keeps the context's objects apart from the Direct3D
        // surfaces of the same process in the backend's caches.
        st.contexts.emplace_back(new OpenGL::Context(*gpu_backend, process_id | 0x80000000u, format));
        st.context_windows.push_back(hwnd);
        return unsigned(st.contexts.size());
    }
    case DrvDeleteContext:
    {
        const unsigned dhglrc = a[0];
        if (!OpenGLContext(dhglrc)) return 0;
        if (st.scanout)
        {
            if (User::ScanoutBits() == st.scanout) User::SettleScanout();
            st.scanout = nullptr;
        }
        for (auto it = st.current.begin(); it != st.current.end();)
            if (it->second == dhglrc) it = st.current.erase(it);
            else ++it;
        st.contexts[dhglrc - 1].reset();
        return 1;
    }
    case DrvSetContext:
    {
        // (hdc, dhglrc, pfnSetProcTable): the context becomes current on
        // this thread, and the answer is the dispatch table.
        OpenGL::Context *context = OpenGLContext(a[1]);
        if (!context) return 0;
        const unsigned hwnd = User::WindowFromDC(a[0]);
        if (hwnd) st.context_windows[a[1] - 1] = hwnd;
        int width = 1, height = 1;
        ClientSize(st.context_windows[a[1] - 1], width, height);
        if (st.scanout)
        {
            if (User::ScanoutBits() == st.scanout) User::SettleScanout();
            st.scanout = nullptr;
        }
        context->Drawable(width, height);
        st.current[thread] = a[1];
        if (!st.table)
        {
            const unsigned bytes = 4 + Win98OpenGL::DispatchCount * 4;
            st.table = Allocate(bytes);
            if (!st.table) return 0;
            std::vector<unsigned> words(1 + Win98OpenGL::DispatchCount);
            words[0] = Win98OpenGL::DispatchCount;
            for (unsigned i = 0; i < Win98OpenGL::DispatchCount; ++i)
            {
                const auto &d = Win98OpenGL::Dispatch[i];
                words[1 + i] = NewImport(OpenGLIcdModule(), d.name, d.argument_bytes / 4);
                if (!words[1 + i]) return 0;
            }
            if (!Store(st.table, words.data(), bytes)) return 0;
        }
        return queued.BatchTable(thread);
    }
    case DrvReleaseContext:
    {
        // wglMakeCurrent(NULL): what the context drew goes out first.
        OpenGL::Context *context = OpenGLContext(a[0]);
        if (context)
        {
            context->Flush();
            OpenGL::Picture picture;
            if (context->TakePresent() && context->Front(picture)) OpenGLPresent(st.context_windows[a[0] - 1], picture);
        }
        auto found = st.current.find(thread);
        if (found != st.current.end() && found->second == a[0]) st.current.erase(found);
        return 1;
    }
    case DrvSwapBuffers:
    {
        // The context current on this thread if it draws into this window,
        // otherwise any context of the window.
        const unsigned hwnd = User::WindowFromDC(a[0]);
        unsigned dhglrc = 0;
        auto found = st.current.find(thread);
        if (found != st.current.end() && st.context_windows[found->second - 1] == hwnd) dhglrc = found->second;
        for (unsigned i = 0; !dhglrc && i < st.contexts.size(); ++i)
            if (st.contexts[i] && st.context_windows[i] == hwnd) dhglrc = i + 1;
        OpenGL::Context *context = OpenGLContext(dhglrc);
        if (!context) return 0;
        OpenGL::Picture picture;
        if (context->Swap(picture, false)) OpenGLPresent(hwnd, picture);
        // The window may have changed its size; the next frame follows it.
        int width = 1, height = 1;
        ClientSize(hwnd, width, height);
        if (st.scanout && (width != picture.width || height != picture.height))
        {
            if (User::ScanoutBits() == st.scanout) User::SettleScanout();
            st.scanout = nullptr;
        }
        context->Drawable(width, height);
        return 1;
    }
    case DrvShareLists:
    {
        OpenGL::Context *first = OpenGLContext(a[0]), *second = OpenGLContext(a[1]);
        return first && second && second->ShareWith(*first) ? 1 : 0;
    }
    case DrvGetProcAddress:
    {
        const char *name = ReadString(a[0]);
        for (const auto &e : Extensions)
            if (!strcmp(e.name, name))
            {
                const auto batch = st.batches.find(thread);
                if (batch != st.batches.end() && OpenGLBatch::Eligible(e.entry))
                    return batch->second.code + OpenGLBatch::Index(e.entry) * 32;
                return NewImport(OpenGLIcdModule(), e.name, e.words);
            }
        return 0;
    }
    case DrvCopyContext:
    case DrvDescribeLayerPlane:
    case DrvSetLayerPaletteEntries:
    case DrvGetLayerPaletteEntries:
    case DrvRealizeLayerPalette:
    case DrvSwapLayerBuffers:
    default:
        return 0;
    }
}
} // namespace X86
#else
namespace X86
{
struct Application::OpenGLState {};
const char *Application::OpenGLIcdModule() { return "NEO98GL32.DLL"; }
unsigned Application::OpenGLEntry(const char *) { return ~0u; }
unsigned Application::OpenGLArgumentCount(const char *) { return ~0u; }
bool Application::OpenGLAvailable() { return false; }
bool Application::OpenGLEscapeSupported(unsigned) { return false; }
unsigned Application::OpenGLEscape(unsigned, unsigned, unsigned, unsigned) { return 0; }
unsigned Application::OpenGLApi(unsigned, const unsigned *) { return 0; }
OpenGL::Context *Application::OpenGLContext(unsigned) { return nullptr; }
void Application::OpenGLPresent(unsigned, const OpenGL::Picture &) {}
void Application::OpenGLRelease() { delete opengl; opengl = nullptr; }
void Application::OpenGLStateSync() {}
} // namespace X86
#endif
