// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_dib_stretch.h"
#include "../src/dibeng.h"
#include <cstdlib>
#include <cstring>
struct RasterProvider {
 const unsigned *source;unsigned *destination;int source_width,destination_width;unsigned rop,pattern;
 int *AllocateSamples(unsigned n){return static_cast<int*>(malloc(n*sizeof(int)));}
 void FreeSamples(int*p){free(p);}
 void Pixel(int x,int y,int sx,int sy){auto &d=destination[y*destination_width+x];d=DibEng::ApplyRop((rop>>16)&255,source[sy*source_width+sx],d,pattern);}
};
extern "C" unsigned dib_stretch_raster(const int *a,const unsigned *source,unsigned *destination){
 using namespace Win98DibStretch;RasterProvider e{source,destination,a[0],a[2],unsigned(a[17]),unsigned(a[18])};
 Request r{a[0],a[1],{a[4],a[5],a[4]+a[6],a[5]+a[7]},{a[8],a[9],a[8]+a[10],a[9]+a[11]},{a[12],a[13],a[14],a[15]}};
 return Raster(e,r);
}
extern "C" void dib_stretch_fill(unsigned seed,unsigned char *out){
 for(unsigned i=0;i<65536;i+=4){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;memcpy(out+i,&seed,4);}
}
#ifdef DIB_STRETCH_RASTER_STANDALONE
#include <cstdio>
int main(int argc,char **argv){if(argc!=2)return 1;FILE*f=fopen(argv[1],"rb");if(!f)return 2;int a[19];unsigned seed[2],expected,nranges,count=0;unsigned source[16384],destination[16384],after[16384];unsigned char original[65536];
 while(fread(a,sizeof a,1,f)==1){if(fread(seed,sizeof seed,1,f)!=1||fread(&expected,4,1,f)!=1||fread(&nranges,4,1,f)!=1)return 3;
  dib_stretch_fill(seed[0],original);memcpy(source,original+128,65536-128);memcpy(reinterpret_cast<unsigned char*>(source)+65536-128,original,128);
  dib_stretch_fill(seed[1],original);memcpy(destination,original+128,65536-128);memcpy(reinterpret_cast<unsigned char*>(destination)+65536-128,original,128);memcpy(after,destination,sizeof after);
  for(unsigned r=0;r<nranges;++r){unsigned range[2];if(fread(range,sizeof range,1,f)!=1||range[0]>65536||range[1]>65536-range[0]||fread(reinterpret_cast<unsigned char*>(after)+range[0],range[1],1,f)!=1)return 3;}
  if(dib_stretch_raster(a,source,destination)!=expected||memcmp(destination,after,sizeof after)){fprintf(stderr,"stretch raster case %u\n",count);return 4;}++count;
 }fclose(f);printf("Stretch raster ASan: %u\n",count);return 0;
}
#endif
