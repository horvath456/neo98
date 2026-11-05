// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_image_directory.h"
#include <cassert>
#include <cstdio>
#include <vector>
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);unsigned size,count=0;while(fread(&size,4,1,f)==1){unsigned short v[11];assert(fread(v,2,11,f)==11&&size>=20&&size<65536);std::vector<unsigned char>b(size);assert(fread(b.data(),1,size,f)==size);Win98ImageDirectory::Display d{v[0],v[1],v[2],v[3],v[4],v[5]};assert(Win98ImageDirectory::Lookup(d,b.data(),v[6],v[7],v[8],v[9])==v[10]);++count;}fclose(f);assert(count==7000);puts("IMAGE DIRECTORY SANITIZER REPLAY PASS: 7000 original results");}
