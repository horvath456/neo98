// SPDX-License-Identifier: GPL-2.0-or-later
// Native brush/pen provider using the implemented DIBENG COLORINFO body.
namespace {
bool RealizeDeviceColor(const Object &object,COLORREF color,unsigned &physical,const State*state)
{
    if(state&&!ResolveLogicalColor(*state,color,color))return false;
    if(!DibEng::Valid(object.bitmap))return false;
    if(color&0xff000000){if(object.dib&&object.bitmap.depth<=8&&(color>>16)==0xffff){Win98DibPalette::DirectColor(color,physical);return true;}return false;}
    if(object.bitmap.depth<=8&&object.bitmap.depth!=1&&!object.dib)return false;
    // On device surfaces rather than DIB sections, black and white directly
    // select the first and last palette slots.
    if(object.dib&&object.dib->colours_only&&object.bitmap.depth<=8&&object.bitmap.depth>1&&
       ((color&0xffffff)==0||(color&0xffffff)==0xffffff)){
        physical=(color&0xffffff)?(1u<<unsigned(object.bitmap.depth))-1:0;
        return true;
    }
    struct PaletteProvider {
        const Object &object;
        unsigned Count(){return object.dib?object.dib->format.colors:2;}
        unsigned Palette(unsigned index){return Read32(index*4);}
        unsigned Read32(unsigned address){return object.dib?
            object.dib->format.Read32(0x100+object.dib->format.palette+address):address?0xffffff:0;}
    } palette{object};
    Win98GdiPixel::DriverColor(palette,object.bitmap.depth,
        object.dib&&object.dib->format.rgb565,color,physical,true,
        !object.dib&&object.bitmap.depth==1);
    return true;
}
}
