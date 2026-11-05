#version 450
#ifdef NEO98_FLAT
#define COLOR_QUALIFIER flat
#else
#define COLOR_QUALIFIER noperspective
#endif
layout(location = 0) COLOR_QUALIFIER in vec4 diffuse;
layout(location = 1) COLOR_QUALIFIER in vec4 specular;
layout(location = 2) in vec4 texcoord[8];
layout(location = 10) in float depth;
layout(location = 11) in float eye_w;
layout(binding=0) uniform sampler2DArray images[8];
layout(binding=4) uniform sampler3D volumes[8];
layout(std140,binding=1) uniform Uniforms {
    uvec4 stages[8]; // color op, alpha op, texture kind and key flag, projection count
    uvec4 args[8]; // color args 1/2, alpha args 1/2
    vec4 bumps[8];
    vec4 luminance[8];
    vec4 factor;
    vec4 fog_color;
    vec4 fog; // start, end, density, table mode
    uvec4 tests; // alpha comparison, reference, enable, specular enable
    uvec4 flags; // fog enable
    vec4 w_range;
} u;
layout(location = 0) out vec4 color;
// Specialization removes unused texture stages and combiner branches when a
// pipeline is compiled. Programmable pixel shaders keep the uniform path.
#ifdef NEO98_SPECIALIZE_FIXED
layout(constant_id=0) const uint fixed_stage_count=8u;
layout(constant_id=1) const uint fs_0_0=0u;
layout(constant_id=2) const uint fs_0_1=0u;
layout(constant_id=3) const uint fs_0_2=0u;
layout(constant_id=4) const uint fs_0_3=0u;
layout(constant_id=5) const uint fs_1_0=0u;
layout(constant_id=6) const uint fs_1_1=0u;
layout(constant_id=7) const uint fs_1_2=0u;
layout(constant_id=8) const uint fs_1_3=0u;
layout(constant_id=9) const uint fs_2_0=0u;
layout(constant_id=10) const uint fs_2_1=0u;
layout(constant_id=11) const uint fs_2_2=0u;
layout(constant_id=12) const uint fs_2_3=0u;
layout(constant_id=13) const uint fs_3_0=0u;
layout(constant_id=14) const uint fs_3_1=0u;
layout(constant_id=15) const uint fs_3_2=0u;
layout(constant_id=16) const uint fs_3_3=0u;
layout(constant_id=17) const uint fs_4_0=0u;
layout(constant_id=18) const uint fs_4_1=0u;
layout(constant_id=19) const uint fs_4_2=0u;
layout(constant_id=20) const uint fs_4_3=0u;
layout(constant_id=21) const uint fs_5_0=0u;
layout(constant_id=22) const uint fs_5_1=0u;
layout(constant_id=23) const uint fs_5_2=0u;
layout(constant_id=24) const uint fs_5_3=0u;
layout(constant_id=25) const uint fs_6_0=0u;
layout(constant_id=26) const uint fs_6_1=0u;
layout(constant_id=27) const uint fs_6_2=0u;
layout(constant_id=28) const uint fs_6_3=0u;
layout(constant_id=29) const uint fs_7_0=0u;
layout(constant_id=30) const uint fs_7_1=0u;
layout(constant_id=31) const uint fs_7_2=0u;
layout(constant_id=32) const uint fs_7_3=0u;
layout(constant_id=33) const uint fa_0_0=0u;
layout(constant_id=34) const uint fa_0_1=0u;
layout(constant_id=35) const uint fa_0_2=0u;
layout(constant_id=36) const uint fa_0_3=0u;
layout(constant_id=37) const uint fa_1_0=0u;
layout(constant_id=38) const uint fa_1_1=0u;
layout(constant_id=39) const uint fa_1_2=0u;
layout(constant_id=40) const uint fa_1_3=0u;
layout(constant_id=41) const uint fa_2_0=0u;
layout(constant_id=42) const uint fa_2_1=0u;
layout(constant_id=43) const uint fa_2_2=0u;
layout(constant_id=44) const uint fa_2_3=0u;
layout(constant_id=45) const uint fa_3_0=0u;
layout(constant_id=46) const uint fa_3_1=0u;
layout(constant_id=47) const uint fa_3_2=0u;
layout(constant_id=48) const uint fa_3_3=0u;
layout(constant_id=49) const uint fa_4_0=0u;
layout(constant_id=50) const uint fa_4_1=0u;
layout(constant_id=51) const uint fa_4_2=0u;
layout(constant_id=52) const uint fa_4_3=0u;
layout(constant_id=53) const uint fa_5_0=0u;
layout(constant_id=54) const uint fa_5_1=0u;
layout(constant_id=55) const uint fa_5_2=0u;
layout(constant_id=56) const uint fa_5_3=0u;
layout(constant_id=57) const uint fa_6_0=0u;
layout(constant_id=58) const uint fa_6_1=0u;
layout(constant_id=59) const uint fa_6_2=0u;
layout(constant_id=60) const uint fa_6_3=0u;
layout(constant_id=61) const uint fa_7_0=0u;
layout(constant_id=62) const uint fa_7_1=0u;
layout(constant_id=63) const uint fa_7_2=0u;
layout(constant_id=64) const uint fa_7_3=0u;
layout(constant_id=65) const uint fixed_alpha_enable=0u;
layout(constant_id=66) const uint fixed_alpha_function=8u;
layout(constant_id=67) const uint fixed_specular=0u;
layout(constant_id=68) const uint fixed_fog_enable=0u;
layout(constant_id=69) const uint fixed_fog_mode=0u;
uvec4 fixed_stages(int i){switch(i){
case 0:return uvec4(fs_0_0,fs_0_1,fs_0_2,fs_0_3);
case 1:return uvec4(fs_1_0,fs_1_1,fs_1_2,fs_1_3);
case 2:return uvec4(fs_2_0,fs_2_1,fs_2_2,fs_2_3);
case 3:return uvec4(fs_3_0,fs_3_1,fs_3_2,fs_3_3);
case 4:return uvec4(fs_4_0,fs_4_1,fs_4_2,fs_4_3);
case 5:return uvec4(fs_5_0,fs_5_1,fs_5_2,fs_5_3);
case 6:return uvec4(fs_6_0,fs_6_1,fs_6_2,fs_6_3);
default:return uvec4(fs_7_0,fs_7_1,fs_7_2,fs_7_3);}}
uvec4 fixed_args(int i){switch(i){
case 0:return uvec4(fa_0_0,fa_0_1,fa_0_2,fa_0_3);
case 1:return uvec4(fa_1_0,fa_1_1,fa_1_2,fa_1_3);
case 2:return uvec4(fa_2_0,fa_2_1,fa_2_2,fa_2_3);
case 3:return uvec4(fa_3_0,fa_3_1,fa_3_2,fa_3_3);
case 4:return uvec4(fa_4_0,fa_4_1,fa_4_2,fa_4_3);
case 5:return uvec4(fa_5_0,fa_5_1,fa_5_2,fa_5_3);
case 6:return uvec4(fa_6_0,fa_6_1,fa_6_2,fa_6_3);
default:return uvec4(fa_7_0,fa_7_1,fa_7_2,fa_7_3);}}
#define STAGE(i) fixed_stages(i)
#define ARGS(i) fixed_args(i)
#define STAGE_COUNT int(fixed_stage_count)
#define ALPHA_ENABLE fixed_alpha_enable
#define ALPHA_FUNCTION fixed_alpha_function
#define SPECULAR_ENABLE fixed_specular
#define FOG_ENABLE fixed_fog_enable
#define FOG_MODE fixed_fog_mode
#else
#define STAGE(i) u.stages[i]
#define ARGS(i) u.args[i]
#define STAGE_COUNT 8
#define ALPHA_ENABLE u.tests.z
#define ALPHA_FUNCTION u.tests.x
#define SPECULAR_ENABLE u.tests.w
#define FOG_ENABLE u.flags.x
#define FOG_MODE uint(u.fog.w)
#endif
vec4 fetch_texture(int stage, vec3 uv) {
    switch(stage) {
    case 0:return texture(images[0],uv);case 1:return texture(images[1],uv);
    case 2:return texture(images[2],uv);case 3:return texture(images[3],uv);
    case 4:return texture(images[4],uv);case 5:return texture(images[5],uv);
    case 6:return texture(images[6],uv);default:return texture(images[7],uv);
    }
}
vec4 fetch_volume(int stage,vec3 uv){
    switch(stage){case 0:return texture(volumes[0],uv);case 1:return texture(volumes[1],uv);
    case 2:return texture(volumes[2],uv);case 3:return texture(volumes[3],uv);case 4:return texture(volumes[4],uv);
    case 5:return texture(volumes[5],uv);case 6:return texture(volumes[6],uv);default:return texture(volumes[7],uv);}
}
vec3 cube_coords(vec3 p) {
    vec3 a=abs(p);float face,sc,tc,ma;
    if(a.x>=a.y&&a.x>=a.z){ma=a.x;if(p.x>=0){face=0;sc=-p.z;tc=-p.y;}else{face=1;sc=p.z;tc=-p.y;}}
    else if(a.y>=a.z){ma=a.y;if(p.y>=0){face=2;sc=p.x;tc=p.z;}else{face=3;sc=p.x;tc=-p.z;}}
    else{ma=a.z;if(p.z>=0){face=4;sc=p.x;tc=-p.y;}else{face=5;sc=-p.x;tc=-p.y;}}
    return vec3(vec2(sc,tc)/max(ma,1e-20)*0.5+0.5,face);
}
vec4 argument(uint arg,vec4 current,vec4 tex) {
    vec4 v;
    switch(arg&15u){case 0u:v=diffuse;break;case 1u:v=current;break;case 2u:v=tex;break;case 3u:v=u.factor;break;case 4u:v=specular;break;default:v=current;}
    if((arg&32u)!=0u)v=vec4(v.a);if((arg&16u)!=0u)v=1-v;return v;
}
vec4 operate(uint op,vec4 a,vec4 b,vec4 current,vec4 tex) {
    switch(op) {
    case 2u:return a;case 3u:return b;case 4u:return a*b;case 5u:return a*b*2;case 6u:return a*b*4;
    case 7u:return a+b;case 8u:return a+b-0.5;case 9u:return(a+b-0.5)*2;case 10u:return a-b;case 11u:return a+b-a*b;
    case 12u:return mix(b,a,diffuse.a);case 13u:return mix(b,a,tex.a);case 14u:return mix(b,a,u.factor.a);
    case 15u:return a+b*(1-tex.a);case 16u:return mix(b,a,current.a);case 17u:return vec4(current.rgb*tex.a,current.a);
    case 18u:return vec4(a.rgb+b.rgb*a.a,a.a);case 19u:return vec4(a.rgb*b.rgb+vec3(a.a),a.a);
    case 20u:return vec4(a.rgb+b.rgb*(1-a.a),a.a);case 21u:return vec4((1-a.rgb)*b.rgb+vec3(a.a),a.a);
    case 24u:return vec4(dot(a.rgb*2-1,b.rgb*2-1));default:return current;
    }
}
bool compare_alpha(uint fn,float a,float b) {
    switch(fn){case 1u:return false;case 2u:return a<b;case 3u:return abs(a-b)<0.5/255;case 4u:return a<=b;case 5u:return a>b;case 6u:return abs(a-b)>=0.5/255;case 7u:return a>=b;default:return true;}
}
vec4 finish_color(vec4 current) {
    if(ALPHA_ENABLE!=0u&&!compare_alpha(ALPHA_FUNCTION,current.a,float(u.tests.y)/255))discard;
    if(SPECULAR_ENABLE!=0u)current.rgb=clamp(current.rgb+specular.rgb,0,1);
    if(FOG_ENABLE!=0u){float f=specular.a;uint mode=FOG_MODE;float d=abs(depth);
        if(mode==3u)f=(u.fog.y-d)/max(u.fog.y-u.fog.x,1e-6);else if(mode==1u)f=exp(-u.fog.z*d);else if(mode==2u)f=exp(-u.fog.z*u.fog.z*d*d);
        current.rgb=mix(u.fog_color.rgb,current.rgb,clamp(f,0,1));}
    return clamp(current,0,1);
}

void texture_stage(int i, inout vec4 current, inout vec2 bump, inout float lum) {
        uint op=STAGE(i).x;if(op<=1u)return;
        uint kind=STAGE(i).z&255u;vec4 tex=vec4(1);vec4 t=texcoord[i];
        if(kind!=0u) {
            vec3 uv=vec3(t.xy,0);uint project=STAGE(i).w;
            if(project>0u&&project<=4u&&t[project-1u]!=0)uv.xy/=t[project-1u];
            uv.xy+=bump;if(kind==2u)uv=cube_coords(t.xyz);
            tex=kind==3u?fetch_volume(i,t.xyz):fetch_texture(i,uv);tex.rgb*=lum;
            if((STAGE(i).z&256u)!=0u&&tex.a<0.5)discard;
        }
        bump=vec2(0);lum=1;
        if(op==22u||op==23u){vec2 d=(tex.rg*255-128)/127;vec4 m=u.bumps[i];bump=vec2(d.x*m.x+d.y*m.z,d.x*m.y+d.y*m.w);if(op==23u)lum=clamp(tex.b*u.luminance[i].x+u.luminance[i].y,0,1);return;}
        vec4 rgb=operate(op,argument(ARGS(i).x,current,tex),argument(ARGS(i).y,current,tex),current,tex);
        vec4 alpha=STAGE(i).y<=1u?current:operate(STAGE(i).y,argument(ARGS(i).z,current,tex),argument(ARGS(i).w,current,tex),current,tex);
        current=clamp(vec4(rgb.rgb,alpha.a),0,1);
}

void main() {
    vec4 current=diffuse;vec2 bump=vec2(0);float lum=1;
#ifdef NEO98_SPECIALIZE_FIXED
    if (STAGE_COUNT > 0) texture_stage(0,current,bump,lum);
    if (STAGE_COUNT > 1) texture_stage(1,current,bump,lum);
    if (STAGE_COUNT > 2) texture_stage(2,current,bump,lum);
    if (STAGE_COUNT > 3) texture_stage(3,current,bump,lum);
    if (STAGE_COUNT > 4) texture_stage(4,current,bump,lum);
    if (STAGE_COUNT > 5) texture_stage(5,current,bump,lum);
    if (STAGE_COUNT > 6) texture_stage(6,current,bump,lum);
    if (STAGE_COUNT > 7) texture_stage(7,current,bump,lum);
#else
    for(int i=0;i<STAGE_COUNT;++i) {
        if(STAGE(i).x<=1u)break;
        texture_stage(i,current,bump,lum);
    }
#endif
    color=finish_color(current);
#ifdef NEO98_WBUFFER
    gl_FragDepth=clamp((eye_w-u.w_range.x)/max(u.w_range.y-u.w_range.x,1e-6),0,1);
#endif
}
