// SPDX-License-Identifier: GPL-2.0-or-later
#include "window_region_bridge.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);U count=0;assert(fread(&count,4,1,f)==1);for(U i=0;i<count;++i){State s,want;U a[5];assert(fread(&s,sizeof s,1,f)==1&&fread(a,sizeof a,1,f)==1&&fread(&want,sizeof want,1,f)==1);U got=window_region_run(&s,a[0],a[1],a[2],a[3]);assert(got==a[4]&&!memcmp(&s,&want,sizeof s));}assert(fgetc(f)==EOF);fclose(f);printf("Window region sanitizer replay PASS cases=%u\n",count);}
