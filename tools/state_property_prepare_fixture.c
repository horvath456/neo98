/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U PropertySheetA(const void*);API U PropertySheetW(const void*);
API U CreatePropertySheetPageW(const void*);
API U DestroyPropertySheetPage(U);
API U MessageBoxA(U,const char*,const char*,U);API U GetCommandLineA(void);
API U GetParent(U);API U SetWindowLongA(U,int,U);API U GetModuleHandleA(const char*);
API U SetTimer(U,U,U,U);API U KillTimer(U,U);API U SendMessageA(U,U,U,U);
#pragma pack(push,2)
typedef struct {U style,extended;W controls,x,y,width,height,menu,klass,title;} Template;
#pragma pack(pop)
static const Template page_template={0x40000400,0,0,0,0,180,70,0,0,0};
static const W wide_page[]={'P','a','g','e',0},wide_caption[]={'P','r','o','p','e','r','t','y','P','r','e','p','a','r','e',0};
static U mode,entered,finished,addref,created,initialized,active,released,sheetinit,sheetpre,applied,reset,timer_done,veto,queries,first_page,helped,nonclient,removed_handle;
static void check(int yes,U code){if(!yes)ExitProcess(code);}
static void box(U h,U which){if(mode!=which&&!(mode=='D'&&which=='A')&&!(mode=='G'&&which=='P')&&!(mode=='K'&&which=='I')&&!(mode=='M'&&which=='A'&&addref==2)&&!(mode=='L'&&which=='R'&&released==1)&&!(mode=='E'&&which=='C'&&created==2)&&!(mode=='T'&&which=='N'&&active==2)&&!(mode=='B'&&which=='U')&&!(mode=='F'&&which=='U')&&!(mode=='h'&&which=='H')&&!(which=='R'&&((released==1&&(mode=='r'||mode=='a'||mode=='b'||mode=='x'||mode=='X'||mode=='j'))||(released==2&&mode=='s'))))return;
 ++entered;MessageBoxA(h,"Saved callback must finish once","PropertyPending",0);++finished;if(mode=='G'||mode=='K'||mode=='x'||mode=='n')ExitProcess(0);}
static U __stdcall page_callback(U hwnd,U reason,U*p){
 check(!hwnd&&p[0]==48,26500);
 if(reason==0){++addref;box(0,'A');}else if(reason==1){++released;box(0,'R');}else if(reason==2){++created;box(0,'C');if(created==2&&(mode=='c'||mode=='e')){box(0,mode);if(mode=='c')return 0;}}
 return 1;
}
static U __stdcall page_proc(U h,U msg,U wp,U lp){(void)wp;
 if(msg==0x110){check(lp!=0,26501);++initialized;if(initialized==1)first_page=h;box(h,'I');if(initialized==2&&mode=='d')box(h,mode);if(initialized==1&&(mode=='T'||mode=='Q'||mode=='J'||mode=='X'||mode=='h'||mode=='c'||mode=='d'||mode=='e'||mode=='w'||mode=='j'||mode=='n'||mode=='v'||mode=='t'||mode=='u'))SetTimer(h,7,700,0);return 1;}
 if(msg==0x82){++nonclient;if((h!=first_page&&(mode=='w'||mode=='n'||mode=='u'))||(h==first_page&&mode=='t'))box(0,mode);}
 if(msg==2&&h!=first_page&&mode=='v')box(0,mode);
 if(msg==0x113&&wp==7){KillTimer(h,7);
  if(mode=='X'||mode=='c'||mode=='d'||mode=='e'){U bad[12]={48,mode=='X'?0x88:0x489,GetModuleHandleA(0),mode=='X'?0xffee:(U)&page_template,0,(U)wide_page,(U)page_proc,0,(U)page_callback,0,0,0};U handle=CreatePropertySheetPageW(bad);check(handle,26531);U success=mode=='d'||mode=='e';check(SendMessageA(GetParent(h),0x467,0,handle)==success,26532);if(success){check(SendMessageA(GetParent(h),0x466,1,0)==0&&!DestroyPropertySheetPage(handle),26534);}else check(DestroyPropertySheetPage(handle)!=0,26535);}
  else if(mode=='w'||mode=='j'||mode=='n'||mode=='v'||mode=='t'||mode=='u'){check(SendMessageA(GetParent(h),0x466,mode=='t'?0:mode=='u'?99:1,mode=='u'?removed_handle:0)==0,26536);if(mode=='u')check(!DestroyPropertySheetPage(removed_handle),26537);}
  else if(mode=='h')check(SendMessageA(GetParent(h),0x111,9,0)==1,26533);
  else check(SendMessageA(GetParent(h),mode=='T'?0x465:0x46c,mode=='T'?0:0x1357,mode=='T'?0:0x2468)==(mode=='T'?1:0x5432),26520);++timer_done;return 1;}
 if(msg==0x46c&&(mode=='Q'||mode=='J')){check(wp==0x1357&&lp==0x2468,26524);++queries;if((mode=='Q'&&h==first_page)||(mode=='J'&&h!=first_page)){box(h,mode);SetWindowLongA(h,0,0x5432);}else SetWindowLongA(h,0,0);return 1;}
 if(msg==0x4e){U*n=(U*)lp;
  if(n[2]==(U)-200){++active;box(h,'N');if(active==1&&(mode=='U'||mode=='Y'||mode=='F'))SendMessageA(GetParent(h),0x468,h,0);}
  if(n[2]==(U)-201)box(h,'Y');
  if(n[2]==(U)-202){++applied;box(h,'U');if(mode=='F'&&!veto){++veto;SetWindowLongA(h,0,2);return 1;}}
  if(n[2]==(U)-203){++reset;box(h,'Z');}
  if(n[2]==(U)-209)box(h,'V');
  if(n[2]==(U)-211)box(h,'O');
  if(n[2]==(U)-205){++helped;box(h,'H');}
  SetWindowLongA(h,0,0);return 1;}return 0;
}
static U __stdcall sheet_callback(U hwnd,U reason,U parameter){
 if(reason==2){check(!hwnd&&parameter,26502);++sheetpre;box(0,'P');}
 if(reason==1){check(hwnd&&!parameter,26503);++sheetinit;box(hwnd,'S');}return 0;
}
void mainCRTStartup(void){
 const char*c=(const char*)GetCommandLineA();while(*c){if(*c==' '&&c[1])mode=c[1];++c;}
 U page[24]={48,0x89,GetModuleHandleA(0),(U)&page_template,0,(U)"Page",(U)page_proc,0,(U)page_callback,0,0,0};
 U query=mode=='Q'||mode=='J',invalid=mode=='r'||mode=='s'||mode=='a'||mode=='b'||mode=='x';
 U remove=mode=='w'||mode=='j'||mode=='n'||mode=='v'||mode=='t'||mode=='u',added=mode=='X'||mode=='c'||mode=='d'||mode=='e';
 U two=mode=='M'||mode=='L'||mode=='E'||query||invalid||remove;if(two)for(U i=0;i<12;++i)page[12+i]=page[i];
 if(mode=='E'||query||remove)page[13]|=0x400;
 if(mode=='H'||mode=='h')page[1]|=0x20;
 U header[13]={52,0x108,0,page[2],0,(U)"PropertyPrepare",1,0,(U)page,(U)sheet_callback,0,0,0};
 if(two)header[6]=2;
 if(mode=='H'||mode=='h')header[1]|=0x200;
 U answer;
 if(invalid){page[5]=(U)wide_page;page[17]=(U)wide_page;U handles[2]={CreatePropertySheetPageW(page),CreatePropertySheetPageW(page+12)};check(handles[0]&&handles[1],26525);
  U converted=mode=='a'||mode=='x',ansi=converted||mode=='b';header[0]=converted?52:44;header[1]=0x100|(converted?0x80000000:0);header[5]=ansi?(U)"PropertyPrepare":(U)wide_caption;header[8]=(U)handles;
  answer=ansi?PropertySheetA(header):PropertySheetW(header);check(answer==(U)-1,26526);check(addref==2&&released==2&&!created&&!initialized&&!active&&!sheetpre&&!sheetinit,26527);
  check(!DestroyPropertySheetPage(handles[0])&&!DestroyPropertySheetPage(handles[1]),26528);check(entered==1&&finished==1,26506);ExitProcess(0);
 }
 if(mode=='u'){page[5]=(U)wide_page;page[17]=(U)wide_page;U handles[2]={CreatePropertySheetPageW(page),CreatePropertySheetPageW(page+12)};check(handles[0]&&handles[1],26538);removed_handle=handles[1];header[1]=0x100;header[5]=(U)wide_caption;header[8]=(U)handles;answer=PropertySheetW(header);}
 else if(mode=='D'){page[5]=(U)wide_page;U handle=CreatePropertySheetPageW(page);check(handle!=0,26507);
  header[1]=0x100;header[5]=(U)wide_caption;header[8]=(U)&handle;answer=PropertySheetW(header);
 }else answer=PropertySheetA(header);check(answer==(mode=='U'||mode=='Y'||mode=='B'||mode=='O'),26504);
 check(addref==1+two+added&&created==1+(mode=='E'||query||remove||mode=='c'||mode=='d'||mode=='e')&&initialized==1+(mode=='E'||query||remove||mode=='d'||mode=='e')&&active==1+(mode=='T'||mode=='U'||mode=='Y'||mode=='t')&&released==1+two+added&&sheetinit==1&&sheetpre==1,26505);
 if(remove||added)check(nonclient==1+remove+(mode=='d'||mode=='e'),26539);
 if(mode=='T'||query||added||remove||mode=='h')check(timer_done==1,26521);
 if(query)check(queries==(mode=='Q'?1:2),26529);
 if(mode=='H'||mode=='h')check(helped==1,26530);
 if(mode=='U'||mode=='Y'||mode=='B'||mode=='O'||mode=='F')check(applied==1,26522);
 if(mode=='F')check(veto==1,26523);
 check(entered==1&&finished==1,26506);ExitProcess(0);
}
