#version 450
layout(set=0,binding=0) uniform sampler2D picture;
layout(push_constant) uniform Format { uvec4 maximum; uvec4 canvas; } format;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
void main()
{
    // Match the guest format's encode and USER's integer decode exactly.
    ivec2 logical = ivec2(uv * vec2(format.canvas.xy));
    ivec2 source = logical * textureSize(picture, 0) / ivec2(format.canvas.xy);
    uvec3 bytes = uvec3(round(texelFetch(picture, source, 0).rgb * 255.0));
    uvec3 packed = (bytes * format.maximum.rgb + 127u) / 255u;
    uvec3 expanded = (packed * 255u + format.maximum.rgb / 2u) / format.maximum.rgb;
    color = vec4(vec3(expanded) / 255.0, 1.0);
}
