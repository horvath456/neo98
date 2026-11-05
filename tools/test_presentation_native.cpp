// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/user.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static std::vector<unsigned char> previous;
static unsigned callbacks;
static unsigned pixel(int x,int y)
{
    const auto *p=User::Pixels()+(y*800+x)*4;
    return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16);
}
static void fill(User::HWND h,int width,int height,unsigned colour)
{
    const auto dc=User::GetDC(h);assert(dc);
    const Gdi::RECT area={0,0,width,height};
    const auto brush=Gdi::CreateSolidBrush(colour);assert(brush);
    assert(Gdi::FillRect(dc,&area,brush));Gdi::DeleteObject(brush);
    User::DrawingChanged(dc);
    assert(User::ReleaseDC(h,dc));
    // GetDC drawing leaves the update region intact. This fixture has now
    // painted the entire client and must validate it before children paint.
    assert(User::ValidateRect(h,nullptr));
}
static void draw(void*,unsigned,unsigned,Gdi::HDC dc,const Gdi::RECT&r,unsigned)
{
    ++callbacks;
    // Callbacks run on the owning thread, outside composition. They must not
    // modify the published image midway through a composition pass.
    assert(!User::Rendering());
    assert(!memcmp(previous.data(),User::Pixels(),previous.size()));
    auto brush=Gdi::CreateSolidBrush(Gdi::RGB(255,0,0));
    assert(Gdi::FillRect(dc,&r,brush));Gdi::DeleteObject(brush);
    User::DrawingChanged(dc);
    assert(!memcmp(previous.data(),User::Pixels(),previous.size()));
}
int main()
{
    assert(User::Initialize());assert(User::Render());
    previous.assign(User::Pixels(),User::Pixels()+800*600*4);
    auto h=User::CreateWindow("BUTTON","",0x9000000b,20,20,100,40,0,0,0);
    assert(h);User::CompleteCreation(h);
    User::SetControlCallbacks(nullptr,draw,nullptr);
    // Composition records paint demand without calling into the guest.
    assert(User::Render());assert(!callbacks);
    // The owning thread handles pending drawing, then the next composition
    // publishes it.
    assert(User::ServeControlPaints()==1);assert(callbacks==1);
    assert(User::Render());
    assert(memcmp(previous.data(),User::Pixels(),previous.size()));
    previous.assign(User::Pixels(),User::Pixels()+previous.size());
    // Moving a window preserves its content.
    User::MoveWindow(h,30,30,100,40);
    assert(!User::PendingControlPaint());
    assert(User::Render());
    assert(!User::PendingControlPaint());
    previous.assign(User::Pixels(),User::Pixels()+previous.size());
    // Invalidation requests a paint serviced only by the owning thread.
    User::InvalidateRect(h,nullptr,true);
    assert(User::PendingControlPaint());
    assert(User::Render());assert(callbacks==1);
    assert(User::ServeControlPaints()==1);assert(callbacks==2);
    assert(!User::PendingControlPaint());
    assert(User::Render());
    User::SetControlCallbacks(nullptr,nullptr,nullptr);
    // A transparent group box paints on the current parent client. The last
    // published frame still contains blue at this point, while the parent has
    // already painted its new red client into its backing surface.
    assert(User::RegisterClass("ParentSurface",1,0,0));
    auto parent=User::CreateWindow("ParentSurface","",0x90000000,200,100,180,100,0,0,0);
    assert(parent);User::CompleteCreation(parent);fill(parent,180,100,Gdi::RGB(0,0,255));
    assert(User::Render());assert(pixel(250,150)==0x0000ff);
    auto group=User::CreateWindow("BUTTON","Sound",0x50000007,10,10,140,70,parent,0,0);
    assert(group);User::CompleteCreation(group);
    assert(User::Render());
    fill(parent,180,100,Gdi::RGB(255,0,0));
    assert(User::ServeControlPaints()>=1);
    assert(User::Render());
    assert(pixel(250,150)==0xff0000);
    assert(User::DestroyWindow(parent));
    // The original property page can create and service its group boxes before
    // it paints its own background. The group box does not colour its inside,
    // so what the parent paints later shows there; nothing repaints the child
    // for it, the parent draws on the same screen (Win98, phase 2 of the
    // screen rebuild).
    parent=User::CreateWindow("ParentSurface","",0x90000000,400,100,180,100,0,0,0);
    assert(parent);User::CompleteCreation(parent);
    group=User::CreateWindow("BUTTON","Schemes",0x50000007,10,10,140,70,parent,0,0);
    assert(group);User::CompleteCreation(group);
    assert(User::Render());
    // The fixture deliberately defers this parent's background drawing.
    // Remove its queued paint so the child can paint first.
    assert(User::ValidateRect(parent,nullptr));
    assert(User::ServeControlPaints()>=1);assert(User::Render());
    assert(pixel(450,150)==0x008080);
    fill(parent,180,100,Gdi::RGB(255,0,0));
    assert(User::Render());
    assert(pixel(450,150)==0xff0000);
    assert(User::DestroyWindow(parent));
    // A child also retains its painted client across a same-size move and
    // subsequent composition, with no unrelated repaint demand.
    parent=User::CreateWindow("ParentSurface","",0x90000000,600,100,180,100,0,0,0);
    assert(parent);User::CompleteCreation(parent);fill(parent,180,100,Gdi::RGB(0,0,255));
    auto child=User::CreateWindow("BUTTON","Move",0x50000000,10,10,80,30,parent,0,0);
    assert(child);User::CompleteCreation(child);
    assert(User::Render());
    fill(parent,180,100,Gdi::RGB(0,0,255));
    assert(User::ServeControlPaints()>=1);assert(User::Render());
    assert(!User::PendingControlPaint());
    assert(User::MoveWindow(child,20,20,80,30));
    assert(!User::PendingControlPaint());
    assert(User::Render());assert(!User::PendingControlPaint());
    assert(User::DestroyWindow(parent));
    User::Paint paint{};
    User::InvalidateRect(h,nullptr,true);
    assert(User::BeginPaint(h,paint));assert(User::PaintInProgress());
    // An open paint must not stop Render for the whole desktop.
    User::Render();
    assert(!User::EndPaint(h,User::Paint{}));assert(User::PaintInProgress());
    assert(User::DestroyWindow(h));assert(!User::PaintInProgress());
    assert(!User::EndPaint(h,paint));
    // Explorer owns many invisible helper windows. Capacity must exceed the
    // old 192-slot ceiling and retired handles must remain invalid.
    std::vector<unsigned> windows;
    for(unsigned i=0;i<400;++i){
        auto window=User::CreateWindow("STATIC","",0,0,0,10,10,0,0,0);
        assert(window && window<=65535);windows.push_back(window);
    }
    for(auto window:windows)assert(User::DestroyWindow(window));
    for(auto window:windows)assert(!User::IsWindow(window));
    // A client may retain GetDC while resizing (SkiFree's status window).
    // BeginPaint must bind the larger canvas without retiring that DC.
    auto resized=User::CreateWindow("STATIC","",0x90000000,0,0,1,1,0,0,0);
    assert(resized);User::CompleteCreation(resized);
    auto retained=User::GetDC(resized);assert(retained);
    assert(User::MoveWindow(resized,0,0,120,40));
    User::InvalidateRect(resized,nullptr,true);
    User::Paint enlarged{};assert(User::BeginPaint(resized,enlarged));
    assert(User::EndPaint(resized,enlarged));
    assert(User::ReleaseDC(resized,retained));
    assert(User::DestroyWindow(resized));
    User::Shutdown();
    puts("PRESENTATION PASS: the composition makes no callback and only complete frames are visible");
}
