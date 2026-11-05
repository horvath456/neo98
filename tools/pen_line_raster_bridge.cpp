// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_pen_line.h"
struct Pixels {unsigned*data;unsigned Read(int x,int y){return data[y*32+x];}void Pixel(int x,int y,unsigned v){data[y*32+x]=v;}};
extern "C" unsigned pen_line_raster(const int*a,const int*points,unsigned*data){Pixels e{data};return Win98PenLine::Raster(e,reinterpret_cast<const Win98PenLine::Point*>(points),a[0],a[1],a[2],unsigned(a[3]),unsigned(a[4]),a[5],{a[6],a[7],a[8],a[9]});}

#include <cstring>
extern "C" void pen_line_fill(unsigned seed,unsigned char*out){for(unsigned i=0;i<65536;i+=4){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;memcpy(out+i,&seed,4);}}
#ifdef PEN_LINE_RASTER_STANDALONE
#include <cstdio>
int main(int argc,char**argv){if(argc!=2)return 1;FILE*f=fopen(argv[1],"rb");if(!f)return 2;int args[10],points[32];unsigned count=0,seed,wanted,n,change[2];alignas(4) unsigned char data[65536],expected[65536];
 while(fread(args,sizeof args,1,f)==1){if(args[0]<2||args[0]>16||fread(points,args[0]*8,1,f)!=1||fread(&seed,4,1,f)!=1||fread(&wanted,4,1,f)!=1||fread(&n,4,1,f)!=1)return 3;pen_line_fill(seed,data);memcpy(expected,data,sizeof data);
  for(unsigned i=0;i<n;++i){if(fread(change,sizeof change,1,f)!=1||change[0]>=16384)return 3;memcpy(expected+change[0]*4,change+1,4);}
  if(pen_line_raster(args,points,reinterpret_cast<unsigned*>(data+128))!=wanted||memcmp(data,expected,sizeof data)){fprintf(stderr,"Pen raster replay case %u\n",count);return 4;}++count;
 }fclose(f);printf("Pen raster ASan/UBSan PASS: %u original cases\n",count);return 0;}
#endif
