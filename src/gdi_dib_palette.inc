// SPDX-License-Identifier: GPL-2.0-or-later
// Native DIB descriptors and logical-palette translation. For an indexed
// target with a realized palette and DIB_PAL_COLORS, map each source palette
// index to its realized device slot; out-of-range indices use entry zero.
// Preserve identity mappings explicitly. This path transfers indices directly
// instead of converting through RGB and choosing nearest colors, which would
// break PC_EXPLICIT palettes.
void BuildPaletteTranslation(DibFormat&f,const Win98GdiPalette::Palette&p,
                             const unsigned short*indices,Context&context){
 f.translate=false;
 if(f.depth!=4&&f.depth!=8)return;
 auto*target=Find(context.current.bitmap,BitmapObject);
 if(!target||!target->dib||target->bitmap.depth>8)return;   // Destination has RC_PALETTE
 if(!p.realized||!p.entries||!p.count)return;
 const unsigned entries=1u<<f.depth;
 for(unsigned i=0;i<entries;++i){
  // Slots absent from the index table map to themselves.
  unsigned index=i<f.colors?indices[i]:i;
  if(index>=p.count)index=0;                                 //
  f.xlate[i]=i<f.colors?p.entries[index].mapping[1]:(unsigned short)i;
 }
 f.translate=true;
}
bool ResolveDibPalette(BitmapIO&io,DibFormat&f,Context*context,bool constructing){
 if(f.depth>8)return true;
 if(!context){
  if(!constructing){io.Error(87);return false;}
  // Original CreateDIBSection skips GDI when hdc is zero.
  for(unsigned i=0;i<f.colors;++i)f.Write32(0x100+f.palette+i*4,0);
  return true;
 }
 auto*object=Find(context->current.palette,PaletteObject);
 if(!object||!object->palette.count){io.Error(87);return false;}
 struct Provider{
  DibFormat&f;unsigned short indices[256]{};
  unsigned Index(unsigned i){return indices[i];}
  void Color(unsigned i,unsigned color){f.Write32(0x100+f.palette+i*4,color);}
 }e{f};
 for(unsigned i=0;i<f.colors;++i)e.indices[i]=f.Read16(0x100+f.palette+i*2);
 if(constructing){unsigned used=f.Read32(0x120);Win98DibPalette::Construct(e,object->palette,f.depth,f.colors,used,true);f.Write32(0x120,used);}
 else Win98DibPalette::Transfer(e,object->palette,f.colors);
 if(!constructing)BuildPaletteTranslation(f,object->palette,e.indices,*context);
 return true;
}
