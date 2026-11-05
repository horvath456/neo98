// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_bitmap_entry.h"
#include <cassert>
#include <cstdio>
#include <vector>
using U = unsigned;
struct EntryEnvironment
{
    U type, floor, failure, result;
    std::vector<U> events;
    void Event(U a,U b=0,U c=0,U d=0) { events.insert(events.end(),{a,b,c,d}); }
    U BitmapType(U h) { Event(18,h); return h && h>floor ? type&0x5fff : 0; }
    bool ProbeReadByte(U p) { Event(15,p);return p && !(failure&1); }
    bool ProbeWriteByte(U p) { Event(16,p);return p && !(failure&2); }
    void Reject(U error,U value) { Event(14,error,value); }
    U GetBitmapBits(U h,U count,U p) { Event(17,h,count,p);return result; }
    U SetBitmapBits(U h,U count,U p) { Event(19,h,count,p);return result; }
};
extern "C" U bitmap_entry_run(const U *a,U *trace,U *n)
{
    EntryEnvironment e{a[4],a[5],a[6],a[7],{}};
    U result=a[0] ? Win98BitmapEntry::SetBits(e,a[1],a[2],a[3]) : Win98BitmapEntry::GetBits(e,a[1],a[2],a[3]);
    *n=e.events.size();for(U i=0;i<*n;++i)trace[i]=e.events[i];return result;
}
#ifdef BITMAP_ENTRY_STANDALONE
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=std::fopen(argv[1],"rb");assert(f);U count;assert(std::fread(&count,4,1,f)==1);
    for(U i=0;i<count;++i){U a[8],trace[64],n;assert(std::fread(a,4,8,f)==8);bitmap_entry_run(a,trace,&n);}
    assert(std::fgetc(f)==EOF);std::fclose(f);std::printf("bitmap entry ASan/UBSan: %u cases passed\n",count);
}
#endif
