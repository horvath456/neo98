#version 450
layout(location=0) in vec4 position;
layout(location=1) in vec4 color;
layout(location=2) in vec4 secondary;
layout(location=3) in vec4 coords0;
layout(location=4) in vec4 coords1;
layout(location=0) noperspective out vec4 diffuse;
layout(location=1) noperspective out vec4 specular;
layout(location=2) out vec4 texcoord[8];
layout(location=10) out float depth;
layout(location=11) out float eye_w;
out gl_PerVertex { vec4 gl_Position; float gl_PointSize; float gl_ClipDistance[6]; };
void main() {
    gl_Position=vec4(position.x,-position.y,position.zw);
    gl_PointSize=1;
    diffuse=color.bgra;
    specular=secondary.bgra;
    texcoord[0]=coords0;texcoord[1]=coords1;
    for(int i=2;i<8;++i)texcoord[i]=vec4(0,0,0,1);
    depth=position.w!=0?position.z/position.w:0;
    eye_w=position.w;
    for(int i=0;i<6;++i)gl_ClipDistance[i]=1;
}
