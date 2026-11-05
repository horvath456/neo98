// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport) __stdcall
unsigned API CreatePopupMenu(void);int API AppendMenuA(unsigned,unsigned,unsigned,const char*);int API DestroyMenu(unsigned);unsigned API TrackPopupMenuEx(unsigned,unsigned,int,int,unsigned,void*);unsigned API TrackPopupMenu(unsigned,unsigned,int,int,unsigned,unsigned,void*);unsigned API EndMenu(void);
unsigned API RegisterClassA(const void*);unsigned API GetModuleHandleA(const char*);unsigned API CreateWindowExA(unsigned,const char*,const char*,unsigned,int,int,int,int,unsigned,unsigned,unsigned,void*);unsigned API DefWindowProcA(unsigned,unsigned,unsigned,unsigned);int API DestroyWindow(unsigned);int API SetWindowTextA(unsigned,const char*);void API ExitProcess(unsigned);
unsigned API GetMenuItemRect(unsigned,unsigned,unsigned,void*);int API MenuItemFromPoint(unsigned,unsigned,int,int);
unsigned API GetMenuBarInfo(unsigned,int,int,void*);unsigned API FindWindowA(const char*,const char*);
struct Class {unsigned style;void*procedure;unsigned cls_extra,wnd_extra,instance,icon,cursor,brush;const char*menu;const char*name;};
__declspec(dllexport) volatile unsigned popup_phase=0;
__declspec(dllexport) volatile unsigned popup_owner=0,popup_menu=0,popup_child=0;
static unsigned commands=0,entered=0,exited=0;
static unsigned mutated=0;
static unsigned __stdcall proc(unsigned h,unsigned m,unsigned wp,unsigned lp){if(m==0x8001){int rect[4];unsigned bar[8]={32};unsigned popup=FindWindowA("#32768",0);/* Win98 SE measures GetMenuItemRect against the window passed; the popup window gives screen positions (menu_item_rect contract). */if(!popup||!GetMenuItemRect(popup,popup_menu,2,rect)||MenuItemFromPoint(h,popup_menu,(rect[0]+rect[2])/2,(rect[1]+rect[3])/2)!=2)ExitProcess(9312);if(!popup||!GetMenuBarInfo(popup,-4,3,bar)||bar[1]!=(unsigned)rect[0]||bar[2]!=(unsigned)rect[1]||bar[3]!=(unsigned)rect[2]||bar[4]!=(unsigned)rect[3]||bar[5]!=popup_menu)ExitProcess(9313);}
 if(m==0x211)++entered;if(m==0x212)++exited;if(m==0x111){if(wp!=42)ExitProcess(9300);++commands;}
 if(m==0x117&&wp==popup_menu&&popup_phase==3&&!mutated){mutated=1;if(!AppendMenuA(popup_menu,0,60,"&Added"))ExitProcess(9314);}
 if(m==0x117&&popup_phase==5)EndMenu();if(m==0x117&&popup_phase==6)DestroyWindow(h);return DefWindowProcA(h,m,wp,lp);}
#define CHECK(x,n) do{if(!(x))return n;}while(0)
int run_menu_tracking_test(void){struct Class c={0,proc,0,0,GetModuleHandleA(0),0,0,0,0,"PopupOwner"};CHECK(RegisterClassA(&c),9301);popup_owner=CreateWindowExA(0,c.name,"Popup test",0x10cf0000,40,40,300,220,0,0,c.instance,0);CHECK(popup_owner,9302);
 popup_menu=CreatePopupMenu();popup_child=CreatePopupMenu();CHECK(popup_menu&&popup_child,9303);
 CHECK(AppendMenuA(popup_menu,1,41,"&Disabled")&&AppendMenuA(popup_menu,0x800,0,0)&&AppendMenuA(popup_menu,0,42,"&Open")&&AppendMenuA(popup_child,0,51,"Child")&&AppendMenuA(popup_menu,0x10,popup_child,"&Submenu"),9304);
 popup_phase=1;CHECK(TrackPopupMenuEx(popup_menu,0x100,790,590,popup_owner,0)==42&&!commands,9305);
 popup_phase=2;CHECK(TrackPopupMenu(popup_menu,0,200,200,0,popup_owner,0)==1&&commands==1,9306);
 popup_phase=3;CHECK(TrackPopupMenuEx(popup_menu,0x100,300,200,popup_owner,0)==60&&mutated,9307);
 popup_phase=4;CHECK(!TrackPopupMenuEx(popup_menu,0x100,300,200,popup_owner,0),9308);
 popup_phase=5;CHECK(!TrackPopupMenuEx(popup_menu,0x100,300,200,popup_owner,0),9309);
 popup_phase=6;CHECK(!TrackPopupMenuEx(popup_menu,0x100,300,200,popup_owner,0),9315);
 CHECK(entered>=6&&exited>=6,9310);CHECK(DestroyMenu(popup_menu),9311);popup_phase=7;return 0;}
#ifdef MENU_TRACKING_STANDALONE
void mainCRTStartup(void){ExitProcess(run_menu_tracking_test());}
#endif
