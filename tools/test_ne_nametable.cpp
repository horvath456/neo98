// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_ne_image.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"NE nametable check failed at %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char**argv){
 CHECK(argc==2);FILE*f=std::fopen(argv[1],"rb");CHECK(f);std::fseek(f,0,SEEK_END);long n=std::ftell(f);std::rewind(f);
 std::vector<unsigned char>d(n);CHECK(std::fread(d.data(),1,d.size(),f)==d.size());std::fclose(f);
 auto lookup=[](const std::vector<unsigned char>&b,const char*name,unsigned&o,unsigned&s){Win98Ne::Image image;unsigned error=0;return image.Parse(b.data(),b.size(),error)&&image.ResourceAt(0x8002,0,name,o,s);};
 unsigned offset=0,bytes=0;CHECK(lookup(d,"TETRIS",offset,bytes)&&offset==0x5b20&&bytes==240);
 CHECK(lookup(d,"bg",offset,bytes)&&offset==0x5c10&&bytes==2704);
 Win98Ne::Image named;unsigned named_error=0;CHECK(named.Parse(d.data(),d.size(),named_error));
 CHECK(named.ResourceAt(0x8004,0,"tetris",offset,bytes)&&offset==0x66a0&&bytes==160);
 CHECK(named.ResourceAt(0x8005,0,"STARTGAME",offset,bytes)&&offset==0x6740&&bytes==400);
 Win98Ne::Image numeric;unsigned error=0;CHECK(numeric.Parse(d.data(),d.size(),error));CHECK(numeric.ResourceAt(0x8002,0x8001,nullptr,offset,bytes)&&offset==0x5b20);
 CHECK(numeric.ResourceAt(0x8004,0x8001,nullptr,offset,bytes)&&offset==0x66a0&&bytes==160);
 const unsigned char marker[]={0x0e,0,2,0,1,0x80,0,'T','E','T','R','I','S',0};
 auto at=std::search(d.begin(),d.end(),marker,marker+sizeof marker);CHECK(at!=d.end());
 const auto original=d;const auto entry=at-d.begin();
 d[entry+sizeof marker-1]='X'; // Missing ASCIZ terminator inside the entry.
 CHECK(!lookup(d,"TETRIS",offset,bytes));
 d=original;d.resize(d.size()-1); // Declared name-table resource exceeds EOF.
 CHECK(!lookup(d,"TETRIS",offset,bytes));
 d=original;d[entry]=7;
 CHECK(!lookup(d,"TETRIS",offset,bytes));
 std::puts("PASS NE RT_NAMETABLE named, numeric, case-insensitive and malformed entry");
}
