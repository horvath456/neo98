// SPDX-License-Identifier: GPL-2.0-or-later
// Unchanged x86 guest: owner-draw, bitmap, checked and column-break menu items.
#define API __declspec(dllimport) __stdcall
unsigned API CreatePopupMenu(void);int API AppendMenuA(unsigned,unsigned,unsigned,const char*);
int API DestroyMenu(unsigned);unsigned API TrackPopupMenu(unsigned,unsigned,int,int,unsigned,unsigned,void*);
unsigned API EndMenu(void);unsigned API RegisterClassA(const void*);unsigned API GetModuleHandleA(const char*);
unsigned API CreateWindowExA(unsigned,const char*,const char*,unsigned,int,int,int,int,unsigned,unsigned,unsigned,void*);
unsigned API DefWindowProcA(unsigned,unsigned,unsigned,unsigned);int API DestroyWindow(unsigned);
void API ExitProcess(unsigned);unsigned API GetMenuItemRect(unsigned,unsigned,unsigned,void*);
int API MenuItemFromPoint(unsigned,unsigned,int,int);unsigned API WindowFromDC(unsigned);int API SetMenuItemBitmaps(unsigned,unsigned,unsigned,unsigned,unsigned);
unsigned API CheckMenuItem(unsigned,unsigned,unsigned);unsigned API GetMenuCheckMarkDimensions(void);
unsigned API SetMenuDefaultItem(unsigned,unsigned,unsigned);int API SetMenuItemInfoA(unsigned,unsigned,int,const void*);int API FillRect(unsigned,const void*,unsigned);
unsigned API CreateBitmap(int,int,unsigned,unsigned,const void*);int API DeleteObject(unsigned);
int API PatBlt(unsigned,int,int,int,int,unsigned);unsigned API SetBkColor(unsigned,unsigned);
unsigned API GetCurrentThreadId(void);void API Sleep(unsigned);
unsigned API CreateThread(void*,unsigned,void*,void*,unsigned,unsigned*);
unsigned API WaitForSingleObject(unsigned,unsigned);int API CloseHandle(unsigned);
int API PeekMessageA(void*,unsigned,unsigned,unsigned,unsigned);
int API TranslateMessage(const void*);unsigned API DispatchMessageA(const void*);

struct Class {unsigned style;void*procedure;unsigned cls_extra,wnd_extra,instance,icon,cursor,brush;const char*menu;const char*name;};
struct Measure {unsigned type,id,item,width,height,data;};
struct ItemInfo {unsigned size,mask,type,state,id,submenu,checked,unchecked,data;const char*text;unsigned cch,bitmap;};
struct Draw {unsigned type,id,item,action,state,window,dc;int left,top,right,bottom;unsigned data;};

__declspec(dllexport) volatile unsigned owner_window=0,owner_menu=0,owner_bitmap=0,check_bitmap=0;
__declspec(dllexport) volatile unsigned measure_calls=0,draw_calls=0,draw_left=0,draw_top=0,draw_right=0,draw_bottom=0;
__declspec(dllexport) volatile unsigned draw_state=0,draw_menu=0,draw_data=0,check_size=0,draw_window=0;
static unsigned owner_thread, destroy_during_draw;

static unsigned __stdcall proc(unsigned h,unsigned m,unsigned wp,unsigned lp)
{
 if(m==0x11f&&GetCurrentThreadId()!=owner_thread)ExitProcess(9447);
 if(m==0x2c){ // WM_MEASUREITEM
  if(GetCurrentThreadId()!=owner_thread)ExitProcess(9440);
  struct Measure*p=(struct Measure*)lp;
  if(wp||p->type!=1||p->id||p->item!=42||p->data!=0x1234)ExitProcess(9401);
  p->width=90;p->height=24;++measure_calls;return 1;
 }
 if(m==0x2b){ // WM_DRAWITEM
  if(GetCurrentThreadId()!=owner_thread)ExitProcess(9441);
  struct Draw*p=(struct Draw*)lp;
  if(wp||p->type!=1||p->item!=42||p->data!=0x1234)ExitProcess(9402);
  draw_left=p->left;draw_top=p->top;draw_right=p->right;draw_bottom=p->bottom;
  draw_state=p->state;draw_menu=p->window;draw_data=p->data;draw_window=WindowFromDC(p->dc);++draw_calls;
  if(destroy_during_draw){EndMenu();DestroyMenu(owner_menu);DestroyWindow(h);return 1;}
  if(draw_calls==1)Sleep(1); // A partial popup must not enter the composed frame.
  PatBlt(p->dc,p->left,p->top,p->right-p->left,p->bottom-p->top,0x42); // BLACKNESS
  return 1;
 }
 if(m==0x8001){ // host probe while the popup is tracked
  int rect[4];
  /* Win98 SE measures GetMenuItemRect against the window passed; only the
     popup window (#32768, the window of the WM_DRAWITEM DC) gives screen
     positions that hit-test (menu_item_rect contract). WM_DRAWITEM itself
     reports popup client coordinates. */
  if(!draw_calls||!draw_window)ExitProcess(9404);
  if(!GetMenuItemRect(draw_window,owner_menu,2,rect))ExitProcess(9403);
  if(rect[2]-rect[0]!=(int)(draw_right-draw_left)||rect[3]-rect[1]!=(int)(draw_bottom-draw_top))ExitProcess(9405);
  if(MenuItemFromPoint(h,owner_menu,(rect[0]+rect[2])/2,(rect[1]+rect[3])/2)!=2)ExitProcess(9406);
  if(rect[2]-rect[0]<90||rect[3]-rect[1]!=24)ExitProcess(9407);
  if(draw_menu!=owner_menu)ExitProcess(9408);
  if(!GetMenuItemRect(draw_window,owner_menu,5,rect))ExitProcess(9409);
  if(MenuItemFromPoint(h,owner_menu,(rect[0]+rect[2])/2,(rect[1]+rect[3])/2)!=5)ExitProcess(9410);
  return 0;
 }
 return DefWindowProcA(h,m,wp,lp);
}
#define CHECK(x,n) do{if(!(x))return n;}while(0)
int run_menu_ownerdraw_test(void)
{
 owner_thread=GetCurrentThreadId();
 struct Class c={0,proc,0,0,GetModuleHandleA(0),0,0,0,0,"OwnerDrawOwner"};
 CHECK(RegisterClassA(&c),9420);
 owner_window=CreateWindowExA(0,c.name,"Owner-draw menu",0x10cf0000,40,40,320,240,0,0,c.instance,0);
 CHECK(owner_window,9421);
 owner_bitmap=CreateBitmap(20,12,1,1,0);check_bitmap=CreateBitmap(13,13,1,1,0);
 CHECK(owner_bitmap&&check_bitmap,9422);
 owner_menu=CreatePopupMenu();CHECK(owner_menu,9423);
 CHECK(AppendMenuA(owner_menu,0,41,"&Normal\tCtrl+N"),9424);       // 0
 CHECK(AppendMenuA(owner_menu,0x800,0,0),9425);                     // 1 separator
 CHECK(AppendMenuA(owner_menu,0x100,42,(const char*)0x1234),9426);  // 2 owner-draw
 CHECK(AppendMenuA(owner_menu,4,43,(const char*)owner_bitmap),9427);// 3 bitmap item
 CHECK(AppendMenuA(owner_menu,0,44,"&Checked"),9428);               // 4
 CHECK(AppendMenuA(owner_menu,0x20,45,"C&olumn"),9429);             // 5 MF_MENUBARBREAK
 CHECK(AppendMenuA(owner_menu,0,46,"&Last"),9430);                  // 6
 {struct ItemInfo info={48,0x80,0,0,0,0,0,0,0,0,0,0};info.bitmap=owner_bitmap;
  CHECK(SetMenuItemInfoA(owner_menu,41,0,&info),9437);} // MIIM_BITMAP beside the text
 CHECK(SetMenuItemBitmaps(owner_menu,44,0,0,check_bitmap),9431);
 CheckMenuItem(owner_menu,44,0x408);
 SetMenuDefaultItem(owner_menu,6,1);
 check_size=GetMenuCheckMarkDimensions();
 CHECK((check_size&0xffff)&&(check_size>>16),9432);
 CHECK(TrackPopupMenu(owner_menu,0x100,120,120,0,owner_window,0)==46,9433);
 CHECK(measure_calls>=1&&draw_calls>=1,9434);
 CHECK(DestroyMenu(owner_menu),9435);
 CHECK(DeleteObject(owner_bitmap)&&DeleteObject(check_bitmap),9436);
 // Closing and destroying from WM_DRAWITEM invalidates every retained
 // view/menu pointer. The in-progress private surface must be discarded.
 owner_menu=CreatePopupMenu();CHECK(owner_menu,9442);
 CHECK(AppendMenuA(owner_menu,0x100,42,(const char*)0x1234),9443);
 destroy_during_draw=1;
 CHECK(TrackPopupMenu(owner_menu,0x100,120,120,0,owner_window,0)==0,9444);
 owner_window=0;
 return 0;
}
#ifdef MENU_OWNERDRAW_STANDALONE
static unsigned __stdcall menu_worker(void *unused){(void)unused;unsigned result=run_menu_ownerdraw_test();if(result)ExitProcess(result);return 0;}
void mainCRTStartup(void){
 unsigned id=0,thread=CreateThread(0,0,menu_worker,0,0,&id);
 if(!thread)ExitProcess(9445);
 while(WaitForSingleObject(thread,0)==258){
  unsigned message[7];
  while(PeekMessageA(message,0,0,0,1)){TranslateMessage(message);DispatchMessageA(message);}
  Sleep(1);
 }
 CloseHandle(thread);ExitProcess(0);
}
#endif
