// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstring>
#include "keyboard_layout_loader_bridge.cpp"
template<class F,class Run> unsigned replay(const char*name,Run run){
 FILE*f=fopen(name,"rb");assert(f);unsigned count=0;U args[4];
 while(fread(args,sizeof args,1,f)==1){F before,after;unsigned char memory[1024],expected[1024];U n,events[1024],actual[1025]{};
  assert(fread(&before,sizeof before,1,f)==1&&fread(&after,sizeof after,1,f)==1&&fread(memory,sizeof memory,1,f)==1&&fread(expected,sizeof expected,1,f)==1&&fread(&n,sizeof n,1,f)==1);assert(n<=1024);assert(fread(events,4,n,f)==n);
  assert(run(&before,args[0],args[1],args[2],memory,actual)==args[3]);assert(!memcmp(&before,&after,sizeof before));assert(!memcmp(memory,expected,sizeof memory));assert(actual[0]==n&&!memcmp(actual+1,events,4*n));++count;
 }assert(feof(f));fclose(f);return count;
}
int main(int argc,char**argv){assert(argc==3);const U n=replay<Fixture>(argv[1],keyboard_layout_case)+replay<LoaderFixture>(argv[2],keyboard_loader_case);printf("Keyboard layout original replay PASS: %u cases under ASan/UBSan\n",n);}
