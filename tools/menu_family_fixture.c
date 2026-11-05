// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport) __stdcall
unsigned API CreateMenu(void);unsigned API CreatePopupMenu(void);int API DestroyMenu(unsigned);int API IsMenu(unsigned);
int API AppendMenuA(unsigned,unsigned,unsigned,const char*);int API InsertMenuA(unsigned,unsigned,unsigned,unsigned,const char*);int API ModifyMenuA(unsigned,unsigned,unsigned,unsigned,const char*);
int API RemoveMenu(unsigned,unsigned,unsigned);int API DeleteMenu(unsigned,unsigned,unsigned);unsigned API GetMenuItemCount(unsigned);unsigned API GetMenuItemID(unsigned,unsigned);unsigned API GetSubMenu(unsigned,unsigned);
unsigned API GetMenuState(unsigned,unsigned,unsigned);unsigned API CheckMenuItem(unsigned,unsigned,unsigned);unsigned API EnableMenuItem(unsigned,unsigned,unsigned);int API CheckMenuRadioItem(unsigned,unsigned,unsigned,unsigned,unsigned);
int API SetMenuDefaultItem(unsigned,unsigned,unsigned);unsigned API GetMenuDefaultItem(unsigned,unsigned,unsigned);unsigned API GetMenuStringA(unsigned,unsigned,char*,unsigned,unsigned);
int API GetMenuItemInfoA(unsigned,unsigned,unsigned,void*);int API SetMenuItemInfoA(unsigned,unsigned,unsigned,void*);int API InsertMenuItemA(unsigned,unsigned,unsigned,void*);
int API GetMenuInfo(unsigned,void*);int API SetMenuInfo(unsigned,void*);unsigned API GetMenuContextHelpId(unsigned);int API SetMenuContextHelpId(unsigned,unsigned);void API ExitProcess(unsigned);
unsigned API GetLastError(void);void API SetLastError(unsigned);
unsigned API AppendMenuW(unsigned,unsigned,unsigned,unsigned);unsigned API GetMenuItemInfoW(unsigned,unsigned,unsigned,unsigned);unsigned API GetMenuStringW(unsigned,unsigned,unsigned,unsigned,unsigned);unsigned API InsertMenuItemW(unsigned,unsigned,unsigned,unsigned);unsigned API InsertMenuW(unsigned,unsigned,unsigned,unsigned,unsigned);unsigned API ModifyMenuW(unsigned,unsigned,unsigned,unsigned,unsigned);unsigned API SetMenuItemInfoW(unsigned,unsigned,unsigned,unsigned);
struct Info {unsigned size,mask,type,state,id,submenu,checked,unchecked,data;char*text;unsigned length,bitmap;};
struct Details {unsigned size,mask,style,height,brush,help,data;};
#define CHECK(x,n) do{if(!(x))return n;}while(0)
static int equal(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
int run_menu_family_test(void){
 SetLastError(777);CHECK(!AppendMenuW(1,2,3,4)&&GetLastError()==120,9025);
 CHECK(!GetMenuItemInfoW(1,2,3,4)&&!GetMenuStringW(1,2,3,4,5)&&!InsertMenuItemW(1,2,3,4)&&!InsertMenuW(1,2,3,4,5)&&!ModifyMenuW(1,2,3,4,5)&&!SetMenuItemInfoW(1,2,3,4),9026);
 unsigned root=CreateMenu(),child=CreatePopupMenu();CHECK(root&&child&&root!=child,9000);
 CHECK(AppendMenuA(child,0,71,"child one")&&AppendMenuA(child,0,72,"child two"),9001);
 CHECK(AppendMenuA(root,0,10,"first")&&AppendMenuA(root,0x10,child,"popup")&&AppendMenuA(root,0,20,"last"),9002);
 CHECK(GetMenuItemCount(root)==3&&GetSubMenu(root,1)==child&&GetMenuItemID(root,1)==~0u,9003);
 char text[80];CHECK(GetMenuStringA(root,71,text,80,0)==9&&equal(text,"child one"),9004);
 CHECK(ModifyMenuA(root,71,0,73,"changed")&&GetMenuStringA(child,73,text,80,0)==7&&equal(text,"changed"),9005);
 struct Info i={48,0x143,0,0,30,0,0,0,0,"Inserted",0,0};
 CHECK(InsertMenuItemA(root,1,1,&i)&&GetMenuItemID(root,1)==30,9006);
 i.mask=0x63;i.data=0xdeadbeef;i.id=31;i.text="new";i.state=8;
 CHECK(SetMenuItemInfoA(root,30,0,&i),9007);
 i.mask=0x1ff;i.text=text;i.length=80;i.data=0;i.state=0;
 CHECK(GetMenuItemInfoA(root,31,0,&i)&&i.id==31&&i.data==0xdeadbeef&&i.state==8&&i.length==3&&equal(text,"new"),9008);
 i.size=48;i.mask=0x40;i.text=(char*)&i.size;i.length=4;
 CHECK(GetMenuItemInfoA(root,31,0,&i)&&i.size==0x77656e&&i.length==3,9027);i.size=48;
 CHECK(CheckMenuItem(root,31,0)==8&&CheckMenuItem(root,31,8)==0,9009);
 CHECK(EnableMenuItem(root,31,1)==0&&(GetMenuState(root,31,0)&3)==1,9010);
 CHECK(SetMenuDefaultItem(root,31,0)&&GetMenuDefaultItem(root,0,0)==~0u&&GetMenuDefaultItem(root,0,1)==31,9011);
 CHECK(EnableMenuItem(root,31,0)==1&&GetMenuDefaultItem(root,1,0)==1,9012);
 CHECK(CheckMenuRadioItem(root,0,3,3,0x400)&&GetMenuState(root,3,0x400)&8,9013);
 struct Details d={28,0x8000001c,0x08000000,0,0,0x1234,0x87654321};
 CHECK(SetMenuInfo(root,&d),9014);d.mask=0x1c;d.style=d.help=d.data=0;CHECK(GetMenuInfo(child,&d)&&d.style==0x08000000&&d.help==0x1234&&d.data==0x87654321,9015);
 CHECK(SetMenuContextHelpId(child,0x9988)&&GetMenuContextHelpId(child)==0x9988,9016);
 CHECK(RemoveMenu(root,2,0x400)&&IsMenu(child)&&GetMenuItemCount(root)==3,9017);
 CHECK(AppendMenuA(root,0x10,child,"reattached")&&DeleteMenu(root,3,0x400)&&!IsMenu(child),9018);
 unsigned replaced=CreatePopupMenu(),replacement=CreatePopupMenu();CHECK(AppendMenuA(root,0x10,replaced,"replace"),9019);
 i.mask=4;i.submenu=replacement;CHECK(SetMenuItemInfoA(root,3,1,&i)&&!IsMenu(replaced)&&IsMenu(replacement),9020);
 i.submenu=root;CHECK(!SetMenuItemInfoA(root,3,1,&i)&&IsMenu(root),9021);
 CHECK(DestroyMenu(root)&&!IsMenu(root)&&!IsMenu(replacement)&&!DestroyMenu(root),9022);
 for(unsigned n=0;n<100;++n){unsigned h=CreateMenu();CHECK(h&&h!=root,9023);CHECK(AppendMenuA(h,0,n,"lifetime")&&DestroyMenu(h)&&!IsMenu(h),9024);}
 return 0;
}
#ifdef MENU_FAMILY_STANDALONE
void mainCRTStartup(void){ExitProcess(run_menu_family_test());}
#endif
