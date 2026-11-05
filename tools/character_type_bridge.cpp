// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_character_type.h"
#include <cassert>
#include <cstring>
#include <cstdio>
namespace {
using U = unsigned;
constexpr U Base = 0x300000, Bytes = 2048;
struct E {
    unsigned char *memory; U *config; const unsigned char *table; U size;
    U SystemLocale() { return config[0]; }
    U UserLocale() { return config[1]; }
    U SystemTable() { return 1; }
    U LookupLocale(U locale) { return locale == config[4] ? 1 : 0; }
    U Read8(U p) { assert(p >= Base && p - Base < Bytes); return memory[p - Base]; }
    void Write16(U p, U v) { assert(p >= Base && p - Base <= Bytes - 2); memory[p-Base]=v; memory[p-Base+1]=v>>8; }
    U Length(U p) { U n=0; while(Read8(p+n)) ++n; return n; }
    U Half(U p) { assert(p <= size && size-p >= 2); return table[p] | U(table[p+1]) << 8; }
    U TypeWord(U, U type, U index) {
        const U p = (type == 1 ? 15 : type == 2 ? 16 : 17) * 4;
        return Half(((Half(p) | Half(p+2)<<16) + index) * 2);
    }
    bool Multibyte(U) { return config[2]; }
    bool LocaleLead(U, U c) { return c == 0x81 || c == 0xe0; }
    bool SystemLead(U c) { return config[3] && LocaleLead(0,c); }
    void Error(U error) { config[5] = error; }
};
}
extern "C" unsigned character_type(unsigned char *memory, unsigned *config,
                                    const unsigned char *table, unsigned size, const unsigned *args) {
    E e{memory,config,table,size};
    return Win98CharacterType::Query(e,args[0],args[1],args[2],args[3],args[4]);
}
#ifdef CHARACTER_TYPE_REPLAY
#include <vector>
int main(int argc,char **argv) {
    assert(argc==3); FILE *tables=fopen(argv[1],"rb"),*cases=fopen(argv[2],"rb"); assert(tables&&cases);
    std::vector<unsigned char> data[2];
    for(auto &t:data){unsigned n;assert(fread(&n,4,1,tables)==1);t.resize(n);assert(fread(t.data(),1,n,tables)==n);}
    unsigned count=0,record[13]; unsigned char before[Bytes],after[Bytes];
    while(fread(record,sizeof(record),1,cases)==1){
        assert(fread(before,1,Bytes,cases)==Bytes&&fread(after,1,Bytes,cases)==Bytes);
        unsigned cfg[6];memcpy(cfg,record,20);cfg[5]=987;
        auto &t=data[record[12]];unsigned result=character_type(before,cfg,t.data(),t.size(),record+5);
        assert(result==record[10]&&cfg[5]==record[11]&&!memcmp(before,after,Bytes));++count;
    }
    assert(feof(cases)&&count);fclose(tables);fclose(cases);printf("Character type replay PASS cases=%u\n",count);
}
#endif
