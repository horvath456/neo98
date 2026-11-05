// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/files.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main(){
 assert(!Files::CacheResidentPages());Files::RequestCachePages();assert(!Files::CacheResidentPages());
 Files::ServiceCachePages();assert(Files::CacheResidentPages()==128);
 unsigned char*buffers[129]{};
 for(unsigned i=0;i<129;++i){buffers[i]=Files::AcquireBuffer(4096);assert(buffers[i]);for(unsigned j=0;j<i;++j)assert(buffers[i]!=buffers[j]);memset(buffers[i],i,4096);}
 assert(Files::CacheBufferLoans()==128);
 Files::RequestCachePages();Files::ServiceCachePages();assert(Files::CacheResidentPages()==128);
 for(unsigned i=0;i<129;++i){assert(buffers[i][0]==(unsigned char)i&&buffers[i][4095]==(unsigned char)i);Files::ReleaseBuffer(buffers[i]);}
 for(unsigned i=0;i<128;++i){auto*p=Files::AcquireBuffer(4096);assert(p);bool known=false;for(unsigned j=0;j<128;++j)known|=p==buffers[j];assert(known);Files::ReleaseBuffer(p);}
 assert(Files::CacheBufferLoans()==256);
 auto*large=Files::AcquireBuffer(8192);assert(large);memset(large,0x5a,8192);assert(large[8191]==0x5a);Files::ReleaseBuffer(large);
 assert(!Files::AcquireBuffer(0));Files::ReleaseBuffer(nullptr);
 puts("Native cache pool PASS: deferred refill, 128 distinct loans, exhaustion fallback, reuse and large buffers");
}
