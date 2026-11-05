/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_volume_information.h"
#include <cstring>
struct E{unsigned char*m;const unsigned*c;unsigned error=777;unsigned Byte(unsigned p){return m[p-0x100000];}void Byte(unsigned p,unsigned v){m[p-0x100000]=v;}void Write(unsigned p,unsigned v){memcpy(m+p-0x100000,&v,4);}void Error(unsigned v){error=v;}void Copy(unsigned p,unsigned q){do{Byte(p++,Byte(q));}while(Byte(q++));}void Current(unsigned p){Copy(p,0x100600);}bool FullPath(unsigned p,unsigned out){Copy(out,p);return true;}bool Exists(unsigned i){return i<26&&(c[0]&(1u<<i));}bool IoctlDrive(unsigned i){return Exists(i-1);}bool Serial(unsigned,unsigned&v){v=0x12345678;return !c[1];}unsigned Query(unsigned,unsigned fs,unsigned&max,unsigned&flags){Copy(fs,0x100800);max=255;flags=2;return c[2];}bool Label(unsigned,unsigned p){Copy(p,0x100900);return !c[3];}};
extern "C" unsigned volume_run(unsigned mode,const unsigned*a,const unsigned*c,unsigned char*m,unsigned*error){E e{m,c};unsigned result=mode==0?Win98Volume::Information(e,a,0x101000):mode==1?Win98Volume::Root(e,a[0],0x102000):mode==2?Win98Volume::Drives(e):Win98Volume::Strings(e,a[0],a[1]);*error=e.error;return result;}
#ifdef VOLUME_SANITIZER
#include <cstdio>
#include <cassert>
int main(){unsigned mode,n,a[8],c[4],expected,error,count=0;unsigned char bytes[16384],out[8192];while(fread(&mode,4,1,stdin)==1){assert(fread(&n,4,1,stdin)==1&&n<=8);assert(fread(a,4,8,stdin)==8&&fread(c,4,4,stdin)==4);assert(fread(&expected,4,1,stdin)==1&&fread(&error,4,1,stdin)==1);assert(fread(bytes,1,sizeof bytes,stdin)==sizeof bytes&&fread(out,1,sizeof out,stdin)==sizeof out);unsigned actualerror=0;assert(volume_run(mode,a,c,bytes,&actualerror)==expected&&actualerror==error&&!memcmp(bytes+0x2000,out,sizeof out));++count;}printf("PASS %u original cases ASan/UBSan\n",count);}
#endif
