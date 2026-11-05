// SPDX-License-Identifier: GPL-2.0-or-later
// guest mip chains must not produce invalid Metal descriptors.
#include "host_metal.h"
#include "../src/gpu_backend.h"
#include <cassert>
#include <cstdio>
#include <vector>
int main()
{
    auto *gpu = HostMetal::Install();
    assert(gpu);
    std::vector<unsigned> pixels(64 * 64 * 8, 0xff00ff00);
    unsigned handle = 1, uploads = 0;
    for (int kind = 0; kind < 4; ++kind)
    for (int extent : {1, 2, 16, 64})
    {
        if (kind == 3 && !gpu->CompressedTextures()) continue;
        Gpu::Surface s;
        s.owner = 1; s.handle = handle++;
        s.bits = reinterpret_cast<unsigned char *>(pixels.data());
        s.width = s.height = extent; s.pitch = extent * 4;
        s.bit_count = 32; s.r_mask = 0xff0000; s.g_mask = 0xff00;
        s.b_mask = 0xff; s.a_mask = 0xff000000;
        if (kind == 2) s.slices = 8;
        if (kind == 3) { s.fourcc = 0x31545844; s.pitch = ((extent + 3) / 4) * 8; }
        int w = extent, z = s.slices;
        for (unsigned n = 0; n < Gpu::Surface::MaxLevels; ++n)
        {
            w = w > 1 ? w / 2 : 1; z = z > 1 ? z / 2 : z;
            s.levels[n] = {s.bits, w, w, kind == 3 ? ((w + 3) / 4) * 8 : w * 4, z};
        }
        // The tail repeats 1x1, as the HAL used to accept from attachments.
        for (unsigned count : {0u, 1u, 7u, Gpu::Surface::MaxLevels})
        {
            s.level_count = count; ++s.generation;
            Gpu::Surface faces[6];
            if (kind == 1) { for (auto &f : faces) f = s; s.faces = faces; }
            gpu->SetTexture(0, &s);
            ++uploads;
            s.faces = nullptr;
        }
        gpu->Forget(s.owner, s.handle);
    }
    printf("PASS: %u Metal mip uploads, 2D/cube/volume, DXT supported=%u, 1..64 texels and excess levels\n",
           uploads, unsigned(gpu->CompressedTextures()));
}
