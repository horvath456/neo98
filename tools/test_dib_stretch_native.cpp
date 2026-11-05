// SPDX-License-Identifier: GPL-2.0-or-later
#define main EarlierDibNativeTest
#include "test_dib_api_native.cpp"
#undef main
static unsigned Call(Memory &m,const char *name,std::initializer_list<unsigned> values){unsigned a[13]{},i=0;for(unsigned n:values)a[i++]=n;return Gdi::IsStretchApi(name)?Gdi::StretchApi(name,a,m):Gdi::DibApi(name,a,m);}
int main(){
 using namespace Gdi;Memory m;const unsigned initial=LiveObjects();
 unsigned source_pixels[]={11,12,21,22},destination_pixels[25]{};
 unsigned source=CreateSurfaceDC({reinterpret_cast<unsigned char*>(source_pixels),2,2,8,32});
 unsigned destination=CreateSurfaceDC({reinterpret_cast<unsigned char*>(destination_pixels),5,5,20,32});CHECK(source&&destination);
 CHECK(Call(m,"GetStretchBltMode",{destination})==1);CHECK(SaveDC(destination)==1);
 CHECK(Call(m,"SetStretchBltMode",{destination,4})==1);CHECK(Call(m,"SetStretchBltMode",{destination,5})==0&&Call(m,"GetStretchBltMode",{destination})==4);
 CHECK(Call(m,"StretchBlt",{destination,0,0,5,5,source,0,0,2,2,SRCCOPY}));
 const unsigned xmap[]={0,0,0,1,1},ymap[]={0,0,1,1,1};
 for(unsigned y=0;y<5;++y)for(unsigned x=0;x<5;++x)CHECK(destination_pixels[y*5+x]==source_pixels[ymap[y]*2+xmap[x]]);
 CHECK(RestoreDC(destination,-1)&&Call(m,"GetStretchBltMode",{destination})==1);
 // The same sampler now runs through StretchDIBits and honors translated DCs.
 m.Header(32,2,-2);memcpy(m.data+0x1000,source_pixels,sizeof source_pixels);memset(destination_pixels,0,sizeof destination_pixels);
 CHECK(SetViewportOrgEx(destination,2,1));CHECK(Call(m,"StretchDIBits",{destination,unsigned(-2),unsigned(-1),5,5,0,0,2,2,0x11000,0x10000,0,SRCCOPY})==2);
 for(unsigned y=0;y<5;++y)for(unsigned x=0;x<5;++x)CHECK(destination_pixels[y*5+x]==source_pixels[ymap[y]*2+xmap[x]]);
 CHECK(SetViewportOrgEx(destination,0,0));
 // A failed sample-map allocation changes no destination pixel.
 unsigned before[25];memcpy(before,destination_pixels,sizeof before);fail_after=0;
 CHECK(!Call(m,"StretchBlt",{destination,0,0,5,5,source,0,0,2,2,SRCCOPY})&&m.error==8&&memcmp(before,destination_pixels,sizeof before)==0);
 CHECK(!Call(m,"StretchBlt",{destination,0,0,5,5,source,0,0,2,2,SRCINVERT})&&m.error==50&&memcmp(before,destination_pixels,sizeof before)==0);
 // Reversed endpoint normalization includes the original origin pixel.
 memset(destination_pixels,0,sizeof destination_pixels);
 CHECK(Call(m,"StretchBlt",{destination,1,1,unsigned(-2),unsigned(-2),source,0,0,2,2,SRCCOPY}));
 CHECK(destination_pixels[0]==22&&destination_pixels[1]==21&&destination_pixels[5]==12&&destination_pixels[6]==11&&destination_pixels[2]==0);
 // Mirrored source clipping stays inside its physical bitmap.
 unsigned three[]={1,2,3};unsigned three_dc=CreateSurfaceDC({reinterpret_cast<unsigned char*>(three),3,1,12,32});memset(destination_pixels,0,sizeof destination_pixels);
 CHECK(Call(m,"StretchBlt",{destination,0,0,5,1,three_dc,2,0,unsigned(-5),1,SRCCOPY}));CHECK(destination_pixels[0]==3&&destination_pixels[1]==2&&destination_pixels[2]==1&&destination_pixels[3]==0&&destination_pixels[4]==0);CHECK(DeleteDC(three_dc));
 // In-place enlargement snapshots the source before any destination write.
 unsigned overlap[36];for(unsigned i=0;i<36;++i)overlap[i]=i+100;unsigned old_overlap[36];memcpy(old_overlap,overlap,sizeof overlap);
 unsigned overlap_dc=CreateSurfaceDC({reinterpret_cast<unsigned char*>(overlap),6,6,24,32});
 fail_after=1;CHECK(!Call(m,"StretchBlt",{overlap_dc,0,0,6,6,overlap_dc,0,0,3,3,SRCCOPY})&&m.error==8&&memcmp(overlap,old_overlap,sizeof overlap)==0);
 CHECK(Call(m,"StretchBlt",{overlap_dc,0,0,6,6,overlap_dc,0,0,3,3,SRCCOPY}));for(unsigned y=0;y<6;++y)for(unsigned x=0;x<6;++x)CHECK(overlap[y*6+x]==old_overlap[(y/2)*6+x/2]);CHECK(DeleteDC(overlap_dc));
 // Overlap also exists between distinct surface objects with different bases.
 unsigned shared[25];for(unsigned i=0;i<25;++i)shared[i]=i+200;unsigned prior[25];memcpy(prior,shared,sizeof prior);
 unsigned sa=CreateSurfaceDC({reinterpret_cast<unsigned char*>(shared),4,4,20,32}),sb=CreateSurfaceDC({reinterpret_cast<unsigned char*>(shared+1),4,4,20,32});
 CHECK(Call(m,"StretchBlt",{sb,0,0,4,4,sa,0,0,2,2,SRCCOPY}));for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)CHECK(shared[y*5+x+1]==prior[(y/2)*5+x/2]);CHECK(DeleteDC(sa)&&DeleteDC(sb));
 // Compatible DDBs use the selected DC format, including monochrome defaults.
 unsigned bitmap=Call(m,"CreateCompatibleBitmap",{destination,7,3});CHECK(bitmap&&HasOriginalBitmap(bitmap));CHECK(Find(bitmap,BitmapObject)->bitmap.depth==32&&Find(bitmap,BitmapObject)->bitmap.width==7);CHECK(DeleteObject(bitmap));
 unsigned memory_dc=CreateCompatibleDC();bitmap=Call(m,"CreateDiscardableBitmap",{memory_dc,7,3});CHECK(bitmap&&HasOriginalBitmap(bitmap)&&Find(bitmap,BitmapObject)->bitmap.depth==1);CHECK(DeleteObject(bitmap));
 bitmap=Call(m,"CreateCompatibleBitmap",{memory_dc,0,3});CHECK(bitmap);CHECK(DeleteObject(bitmap));CHECK(DeleteDC(memory_dc));
 // Selecting a DIBSection changes the compatible factory to the original
 // DIBSection branch. Failure restores the template and releases no live bits.
 m.Header(16,4,-3,3);m.W(0x10028,0xf800);m.W(0x1002c,0x7e0);m.W(0x10030,31);
 unsigned section=Call(m,"CreateDIBSection",{0,0x10000,0,0x10100,0,0});CHECK(section);unsigned dc=CreateCompatibleDC(),saved=SelectObject(dc,section);CHECK(saved);
 auto *template_object=Find(section,BitmapObject);unsigned char header[1080];memcpy(header,template_object->dib->format.info,sizeof header);unsigned live=m.allocations-m.frees;
 m.fail_lease=true;CHECK(!Call(m,"CreateCompatibleBitmap",{dc,9,5}));CHECK(m.allocations-m.frees==live&&memcmp(header,template_object->dib->format.info,sizeof header)==0);m.fail_lease=false;
 for(unsigned i=0;i<80;++i){unsigned clone=Call(m,"CreateCompatibleBitmap",{dc,9,5});CHECK(clone&&HasDibBitmap(clone));auto *object=Find(clone,BitmapObject);CHECK(object->bitmap.width==9&&object->bitmap.height==5&&object->dib->format.rgb565);CHECK(memcmp(header,template_object->dib->format.info,sizeof header)==0);CHECK(DeleteObject(clone));}
 CHECK(SelectObject(dc,saved)==section&&DeleteObject(section)&&DeleteDC(dc));
 CHECK(DeleteDC(source)&&DeleteDC(destination));ShutdownBitmaps();CHECK(LiveObjects()==initial&&m.allocations==m.frees);
 puts("Stretch native PASS: original sampling, modes, translation, mirrors, clipped sources, overlapping surfaces, OOM and compatible DDB/DIBSection lifetime");return 0;
}
