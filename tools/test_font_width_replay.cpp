// SPDX-License-Identifier: GPL-2.0-or-later
#include "font_width_bridge.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
static void Read(FILE*f,void*p,unsigned n){assert(fread(p,1,n,f)==n);}
int main(int argc,char**argv){assert(argc==6);unsigned total=0;
 for(unsigned mode=0;mode<5;++mode){FILE*f=fopen(argv[mode+1],"rb");assert(f);U count;
  while(fread(&count,4,1,f)==1){assert(count<=14);U a[14]{},expected,flags,draw=0,n;unsigned char input[2048],after[2048];U trace[2049]{},events[2048]{};
   Read(f,a,count*4);Read(f,&expected,4);Read(f,&flags,4);Read(f,input,2048);Read(f,after,2048);Read(f,&n,4);assert(n<=2048);Read(f,events,n*4);U result=0;
   if(mode==0)result=width_convert(a,input,trace);
   else if(mode==1)result=width_run(a,trace,&draw);
   else if(mode==2)result=width_query(a,input,trace);
   else if(mode==3)result=width_abc(a,trace);
   else width_expand(input,a[0],a[1]);
   assert(result==expected&&draw==flags&&trace[0]==n&&!memcmp(trace+1,events,n*4)&&!memcmp(input,after,2048));++total;
  }assert(feof(f));fclose(f);
 }printf("Font width original replay PASS %u cases (ASan/UBSan)\n",total);
}
