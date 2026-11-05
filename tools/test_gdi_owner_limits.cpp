// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
static bool fail_owner_array=false;
static unsigned failed_arrays=0;
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept {
    if (fail_owner_array) { ++failed_arrays; return nullptr; }
    try { return ::operator new[](n); } catch (...) { return nullptr; }
}
static std::vector<unsigned char> load(const char *path) {
    FILE *f=fopen(path,"rb"); assert(f); fseek(f,0,SEEK_END); auto n=ftell(f); rewind(f);
    std::vector<unsigned char> b(n); assert(fread(b.data(),1,n,f)==size_t(n)); fclose(f); return b;
}
static void pump(X86::Application &app, unsigned phase, bool exiting=false) {
    for (unsigned i=0;i<10000 && app.Status()!=X86::Application::Failed && app.Status()!=X86::Application::Exited;++i) {
        if (!exiting && app.Read32(phase)) return;
        app.Pump(64);
    }
    printf("GDI owner guest status=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error()); fflush(stdout);
    assert(exiting && app.Status()==X86::Application::Exited && !app.ExitCode());
}
int main(int argc,char **argv) {
    assert(argc==6); auto exe=load(argv[1]),font=load(argv[2]);
    unsigned phase=strtoul(argv[3],nullptr,10),release=strtoul(argv[4],nullptr,10),handles=strtoul(argv[5],nullptr,10);
    auto start=[&]() {
        auto app=std::make_unique<X86::Application>(nullptr);
        assert(app->Start(exe.data(),exe.size(),font.data(),font.size(),"GDOWNER.EXE"));
        pump(*app,phase); return app;
    };
    auto parent=start(); auto child=start();
    std::vector<unsigned> child_handles, parent_handles;
    for (unsigned i=0;i<2304;++i) {
        child_handles.push_back(child->Read32(handles+4*i));
        parent_handles.push_back(parent->Read32(handles+4*i));
    }
    // Fill the cleanup index, then fail only its next array allocation.
    for (;;) {
        bool full=true;
        for (const auto &object:child->gdi_objects) full &= object.handle!=0;
        if (full) break;
        auto h=Gdi::CreateSolidBrush(0xabcdef); assert(h && child->OwnGdi(h,1)==h);
        child_handles.push_back(h);
    }
    auto *entries=child->gdi_objects.entries;
    auto capacity=child->gdi_objects.capacity;
    fail_owner_array=true;
    assert(child->OwnGdi(child_handles[0],1)==child_handles[0]);
    assert(!child->OwnGdi(parent_handles[0],1));
    fail_owner_array=false;
    assert(child->last_error==8 && Gdi::GetObjectType(parent_handles[0])==2);
    assert(Gdi::GetObjectOwner(parent_handles[0])==(parent->ProcessId()&65535));
    for (unsigned kind=1;kind<=2;++kind) {
        const unsigned before=Gdi::LiveObjects();
        auto h=kind==1 ? Gdi::CreateSolidBrush(0xabcdef) : Gdi::CreateCompatibleDC(0); assert(h);
        fail_owner_array=true; assert(!child->OwnGdi(h,kind)); fail_owner_array=false;
        assert(child->last_error==8 && !Gdi::GetObjectType(h));
        assert(Gdi::LiveObjects()==before);
        assert(child->gdi_objects.entries==entries && child->gdi_objects.capacity==capacity);
    }
    assert(failed_arrays==3);
    for (auto h:child_handles) assert(Gdi::GetObjectType(h));
    // Recovery must grow successfully, and owner reassignment must survive
    // the original creator's teardown for both an object and a DC.
    auto extra=Gdi::CreateSolidBrush(0x123456); assert(extra && child->OwnGdi(extra,1)==extra);
    child_handles.push_back(extra); assert(child->gdi_objects.capacity>capacity);
    for (unsigned i:{0u,2u}) {
        auto h=child_handles[i];
        assert(Gdi::SetObjectOwner(h,parent->ProcessId())==(child->ProcessId()&65535));
        assert(parent->OwnGdi(h,i==2?2:1)==h);
        parent_handles.push_back(h);
    }
    child->Write32(release,1); pump(*child,phase,true); child.reset();
    for (unsigned i=0;i<child_handles.size();++i)
        assert(bool(Gdi::GetObjectType(child_handles[i]))==(i==0 || i==2));
    for (auto h:parent_handles) assert(Gdi::GetObjectType(h));
    parent->Write32(release,1); pump(*parent,phase,true); parent.reset();
    for (auto h:parent_handles) assert(!Gdi::GetObjectType(h));
    puts("GDI owner limits PASS: two public x86 guests, 2304 objects each, holes reused, process cleanup, surviving/transferred owners, three allocation failures and recovery; ASan/UBSan");
}
