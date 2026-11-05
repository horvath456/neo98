// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_crt_search.h"
#include <cassert>
#include <cstdio>
#include <vector>
using U = unsigned;
struct SearchEnvironment
{
    static constexpr U base = 0x10000000;
    std::vector<unsigned char> bytes;
    unsigned char table[256];
    std::vector<U> events;
    U multibyte;
    SearchEnvironment(const unsigned char *data,U size,const unsigned char *lead,U mode)
        :bytes(data,data+size),multibyte(mode)
    { for(U i=0;i<256;++i)table[i]=lead[i]; }
    void Event(U a,U b=0,U c=0) { events.insert(events.end(),{a,b,c}); }
    U Byte(U p) const { assert(p>=base && p-base<bytes.size());return bytes[p-base]; }
    U Read8(U p) { U n=Byte(p);Event(1,p,n);return n; }
    U Read32(U p) { U n=Byte(p)|(Byte(p+1)<<8)|(Byte(p+2)<<16)|(Byte(p+3)<<24);Event(2,p,n);return n; }
    bool Multibyte() { Event(3,multibyte);return multibyte!=0; }
    bool LeadByte(U b) { Event(4,b,table[b]);return table[b]&4; }
    void Lock(U id) { Event(5,id); }
    void Unlock(U id) { Event(6,id); }
    U Run(U op,U offset,U character)
    {
        switch(op)
        {
        case 0:return Win98CrtSearch::Find(*this,base+offset,character);
        case 1:return Win98CrtSearch::FindLast(*this,base+offset,character);
        case 2:return Win98CrtSearch::MultiFind(*this,base+offset,character);
        case 3:return Win98CrtSearch::MultiFindLast(*this,base+offset,character);
        default:assert(false);return 0;
        }
    }
};
extern "C" {
void *crt_search_new(const unsigned char *data,U size,const unsigned char *table,U mode) { return new SearchEnvironment(data,size,table,mode); }
void crt_search_delete(void *p) { delete static_cast<SearchEnvironment *>(p); }
U crt_search_run(void *p,U op,U offset,U character) { return static_cast<SearchEnvironment *>(p)->Run(op,offset,character); }
const U *crt_search_events(void *p,U *count) { auto &v=static_cast<SearchEnvironment *>(p)->events;*count=v.size();return v.data(); }
}
#ifdef CRT_SEARCH_STANDALONE
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=std::fopen(argv[1],"rb");assert(f);U count;assert(std::fread(&count,4,1,f)==1);
    for(U i=0;i<count;++i)
    {
        U a[5];unsigned char table[256];assert(std::fread(a,4,5,f)==5 && std::fread(table,1,256,f)==256);
        std::vector<unsigned char> bytes(a[4]);assert(std::fread(bytes.data(),1,a[4],f)==a[4]);
        SearchEnvironment e(bytes.data(),a[4],table,a[1]);e.Run(a[0],a[2],a[3]);
    }
    assert(std::fgetc(f)==EOF);std::fclose(f);std::printf("CRT search ASan/UBSan: %u cases passed\n",count);
}
#endif
