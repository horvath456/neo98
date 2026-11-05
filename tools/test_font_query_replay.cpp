// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstring>
#include "font_query_bridge.cpp"
static U Events(FILE*f,U*events){U n;assert(fread(&n,4,1,f)==1&&n<=1024);assert(fread(events,4,n,f)==n);return n;}
int main(int argc,char**argv){assert(argc==5);U count=0;FILE*f=fopen(argv[1],"rb");assert(f);Fixture fixture;U args[3],expected[1024],trace[1025];unsigned char data[4096],after[1024];
 while(fread(&fixture,sizeof fixture,1,f)==1){assert(fread(args,sizeof args,1,f)==1&&fread(data,1024,1,f)==1&&fread(after,sizeof after,1,f)==1);U n=Events(f,expected);memset(trace,0,sizeof trace);assert(font_face(&fixture,args[0],args[1],data,trace)==args[2]);assert(!memcmp(data,after,sizeof after)&&trace[0]==n&&!memcmp(trace+1,expected,n*4));++count;}assert(feof(f));fclose(f);
 f=fopen(argv[2],"rb");assert(f);U request[11];while(fread(request,sizeof request,1,f)==1){assert(fread(data,sizeof data,1,f)==1);U n=Events(f,expected);memset(trace,0,sizeof trace);assert(font_enum(request,data,trace)==request[10]);assert(trace[0]==n&&!memcmp(trace+1,expected,n*4));++count;}assert(feof(f));fclose(f);
 f=fopen(argv[3],"rb");assert(f);U family[22];while(fread(family,sizeof family,1,f)==1){assert(font_family(family[0],family[1],family[2],family[3],family+5,family[4])==family[21]);++count;}assert(feof(f));fclose(f);
 f=fopen(argv[4],"rb");assert(f);U width[2];while(fread(width,sizeof width,1,f)==1){assert(fread(data,1024,1,f)==1&&fread(after,sizeof after,1,f)==1);font_expand(data,width[0],width[1]);assert(!memcmp(data,after,sizeof after));++count;}assert(feof(f));fclose(f);
 printf("Font query original replay PASS: %u cases under ASan/UBSan\n",count);
}
