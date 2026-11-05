/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// Body appended to the shared D3D8/9 device setup by test_d3d_mip_filter_host.py.
    number("MIPPROBE texturecaps",caps[15]);number("MIPPROBE filtercaps",caps[16]);
    void *tex=0;U rect[2];const U colors[]={0xffff0000,0xff00ff00,0xff0000ff,0xffffff00};
#ifdef DX9
    value=((M9)dvt[CreateTexture])(device,8,8,MIP_LEVELS,0,TEX_FORMAT,1,(U)&tex,0);
#else
    value=((M8)dvt[CreateTexture])(device,8,8,MIP_LEVELS,0,TEX_FORMAT,1,(U)&tex);
#endif
    number("MIPPROBE create",value);if(value)fail(22);number("MIPPROBE levels",((M1)(*(U ***)tex)[13])(tex));
    for(U level=0;level<MIP_LEVELS;++level){
        U size=8>>level;
        value=((M5)(*(U ***)tex)[TextureLockRect])(tex,level,(U)rect,0,0);if(value)fail(23);
        #if TEX_FORMAT == 21
        for(U y=0;y<size;++y)for(U x=0;x<size;++x)*(U*)(rect[1]+y*rect[0]+x*4)=colors[level];
#else
        const U rgb565[]={0xf800,0x07e0,0x001f,0xffe0};
        U blocks=(size+3)/4;
        for(U y=0;y<blocks;++y)for(U x=0;x<blocks;++x){
            U *p=(U*)(rect[1]+y*rect[0]+x*(TEX_FORMAT==0x31545844?8:16));
            if(TEX_FORMAT!=0x31545844){*p++=0x0000ffff;*p++=0;}
            *p++=rgb565[level]|(rgb565[level]<<16);*p=0;
        }
#endif
        ((M2)(*(U ***)tex)[TextureUnlockRect])(tex,level);
    }
    ((M2)dvt[SetFormat])(device,0x144);((M3)dvt[SetRenderState])(device,7,0);((M3)dvt[SetRenderState])(device,22,1);
    ((M3)dvt[SetTexture])(device,0,(U)tex);
    ((M4)dvt[SetTextureStageState])(device,0,1,2);((M4)dvt[SetTextureStageState])(device,0,2,2);
    ((M7)dvt[Clear])(device,0,0,1,0xff202020,0x3f800000,0);((M1)dvt[BeginScene])(device);
    for(U filter=0;filter<4;++filter){
        number("MIPPROBE filter",filter);
        U mode=filter<3?filter:2;
#ifdef DX9
        value=((M4)dvt[69])(device,0,7,mode);
        ((M4)dvt[69])(device,0,5,1);((M4)dvt[69])(device,0,6,1);
#else
        value=((M4)dvt[SetTextureStageState])(device,0,18,mode);
        ((M4)dvt[SetTextureStageState])(device,0,16,1);((M4)dvt[SetTextureStageState])(device,0,17,1);
#endif
        number("MIPPROBE set",value);
        struct V{float x,y,z,w;U color;float u,v;};
        float x=16+filter*96;const float extent=filter==3?64.0f:19.02731384f;
        struct V v[]={{x-.5f,63.5f,.5f,1,0xffffffff,0,0},{x+63.5f,63.5f,.5f,1,0xffffffff,extent,0},
                      {x-.5f,127.5f,.5f,1,0xffffffff,0,extent},{x+63.5f,127.5f,.5f,1,0xffffffff,extent,extent}};
        value=((M5)dvt[DrawPrimitiveUP])(device,5,2,(U)v,sizeof(struct V));number("MIPPROBE draw",value);
    }
    ((M1)dvt[EndScene])(device);((M5)dvt[Present])(device,0,0,0,0);
    number("MIPPROBE done",1);Sleep(8000);ExitProcess(0);
}
