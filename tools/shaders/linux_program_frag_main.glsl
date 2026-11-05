void main(){vec4 coords[8];for(int i=0;i<8;++i)coords[i]=texcoord[i];color=finish_color(translated(diffuse,specular,coords));
#ifdef NEO98_WBUFFER
    gl_FragDepth=clamp((eye_w-u.w_range.x)/max(u.w_range.y-u.w_range.x,1e-6),0,1);
#endif
}
