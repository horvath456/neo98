// SPDX-License-Identifier: GPL-2.0-or-later
// The Metal rasterizer behind the Direct3D HAL and the OpenGL driver of the
// display driver, the host half of src/gpu_backend.h. Install answers the one
// backend of the process, or null on a machine without a Metal device; the
// runners hand it to X86::Application::SetGpuBackend.
#pragma once
namespace Gpu { class Backend; }
namespace HostMetal
{
Gpu::Backend *Install();
// Number of physical render passes, for batching regressions and diagnostics.
unsigned long long RenderPasses();
void LogStatistics(); // NEO98_METAL_STATS; also usable before the host's _exit.
// Translates a pixel shader and compiles it with the backend's shader source,
// as a draw would; false with the reason for either step. For
// tools/test_d3d_shader_msl.cpp.
bool CompilePixelShader(const unsigned *tokens, unsigned count, char *error, unsigned error_size);
// The same for a vertex shader.
bool CompileVertexShader(const unsigned *tokens, unsigned count, char *error, unsigned error_size);
}
