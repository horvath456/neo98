// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside namespace X86 after HookEnvironment's definition.
bool Application::CbtPresent()
{
    HookEnvironment e(*this);
    return Win98Hooks::First(e,5)!=0;
}
bool Application::CbtEvent(unsigned code,unsigned hwnd,unsigned lp)
{
    return Win98CbtHooks::Veto(code,MessageHook(5,code,hwnd,lp));
}
bool Application::CbtActivate(unsigned hwnd,unsigned mouse,unsigned previous)
{
    if(!CbtPresent())return false;
    Win98CbtHooks::ActivateInfo info{mouse,previous};
    unsigned p=Allocate(sizeof info);
    if(!p){Fail("CBT activation buffer allocation failed");return true;}
    Store(p,&info,sizeof info);
    const bool veto=CbtEvent(5,hwnd,p);
    Free(p);
    return veto||state==Failed;
}
bool Application::CbtCreate(unsigned hwnd,unsigned create)
{
    if(!CbtPresent())return true;
    unsigned before[12];if(!Read(create,before,sizeof before))return false;
    Win98CbtHooks::CreateInfo info{create,0}; // USER initializes HWND_TOP.
    unsigned p=Allocate(sizeof info);
    if(!p){Fail("CBT creation buffer allocation failed");return false;}
    Store(p,&info,sizeof info);
    const bool veto=CbtEvent(3,hwnd,p);
    if(state!=Failed)Read(p,&info,sizeof info);
    Free(p);
    if(veto||state==Failed||!User::IsWindow(hwnd))return false;
    // USER retains the caller's CREATESTRUCT storage; replacing lpcs does not
    // replace that stack record. Coordinates and insertion order are consumed
    // after callback; the guest may also subclass the allocated HWND here.
    unsigned values[12];
    if(!Read(create,values,sizeof values))return false;
    if(memcmp(values+4,before+4,4*sizeof(unsigned))){
        Gdi::RECT r{};User::GetWindowRect(hwnd,&r);Gdi::POINT p{r.left,r.top};
        if(unsigned parent=User::Parent(hwnd))User::ScreenToClient(parent,&p);
        int x=values[7]==before[7]?p.x:(int)values[7];
        int y=values[6]==before[6]?p.y:(int)values[6];
        int width=values[5]==before[5]?r.right-r.left:(int)values[5];
        int height=values[4]==before[4]?r.bottom-r.top:(int)values[4];
        if(!User::MoveWindow(hwnd,x,y,width,height))return false;
    }
    // USER forces child-only windows to HWND_BOTTOM.
    User::OrderWindow(hwnd,(User::Style(hwnd)&0xc0000000)==0x40000000?1:info.after,false);
    return true;
}
struct Application::CbtDestroyEnvironment final:Win98CbtHooks::DestroyEnvironment {
    Application &app;bool silent;
    explicit CbtDestroyEnvironment(Application&a,bool quiet=false):app(a),silent(quiet){}
    bool Valid(unsigned h)override{return User::IsWindow(h);}
    unsigned Flags(unsigned h)override{return User::CbtDestroyFlags(h,0);}
    void Mark(unsigned h,unsigned flags)override{User::CbtDestroyFlags(h,flags);}
    unsigned First(unsigned h)override{return User::DialogNode(h).first;}
    unsigned Next(unsigned h)override{return User::DialogNode(h).next;}
    unsigned DetachFirst(unsigned h)override{return User::CbtDetachFirst(h);}
    void ClearFocus(unsigned h)override{
        // USER and 1951 call USER before and after the
        // WM_DESTROY subtree. An MDI client can return focus to the dying
        // child, so both checks matter; hiding it earlier is insufficient.
        if(!silent&&User::IsWindow(h)&&User::ThreadActivation(app.ThreadId()).focus==h)
            app.SetFocus((User::Style(h)&0xc0000000u)==0x40000000u?User::Parent(h):0);
    }
    void Send(unsigned h,unsigned m)override{
        if(silent)return;
        if(m==2)app.DestroyClipboardWindow(h);
        auto&t=app.threads[app.current_thread];
        bool kept=false;
        if(m==0x82&&t.host_frame_count){const unsigned at=t.host_frame_count-1;const auto&top=t.host_frames[at];
            if(app.PropertySheetWindow(h)&&top.kind==HostPropertyModalFinish&&top.a==h)kept=app.PushHostFrame(HostPropertyNonClient,h,0,0,1);
            else if(top.kind==HostPropertyRemove&&top.d==1&&top.b==h&&app.PropertyWorkRecorded(app.current_thread,at,top.kind,1))kept=app.PushHostFrame(HostPropertyPageNonClient,h,top.a,0,1);
        }
        struct Record{Application&app;bool kept;~Record(){if(kept)app.PopHostFrame();}}record{app,kept};
        app.GiveTail(kept?TailRecord:TailNone);
        if(User::IsWindow(h))app.CallWindow(h,m);
    }
    void Free(unsigned h)override{
        app.FinishPropertyNonClient(h);
    }
};
void Application::DestroyThreadWindows(unsigned thread)
{
    // Original Win98 removes both parent and child windows at thread return,
    // ExitThread and TerminateThread without WM_DESTROY/WM_NCDESTROY callbacks.
    // Reuse resource disposal and child unlinking, not the explicit-destroy
    // message path. The thread_windows native probe checks all three exits.
    CbtDestroyEnvironment environment(*this,true);
    for(unsigned count=0;count<User::WindowCapacity;++count)
    {
        const unsigned hwnd=User::ThreadWindow(process_id,thread);
        if(!hwnd)return;
        Win98CbtHooks::DestroyNonClient(environment,hwnd);
        if(User::IsWindow(hwnd)){Fail("Thread window cleanup retained a live window");return;}
    }
}
void Application::FinishPropertyNonClient(unsigned h){
        for(auto &timer:timers)if(timer.hwnd==h)timer=Timer{};
        for(auto &entry:edit_handles)if(entry.hwnd==h)entry={};
        auto *dialog=FindDialog(h);unsigned font=dialog&&!dialog->modal?dialog->font:0;
        // Resolve pending sends before destroying the window record.
        CancelSendsToWindow(h);
        User::DestroyWindow(h);
        if(dialog&&!dialog->modal)*dialog=Dialog{};
        if(font&&font!=system&&font!=fixed)Api("DeleteObject",&font);
}
unsigned Application::CbtDestroyWindow(unsigned hwnd,bool creation_failure)
{
    if(!User::IsWindow(hwnd)||hwnd==User::GetDesktopWindow()||User::WindowThread(hwnd)!=ThreadId())return 0;
    // Native lifetime guard: callbacks cannot start a second teardown of the
    // record whose WM_DESTROY/WM_NCDESTROY sequence is already in progress.
    if(User::CbtDestroyFlags(hwnd,0)&0x30)return 0;
    CbtDestroyEnvironment e(*this);
    if(!creation_failure){
        if(CbtEvent(4,hwnd,0)||state==Failed)return 0;
        if(!User::IsWindow(hwnd))return 1;
        // USER before the WM_DESTROY chain: WM_PARENTNOTIFY to
        // the parent of a child, the window hides (SetWindowPos 0x97 for a
        // top-level window, ShowWindow(SW_HIDE) for a child), a top-level
        // window takes its owned windows with it, and the active
        // window hands activation on before it goes.
        const unsigned style=User::Style(hwnd);
        const bool child=(style&0xc0000000)==0x40000000;
        if(child&&!(User::ExtendedStyle(hwnd)&4)){
            const unsigned parent=User::Parent(hwnd);
            if(parent&&parent!=User::GetDesktopWindow()){
                CallWindow(parent,0x210,2|((User::ControlID(hwnd)&65535)<<16),hwnd);
                if(!User::IsWindow(hwnd))return 1;
            }
        }
        if(User::Style(hwnd)&0x10000000){
            if(child)ShowWindow(hwnd,0);
            else{unsigned position[1][7]={{hwnd,0,0,0,0,0,0x97}};PositionWindows(1,position);}
            if(!User::IsWindow(hwnd))return 1;
        }
        if(!child){
            for(unsigned n=0;n<192;++n){
                const unsigned owned=User::OwnedWindow(hwnd);
                if(!owned)break;
                if(User::WindowThread(owned)!=ThreadId()){User::ParentStorage(owned,0,User::Style(owned),User::ExtendedStyle(owned));break;}
                CbtDestroyWindow(owned,false);
                if(!User::IsWindow(hwnd))return 1;
                if(User::IsWindow(owned)&&User::Owner(owned)==hwnd)break;
            }
        }
        // only the active window of a queue hands activation
        // on, and not while its task is inside deactivation.
        // A window procedure that destroys its window on WM_NCACTIVATE(FALSE)
        // (DESK.CPL's DeskSaysNoPeekingItsASurprise) would otherwise start
        // the same activation again from within it.
        // an owned pop-up gives the activation to its owner
        // ( code 2, which goes on from there when the owner cannot
        // take it), any other window with code 3. The same happens in the
        // foreground queue when it has no active window at all.
        // The calling queue loses flag 10h first.
        {
            const auto q=User::ThreadActivation(ThreadId());
            const bool active=q.active==hwnd&&!TaskDeactivating(hwnd);
            if(active||(!q.active&&ThreadId()==User::ForegroundQueue())){
                User::TakeQueuePass(ThreadId());
                const unsigned owner=User::Owner(hwnd);
                if(active&&(style&0x80000000)&&owner)ActivateNext(owner,2);
                else ActivateNext(hwnd,3);
                if(!User::IsWindow(hwnd))return 1;
                if (ThreadId()==User::ForegroundQueue() && User::ThreadActivation(ThreadId()).active==hwnd)
                    // Teardown has no focus to restore on host reactivation.
                    // Clear it with the WM_KILLFOCUS below, so the later
                    // USER checks do not send a second WM_KILLFOCUS.
                    LeaveForeground(false, false);
                if(!User::IsWindow(hwnd))return 1;
                ForgetActive(hwnd);
            }
        }
        // EVENT_OBJECT_DESTROY before the window is marked.
        if(User::WinEventListening()){WinEventNotify(0x8001,hwnd,0,0,false);if(!User::IsWindow(hwnd))return 1;}
        e.Mark(hwnd,0x20);Win98CbtHooks::DestroyMessages(e,hwnd);
    }
    if(User::IsWindow(hwnd))Win98CbtHooks::DestroyNonClient(e,hwnd);
    return 1;
}
