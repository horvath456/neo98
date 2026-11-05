// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstring>
using U=unsigned;
extern "C" U dc_drawing(U);
extern "C" U dc_caps(const unsigned char*,U,U,U,U,U);
extern "C" U dc_query(const U*,U*);
extern "C" void dc_initial(unsigned char*);
extern "C" U dc_create(unsigned char*,const U*,U*);
int main(int argc,char**argv){assert(argc==6);unsigned count=0;FILE*f=fopen(argv[1],"rb");assert(f);U args[6];unsigned char data[256];while(fread(args,sizeof args,1,f)==1){assert(fread(data,sizeof data,1,f)==1);assert(dc_caps(data,args[0],args[1],args[2],args[3],args[4])==args[5]);++count;}assert(feof(f));fclose(f);f=fopen(argv[2],"rb");assert(f);unsigned char before[264],after[264];while(fread(before,sizeof before,1,f)==1){assert(fread(after,sizeof after,1,f)==1);dc_initial(before);assert(!memcmp(before,after,sizeof before));++count;}assert(feof(f));fclose(f);f=fopen(argv[3],"rb");assert(f);U request[14],trace[6];unsigned char memory[8192],expected[8192];while(fread(request,sizeof request,1,f)==1){assert(fread(memory,sizeof memory,1,f)==1&&fread(expected,sizeof expected,1,f)==1);memset(trace,0,sizeof trace);assert(dc_create(memory,request,trace)==request[7]);assert(!memcmp(memory,expected,sizeof memory)&&!memcmp(trace,request+8,sizeof trace));++count;}assert(feof(f));fclose(f);f=fopen(argv[4],"rb");assert(f);U q[8],qt[2];while(fread(q,sizeof q,1,f)==1){memset(qt,0,sizeof qt);assert(dc_query(q,qt)==q[5]&&qt[0]==q[6]&&qt[1]==q[7]);++count;}assert(feof(f));fclose(f);f=fopen(argv[5],"rb");assert(f);U drawing[2];while(fread(drawing,sizeof drawing,1,f)==1){assert(dc_drawing(drawing[0])==drawing[1]);++count;}assert(feof(f));fclose(f);printf("DC information original replay PASS: %u cases under ASan/UBSan\n",count);}
