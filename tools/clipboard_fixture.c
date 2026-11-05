/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef int I;typedef void* P;
#define API __declspec(dllimport) __stdcall
U API OpenClipboard(U);U API CloseClipboard(void);U API EmptyClipboard(void);
U API GetClipboardData(U);U API SetClipboardData(U,U);U API CountClipboardFormats(void);
U API EnumClipboardFormats(U);I API IsClipboardFormatAvailable(U);I API GetPriorityClipboardFormat(const U*,I);
U API GetClipboardOwner(void);U API GetOpenClipboardWindow(void);U API GetClipboardViewer(void);
U API GetClipboardSequenceNumber(void);U API SetClipboardViewer(U);U API ChangeClipboardChain(U,U);
U API RegisterClipboardFormatA(const char*);I API GetClipboardFormatNameA(U,char*,I);I API GetClipboardFormatNameW(U,unsigned short*,I);
U API GlobalAlloc(U,U);P API GlobalLock(U);U API GlobalUnlock(U);U API GlobalFree(U);U API GlobalSize(U);
U API GetLastError(void);void API SetLastError(U);
U API RegisterClassA(const void*);U API CreateWindowExA(U,const char*,const char*,U,I,I,I,I,U,U,U,P);U API DestroyWindow(U);U API IsWindow(U);
I API DefWindowProcA(U,U,U,U);I API SendMessageA(U,U,U,U);U API GetWindowTextA(U,char*,I);
void API ExitThread(U);U API GetExitCodeThread(U,U*);
U API CreateThread(P,U,P,P,U,U*);U API WaitForSingleObject(U,U);U API CloseHandle(U);U API CreateEventA(P,I,I,const char*);U API SetEvent(U);
static U owner,viewer1,viewer2,next_viewer,fmt,rendered,destroyed,renderall,draws,chain,worker_result,victim,worker_mode,worker_ready,worker_release,nested_edit;

static U bytes(const char*s){U n=0;while(s[n])++n;return n;}
static I same(const char*a,const char*b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static U data(const char*s){U h=GlobalAlloc(0x42,bytes(s)+1);char*p=(char*)GlobalLock(h);if(!p)return 0;U i=0;do{p[i]=s[i];}while(s[i++]);GlobalUnlock(h);return h;}
static I __stdcall wnd(U h,U msg,U wp,U lp){
    if(msg==0x305){++rendered;if(victim){U doomed=victim;victim=0;DestroyWindow(doomed);}SetClipboardData(wp,data("rendered"));return 0;}
    if(msg==0x306){++renderall;if(OpenClipboard(h)){SetClipboardData(fmt,data("render all"));CloseClipboard();}return 0;}
    if(msg==0x307){++destroyed;if(victim){U doomed=victim;victim=0;DestroyWindow(doomed);}
        /* The callback accesses the clipboard recursively. Each call needs its own global block so nested data cannot overwrite the outer result. */
        if(nested_edit){U e=nested_edit;nested_edit=0;SendMessageA(e,0xb1,0,2);SendMessageA(e,0x301,0,0);}
        return 0;}

    if(msg==0x308){++draws;return 0;}
    if(msg==0x30d){++chain;if(next_viewer==wp)next_viewer=lp;return 0x4567;}
    return DefWindowProcA(h,msg,wp,lp);
}
static U __stdcall worker(P unused){(void)unused;if(worker_mode){worker_result=OpenClipboard(viewer1);SetEvent(worker_ready);WaitForSingleObject(worker_release,0xffffffff);CloseClipboard();return 0;}worker_result=GetClipboardData(fmt)==0 && !OpenClipboard(viewer1);return 0;}
static U __stdcall abandoned_clipboard(P mode){
    if(!OpenClipboard(0) || !EmptyClipboard())return 1;
    U h=data("abandoned thread clipboard");if(!h || SetClipboardData(fmt,h)!=h)return 2;
    if(mode)ExitThread(0x98);
    return 0x98;
}
#define CHECK(x,n) do{if(!(x))return n;}while(0)
int run_clipboard_test(void){
    struct {U style;P procedure;I cls,win;U instance,icon,cursor,background;const char*menu,*name;} wc={0,(P)wnd,0,0,0,0,0,0,0,"ClipboardFamily"};
    CHECK(RegisterClassA(&wc),7400);
    owner=CreateWindowExA(0,wc.name,"owner",0x80000000,0,0,200,100,0,0,0,0);
    viewer1=CreateWindowExA(0,wc.name,"viewer1",0x80000000,0,0,100,100,0,0,0,0);
    viewer2=CreateWindowExA(0,wc.name,"viewer2",0x80000000,0,0,100,100,0,0,0,0);
    CHECK(owner && viewer1 && viewer2,7401);
    fmt=RegisterClipboardFormatA("Neo98.Clipboard.Family");CHECK(fmt>=0xc000 && RegisterClipboardFormatA("neo98.clipboard.family")==fmt,7402);
    char text[96];CHECK(GetClipboardFormatNameA(fmt,text,sizeof text)==22 && same(text,"Neo98.Clipboard.Family"),7403);
    /* Standard formats do not have registered names. */
    text[0]='x';CHECK(!GetClipboardFormatNameA(1,text,sizeof text) && !text[0],7404);
    SetLastError(55);CHECK(!GetClipboardFormatNameW(fmt,(unsigned short*)text,48) && GetLastError()==120,7405);
    CHECK(!CloseClipboard() && !EmptyClipboard() && !GetClipboardData(1),7406);
    CHECK(OpenClipboard(owner) && OpenClipboard(owner) && GetOpenClipboardWindow()==owner && !OpenClipboard(viewer1),7407);
    U sequence=GetClipboardSequenceNumber();CHECK(EmptyClipboard() && GetClipboardOwner()==owner && !CountClipboardFormats(),7408);
    CHECK(GetClipboardSequenceNumber()==sequence+1,7409);
    U priority[]={9,fmt,1};CHECK(GetPriorityClipboardFormat(priority,3)==0,7410);
    U source=data("first");CHECK(source && SetClipboardData(fmt,source)==source && IsClipboardFormatAvailable(fmt)==(I)fmt,7411);
    char*original=(char*)GlobalLock(source);original[0]='X';GlobalUnlock(source);
    U copy=GetClipboardData(fmt);CHECK(copy && GlobalSize(copy)>=6 && same((char*)GlobalLock(copy),"first"),7412);GlobalUnlock(copy);
    CHECK(GetClipboardData(fmt)==copy && EnumClipboardFormats(0)==fmt && !EnumClipboardFormats(fmt),7413);
    CHECK(GetPriorityClipboardFormat(priority,3)==(I)fmt && GetPriorityClipboardFormat(priority,1)==-1 && GetPriorityClipboardFormat(priority,0)==-1,7414);
    U thread=CreateThread(0,0,(P)worker,0,0,0);CHECK(thread && WaitForSingleObject(thread,0xffffffff)==0 && worker_result && CloseHandle(thread),7415);
    CHECK(CloseClipboard() && !GetOpenClipboardWindow() && !CloseClipboard() && !GetClipboardData(fmt),7416);
    CHECK(IsClipboardFormatAvailable(fmt)==(I)fmt && !IsClipboardFormatAvailable(fmt|0x10000),7417);
    CHECK(OpenClipboard(viewer1) && EmptyClipboard() && destroyed==1 && GetClipboardOwner()==viewer1,7418);
    CHECK(!SetClipboardData(fmt,0) && CountClipboardFormats()==1 && IsClipboardFormatAvailable(fmt),7419);
    CHECK(CloseClipboard() && OpenClipboard(owner),7420);
    copy=GetClipboardData(fmt);CHECK(copy && same((char*)GlobalLock(copy),"rendered") && rendered==1,7421);GlobalUnlock(copy);
    CHECK(CloseClipboard(),7422);
    draws=0;CHECK(!SetClipboardViewer(viewer1) && draws==1 && GetClipboardViewer()==viewer1,7423);
    next_viewer=viewer1;CHECK(SetClipboardViewer(viewer2)==viewer1 && draws==2 && GetClipboardViewer()==viewer2,7424);
    CHECK(ChangeClipboardChain(viewer1,0)==0x4567 && chain==1 && !next_viewer,7425);
    CHECK(ChangeClipboardChain(viewer2,viewer1) && GetClipboardViewer()==viewer1,7426);
    CHECK(OpenClipboard(owner) && EmptyClipboard(),7427);
    source=data("ANSI \xe9");CHECK(SetClipboardData(1,source)==source,7428);
    sequence=GetClipboardSequenceNumber();CHECK(CloseClipboard() && IsClipboardFormatAvailable(7)==7 && IsClipboardFormatAvailable(16)==16,7429);
    CHECK(GetClipboardSequenceNumber()==sequence+2 && draws>=3,7430);
    CHECK(OpenClipboard(owner),7431);copy=GetClipboardData(7);CHECK(copy && ((unsigned char*)GlobalLock(copy))[5]==0x82,7432);GlobalUnlock(copy);
    copy=GetClipboardData(16);CHECK(copy && *(U*)GlobalLock(copy)==0x409,7433);GlobalUnlock(copy);CHECK(CloseClipboard(),7434);
    /* Native EDIT and public API must use the same stored object. */
    U edit=CreateWindowExA(0,"EDIT","",0x80000000,0,0,160,80,0,0,0,0);CHECK(edit,7435);
    SendMessageA(edit,0x302,0,0);CHECK(GetWindowTextA(edit,text,sizeof text)==6 && same(text,"ANSI \xe9"),7436);
    SendMessageA(edit,0xb1,0,4);CHECK(SendMessageA(edit,0x301,0,0)==4,7458);
    SendMessageA(edit,0xb1,0,0);sequence=GetClipboardSequenceNumber();CHECK(!SendMessageA(edit,0x301,0,0) && GetClipboardSequenceNumber()==sequence,7459);
    CHECK(OpenClipboard(owner),7437);copy=GetClipboardData(1);CHECK(copy && same((char*)GlobalLock(copy),"ANSI"),7438);GlobalUnlock(copy);CHECK(CloseClipboard(),7439);
    CHECK(OpenClipboard(owner) && EmptyClipboard() && !SetClipboardData(fmt,0) && CloseClipboard(),7440);
    renderall=0;CHECK(DestroyWindow(owner) && renderall==1 && !GetClipboardOwner(),7441);
    CHECK(OpenClipboard(0),7442);copy=GetClipboardData(fmt);CHECK(copy && same((char*)GlobalLock(copy),"render all"),7443);GlobalUnlock(copy);
    CHECK(EmptyClipboard() && CloseClipboard(),7444);

    U locked=CreateWindowExA(0,"EDIT","keep selection",0x80000000,0,0,160,80,0,0,0,0);CHECK(locked,7453);
    SendMessageA(locked,0xb1,0,4);worker_mode=1;worker_ready=CreateEventA(0,1,0,0);worker_release=CreateEventA(0,1,0,0);
    thread=CreateThread(0,0,(P)worker,0,0,0);CHECK(thread && worker_ready && worker_release && WaitForSingleObject(worker_ready,0xffffffff)==0 && worker_result,7454);
    SendMessageA(locked,0x300,0,0);CHECK(GetWindowTextA(locked,text,sizeof text)==14 && same(text,"keep selection"),7455);
    SendMessageA(locked,0x102,24,0);CHECK(GetWindowTextA(locked,text,sizeof text)==14 && same(text,"keep selection"),7456);
    CHECK(SetEvent(worker_release) && WaitForSingleObject(thread,0xffffffff)==0 && CloseHandle(thread) && CloseHandle(worker_ready) && CloseHandle(worker_release) && DestroyWindow(locked),7457);
    U doomed=CreateWindowExA(0,"EDIT","cut",0x80000000,0,0,160,80,0,0,0,0);CHECK(doomed,7447);
    CHECK(OpenClipboard(viewer1) && EmptyClipboard() && !SetClipboardData(1,0) && CloseClipboard(),7448);
    victim=doomed;SendMessageA(doomed,0x302,0,0);CHECK(!IsWindow(doomed) && !victim,7449);
    doomed=CreateWindowExA(0,"EDIT","cut",0x80000000,0,0,160,80,0,0,0,0);CHECK(doomed,7450);
    SendMessageA(doomed,0xb1,0,3);victim=doomed;SendMessageA(doomed,0x300,0,0);CHECK(!IsWindow(doomed) && !victim,7451);
    CHECK(OpenClipboard(0) && EmptyClipboard() && CloseClipboard(),7452);
    CHECK(ChangeClipboardChain(viewer1,0) && !GetClipboardViewer(),7445);
    for(U ending=0;ending<2;++ending){
        U exitcode=0,old_destroyed=destroyed,old_renderall=renderall;
        thread=CreateThread(0,0,(P)abandoned_clipboard,(P)ending,0,0);
        CHECK(thread && WaitForSingleObject(thread,0xffffffff)==0,7460+ending*5);
        CHECK(GetExitCodeThread(thread,&exitcode) && exitcode==0x98 && CloseHandle(thread),7461+ending*5);
        CHECK(!CountClipboardFormats() && !GetClipboardOwner() && !GetOpenClipboardWindow(),7462+ending*5);
        CHECK(OpenClipboard(viewer1) && CloseClipboard(),7463+ending*5);
        CHECK(destroyed==old_destroyed && renderall==old_renderall,7464+ending*5);
    }
    {
        U outer=CreateWindowExA(0,"EDIT","OUTERTEXT",0x80000000,0,0,160,80,0,0,0,0);CHECK(outer,7470);
        U nested=CreateWindowExA(0,"EDIT","xx",0x80000000,0,0,160,80,0,0,0,0);CHECK(nested,7471);
        CHECK(OpenClipboard(viewer1),7479);
        CHECK(EmptyClipboard(),7480);
        CHECK(SetClipboardData(1,data("seed")),7481);
        CHECK(CloseClipboard(),7482);

        SendMessageA(outer,0xb1,0,9);
        nested_edit=nested;
        CHECK(SendMessageA(outer,0x301,0,0)==9,7473);
        CHECK(!nested_edit,7474);
        CHECK(OpenClipboard(viewer1),7475);
        {const char*p=(const char*)GlobalLock(GetClipboardData(1));CHECK(p && same(p,"OUTERTEXT"),7476);}
        CHECK(CloseClipboard(),7477);
        CHECK(DestroyWindow(outer) && DestroyWindow(nested),7478);
    }
    CHECK(DestroyWindow(viewer1) && DestroyWindow(viewer2) && DestroyWindow(edit),7446);

    return 0;
}
