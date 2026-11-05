// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <array>
#include "shell_registration_bridge.cpp"
static std::vector<std::array<unsigned,6>> events;
static void event(unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,unsigned f){events.push_back({a,b,c,d,e,f});}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);Wire w,expected;unsigned count=0;
while(fread(&w,sizeof w,1,f)==1){unsigned op,h,result,n;assert(fread(&op,4,1,f)==1&&fread(&h,4,1,f)==1&&fread(&result,4,1,f)==1&&fread(&expected,sizeof expected,1,f)==1&&fread(&n,4,1,f)==1);std::vector<std::array<unsigned,6>>trace(n);assert(fread(trace.data(),24,n,f)==n);events.clear();assert(shell_registration(&w,event,op,h)==result&&!memcmp(&w,&expected,sizeof w)&&events==trace);++count;}assert(count==5003);fclose(f);puts("SHELL REGISTRATION SANITIZER REPLAY PASS: 5003 original results/events");}
