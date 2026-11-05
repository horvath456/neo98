// SPDX-License-Identifier: GPL-2.0-or-later
#include "font_charset_bridge.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
static void Read(FILE*f,void*p,unsigned n){assert(fread(p,1,n,f)==n);}
int main(int argc,char**argv){assert(argc==6);unsigned total=0;U tables[42];FILE*f=fopen(argv[5],"rb");assert(f);Read(f,tables,sizeof tables);fclose(f);
 for(unsigned mode=0;mode<4;++mode){FILE*f=fopen(argv[mode+1],"rb");assert(f);U count;
  while(fread(&count,4,1,f)==1){assert(count<=13);U a[13]{},expected,n;unsigned char input[4096],after[4096];U trace[1025]{},events[1024]{};
   Read(f,a,count*4);Read(f,&expected,4);Read(f,input,4096);Read(f,after,4096);Read(f,&n,4);assert(n<=1024);Read(f,events,n*4);U result=0;
   if(mode==0)result=charset_face(a,input,trace);
   else if(mode==1)result=charset_signature(a,input,trace);
   else if(mode==2)result=charset_table(a,input,trace);
   else result=charset_translate(a,input,tables,trace);
   assert(result==expected&&trace[0]==n&&!memcmp(trace+1,events,n*4)&&!memcmp(input,after,4096));++total;
  }assert(feof(f));fclose(f);
 }printf("Font charset original replay PASS %u cases (ASan/UBSan)\n",total);
}
