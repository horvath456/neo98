// SPDX-License-Identifier: GPL-2.0-or-later
#include "bitmap_bridge.cpp"
#include "../src/win98_bitmap_query.h"
struct QueryEnvironment : BitmapEnvironment
{
    using BitmapEnvironment::BitmapEnvironment;
    U error = 0xa5a5;
    U GetBitmap16(U handle, unsigned char (&out)[84])
    {
        Event(20,handle,84);
        if (!handle || (Read16(Object(handle)+2)&0x5fff)!=0x4f4b) return 0;
        return Win98BitmapQuery::ReadDdb(*this,handle,out);
    }
    void WriteGuest8(U p,U value) { Write8(p,value); }
    void Error(U value) { error=value;Event(21,value); }
    U MapPointer(U) { std::abort(); } // The ordinary DDB query always returns zero.
};
extern "C" {
void *bitmap_query_new() { return new QueryEnvironment(0,0x400,0x1234); }
void bitmap_query_delete(void *p) { delete static_cast<QueryEnvironment *>(p); }
U bitmap_query_create(void *p,const U *a) { return static_cast<QueryEnvironment *>(p)->Run(a); }
U bitmap_query_get(void *p,U handle,U count,U output) { return Win98BitmapQuery::Get(*static_cast<QueryEnvironment *>(p),handle,count,output); }
U bitmap_query_error(void *p) { return static_cast<QueryEnvironment *>(p)->error; }
void bitmap_query_reset(void *p,U failure)
{
    auto &e=*static_cast<QueryEnvironment *>(p);e.mode=failure;e.locks=0;e.error=0xa5a5;e.events.clear();
    for(U i=0;i<128;++i)e.memory[0x601000+i]=0x77;
}
}
#ifdef BITMAP_QUERY_STANDALONE
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=std::fopen(argv[1],"rb");assert(f);U cases;assert(std::fread(&cases,4,1,f)==1);
    for(U i=0;i<cases;++i)
    {
        U a[8],q[4];assert(std::fread(a,4,8,f)==8&&std::fread(q,4,4,f)==4);
        QueryEnvironment e(0,0x400,0x1234);assert(e.Run(a)==0x1002);bitmap_query_reset(&e,q[3]);
        Win98BitmapQuery::Get(e,q[0],q[1],q[2]);
    }
    assert(std::fgetc(f)==EOF);std::fclose(f);std::printf("bitmap query ASan/UBSan: %u cases passed\n",cases);
}
#endif
