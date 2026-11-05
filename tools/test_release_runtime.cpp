// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic checks for raster truth tables and registry journal migration.
#include "../src/rop_truth_table.h"
#include "../src/registry.h"
#include <cassert>
#include <cstring>
#include <vector>
struct Memory : Registry::Storage {
    std::vector<unsigned char> bytes;
    unsigned Size() override { return bytes.size(); }
    bool Load(unsigned char *p, unsigned n) override { memcpy(p,bytes.data(),n); return true; }
    bool Save(const unsigned char *p, unsigned n) override { bytes.assign(p,p+n); return true; }
};
int main() {
    for (unsigned r=0;r<256;++r) {
        assert((DibEng::RasterOps[r](0xcccccccc,0xaaaaaaaa,0xf0f0f0f0)&255)==r);
        for (unsigned b=0;b<3;++b) {
            bool depends=false;
            for(unsigned i=0;i<8;++i) depends |= ((r>>i)^(r>>(i^(1<<b))))&1;
            assert(bool(DibEng::RasterInputs[r]&(1<<b))==depends);
        }
    }
    Memory memory;
    Registry::Store store;
    Registry::Attach(&memory);
    assert(Registry::Restore(store)==0);
    Registry::Note(1,"HKEY_LOCAL_MACHINE\\ReleaseTest",nullptr,0,nullptr,0);
    assert(Registry::Flush());
    assert(memory.bytes.size()>9);
    const unsigned char legacy[]={65,82,77,57,56,82,69,71};
    memcpy(memory.bytes.data(),legacy,8);
    Registry::Attach(&memory);
    assert(Registry::Restore(store)==0);
    memory.bytes[0]='X';
    Registry::Attach(&memory);
    assert(Registry::Restore(store)==1015);
    Registry::Attach(nullptr);
}
