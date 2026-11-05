/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_shell_os.h"
#include <cstring>
struct E{const unsigned*c;const unsigned char*v;const char*product;unsigned*out,n=0;void T(unsigned event,unsigned a=0){out[1+n++]=event;out[1+n++]=a;}bool Version(unsigned char*data){unsigned size=Win98ShellOs::Read32(data);T(1,size);if(size==156&&c[0])return false;memcpy(data+4,v+4,size-4);return true;}unsigned Open(unsigned&key){T(2);key=9;return c[1];}unsigned Product(unsigned key,char*data,unsigned size){T(3,key);if(c[2])return c[2];strcpy(data,product);return 0;}bool Contains(const char*s,const char*word){T(4,*word=='E'?1:2);for(unsigned at=0;s[at];++at){unsigned i=0;while(word[i]&&s[at+i]&&Win98ShellOs::LowerAscii((unsigned char)s[at+i])==Win98ShellOs::LowerAscii((unsigned char)word[i]))++i;if(!word[i])return true;}return false;}void Close(unsigned key){T(5,key);}unsigned Metric(unsigned index){T(6,index);return c[3];}};
extern "C" unsigned shell_os_run(unsigned selector,const unsigned*c,const unsigned char*v,const char*product,Win98ShellOs::State*s,unsigned*out){E e{c,v,product,out};unsigned result=Win98ShellOs::Query(e,*s,selector);out[0]=e.n;return result;}
#ifdef SHELL_OS_SANITIZER
#include <cstdio>
#include <cassert>
int main(){unsigned selector,c[4],expected,out[33],actual[33],count=0;unsigned char version[156];char product[260];Win98ShellOs::State state,after;while(fread(&selector,4,1,stdin)==1){assert(fread(c,4,4,stdin)==4&&fread(version,1,156,stdin)==156&&fread(product,1,260,stdin)==260&&fread(&state,1,sizeof state,stdin)==sizeof state&&fread(&after,1,sizeof after,stdin)==sizeof after&&fread(&expected,4,1,stdin)==1&&fread(out,4,33,stdin)==33);memset(actual,0,sizeof actual);assert(shell_os_run(selector,c,version,product,&state,actual)==expected&&!memcmp(&state,&after,sizeof state)&&!memcmp(out,actual,sizeof out));++count;}printf("PASS %u original shell OS cases ASan/UBSan\n",count);}
#endif
