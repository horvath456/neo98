// SPDX-License-Identifier: GPL-2.0-or-later
// One OpenGL 1.1 rendering context of the NEO98 installable client driver.
// The original OPENGL32.DLL of Windows 98 finds the driver through the
// display driver (src/x86_opengl.cpp) and jumps through the dispatch table
// the driver hands back from DrvSetContext. Every entry of that table is a
// gate that ends here, in Context::Call, with the slot number of the function
// and the stack words of its arguments.
// The context is the OpenGL state machine. Its vertex stage follows the rules
// of OpenGL on the CPU: transforms, lighting with two sides and colour
// material, texture coordinate generation, the texture matrix, fog factors,
// flat shading after the provoking vertex, face culling and polygon modes.
// What comes out are lit vertices in clip space, and those go to the one
// rasterizer of the port, Gpu::Backend (src/gpu_backend.h, on macOS
// Metal), which clips, textures, tests and blends on the GPU. Draws collect in
// a queue and reach the backend in one batch at SwapBuffers, glFlush,
// glFinish and before anything reads or writes the pixels on the CPU.
// Nothing here knows Unicorn or the guest's address space: pointers go
// through Guest.
#pragma once
#include "gpu_backend.h"
#include "opengl_extensions.h"
#include <memory>
#include <vector>

namespace OpenGL
{
// What a context needs from the process it runs in.
class Guest
{
  public:
    virtual ~Guest() {}
    // Guest memory. A failed read leaves the bytes zero.
    virtual bool Read(unsigned address, void *out, unsigned bytes) = 0;
    virtual bool Write(unsigned address, const void *in, unsigned bytes) = 0;
    // Optional contiguous read view, valid until this call returns to the guest.
    virtual const unsigned char *Span(unsigned, unsigned) { return nullptr; }
    // The guest address of the constant string glGetString answers.
    virtual unsigned String(unsigned name) = 0;
};

// The pixel format the context was made for.
struct Format
{
    bool double_buffer = true;
    int depth_bits = 24, stencil_bits = 8;
};

struct Picture
{
    const unsigned char *bits = nullptr;   // 32-bit X8R8G8B8 rows, top row first
    int width = 0, height = 0, pitch = 0;
};

class Context
{
  public:
    Context(Gpu::Backend &backend, unsigned owner, const Format &format);
    ~Context();
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    // One function of the dispatch table: its slot (Win98OpenGL::Slot) and
    // the stack words of its arguments. The answer goes to EAX.
    unsigned Call(unsigned slot, const unsigned *args, Guest &guest);
    // The client area of the window the context draws into; the buffers
    // follow it. Called when the context becomes current and before a swap.
    void Drawable(int width, int height);
    // PFD_SWAP_COPY: copy the completed back buffer into the front buffer.
    // CPU presenters also read the front buffer back from the backend.
    bool Swap(Picture &picture, bool readback = true);
    // After glFlush or glFinish with the front buffer as a draw target, the
    // window has to show the front buffer: TakePresent answers true once, and
    // Front gives the picture.
    bool TakePresent();
    bool Front(Picture &picture);
    // Everything queued goes to the backend now.
    void Flush();
    // Before a saved state: what was queued is drawn, and what the
    // rasterizer holds of the colour buffers goes into their bytes.
    void StateSync();
    // wglShareLists: this context uses the lists and textures of `other`.
    bool ShareWith(Context &other);
    const Format &PixelFormat() const { return format; }

    struct State;
  private:
    Gpu::Backend &backend;
    Format format;
    std::unique_ptr<State> state;
};
} // namespace OpenGL
