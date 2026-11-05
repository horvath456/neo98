// SPDX-License-Identifier: GPL-2.0-or-later
// The 3dfx Voodoo of a DOS machine, drawn by Gpu::Backend.
// A DOS program reaches the card through its registers, with or without a
// Glide of its own linked in. The DOS machine emulates the card up to the
// point where it would fill pixels and hands over what it was asked to draw
// (src/dos_voodoo_frame.h). This class draws it: it turns the registers of a
// command into the state of Backend::DrawGlide, the pixel pipeline the Glide
// driver of the Windows side draws with (src/glide_context.cpp), keeps the
// three colour buffers and the depth buffer of the card as surfaces of the
// backend, and answers with the picture the monitor shows. Nothing is
// rasterized on the CPU.
#pragma once
#include "dos_voodoo_frame.h"
#include "gpu_backend.h"
#include <vector>

namespace DosVm { struct Provider; struct Session; }
namespace DosVoodoo
{
class Renderer
{
  public:
    Renderer(Gpu::Backend &gpu, unsigned owner);
    ~Renderer();
    // Draws one frame of the card at `scale` target pixels per screen pixel
    // (1 to 8). A frame the program cut with a read through the linear frame
    // buffer names the buffers it reads (read_mask): those come back from
    // the GPU into `readback`, at the resolution of the card. True when the
    // picture changed.
    bool Draw(const DosVoodooFrame &frame, unsigned scale, DosVoodooReadback *readback);
    // Whether the card's output is what the monitor shows.
    bool Active() const { return active; }
    bool Healthy() const { return healthy; }
    // The picture after the last frame, XRGB rows without padding: at the
    // resolution of the program, and as it was drawn.
    const unsigned *Picture() const { return picture.data(); }
    unsigned Width() const { return width; }
    unsigned Height() const { return height; }
    unsigned Scale() const { return scale; }
    // Made when asked for: only the full screen shows it.
    const unsigned *Full();
    unsigned FullWidth() const { return width * scale; }
    unsigned FullHeight() const { return height * scale; }
    // Counted for the log and the tests.
    unsigned long long Frames() const { return frames; }
    unsigned long long Triangles() const { return triangles; }
    // The frames cut by a read through the linear frame buffer.
    unsigned long long Reads() const { return reads; }

  private:
    struct Buffer
    {
        std::vector<unsigned> pixels;   // BGRA at the target resolution
        Gpu::Surface surface;
    };
    struct Texture
    {
        std::vector<unsigned> texels;   // every level, the largest first
        Gpu::Surface surface;
        unsigned levels = 0;
    };
    void Resize(unsigned new_width, unsigned new_height, unsigned new_scale);
    void Drop();
    void Upload(const DosVoodooTexture &t);
    void State(const DosVoodooCommand &c, Gpu::GlideState &state) const;
    void Present(unsigned shown);
    void Read(unsigned mask, DosVoodooReadback &readback);
    void Restore(const DosVoodooFrame &frame);
    void Gamma(const DosVoodooFrame &frame);

    Gpu::Backend &gpu;
    unsigned owner;
    bool active = false, healthy = true;
    unsigned width = 0, height = 0, scale = 1;
    Buffer color[3];
    std::vector<unsigned short> depth_bytes;
    Gpu::Surface depth;
    std::vector<Texture> textures;
    unsigned next_handle = 1;
    int clip[4] = {0, 0, 0, 0};       // target pixels
    unsigned char fog_table[64] = {};
    std::vector<Gpu::GlideVertex> scratch;
    std::vector<unsigned> picture, full;
    unsigned shown = 0;
    bool full_made = false;
    float gamma_exponent[3] = {1, 1, 1}, gamma_factor[3] = {1, 1, 1};
    unsigned char gamma[3][256];
    bool gamma_linear = true;
    unsigned long long frames = 0, triangles = 0, reads = 0;
};
// Service only GPU packets/readback. It never enters a guest or updates USER.
bool Drain(const DosVm::Provider &, DosVm::Session *, Gpu::Backend &, unsigned owner,
               Renderer *&, unsigned scale = 1);
} // namespace DosVoodoo
