// SPDX-License-Identifier: GPL-2.0-or-later
float saturate(float x){return clamp(x,0.0,1.0);}
vec2 saturate(vec2 x){return clamp(x,0.0,1.0);}
vec3 saturate(vec3 x){return clamp(x,0.0,1.0);}
vec4 saturate(vec4 x){return clamp(x,0.0,1.0);}
float vs_rcp(float x){return x==0.0?uintBitsToFloat(0x7f800000u):1.0/x;}
float vs_rsq(float x){float a=abs(x);return a==0.0?uintBitsToFloat(0x7f800000u):inversesqrt(a);}
float vs_log(float x){float a=abs(x);return a==0.0?-uintBitsToFloat(0x7f800000u):log2(a);}
vec4 vs_expp(float x){float f=floor(x);return vec4(exp2(f),x-f,exp2(x),1.0);}
vec4 vs_logp(float x){float a=abs(x);if(a==0.0)return vec4(-uintBitsToFloat(0x7f800000u),1,-uintBitsToFloat(0x7f800000u),1);float e=floor(log2(a));return vec4(e,a/exp2(e),log2(a),1);}
vec4 vs_lit(vec4 s){vec4 d=vec4(1,0,0,1);if(s.x>0){d.y=s.x;d.z=s.y>0?pow(s.y,clamp(s.w,-128.0,128.0)):0;}return d;}
vec4 vs_nrm(vec4 v){float l=length(v.xyz);return l>0?v/l:v;}
vec3 reflect_eye(vec3 n,vec3 e){float nn=dot(n,n);return nn!=0?2*dot(n,e)/nn*n-e:-e;}
#ifdef NEO98_PIXEL_PROGRAM
layout(std140,binding=3) uniform PixelConstants {vec4 floats[32];ivec4 integers[16];uvec4 booleans[4];} pc;
uint shader_bool(uint i){return pc.booleans[i/4u][i%4u];}
vec4 shader_sample(uint stage,vec4 c,bool projected){
    if(stage>=8u)return vec4(0,0,0,1);uint kind=u.stages[stage].z&255u;
    if(kind==0u)return vec4(0,0,0,1);
    vec3 uv=vec3(c.xy,0);uint p=u.stages[stage].w;
    if(projected&&p>0u&&p<=4u&&c[p-1u]!=0)uv.xy/=c[p-1u];
    if(kind==2u)uv=cube_coords(c.xyz);
    vec4 v=kind==3u?fetch_volume(int(stage),c.xyz):fetch_texture(int(stage),uv);
    if((u.stages[stage].z&512u)!=0u)v.xy=(v.xy*255-128)/127;
    return v;
}
vec4 shader_sample2(uint stage,vec4 c,uint mode){
    if(stage>=8u||(u.stages[stage].z&255u)==0u)return vec4(0,0,0,1);
    if(mode==0u||(u.stages[stage].z&255u)>=2u)return shader_sample(stage,c,false);
    vec4 v=vec4(0,0,0,1);vec3 uv=vec3(c.xy,0);
    switch(stage){
    case 0u:v=mode==1u?texture(images[0],uv,c.w):textureLod(images[0],uv,c.w);break;
    case 1u:v=mode==1u?texture(images[1],uv,c.w):textureLod(images[1],uv,c.w);break;
    case 2u:v=mode==1u?texture(images[2],uv,c.w):textureLod(images[2],uv,c.w);break;
    case 3u:v=mode==1u?texture(images[3],uv,c.w):textureLod(images[3],uv,c.w);break;
    case 4u:v=mode==1u?texture(images[4],uv,c.w):textureLod(images[4],uv,c.w);break;
    case 5u:v=mode==1u?texture(images[5],uv,c.w):textureLod(images[5],uv,c.w);break;
    case 6u:v=mode==1u?texture(images[6],uv,c.w):textureLod(images[6],uv,c.w);break;
    case 7u:v=mode==1u?texture(images[7],uv,c.w):textureLod(images[7],uv,c.w);break;}
    if((u.stages[stage].z&512u)!=0u)v.xy=(v.xy*255-128)/127;
    return v;
}
vec4 texbem(uint stage,vec4 coords,vec4 bump,bool luminance){vec4 m=u.bumps[stage];vec2 uv=coords.xy+vec2(bump.x*m.x+bump.y*m.z,bump.x*m.y+bump.y*m.w);vec4 v=shader_sample(stage,vec4(uv,0,1),false);if(luminance)v.rgb*=saturate(bump.z*u.luminance[stage].x+u.luminance[stage].y);return v;}
vec4 bem(uint stage,vec4 a,vec4 b){vec4 m=u.bumps[stage];return vec4(a.x+b.x*m.x+b.y*m.z,a.y+b.x*m.y+b.y*m.w,a.z,a.w);}
#else
uint shader_bool(uint i){return vc.booleans[i/4u][i%4u];}
#endif
