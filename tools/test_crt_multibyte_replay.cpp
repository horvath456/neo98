// SPDX-License-Identifier: GPL-2.0-or-later
#include "crt_multibyte_bridge.cpp"
#include <cstdio>
static void read(FILE*f,void*p,size_t n){assert(fread(p,1,n,f)==n);}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);unsigned n;read(f,&n,4);for(unsigned i=0;i<n;++i){Input s,w;unsigned args[5];read(f,&s,sizeof s);read(f,args,sizeof args);read(f,&w,sizeof w);assert(multibyte_run(&s,args[0],args[1],args[2],args[3])==args[4]&&!memcmp(&s,&w,sizeof s));}assert(fgetc(f)==EOF);fclose(f);printf("CRT multibyte sanitizer replay PASS cases=%u\n",n);}
