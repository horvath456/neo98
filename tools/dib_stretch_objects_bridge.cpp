// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_dib_stretch_objects.h"
#include <cstring>
struct Objects {
 unsigned char *memory;const unsigned*a;unsigned*trace;
 unsigned Read8(unsigned p){return memory[p&65535];}unsigned Read16(unsigned p){return Read8(p)|(Read8(p+1)<<8);}unsigned Read32(unsigned p){return Read16(p)|(Read16(p+2)<<16);}
 void Write32(unsigned p,unsigned v){for(unsigned i=0;i<4;++i)memory[(p+i)&65535]=v>>(i*8);}
 void Event(unsigned kind,unsigned p0=0,unsigned p1=0,unsigned p2=0,unsigned p3=0,unsigned p4=0,unsigned p5=0,unsigned p6=0,unsigned p7=0){unsigned values[]={kind,p0,p1,p2,p3,p4,p5,p6,p7};for(unsigned v:values)trace[++trace[0]]=v;}
 unsigned DcObject(unsigned dc){return Read16(dc);}unsigned DibHeader(unsigned){return 0x4000;}unsigned Object(unsigned h){return Read16(h);}
 unsigned AllocateBitmap(const Win98Bitmap::Bitmap &b,unsigned dc,unsigned flags){Event(1,b.width,b.height,b.stride,b.planes,b.bpp,b.bits,dc,flags);return a[7];}
 unsigned CreateSection(unsigned dc,unsigned info){Event(2,dc,info,Read32(info+4),Read32(info+8));return a[7];}
 void MakeDiscardable(unsigned block){Event(3,block);}
};
extern "C" unsigned dib_stretch_objects(unsigned char *memory,const unsigned*a,unsigned*trace){
 using namespace Win98DibStretchObjects;Objects e{memory,a,trace};trace[0]=0;
 if(a[0]==0)return DeviceBitmap(e,a[1],a[2],a[3],a[4],a[5],a[6],a[8]);
 if(a[0]==1)return Compatible(e,a[1],a[2],a[3],a[4]);
 if(a[0]==2){U mode=a[1];U result=SetMode(mode,a[2]);e.Write32(0x5000,mode);return result;}
 if(a[0]==3)return SoftwareMode(a[1]);
 Rect r{int(a[1]),int(a[2]),int(a[3]),int(a[4])};U flags=Normalize(r,a[5]);e.Write32(0x5000,U(r.left));e.Write32(0x5004,U(r.top));e.Write32(0x5008,U(r.right));e.Write32(0x500c,U(r.bottom));return flags;
}
#ifdef DIB_STRETCH_OBJECTS_STANDALONE
#include <cstdio>
int main(int argc,char**argv){if(argc!=2)return 1;FILE*f=fopen(argv[1],"rb");if(!f)return 2;unsigned a[9],expected,n,count=0,trace[128],events[128];unsigned char before[65536],after[65536];const unsigned ranges[][2]={{0,64},{0x1000,0x11a0},{0x2200,0x2240},{0x3000,0x3040},{0x4000,0x4040},{0x5000,0x5010}};
 while(fread(a,sizeof a,1,f)==1){if(fread(&expected,4,1,f)!=1||fread(&n,4,1,f)!=1||n>128)return 3;memset(before,0,sizeof before);memset(after,0,sizeof after);
  for(auto &r:ranges)if(fread(before+r[0],r[1]-r[0],1,f)!=1||fread(after+r[0],r[1]-r[0],1,f)!=1)return 3;
  if(n&&fread(events,n*4,1,f)!=1)return 3;
  if(dib_stretch_objects(before,a,trace)!=expected||memcmp(before,after,sizeof before)||trace[0]!=n||memcmp(trace+1,events,n*4)){fprintf(stderr,"Stretch objects case %u\n",count);return 4;}++count;
 }fclose(f);printf("Stretch objects ASan: %u\n",count);return 0;}
#endif
