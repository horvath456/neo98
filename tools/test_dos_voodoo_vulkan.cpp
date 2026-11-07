// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the DOS card's host contract without a guest or Windows inputs.
#include "../src/dos_voodoo.h"
#include "host_gpu.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

int main()
{
    auto *gpu = HostGpu::Install();
    if (!gpu) { puts("SKIP no Vulkan device"); return 77; }
    printf("DOS Voodoo backend: %s\n", gpu->Name());
    constexpr unsigned W = 8, H = 8, Pitch = DosVoodooReadback::Pitch;
    std::vector<unsigned short> buffers[4];
    DosVoodooReadback readback;
    for (auto &b : buffers) b.resize(Pitch * H);
    for (unsigned i = 0; i < 3; ++i) readback.color[i] = buffers[i].data();
    readback.depth = buffers[3].data();
    for (unsigned scale : {1u, 2u})
    {
        DosVoodoo::Renderer renderer(*gpu, 123);
        DosVoodooCommand commands[3];
        for (unsigned i = 0; i < 3; ++i)
        {
            commands[i].type = DosVoodooCommand::Fill;
            commands[i].buffer = i;
            commands[i].fbz_mode = (1u << 9) | (1u << 10);
            commands[i].auxiliary = true;
            commands[i].constant1 = 0xff000000u | (0xffu << (i * 8));
            commands[i].za_color = 0x3456;
        }
        DosVoodooFrame frame;
        frame.active = true; frame.width = W; frame.height = H;
        frame.commands = commands; frame.command_count = 3;
        frame.read_mask = 15;
        renderer.Draw(frame, scale, &readback);
        assert(renderer.Healthy());
        for (unsigned i = 0; i < 3; ++i)
            assert(buffers[i][Pitch + 1] == (i == 0 ? 0x001f : i == 1 ? 0x07e0 : 0xf800));
        assert(buffers[3][Pitch + 1] == 0x3456);
        puts("ok three color buffers and shared depth readback");

        DosVoodooVertex vertices[3];
        vertices[1].x = W; vertices[2].y = H;
        for (auto &v : vertices) { v.r = v.a = 255; v.z = 1000; v.oow = 1; }
        DosVoodooCommand triangle;
        triangle.vertex_count = 3;
        triangle.fbz_mode = (1u << 9) | (1u << 10) | (1u << 4) | (1u << 5);
        frame.commands = &triangle; frame.command_count = 1;
        frame.vertices = vertices; frame.vertex_count = 3;
        renderer.Draw(frame, scale, &readback);
        assert(renderer.Healthy() && renderer.Triangles() == 1);
        assert(buffers[0][Pitch + 1] == 0xf800 && buffers[3][Pitch + 1] == 1000);
        assert(buffers[0][7 * Pitch + 7] == 0x001f && buffers[3][7 * Pitch + 7] == 0x3456);
        puts("ok DOS triangles use the Glide shaders and depth test");

        // A saved card restores all buffers before any subsequent drawing.
        std::vector<unsigned short> saved[4];
        for (unsigned i = 0; i < 4; ++i) saved[i] = buffers[i];
        frame.commands = nullptr; frame.command_count = 0;
        frame.restore_pitch = Pitch;
        for (unsigned i = 0; i < 3; ++i) frame.restore_color[i] = saved[i].data();
        frame.restore_depth = saved[3].data();
        DosVoodoo::Renderer restored(*gpu, 124);
        restored.Draw(frame, scale, &readback);
        assert(restored.Healthy());
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned y = 0; y < H; ++y)
                for (unsigned x = 0; x < W; ++x)
                    assert(buffers[i][y * Pitch + x] == saved[i][y * Pitch + x]);
        puts("ok saved color and depth restore at native and scaled resolution");
        frame.restore_pitch = 0;
        frame.active = false;
        assert(restored.Draw(frame, scale, &readback) && !restored.Active());
        puts("ok inactive card hands the display back");
    }
}
