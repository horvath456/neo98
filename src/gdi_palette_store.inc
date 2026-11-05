// SPDX-License-Identifier: GPL-2.0-or-later
// Native handles/storage for the original logical palette entry and reference kernel.
unsigned default_palette=0;
unsigned NewPalette(unsigned count,const unsigned*colors,bool stock=false){
 if(count>4096)return 0;unsigned h=Allocate(PaletteObject);if(!h)return 0;
 auto&o=*Find(h,PaletteObject);o.palette.entries=new Win98GdiPalette::Entry[count]{};
 if(!o.palette.entries){Free(h);return 0;}o.palette.count=count;o.palette.stock=stock;
 for(unsigned i=0;i<count;++i)o.palette.entries[i].color=Win98GdiPalette::Normalize(colors[i]);return h;
}
struct PaletteStorage{
 bool Resize(Win98GdiPalette::Palette&p,unsigned count){
  if(count>4096)return false;auto*next=new Win98GdiPalette::Entry[count]{};if(!next)return false;
  for(unsigned i=0;i<count&&i<p.count;++i)next[i]=p.entries[i];delete[] p.entries;p.entries=next;return true;
 }
};
bool ResolveLogicalColor(const State&s,unsigned color,unsigned&result){
 if(!(color&0xff000000)){result=color;return true;}
 auto*p=Find(s.palette,PaletteObject);if(!p)return false;
 auto*b=Find(s.bitmap,BitmapObject);bool indexed=b&&b->dib&&b->bitmap.depth<=8;
 // On indexed device surfaces, PALETTEINDEX and PALETTERGB use the logical
 // entry's realized slot, encoded as FFFFxxxx for the driver. DIB sections
 // and true-color surfaces use the entry's RGB color instead.
 const unsigned tag=color>>24;
 if(tag==0x10&&indexed){result=0xffff0000u|(color&65535);return true;}
 if(PaletteDisplay()&&(tag==1||tag==2)&&b&&b->bitmap.depth<=8&&b->bitmap.depth>1&&DeviceSurface(*b)&&p->palette.count){
  unsigned index=tag==2?Win98GdiPalette::Nearest(p->palette,color&0xffffff):color&0xffff;
  if(index>=p->palette.count)index=0;
  result=0xffff0000u|(p->palette.entries[index].mapping[1]&255u);
  return true;
 }
 result=Win98GdiPalette::Tagged(p->palette,color,indexed);
 // Only the original PC_EXPLICIT indexed-DIB encoding is a direct physical color.
 return !(result&0xff000000)||(indexed&&(result>>16)==0xffff);
}
