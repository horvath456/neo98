#version 450
#ifdef NEO98_FLAT
#define COLOR_QUALIFIER flat
#else
#define COLOR_QUALIFIER noperspective
#endif
layout(location = 0) in vec4 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec4 secondary;
layout(location = 3) in vec4 coords[8];
layout(location = 11) in vec2 extra;
layout(location = 12) in vec4 clip0;
layout(location = 13) in vec2 clip1;
layout(location = 0) COLOR_QUALIFIER out vec4 diffuse;
layout(location = 1) COLOR_QUALIFIER out vec4 specular;
layout(location = 2) out vec4 texcoord[8];
layout(location = 10) out float depth;
layout(location = 11) out float eye_w;
out gl_PerVertex { vec4 gl_Position; float gl_PointSize; float gl_ClipDistance[6]; };
void main() {
    gl_Position = position;
    gl_PointSize = extra.y;
    diffuse = color;
    specular = secondary;
    for (int i=0;i<8;++i)texcoord[i]=coords[i];
    depth=extra.x;
    eye_w=position.w;
    for(int i=0;i<4;++i)gl_ClipDistance[i]=clip0[i];
    gl_ClipDistance[4]=clip1[0];gl_ClipDistance[5]=clip1[1];
}
