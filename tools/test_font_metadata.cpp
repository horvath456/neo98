// SPDX-License-Identifier: GPL-2.0-or-later
#include "dib_text.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using U=unsigned;
static void SetFace(std::vector<unsigned char>&b,U p){for(U i=0;i<4;++i)b[105+i]=p>>(8*i);}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);U n=ftell(f);rewind(f);std::vector<unsigned char>raw(n);assert(fread(raw.data(),1,n,f)==n);fclose(f);U count=0;
 for(U i=0;i<=DibText::FontCount();++i){DibText::Font font;if(!i)assert(font.Load(raw.data(),raw.size()));else font=DibText::OriginalFont(i-1);char name[32]{};assert(font.FaceName(name));assert(!strcmp(name,i?"MS Sans Serif":"Fixedsys"));++count;
  std::vector<unsigned char>b(font.data,font.data+font.length);const U bytes=b.size();
  for(U offset:{0u,bytes,~0u}){SetFace(b,offset);DibText::Font bad;assert(bad.Load(b.data(),b.size()));assert(!bad.FaceName(name));++count;}
  SetFace(b,bytes-5);memcpy(b.data()+bytes-5,"EDGE",5);DibText::Font edge;assert(edge.Load(b.data(),b.size()));assert(edge.FaceName(name)&&!strcmp(name,"EDGE"));++count;
  b.back()='X';assert(!edge.FaceName(name));++count;
  SetFace(b,bytes-32);memset(b.data()+bytes-32,'X',32);assert(!edge.FaceName(name));++count;
 }
 printf("Font metadata PASS: %u resource and bounds cases under ASan/UBSan\n",count);
}
