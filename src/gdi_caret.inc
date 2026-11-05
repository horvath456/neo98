// SPDX-License-Identifier: GPL-2.0-or-later
// Native pixel/resource provider for the implemented USER caret state.
bool CaretBitmap(unsigned handle,unsigned&width,unsigned&height)
{
    auto*o=Find(handle,BitmapObject);if(!o||!DibEng::Valid(o->bitmap)||(o->bitmap.depth!=1&&o->bitmap.depth!=32))return false;
    width=o->bitmap.width;height=o->bitmap.height;return width<=32767&&height<=32767;
}
bool ReferenceCaretBitmap(unsigned handle,bool add)
{
    auto*o=Find(handle,BitmapObject);if(!o)return false;
    if(add)Retain(handle,BitmapObject,o->owner);else if(o->references)Release(handle,BitmapObject);
    return true;
}
bool DrawCaret(HDC handle,unsigned image,int x,int y,int width,int height)
{
    auto*c=DC(handle);RECT area;if(!c||!(c->current.mapping.transform_flags&1)||!Rectangle(c->current,x,y,width,height,area))return false;
    auto*source=image>1?Find(image,BitmapObject):nullptr;
    if(image>1&&(!source||(source->bitmap.depth!=1&&source->bitmap.depth!=32)))return false;
    auto*target=Find(c->current.bitmap,BitmapObject);
    if(!target||(target->bitmap.depth!=32&&target->bitmap.depth!=24&&target->bitmap.depth!=16&&target->bitmap.depth!=8))return false;
    // The caret inverts the pixels of the device (USER draws it with an
    // inverting blt on the window DC): the colour bits at 24 and 32 bits, the
    // whole pixel value in the palettized and 16-bit modes (DSTINVERT).
    const unsigned all=target->bitmap.depth>=24?0xffffffu:target->bitmap.depth==16?0x7fffu:0xffu;
    BitmapAccess read(source,false),write(target,true);auto&dst=target->bitmap;
    RECT bounded;if(!Intersect(bounded,area,{0,0,dst.width,dst.height}))return true;
    for(unsigned i=0;i<c->current.clips;++i){RECT clip;if(!Intersect(clip,bounded,ClipAt(c->current,i)))continue;
        for(int row=clip.top;row<clip.bottom;++row)for(int col=clip.left;col<clip.right;++col){
            unsigned mask=0xffffff;
            if(image==1)mask=((row+col)&1)?0xffffff:0;
            else if(source){const int sx=col-area.left,sy=row-area.top;if(sx>=source->bitmap.width||sy>=source->bitmap.height)continue;
                mask=DibEng::ReadPixel(source->bitmap,sx,sy);if(source->bitmap.depth==1)mask=mask?0xffffff:0;}
            DibEng::WritePixel(dst,col,row,DibEng::ReadPixel(dst,col,row)^(dst.depth>=24?(mask&0xffffff):(mask?all:0u)));
        }
    }
    return true;
}
