// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef GDI_PIXEL_DRIVER_REPLAY
#include "gdi_pixel_driver_bridge.cpp"
#else
#include "gdi_pixel_core_bridge.cpp"
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);U count;assert(fread(&count,4,1,f)==1);for(U n=0;n<count;++n){Input s,want;
#ifdef GDI_PIXEL_DRIVER_REPLAY
 U a[3];assert(fread(&s,sizeof s,1,f)==1&&fread(a,sizeof a,1,f)==1&&fread(&want,sizeof want,1,f)==1);U physical=0x12345678,result=pixel_driver(&s,a[0],&physical);assert(result==a[1]&&(a[0]!=1||physical==a[2])&&!memcmp(&s,&want,sizeof s));
#else
 U a[5];assert(fread(&s,sizeof s,1,f)==1&&fread(a,sizeof a,1,f)==1&&fread(&want,sizeof want,1,f)==1);U result=pixel_core(&s,a[0],int(a[1]),int(a[2]),a[3]);assert(result==a[4]&&!memcmp(&s,&want,sizeof s));
#endif
 }assert(fgetc(f)==EOF);fclose(f);printf("GDI pixel sanitizer replay PASS cases=%u\n",count);}
