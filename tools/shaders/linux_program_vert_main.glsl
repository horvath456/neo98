void main(){
    vec4 p=vec4(0,0,0,1),d[2],t[8],fog=vec4(1),size=vec4(1);
    d[0]=vec4(1);d[1]=vec4(0);for(int i=0;i<8;++i)t[i]=vec4(0,0,0,1);
    translated(input_registers,p,d,t,fog,size);
    eye_w=p.w;depth=p.w!=0?p.z/p.w:0;
    p.x+=p.w/max(vc.viewport.x,1);p.y=-p.y+p.w/max(vc.viewport.y,1);
    gl_Position=p;gl_PointSize=clamp(size.x,vc.viewport.z,vc.viewport.w);
    diffuse=clamp(d[0],0,1);specular=clamp(d[1],0,1);if(vc.flags.x!=0u)specular.a=fog.x;
    for(int i=0;i<8;++i)texcoord[i]=t[i];for(int i=0;i<6;++i)gl_ClipDistance[i]=1;
}
