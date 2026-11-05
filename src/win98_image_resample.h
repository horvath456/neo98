// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// USER32 and the 4/8/24 -> 24 kernels used by USER.
namespace Win98ImageResample {
inline unsigned Blend(unsigned a,unsigned b,unsigned c,unsigned d,unsigned x,unsigned y)
{
    if (!x) return (a*(65536-y)+c*y)>>16;
    if (!y) return (a*(65536-x)+b*x)>>16;
    if (x==32768 && y==32768) return (a+b+c+d)>>2;
    // truncates each horizontal product before adding it.
    const unsigned top=((a*(65536-x))>>16)+((b*x)>>16);
    const unsigned bottom=((c*(65536-x))>>16)+((d*x)>>16);
    return (top*(65536-y)+bottom*y)>>16;
}
inline bool Scale(const unsigned char *source,unsigned sw,unsigned sh,unsigned depth,
                  const unsigned *palette,unsigned char *target,unsigned width,unsigned height)
{
    if (!source || !target || !sw || !sh || sw>32767 || sh>32767 ||
        width<2 || height<2 || width>32767 || height>32767 ||
        (depth!=4 && depth!=8 && depth!=24) || (depth!=24 && !palette)) return false;
    const unsigned stride=((sw*depth+31)/32)*4, pitch=((width*24+31)/32)*4;
    const bool half=width*2==sw && height*2==sh;
    const unsigned dx=half?131072:((sw-1)<<16)/(width-1);
    const unsigned dy=half?131072:((sh-1)<<16)/(height-1);
    auto sample=[&](unsigned x,unsigned y,unsigned &index) {
        if(x>=sw)x=sw-1;
        if(y>=sh)y=sh-1;
        const auto *row=source+y*stride;
        if(depth==24){index=~0u;return unsigned(row[x*3])|(unsigned(row[x*3+1])<<8)|(unsigned(row[x*3+2])<<16);}
        index=depth==8?row[x]:(row[x/2]>>((x&1)?0:4))&15;
        return palette[index];
    };
    unsigned fy=half?32768:0;
    for(unsigned y=0;y<height;++y,fy+=dy){
        unsigned fx=half?32768:0;
        for(unsigned x=0;x<width;++x,fx+=dx){
            const unsigned sx=fx>>16,sy=fy>>16;
            // Each original kernel clears the next-row stride on its last
            // destination row, including the exact half-size special case.
            unsigned indices[4],colors[4];
            colors[0]=sample(sx,sy,indices[0]);colors[1]=sample(sx+1,sy,indices[1]);
            colors[2]=sample(sx,y+1==height?sy:sy+1,indices[2]);
            colors[3]=sample(sx+1,y+1==height?sy:sy+1,indices[3]);
            const bool constant=depth!=24 && indices[0]==indices[1] && indices[0]==indices[2] && indices[0]==indices[3];
            for(unsigned channel=0;channel<3;++channel){
                const unsigned shift=channel*8;
                target[y*pitch+x*3+channel]=constant?(colors[0]>>shift)&255:
                    Blend((colors[0]>>shift)&255,(colors[1]>>shift)&255,(colors[2]>>shift)&255,(colors[3]>>shift)&255,fx&65535,fy&65535);
            }
        }
    }
    return true;
}
}
