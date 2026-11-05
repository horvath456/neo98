// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_window_parent.h"
namespace X86 {
// A dedicated single-window provider. Existing deferred batches are unchanged.
unsigned Application::ParentPosition(unsigned hwnd,unsigned after,int x,int y,int width,int height,unsigned flags)
{
    if(!User::IsWindow(hwnd)||hwnd==User::GetDesktopWindow()){last_error=1400;return 0;}
    unsigned position[1][7]={{hwnd,after,(unsigned)x,(unsigned)y,(unsigned)width,(unsigned)height,flags&65535}};
    return PositionWindows(1,position);
}
struct Application::ParentEnvironment final:Win98WindowParent::Environment {
    Application&app;unsigned target,new_parent;
    ParentEnvironment(Application&a,unsigned h,unsigned parent):app(a),target(h),new_parent(parent){desktop=User::GetDesktopWindow();}
    Win98WindowParent::Node Read(unsigned h)const override {
        if(!User::IsWindow(h))return {0,0,0,0,0,0x10,0,0,0};
        const auto n=User::WindowNode(h);Gdi::RECT r{};User::GetWindowRect(h,&r);
        // Native GUI threads share one input queue, so no AttachThreadInput
        // transition is required here. Original differing-task branches remain
        // covered in the state oracle, not implemented as cross-queue support.
        return {n.parent,n.owner,1,n.style,n.extended,User::CbtDestroyFlags(h,0),h,r.left,r.top};
    }
    void Write(unsigned h,const Win98WindowParent::Node&w)override {
        User::ParentStorage(h,w.parent,w.style,w.extended);
    }
    bool Safe()const {
        if(!User::IsWindow(target)||!User::IsWindow(new_parent))return false;
        unsigned h=new_parent;
        for(unsigned n=0;h&&n<193;++n){if(h==target)return false;h=User::Parent(h);}
        return h==0;
    }
    unsigned Service(unsigned op,unsigned h,unsigned a,unsigned b,unsigned c)override {
        using namespace Win98WindowParent;
        if(app.state==Failed||app.state==Exited)return 0;
        if(op==Show)return User::IsWindow(h)?app.ShowWindow(h,a):0;
        if(op==Valid)return Safe();
        if(op==Win98WindowParent::Map){Gdi::POINT p{(int)a,(int)b};if(h&&h!=desktop)User::ScreenToClient(h,&p);return (unsigned(p.x)&65535)|(unsigned(p.y)<<16);}
        if(op==Unlink||op==Refresh||op==Front||op==Link)return User::ParentLinkService(op,h,a,b);
        if(op==Place)return User::IsWindow(h)?app.ParentPosition(h,0,(int)a,(int)b,0,0,c):0;
        // Accessibility is disabled; input attachment is unreachable with the
        // shared native input queue. Neither is a successful stub API export.
        return 0;
    }
};
unsigned Application::ParentApi(ApiName name,const unsigned*a)
{
    const unsigned h=a[0];
    if(!User::IsWindow(h)||h==User::GetDesktopWindow()){last_error=1400;return 0;}
    if(API_IS(name,"BringWindowToTop")){
        const auto p=Win98WindowParent::Raise(h);
        return ParentPosition(p.hwnd,p.after,p.x,p.y,p.width,p.height,p.flags);
    }
    // The native tree has no message-only container yet. Preserve generation
    // HWNDs, rather than mistaking a stale handle for an original WORD handle.
    if(a[1]==0xfffd||a[1]==0xfffffffdu){last_error=120;return 0;}
    const unsigned parent=a[1]?a[1]:User::GetDesktopWindow();
    if(!User::IsWindow(parent)){last_error=1400;return 0;}
    if(User::WindowProcess(h)!=process_id||(parent!=User::GetDesktopWindow()&&User::WindowProcess(parent)!=process_id)){last_error=50;return 0;}
    ParentEnvironment e(*this,h,parent);
    if(!e.Safe()){last_error=87;return 0;}
    return Win98WindowParent::SetParent(e,h,parent);
}
} // namespace X86
