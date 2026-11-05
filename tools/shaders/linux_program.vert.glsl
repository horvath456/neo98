// SPDX-License-Identifier: GPL-2.0-or-later
#version 450
layout(location=0) in vec4 input_registers[16];
layout(location=0) noperspective out vec4 diffuse;
layout(location=1) noperspective out vec4 specular;
layout(location=2) out vec4 texcoord[8];
layout(location=10) out float depth;
layout(location=11) out float eye_w;
layout(std140,binding=2) uniform VertexConstants {vec4 floats[256];ivec4 integers[16];uvec4 booleans[4];vec4 viewport;uvec4 flags;} vc;
out gl_PerVertex {vec4 gl_Position;float gl_PointSize;float gl_ClipDistance[6];};
