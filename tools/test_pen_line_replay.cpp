// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include <cstring>
extern "C" unsigned pen_line_objects(const unsigned*,unsigned*);
extern "C" unsigned pen_line_state(const unsigned*,unsigned*);
extern "C" unsigned pen_line_lifetime(const unsigned*,unsigned*);
extern "C" unsigned pen_line_query(const unsigned*,unsigned char*,unsigned*);
int main(int argc,char**argv){if(argc!=5)return 1;unsigned counts[4]{};
 for(unsigned family=0;family<4;++family){FILE*f=fopen(argv[family+1],"rb");if(!f)return 2;unsigned row[17],out[9]{};const unsigned words=family==0?15:family==1?17:family==2?15:8;
  while(fread(row,words*4,1,f)==1){unsigned result=0;
   if(family==0){result=pen_line_objects(row,out);if(result!=row[9]||memcmp(out,row+10,5*4))return 3;}
   if(family==1){result=pen_line_state(row,out);if(result!=row[7]||memcmp(out,row+8,9*4))return 4;}
   if(family==2){result=pen_line_lifetime(row,out);if(result!=row[6]||memcmp(out,row+7,8*4))return 5;}
   if(family==3){unsigned char expected[128],memory[128];unsigned error=0xa5a5;memset(memory,0x77,sizeof memory);if(fread(expected,sizeof expected,1,f)!=1)return 6;result=pen_line_query(row,memory,&error);if(result!=row[6]||error!=row[7]||memcmp(memory,expected,sizeof memory))return 7;}
   ++counts[family];
  }fclose(f);if(!counts[family])return 8;
 }
 printf("Pen objects/state/lifetime/query ASan/UBSan PASS: %u/%u/%u/%u cases\n",counts[0],counts[1],counts[2],counts[3]);return 0;
}
