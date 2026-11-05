// SPDX-License-Identifier: GPL-2.0-or-later
// cross the former limits through the native USER/GDI APIs.
#ifndef USER_LIMITS_SOURCE
#define USER_LIMITS_SOURCE "../src/user.cpp"
#endif
#include USER_LIMITS_SOURCE
#include <cstdio>
#include <cstdlib>

namespace GdiTrueType {
bool AbcWidths(const Font &, unsigned, unsigned, int *) { std::abort(); }
}
static bool fail_allocation;
void *operator new(std::size_t size,const std::nothrow_t &) noexcept
{
    if(fail_allocation)return nullptr;
    try{return ::operator new(size);}catch(...){return nullptr;}
}
void *operator new[](std::size_t size,const std::nothrow_t &) noexcept
{
    if(fail_allocation)return nullptr;
    try{return ::operator new[](size);}catch(...){return nullptr;}
}
static unsigned checks;
#define CHECK(c) do { ++checks; if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);std::exit(1);} } while(0)

static void processes()
{
    CHECK(User::Initialize());
    unsigned contexts[64]{};
    for(unsigned i=0;i<64;++i) {
        CHECK(User::AttachProcess(i+1,&contexts[i]));
        User::BindTask(i+1,i+100);
        CHECK(User::CurrentProcess()==i+1);
        unsigned cls[12]={48,0,1000+i,0,0,1};
        CHECK(User::RegisterNativeClass(cls,"ProcessPrivateClass"));
    }
    for(unsigned i=0;i<64;++i) {
        CHECK(User::ProcessContextAt(i)==&contexts[i]);
        CHECK(User::ProcessContextOf(i+1)==&contexts[i]);
        User::BindTask(i+1,i+100);
        CHECK(User::CurrentContext(nullptr)==&contexts[i]);
        unsigned cls[12]{};
        CHECK(User::GetNativeClassInfo(1,"ProcessPrivateClass",cls));
        CHECK(cls[2]==1000+i);
    }
    CHECK(!User::AttachProcess(1,&contexts[0]));
    CHECK(!User::AttachProcess(0,&contexts[0]));
    for(unsigned i=0;i<64;i+=2){User::ReleaseProcessWindows(i+1);User::DetachProcess(i+1);}
    for(unsigned i=1;i<64;i+=2)CHECK(User::ProcessContextOf(i+1)==&contexts[i]);
    for(unsigned i=0;i<64;i+=2)CHECK(User::AttachProcess(i+1,&contexts[i]));
    // Exhaust the actual USER heap, then check a failed attach leaves the
    // existing list intact and a freed owner record can be reused.
    unsigned pid=100;
    while(pid<10000&&User::AttachProcess(pid,nullptr))++pid;
    CHECK(pid>116&&pid<10000);
    CHECK(!User::ProcessContextOf(pid));
    CHECK(User::ProcessContextOf(64)==&contexts[63]);
    User::DetachProcess(100);
    CHECK(User::AttachProcess(pid,nullptr));
    for(unsigned i=101;i<=pid;++i)User::DetachProcess(i);
    for(unsigned i=1;i<=64;++i){User::ReleaseProcessWindows(i);User::DetachProcess(i);}
    CHECK(!User::ProcessContextAt(0));
    Gdi::ShutdownBitmaps();
}

static void savedc()
{
    const auto dc=Gdi::CreateCompatibleDC(0);CHECK(dc);
    for(unsigned i=0;i<128;++i){
        Gdi::SetTextColor(dc,0x1000+i);
        CHECK(Gdi::SaveDC(dc)==int(i+1));
    }
    Gdi::SetTextColor(dc,0xabcdef);
    fail_allocation=true;
    const int failed=Gdi::SaveDC(dc);
    fail_allocation=false;
    CHECK(failed==0&&Gdi::GetTextColor(dc)==0xabcdef);
    CHECK(!Gdi::RestoreDC(dc,129));
    CHECK(!Gdi::RestoreDC(dc,-129));
    CHECK(Gdi::RestoreDC(dc,-5));CHECK(Gdi::GetTextColor(dc)==0x107b);
    CHECK(Gdi::RestoreDC(dc,9));CHECK(Gdi::GetTextColor(dc)==0x1008);
    CHECK(Gdi::RestoreDC(dc,-8));CHECK(Gdi::GetTextColor(dc)==0x1000);
    CHECK(!Gdi::RestoreDC(dc,-1));
    const auto brush=Gdi::CreateSolidBrush(0x123456);CHECK(brush);
    const auto old=Gdi::SelectObject(dc,brush);CHECK(old);
    for(unsigned i=0;i<64;++i)CHECK(Gdi::SaveDC(dc)==int(i+1));
    CHECK(Gdi::SelectObject(dc,old)==brush);
    CHECK(!Gdi::DeleteObject(brush));
    CHECK(Gdi::DeleteDC(dc));
    CHECK(Gdi::DeleteObject(brush));
    Gdi::ShutdownBitmaps();
}

static unsigned set_pixel(unsigned dc,int x,int y,unsigned color)
{
    struct IO:Gdi::BitmapIO {
        bool Read(unsigned,void*,unsigned)override{std::abort();}
        bool Write(unsigned,const void*,unsigned)override{std::abort();}
        bool Probe(unsigned,bool)override{std::abort();}
        void Error(unsigned)override{std::abort();}
        void Fault(const char*)override{std::abort();}
    } io;
    const unsigned args[]={dc,unsigned(x),unsigned(y),color};
    return Gdi::PixelApi("SetPixel",args,io);
}

static unsigned islands(unsigned count)
{
    unsigned region=Gdi::CreateRegionRect(0,0,0,0);CHECK(region);
    for(unsigned i=0;i<count;++i){
        const int x=3*(i%32),y=3*(i/32);
        const auto part=Gdi::CreateRegionRect(x,y,x+2,y+2);CHECK(part);
        CHECK(Gdi::CombineRegions(region,region,part,2));CHECK(Gdi::DeleteObject(part));
    }
    CHECK(Gdi::RegionRectangles(region,nullptr,0,0,0)==count);
    return region;
}
static bool contains(const RectRegion &r,int x,int y)
{
    for(unsigned i=0;i<r.count;++i)if(Win98::PtInRect(&r.rectangles[i],x,y))return true;
    return false;
}
static unsigned window(const char *name,unsigned style=0)
{
    unsigned cls[12]={48,style,1,0,0,1};CHECK(User::RegisterNativeClass(cls,name));
    const auto h=User::CreateWindow(name,"limits",0x90000000u,20,20,300,200,0,0,0);
    CHECK(h);User::CompleteCreation(h);User::Render();User::ValidateRect(h,nullptr);return h;
}
static void redraw()
{
    CHECK(User::Initialize());
    const auto h=window("RedrawLimits");const auto region=islands(128);
    CHECK(User::RedrawWindow(h,nullptr,region,1|4|0x40));
    const auto *w=User::Find(h);CHECK(w->update.count==128);
    for(unsigned i=0;i<128;++i){
        const int x=3*(i%32),y=3*(i/32);
        CHECK(contains(w->update,x,y));CHECK(!contains(w->update,x+2,y));
    }
    CHECK(User::RedrawWindow(h,nullptr,region,8|0x20|0x40));CHECK(!w->update.count);
    CHECK(User::RedrawWindow(h,nullptr,region,1|0x40));
    fail_allocation=true;
    const Gdi::RECT cut{0,1,96,2};
    const bool subtracted=User::Find(h)->update.Subtract(cut);
    fail_allocation=false;
    CHECK(!subtracted);CHECK(w->update.count==128);CHECK(contains(w->update,0,1));
    User::ValidateRect(h,nullptr);
    fail_allocation=true;
    const bool invalidated=User::RedrawWindow(h,nullptr,region,1|0x40);
    fail_allocation=false;
    CHECK(invalidated);CHECK(contains(w->update,299,199));
    CHECK(Gdi::DeleteObject(region));User::Shutdown();Gdi::ShutdownBitmaps();
}

static void leases()
{
    CHECK(User::Initialize());CHECK(User::SetScreenMode(800,600,32));const auto h=window("LeaseLimits");
    unsigned dc[32]{};
    for(unsigned i=0;i<32;++i){
        User::BindTask(0,i+100);
        dc[i]=User::GetDC(h);CHECK(dc[i]);CHECK(User::WindowFromDC(dc[i])==h);
        for(unsigned k=0;k<i;++k)CHECK(dc[k]!=dc[i]);
        Gdi::SetTextColor(dc[i],0x1000+i);
    }
    User::BindTask(0,999);
    fail_allocation=true;const auto failed=User::GetDC(h);fail_allocation=false;
    CHECK(!failed);CHECK(User::Find(h)->leases==32);
    User::InvalidateRect(h,nullptr,true);User::Paint paint{};
    CHECK(User::BeginPaint(h,paint));
    for(auto d:dc)CHECK(d!=paint.dc);
    CHECK(User::EndPaint(h,paint));
    for(unsigned i=0;i<32;++i){
        const unsigned at=(i*13)%32;
        CHECK(Gdi::GetTextColor(dc[at])==0x1000+at);
        CHECK(User::ReleaseDC(h,dc[at]));CHECK(!User::WindowFromDC(dc[at]));
    }
    // Released cache entries can serve another window without growing GDI.
    const auto other=window("LeaseOther");const unsigned objects=Gdi::LiveObjects();
    const auto reused=User::GetDC(other);CHECK(reused);CHECK(Gdi::LiveObjects()==objects);
    CHECK(User::WindowFromDC(reused)==other);CHECK(Gdi::GetTextColor(reused)==0);
    CHECK(User::DestroyWindow(h));CHECK(Gdi::GetObjectType(reused)==3);
    CHECK(User::ReleaseDC(other,reused));
    const unsigned styles[]={0x20u,0x40u};
    for(unsigned style: styles){
        const auto owned=window(style==0x20?"OwnLeaseLimits":"ClassLeaseLimits",style);
        const auto first=User::GetDC(owned);CHECK(first);Gdi::SetTextColor(first,0x654321);
        for(unsigned i=0;i<48;++i)CHECK(User::GetDC(owned)==first);
        for(unsigned i=0;i<49;++i)CHECK(User::ReleaseDC(owned,first));
        CHECK(Gdi::GetTextColor(first)==0x654321);
        CHECK(User::GetDC(owned)==first);CHECK(User::DestroyWindow(owned));
        CHECK(!Gdi::GetObjectType(first));
    }
    // more than 16 distinct classes can retain a class DC.
    unsigned class_windows[24]{},class_dc[24]{};
    for(unsigned i=0;i<24;++i){
        char name[32];std::snprintf(name,sizeof name,"ClassDC%u",i);
        class_windows[i]=window(name,0x40);class_dc[i]=User::GetDC(class_windows[i]);CHECK(class_dc[i]);
        for(unsigned k=0;k<i;++k)CHECK(class_dc[k]!=class_dc[i]);
    }
    for(unsigned i=0;i<24;++i){CHECK(User::ReleaseDC(class_windows[i],class_dc[i]));CHECK(User::DestroyWindow(class_windows[i]));}
    // the transferred GetDCEx region also had a 256-rectangle cap.
    const auto region=islands(320);CHECK(Gdi::OffsetRegionObject(region,20,20));
    const auto clipped=User::GetDC(other);CHECK(clipped);
    CHECK(User::SetDCRegion(other,clipped,region,true));
    CHECK(set_pixel(clipped,0,0,0x123456)!=~0u);
    CHECK(set_pixel(clipped,2,0,0x123456)==~0u);
    CHECK(set_pixel(clipped,93,27,0x123456)!=~0u);
    CHECK(User::ReleaseDC(other,clipped));CHECK(!Gdi::GetObjectType(region));
    User::Shutdown();Gdi::ShutdownBitmaps();
}

static void window_region()
{
    CHECK(User::Initialize());CHECK(User::SetScreenMode(800,600,32));
    const auto h=window("WindowRegionLimits");const auto region=islands(640);
    CHECK(User::SetWindowRegion(h,region,false));
    for(unsigned move=0;move<2;++move){
        if(move)CHECK(User::MoveWindow(h,120,100,300,200));
        const auto dc=User::GetDC(h);CHECK(dc);
        for(unsigned i=0;i<640;++i){
            const int x=3*(i%32),y=3*(i/32);
            CHECK(set_pixel(dc,x,y,0x123456)!=~0u);
            CHECK(set_pixel(dc,x+2,y,0x123456)==~0u);
            CHECK(User::RegionHit(h,x+(move?120:20),y+(move?100:20)));
            CHECK(!User::RegionHit(h,x+(move?120:20)+2,y+(move?100:20)));
        }
        CHECK(User::ReleaseDC(h,dc));
    }
    fail_allocation=true;const auto failed=User::GetDC(h);fail_allocation=false;
    CHECK(!failed);CHECK(User::Find(h)->leases==0);
    const auto retry=User::GetDC(h);CHECK(retry);CHECK(User::ReleaseDC(h,retry));
    CHECK(User::DestroyWindow(h));CHECK(!Gdi::GetObjectType(region));
    User::Shutdown();Gdi::ShutdownBitmaps();
}

static void gdi_objects()
{
    fail_allocation=true;CHECK(!Gdi::CreateSolidBrush(1));fail_allocation=false;
    const auto dc=Gdi::CreateCompatibleDC();CHECK(dc);Gdi::SetTextColor(dc,0x123456);
    CHECK(Gdi::SaveDC(dc)==1);
    unsigned handles[32767]{};bool words[65536]{};unsigned count=0;
    for(;count<32767;++count){
        Gdi::NarrowHandles(count%2==0);
        const auto h=Gdi::CreateSolidBrush(count);
        if(count%2==0)Gdi::NarrowHandles(false);
        if(!h)break;
        handles[count]=h;CHECK(!words[h&65535]);words[h&65535]=true;
        CHECK(Gdi::FullHandle(h&65535)==h);
        if(count%2==0)CHECK(h<=65535);
    }
    CHECK(count>30000);CHECK(!Gdi::CreateSolidBrush(1));
    for(unsigned i=0;i<count;++i){unsigned info[3]{};CHECK(Gdi::SolidBrushInfo(handles[i]&65535,info));CHECK(info[1]==i);}
    // Growing the table preserves the DC and its saved references.
    CHECK(Gdi::GetTextColor(dc)==0x123456);CHECK(Gdi::RestoreDC(dc,1));
    const auto hole=handles[count/2];CHECK(Gdi::DeleteObject(hole));CHECK(!Gdi::GetObjectType(hole));
    Gdi::NarrowHandles(true);handles[count/2]=Gdi::CreateSolidBrush(0xabcdef);Gdi::NarrowHandles(false);
    CHECK(handles[count/2]&&handles[count/2]<=65535);CHECK(!Gdi::CreateSolidBrush(1));
    for(unsigned i=0;i<count;++i)CHECK(Gdi::DeleteObject(handles[i]));
    CHECK(Gdi::DeleteDC(dc));Gdi::ShutdownBitmaps();CHECK(!Gdi::LiveObjects());
    // Reach and skip the generation range reserved for WMF/EMF handles.
    for(unsigned i=0;i<70000;++i){const auto h=Gdi::CreateSolidBrush(i);CHECK(h&&(h&0xf0000000u)!=0xe0000000u);CHECK(Gdi::DeleteObject(h));}
}

static void dce_recovery()
{
    CHECK(User::Initialize());const auto h=window("RecoveryA"),other=window("RecoveryB");
    unsigned dc[2][5]{};
    for(unsigned task=0;task<2;++task){
        User::BindTask(0,task+1);
        for(unsigned i=0;i<5;++i){dc[task][i]=User::GetDC(h);CHECK(dc[task][i]);Gdi::SetTextColor(dc[task][i],0x1000+task*16+i);}
    }
    CHECK(User::Find(h)->leases==10);
    User::BindTask(0,1);
    // The fifth in the MRU list is this task's oldest held DCE.
    const auto reclaimed=User::GetDC(other);CHECK(reclaimed==dc[0][0]);
    CHECK(User::Find(h)->leases==9&&User::Find(other)->leases==1);
    CHECK(User::WindowFromDC(reclaimed)==other);CHECK(Gdi::GetTextColor(reclaimed)==0);
    for(unsigned i=0;i<5;++i)CHECK(Gdi::GetTextColor(dc[1][i])==0x1010+i);
    // A free entry wins even while the caller holds five DCs.
    CHECK(User::ReleaseDC(h,dc[1][4]));
    const auto free=User::GetDC(h);CHECK(free==dc[1][4]);CHECK(User::WindowFromDC(reclaimed)==other);
    CHECK(User::ReleaseDC(h,free));CHECK(User::ReleaseDC(other,reclaimed));
    for(unsigned i=1;i<5;++i)CHECK(User::ReleaseDC(h,dc[0][i]));
    for(unsigned i=0;i<4;++i)CHECK(User::ReleaseDC(h,dc[1][i]));
    CHECK(!User::Find(h)->leases&&!User::Find(other)->leases);
    User::Shutdown();CHECK(User::Initialize());
    // Reclaim a paint lease, including its transferred region and SaveDCs.
    const auto painted=window("RecoveryPaint");User::BindTask(0,7);
    User::InvalidateRect(painted,nullptr,true);User::Paint paint{};CHECK(User::BeginPaint(painted,paint));
    CHECK(Gdi::SaveDC(paint.dc)==1);
    for(unsigned i=0;i<4;++i)CHECK(User::GetDC(painted));
    CHECK(User::GetDC(painted)==paint.dc);CHECK(User::Find(painted)->leases==5);
    CHECK(!User::Find(painted)->lease[4].paint&&!User::Find(painted)->lease[4].paint_clip);
    CHECK(Gdi::SaveDC(paint.dc)==1);CHECK(!User::EndPaint(painted,paint));
    for(unsigned i=0;i<24;++i){CHECK(User::GetDC(painted));CHECK(User::Find(painted)->leases==5);}
    User::Shutdown();Gdi::ShutdownBitmaps();User::BindTask(0,0);
}

static void classes()
{
    CHECK(User::Initialize());
    constexpr unsigned count=640;
    unsigned brushes[count]{};
    for(unsigned i=0;i<count;++i){
        brushes[i]=Gdi::CreateSolidBrush(0x1000+i);CHECK(brushes[i]);
        User::ProtectStockBrush(brushes[i],true);
        char name[40];std::snprintf(name,sizeof name,"MoreClasses%u",i);
        unsigned fields[12]={48,0,0x400000+i,0,0,0x800000+i,0,0,brushes[i]};
        CHECK(User::RegisterNativeClass(fields,name));
    }
    for(unsigned i=0;i<count;++i){
        char name[40];std::snprintf(name,sizeof name,"MoreClasses%u",i);
        unsigned fields[12]{};CHECK(User::GetNativeClassInfo(0x800000+i,name,fields));
        CHECK(fields[2]==0x400000+i&&fields[8]==brushes[i]);
        CHECK(User::UnregisterNativeClass(0x800000+i,name));
        // No stock brush may lose protection after the former 384th slot.
        CHECK(Gdi::GetObjectType(brushes[i])==2);
        User::ProtectStockBrush(brushes[i],false);CHECK(Gdi::DeleteObject(brushes[i]));
    }
    // Failed table growth leaves every published token and class intact.
    Win98ClassStore::Store store;
    fail_allocation=true;
    CHECK(!store.Module(0x400000));CHECK(store.error==8);
    CHECK(!store.Handle(0x123456));CHECK(!store.BodyAllocate(26));
    fail_allocation=false;
    unsigned modules[130]{},handles[130]{},bodies[130]{};
    for(unsigned i=0;i<130;++i){
        modules[i]=store.Module(0x400000+i);handles[i]=store.Handle(0x500000+i);bodies[i]=store.BodyAllocate(26);
        CHECK(modules[i]&&handles[i]&&bodies[i]);store.Write32(bodies[i]+4,0x600000+i);
    }
    for(unsigned i=0;i<130;++i){CHECK(store.ModuleValue(modules[i])==0x400000+i);CHECK(store.HandleValue(handles[i])==0x500000+i);CHECK(store.Read32(bodies[i]+4)==0x600000+i);}
    store.BodyFree(bodies[100]);CHECK(store.BodyAllocate(26)==bodies[100]);CHECK(store.Read32(bodies[100]+4)==0);
    store.ReleaseModules(store.CurrentOwner());CHECK(store.Module(0x700000)==modules[0]);
    User::Shutdown();Gdi::ShutdownBitmaps();
}

int main(int argc,char **argv)
{
    const unsigned test=argc>1?unsigned(std::atoi(argv[1])):6103;
    if(test==6103)processes();
    else if(test==6104)savedc();
    else if(test==6105)redraw();
    else if(test==6106)leases();
    else if(test==6107)window_region();
    else if(test==6113)classes();
    else if(test==6114)dce_recovery();
    else if(test==6115)gdi_objects();
    else return 2;
    std::printf("B-%u PASS: %u checks, ASan/UBSan\n",test,checks);
}
