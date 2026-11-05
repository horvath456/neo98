// SPDX-License-Identifier: GPL-2.0-or-later
#include <initializer_list>
#include "../src/win98_process_entry.h"
#include <cstring>
#include <vector>
using U = unsigned;
static unsigned char bytes[65536];
static std::vector<U> events;
static Win98PathFamily::State state;
static U denied, access_count, first_result, retry_result, mutation;
struct Environment {
    bool Read8(U p, U &value) {
        value = p >= 0x100000 && p < 0x110000 ? bytes[p - 0x100000] : 0;
        events.insert(events.end(), {1,p,value});
        return ++access_count != denied && p >= 0x100000 && p < 0x110000;
    }
    bool Write8(U p, U value) {
        events.insert(events.end(), {2,p,value});
        if (++access_count == denied || p < 0x100000 || p >= 0x110000) return false;
        bytes[p - 0x100000] = value;
        return true;
    }
    U ConvertInput(U p, U mode) {
        events.insert(events.end(), {3,p,mode,state.thread_flags});
        return p ? p + 0x1000 : 0;
    }
    U Create(const U *a) {
        events.push_back(4);events.insert(events.end(),a,a+13);
        events.push_back(state.thread_flags);state.thread_flags ^= mutation;
        return first_result;
    }
    U Retry(U app, U command, const U *, U result) {
        events.insert(events.end(), {5,app,command,result,state.thread_flags});
        return retry_result;
    }
    void FreeInput(U p, U mode) { events.insert(events.end(), {6,p,mode,state.thread_flags}); }
};
extern "C" void reset(const void *data, U process, U thread, U fail, U first, U retry, U change) {
    memcpy(bytes,data,sizeof bytes);events.clear();state={};state.process_flags=process;
    state.thread_flags=thread;denied=fail;access_count=0;first_result=first;retry_result=retry;mutation=change;
}
extern "C" U invoke(U kind, const U *args) {
    Environment e;
    return kind ? Win98ProcessEntry::Launch(e,state,args) : Win98ProcessEntry::Probe(e,args);
}
extern "C" U trace(U *out) { for(U i=0;i<events.size();++i)out[i]=events[i];return events.size(); }
extern "C" U flags() { return state.thread_flags; }
extern "C" const void *memory() { return bytes; }
#ifdef PROCESS_ENTRY_REPLAY
#include <cassert>
#include <cstdio>
int main(int argc, char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);U count;
    assert(fread(&count,4,1,f)==1);
    for(U n=0;n<count;++n) {
        U h[10],a[10],expected[2048];unsigned char data[65536];
        assert(fread(h,4,10,f)==10&&fread(a,4,10,f)==10);
        assert(fread(data,1,sizeof data,f)==sizeof data&&h[9]<=2048);
        assert(fread(expected,4,h[9],f)==h[9]);
        reset(data,h[1],h[2],h[3],h[4],h[5],h[6]);
        assert(invoke(h[0],a)==h[7]&&flags()==h[8]);
        U actual[2048];assert(trace(actual)==h[9]);
        assert(!memcmp(actual,expected,h[9]*4));
        if(!h[0])assert(!memcmp(memory(),data,sizeof data));
    }
    assert(fgetc(f)==EOF);fclose(f);printf("PROCESS ENTRY REPLAY PASS cases=%u\n",count);
}
#endif
