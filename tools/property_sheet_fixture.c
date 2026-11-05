/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Prepared integration guest: run after the real multiline sheet UI is ready. */
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U PropertySheetA(const void*);API U PropertySheetW(const void*);
API U CreatePropertySheetPageW(const void*);API U DestroyPropertySheetPage(U);
API U GetParent(U);API U GetDlgItem(U,U);API U GetWindowLongA(U,int);API U SetWindowLongA(U,int,U);
API U SendMessageA(U,U,U,U);API U GetModuleHandleA(const char*);
static void check(int condition,U code){if(!condition)ExitProcess(code);}
#pragma pack(push,2)
typedef struct {U style,extended;W controls,x,y,width,height,menu,klass,title;} Template;
#pragma pack(pop)
static const Template templates[2]={{0x40000400,0,0,0,0,180,70,0,0,0},{0x40000400,0,0,0,0,180,70,0,0,0}};
static U mode,created[2],released[2],initialized[2],active[2],applied[2],cancelled[2],veto,sheetInit,owner;
static const char caption[]="Original property sheet",clock[]="Clock",zone[]="Zone";
static const W wcaption[]={'O','r','i','g','i','n','a','l',' ','w','i','d','e',' ','s','h','e','e','t',0};
static const W wclock[]={'C','l','o','c','k',0},wzone[]={'Z','o','n','e',0};
static U __stdcall page_callback(U hwnd,U reason,U*p){
 check(!hwnd&&p[0]==48&&p[7]<2,25600);U id=p[7];
 if(reason==2)++created[id];else if(reason==1)++released[id];return 1;
}
static U __stdcall page_proc(U hwnd,U message,U wparam,U lparam){
 (void)wparam;
 if(message==0x110){U*p=(U*)lparam;check(p&&p[7]<2,25601);U id=p[7];SetWindowLongA(hwnd,-21,id+1);++initialized[id];owner=GetParent(hwnd);return 1;}
 if(message==0x4e){U id=GetWindowLongA(hwnd,-21)-1;check(id<2,25602);U*n=(U*)lparam;check(n&&n[0]==GetParent(hwnd),25603);
  if(n[2]==(U)-200){++active[id];SetWindowLongA(hwnd,0,0);return 1;}
  if(n[2]==(U)-201){U deny=!mode&&id==1&&!veto;if(deny)++veto;SetWindowLongA(hwnd,0,deny);return 1;}
  if(n[2]==(U)-202){++applied[id];SetWindowLongA(hwnd,0,0);return 1;}
  if(n[2]==(U)-203){++cancelled[id];return 1;}
 }
 return 0;
}
static U __stdcall sheet_callback(U hwnd,U reason,U parameter){
 if(reason==1){check(hwnd&&!parameter,25604);U tab=GetDlgItem(hwnd,0x3020);check(tab&&(GetWindowLongA(tab,-16)&0x200),25605);check(SendMessageA(tab,0x1304,0,0)==2,25606);++sheetInit;}
 return 0;
}
void run_property_sheet_test(void){
 U pages[2][12]={{48,0x89,0,(U)&templates[0],0,(U)clock,(U)page_proc,0,(U)page_callback,0,0,0},
 {48,0x89,0,(U)&templates[1],0,(U)zone,(U)page_proc,1,(U)page_callback,0,0,0}};
 U header[13]={52,0x108,0,0,0,(U)caption,2,1,(U)pages,(U)sheet_callback,0,0,0};
 for(mode=0;mode<2;++mode){
  for(U i=0;i<2;++i){created[i]=released[i]=initialized[i]=active[i]=applied[i]=cancelled[i]=0;pages[i][2]=GetModuleHandleA(0);}
  sheetInit=0;header[3]=pages[0][2];U result;
  if(!mode)result=PropertySheetA(header);
  else{U handles[2];pages[0][5]=(U)wclock;pages[1][5]=(U)wzone;
   handles[0]=CreatePropertySheetPageW(pages[0]);handles[1]=CreatePropertySheetPageW(pages[1]);check(handles[0]&&handles[1],25607);
   header[1]=0x100;header[5]=(U)wcaption;header[7]=0;header[8]=(U)handles;result=PropertySheetW(header);
  }
  check(result==(mode?0:1),25608);check(sheetInit==1&&owner,25609);
  check(released[0]==1&&released[1]==1,25610);
  if(!mode){check(veto==1&&initialized[0]==1&&initialized[1]==1&&active[0]&&active[1],25611);check(applied[0]&&applied[1],25612);}
  else check(initialized[0]==1&&!initialized[1]&&cancelled[0],25613);
 }
}
void mainCRTStartup(void){run_property_sheet_test();ExitProcess(0);}
