// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "user_settings.h"
#include <string.h>
namespace X86 {
struct Application::CaretEnvironment{
    Application&app;unsigned task=0;
    unsigned CurrentTask(){return task?task:app.ThreadId();}
    unsigned OwnerTask(unsigned h){return User::WindowThread(h);}
    bool SameProcess(unsigned h){return User::IsWindow(h);}
    unsigned TaskFlags(){return app.activation_tasks[app.current_thread].flags14;}
    bool CanCreate(unsigned h){return h&&User::IsWindow(h)&&h!=User::GetDesktopWindow();} // One existing native input queue/process.
    bool Visible(unsigned h){for(unsigned i=0;h&&i<193;++i,h=User::Parent(h))if(!User::IsVisible(h))return false;return true;}
    bool PendingPaint(const Win98Caret::State&s){
        const int x=Win98Caret::Signed(s.x),y=Win98Caret::Signed(s.y);
        return User::CaretPending(s.owner,{x,y,x+Win98Caret::Signed(s.width),y+Win98Caret::Signed(s.height)});
    }
    // USER: the caret is inverted on the screen through a DC of its
    // window; drawing it a second time takes it away again.
    void Draw(const Win98Caret::State&s){app.XorCaret(s);}
    bool Accessibility(){return false;}void Notify(unsigned,unsigned){}
    unsigned BorderWidth(){return User::SystemSettings().Word(0x2e8);}
    unsigned BorderHeight(){return User::SystemSettings().Word(0x2ea);}
    void BitmapReference(unsigned h,bool add){Gdi::ReferenceCaretBitmap(h,add);}
    void BitmapSize(unsigned h,unsigned&w,unsigned&height){Gdi::CaretBitmap(h,w,height);}
    void KillTimer(unsigned owner){for(auto&t:app.timers)if(t.caret&&t.hwnd==owner)t=Timer{};}
    unsigned StartTimer(unsigned owner){
        for(unsigned i=0;i<64;++i){auto&t=app.timers[i];if(t.used||t.pending)continue;
            t=Timer{};t.used=true;t.caret=true;t.hwnd=owner;t.id=0xffffffff;
            for(unsigned j=0;j<Application::ThreadCapacity;++j)if(app.threads[j].id==OwnerTask(owner)){t.thread=j;break;}
            t.interval=app.caret_blink<10?10:app.caret_blink;
            t.deadline=app.Now()+(unsigned long long)t.interval*1000;return i+1;
        }
        return 0; // Original CreateCaret still succeeds if the system timer cannot be allocated.
    }
};
unsigned Application::CaretApi(ApiName name,const unsigned*a){
    CaretEnvironment e{*this};auto&s=caret;
    switch (name.id)
    {
    API_CASE("GetCaretBlinkTime")return caret_blink;
    API_CASE("SetCaretBlinkTime")return Win98Caret::Blink(e,s,caret_blink,a[0]);
    API_CASE("GetCaretPos"){
        if(!a[0]||!virtual_memory.Access(a[0],8,true)){last_error=87;return 0;}
        const Gdi::POINT p{Win98Caret::Signed(s.x),Win98Caret::Signed(s.y)};return Store(a[0],&p,sizeof p);
    }
    API_CASE("CreateCaret"){
        if(!e.CanCreate(a[0])){last_error=1400;return 0;}
        unsigned w=0,h=0;if(a[1]>1&&!Gdi::CaretBitmap(a[1],w,h)){last_error=6;return 0;}
        // Native GDI dimensions are bounded; retain full generation handles.
        if(a[1]<=1&&(Win98Caret::Signed(a[2])<0||Win98Caret::Signed(a[3])<0)){last_error=87;return 0;}
        caret_edit=false;caret_edit_shown=false;
        const unsigned result=Win98Caret::Create(e,s,a[0],a[1],a[2],a[3]);User::CaretChanged();return result;
    }
    API_CASE("SetCaretPos")return Win98Caret::Position(e,s,a[0],a[1]);
    default:
        break;
    }
    const bool destroy=API_IS(name,"DestroyCaret");
    if(!Win98Caret::Valid(e,s,destroy?0:a[0]))return 0;
    if(destroy){Win98Caret::Destroy(e,s);caret_edit=false;caret_edit_shown=false;}
    else if(API_IS(name,"ShowCaret"))Win98Caret::Show(e,s);
    else Win98Caret::Hide(e,s);
    User::CaretChanged();return 1;
}
void Application::CaretWindowDestroyed(unsigned hwnd){
    // Inversions kept for a window that is gone have no pixels any more.
    unsigned kept=0;for(unsigned i=0;i<caret_owed_count;++i)if(caret_owed[i].owner!=hwnd)caret_owed[kept++]=caret_owed[i];
    caret_owed_count=kept;
    if(caret.owner!=hwnd)return;CaretEnvironment e{*this};Win98Caret::Destroy(e,caret);caret_edit=false;caret_edit_shown=false;User::CaretChanged();
}
void Application::FreeCarets(){
    if(caret.owner)CaretWindowDestroyed(caret.owner);
    // Provider installation belongs to the shared desktop.
}
void Application::CaretPulse(){
    FlushCaretInversions();
    if(!caret.owner)return;
    bool alive=false;for(auto&t:threads)if(t.id==User::WindowThread(caret.owner)&&t.fiber&&!t.done){alive=true;break;}
    if(!alive||!User::IsWindow(caret.owner)){CaretWindowDestroyed(caret.owner);return;}
    // The caret blinks in the screen's pixels; while another thread has the
    // primary surface locked, the blink waits for the next pulse
    // (src/user_screen.inc).
    if(User::ScreenLockActive()&&User::ScreenLockedElsewhere(process_id,User::WindowThread(caret.owner)))return;
    CaretEnvironment e{*this};const auto now=Now();
    for(auto&t:timers)if(t.caret&&t.used&&now>=t.deadline){t.deadline=now+(unsigned long long)t.interval*1000;Win98Caret::Tick(e,caret,t.hwnd);}
}
void Application::EditCaret(unsigned hwnd,bool focused){
    CaretEnvironment e{*this,User::WindowThread(hwnd)};
    if(!focused){if(caret_edit&&caret.owner==hwnd)CaretWindowDestroyed(hwnd);return;}
    Gdi::POINT p{};unsigned height=0;bool selected=false;
    if(!User::EditCaretInfo(hwnd,p,height,selected))return;
    Win98Caret::Create(e,caret,hwnd,0,1,height);caret_edit=true;caret_edit_shown=false;
    Win98Caret::Position(e,caret,p.x,p.y);
    if(!selected){Win98Caret::Show(e,caret);caret_edit_shown=true;}
}
unsigned Application::DrawCaret(unsigned hwnd,unsigned dc){
    // Existing native EDIT and explicit APIs share this queue's one state.
    if(caret.owner!=hwnd)return User::IsEdit(hwnd)?1:0;
    CaretEnvironment e{*this,User::WindowThread(hwnd)};
    if(caret_edit){
        if(User::GetFocus()!=hwnd){CaretWindowDestroyed(hwnd);return 1;}
        Gdi::POINT p{};unsigned height=0;bool selected=false;
        if(User::EditCaretInfo(hwnd,p,height,selected)){
            if(caret.height!=height){EditCaret(hwnd,true);}
            else {Win98Caret::Position(e,caret,p.x,p.y);
                if(selected&&caret_edit_shown){Win98Caret::Hide(e,caret);caret_edit_shown=false;}
                else if(!selected&&!caret_edit_shown){Win98Caret::Show(e,caret);caret_edit_shown=true;}}
        }
    }
    (void)dc;
    return 1;
}
// The caret stands in the screen's pixels (USER inverts it through the
// window's DC). Before USER or the program paints the window it belongs to,
// it is taken away and put back afterwards, the way BeginPaint and EndPaint
// hide and show it; otherwise a paint over it would leave the next inversion
// the wrong way round.
bool Application::CaretPaintBracket(unsigned hwnd,bool hide){
    if(!caret.owner||caret.owner!=hwnd)return false;
    CaretEnvironment e{*this,User::WindowThread(hwnd)};
    if(hide)Win98Caret::Hide(e,caret);else Win98Caret::Show(e,caret);
    return true;
}
// While another thread has the primary surface locked, an inversion of the
// caret waits: it is kept and done after the Unlock, before the next one and
// at the next pulse (FlushCaretInversions). Inversions of the same rectangle
// cancel in pairs and commute, so the pixels end up as if every inversion had
// been done at its time; the state of Win98Caret stays what it is. Waiting at
// the call instead would stop SetFocus and the EDIT control, which draw the
// caret from the inside (src/user_screen.inc).
void Application::XorCaret(const Win98Caret::State&s){
    if(!s.owner||!User::IsWindow(s.owner))return;
    const CaretInversion item{s.owner,s.bitmap,Win98Caret::Signed(s.x),Win98Caret::Signed(s.y),
                              Win98Caret::Signed(s.width),Win98Caret::Signed(s.height)};
    if(User::ScreenLockActive()&&User::ScreenLockedElsewhere(User::CurrentProcess(),User::CurrentThread())){
        if(caret_owed_count&&caret_owed[caret_owed_count-1]==item){--caret_owed_count;return;}
        if(caret_owed_count<sizeof caret_owed/sizeof caret_owed[0]){caret_owed[caret_owed_count++]=item;return;}
    }
    FlushCaretInversions();
    InvertCaret(item);
}
void Application::InvertCaret(const CaretInversion&c){
    if(!c.owner||!User::IsWindow(c.owner))return;
    const unsigned dc=User::GetDC(c.owner);
    if(!dc)return;
    Gdi::DrawCaret(dc,c.bitmap,c.x,c.y,c.width,c.height);
    User::DrawingChanged(dc);
    User::ReleaseDC(c.owner,dc);
}
void Application::FlushCaretInversions(){
    if(!caret_owed_count)return;
    if(User::ScreenLockActive()&&User::ScreenLockedElsewhere(User::CurrentProcess(),User::CurrentThread()))return;
    const unsigned count=caret_owed_count;caret_owed_count=0;
    for(unsigned i=0;i<count;++i)InvertCaret(caret_owed[i]);
}
} // namespace X86
