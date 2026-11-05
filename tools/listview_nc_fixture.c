/* SPDX-License-Identifier: GPL-2.0-or-later */
/* exercise the native class through public USER entry points. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct { U style, proc; int class_extra, window_extra; U instance, icon, cursor, background; const char *menu, *name; } Class;
API U RegisterClassA(const Class *);
typedef struct { int left, top, right, bottom; } Rect;
typedef struct { U hwnd, after; int x, y, cx, cy; U flags; } Position;
typedef struct { Rect rect[3]; Position *position; } Calc;
typedef struct { U mask; int index, subitem; U state, state_mask; char *text; int capacity, image; U parameter; } Item;
typedef struct { U mask; int format, width; char *text; int capacity, subitem; } Column;
typedef struct { U size, mask; int min, max; U page; int pos, track; } Scroll;
API U GetModuleHandleA(const char *);
API U LoadLibraryA(const char *);
API U GetProcAddress(U,const char *);
API void ExitProcess(U);
API U CreateFileA(const char *,U,U,void *,U,U,U);
API U CloseHandle(U);
API U WriteFile(U, const void *, U, U *, void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DestroyWindow(U);
API U GetMessageA(void *,U,U,U);
API U TranslateMessage(const void *);
API U DispatchMessageA(const void *);
API U GetWindowLongA(U, int);
API U SetWindowLongA(U, int, U);
API U DefWindowProcA(U, U, U, U);
API U SendMessageA(U, U, U, U);
API U GetClientRect(U, Rect *);
API U GetWindowRect(U, Rect *);
API U SetWindowPos(U, U, int, int, int, int, U);
API U SetScrollInfo(U, int, Scroll *, U);
API U ShowScrollBar(U, int, U);
static U failures, checks, output;
static void text(const char *s) { U n=0,w; while(s[n])++n; WriteFile(output,s,n,&w,0); }
static void check(int pass, const char *name) { ++checks; if(!pass)++failures; text(pass?"PASS ":"FAIL ");text(name);text("\n"); }
static int equal(Rect a,Rect b) { return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom; }
static void geometry(U h,const char *name)
{
    Rect r, want, got; GetWindowRect(h,&r); want=r;
    DefWindowProcA(h,0x83,0,(U)&want);
    got=r; check(SendMessageA(h,0x83,0,(U)&got)==0&&equal(got,want),name);
    Position position={h,0,11,17,240,160,0};
    Calc calc={{{31,47,271,207},{1,2,3,4},{5,6,7,8}},&position};
    Rect expected=calc.rect[0],old1=calc.rect[1],old2=calc.rect[2];
    /* The FALSE form calculates the expected RECT without requiring a params block. */
    DefWindowProcA(h,0x83,0,(U)&expected);
    check(SendMessageA(h,0x83,1,(U)&calc)==0&&equal(calc.rect[0],expected),"NCCALCSIZE TRUE first RECT");
    check(equal(calc.rect[1],old1)&&equal(calc.rect[2],old2)&&calc.position==&position&&position.cx==240,"NCCALCSIZE preserves remaining parameters");
    GetClientRect(h,&got);
    check(got.right==want.right-want.left&&got.bottom==want.bottom-want.top,"committed client matches nonclient calculation");
}
static void list(U parent,U border,U edge)
{
    U h=CreateWindowExA(edge,"SysListView32","",0x50004001|border,11,17,240,160,parent,0,0,0);
    check(h!=0,"ListView created"); if(!h)return;
#ifndef ORIGINAL
    check(GetWindowLongA(h,-4)==0x71000002,"native ListView procedure");
#else
    check(GetWindowLongA(h,-4)==0xbfead662,"original COMCTL32 ListView procedure");
#endif
    geometry(h,"ListView initial RECT");
    SetWindowPos(h,0,23,29,260,180,0x14); geometry(h,"ListView resized RECT");
    Item item={1,0,0,0,0,"row",0,0,0};
    for(int i=0;i<40;++i){item.index=i;SendMessageA(h,0x1007,0,(U)&item);}
    check((GetWindowLongA(h,-16)&0x200000)!=0,"rows enable vertical scrollbar");
    geometry(h,"ListView vertical scrollbar RECT");
    SendMessageA(h,0x115,3,0);check(SendMessageA(h,0x1027,0,0)>0,"page down advances top row");
    SendMessageA(h,0x115,6,0);check(SendMessageA(h,0x1027,0,0)==0,"scroll top restores row zero");
    Scroll scroll={sizeof scroll,7,0,999,100,0,0};
    SetScrollInfo(h,0,&scroll,1); geometry(h,"ListView both scrollbars RECT");
    ShowScrollBar(h,0,0); geometry(h,"ListView horizontal scrollbar removed");
    SendMessageA(h,0x1009,0,0); geometry(h,"ListView rows removed");
    SetWindowLongA(h,-16,GetWindowLongA(h,-16)^0x800000);
    SetWindowLongA(h,-20,GetWindowLongA(h,-20)^0x200);
    SetWindowPos(h,0,0,0,0,0,0x37);geometry(h,"ListView changed border styles");
    DestroyWindow(h);
}
void mainCRTStartup(void)
{
    output=CreateFileA("C:\\NCRESULT.TXT",0x40000000,0,0,2,0,0);
    if(output==(U)-1)ExitProcess(3);
#ifdef ORIGINAL
    U module=LoadLibraryA("COMCTL32.DLL");
    void (__stdcall *init)(void)=(void *)GetProcAddress(module,"InitCommonControls");
    if(!init)ExitProcess(2);init();
#endif
#if defined(AUDIT) && !defined(ORIGINAL)
    /* The same class records as CommonInitApi; avoid loading original COMCTL32. */
    static Class classes[] = {
        {0x400b,0x71000106,0,4,0,0,0,16,0,"SysTabControl32"},
        {0x4003,0x71000109,0,4,0,0,0,16,0,"msctls_updown32"},
        {0x4808,0x71000102,0,4,0,0,0,0,0,"tooltips_class32"},
        {0x4000,0x71000108,0,4,0,0,0,16,0,"msctls_trackbar32"}
    };
    for(U i=0;i<4;++i){classes[i].instance=GetModuleHandleA(0);check(RegisterClassA(&classes[i])!=0,"native class registered");}
#endif
    U parent=CreateWindowExA(0,"STATIC","NC contract",0x92000000,0,0,400,300,0,0,GetModuleHandleA(0),0);
    check(parent!=0,"parent created");
#ifdef INTERACTIVE
    U h=CreateWindowExA(0x200,"SysListView32","",0x50804001,11,17,240,160,parent,0,0,0);
    Column column={2,0,180,0,0,0};
    check(SendMessageA(h,0x101b,0,(U)&column)==0,"interactive column inserted");
    Item item={1,0,0,0,0,"scroll row",0,0,0};
    for(int i=0;i<40;++i){item.index=i;SendMessageA(h,0x1007,0,(U)&item);}
    U msg[7];while(GetMessageA(msg,0,0,0)){TranslateMessage(msg);DispatchMessageA(msg);}ExitProcess(0);
#endif

    for(U i=0;i<4;++i)list(parent,(i&1)?0x800000:0,(i&2)?0x200:0);
#ifdef AUDIT
    const char *names[]={"SysTreeView32","LISTBOX","SysTabControl32","msctls_statusbar32","msctls_updown32","tooltips_class32","msctls_trackbar32","SCROLLBAR"};
    for(U i=0;i<8;++i){
        text("CONTROL ");text(names[i]);text("\n");
        U h=CreateWindowExA(0x200,names[i],"",0x50800000,11,17,240,160,parent,0,0,0);
        check(h!=0,"audit control created");if(!h)continue;
        geometry(h,names[i]);SetWindowPos(h,0,23,29,260,180,0x34);geometry(h,"audit resized RECT");DestroyWindow(h);
    }
#endif
    DestroyWindow(parent);text("NC fixture complete\n");CloseHandle(output);ExitProcess(failures?1:0);
}
