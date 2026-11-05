// SPDX-License-Identifier: GPL-2.0-or-later
#include "message_metadata_bridge.cpp"
#include <cassert>
#include <cstdio>
static void read(FILE*f,void*p,size_t n){assert(fread(p,1,n,f)==n);}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);unsigned n;read(f,&n,4);for(unsigned i=0;i<n;++i){State s,w;unsigned a[4];read(f,&s,sizeof s);read(f,a,sizeof a);read(f,&w,sizeof w);assert(metadata_run(&s,a[0],a[1],a[2])==a[3]&&!memcmp(&s,&w,sizeof w));}assert(fgetc(f)==EOF);fclose(f);printf("Message metadata sanitizer replay PASS cases=%u\n",n);}
