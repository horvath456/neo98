// SPDX-License-Identifier: GPL-2.0-or-later
// Direct3D shader bytecode into the Metal Shading Language, for the Metal
// backend of the Direct3D HAL (tools/host_metal.mm). Only the host builds this;
// the display driver hands the backend the tokens as the program gave them.
// A pixel shader becomes one function
// static float4 NAME(float4 v0, float4 v1, thread const float4 *coords,
// constant FragmentUniforms &u, constant float4 *c,
// constant int4 *ci, constant uint *cb, TEXTURES)
// that answers the colour of r0; the backend wraps it in its fragment entry
// points. The helpers it calls (shader_sample, shader_signed and the stage
// uniforms) are part of the backend's shader source.
#pragma once
#include <string>

namespace Direct3D {

// The pixel shader versions the translation takes: 1.1 to 1.4.
constexpr unsigned PixelShaderVersion = 0xffff0104u;
// The range of a pixel shader register, D3DCAPS8::MaxPixelShaderValue.
constexpr float PixelShaderRange = 8.0f;

// False, with the reason in `error`, for a token stream this translation
// does not know.
bool PixelShaderToMsl(const unsigned *tokens, unsigned count, const char *name, std::string &out, std::string &error);

// The vertex shader versions the translation takes: 1.0 and 1.1, and the
// constant registers they see (D3DCAPS8::MaxVertexShaderConst).
constexpr unsigned VertexShaderVersion = 0xfffe0101u, VertexShaderConstants = 96;
// Shader model 2 (vs_2_0, ps_2_0) and the float constants it sees.
constexpr unsigned VertexShaderConstants2 = 256, PixelShaderConstants2 = 32;

// A vertex shader becomes
// static void NAME(thread const float4 *v, constant float4 *c, constant int4 *ci, constant uint *cb,
// thread float4 &oPos, thread float4 *oD, thread float4 *oT, thread float4 &oFog,
// thread float4 &oPts)
// with the sixteen input registers in v, the float, integer and boolean
// constants in c, ci and cb; the outputs
// keep what the caller put in them where the shader does not write.
bool VertexShaderToMsl(const unsigned *tokens, unsigned count, const char *name, std::string &out, std::string &error);

} // namespace Direct3D
