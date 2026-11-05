/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
typedef unsigned U;
typedef struct {int image;U command;unsigned char state,style;unsigned short reserved;U data,string;} BUTTON;
typedef struct {U root;const char*key;const char*value;} SAVE;
typedef struct {U hwnd,id,code;} HDR;
__declspec(dllimport) U __stdcall InitCommonControls(void);
__declspec(dllimport) U __stdcall GetModuleHandleA(const char*);
__declspec(dllimport) U __stdcall RegisterClassA(void*);
__declspec(dllimport) U __stdcall UnregisterClassA(const char*,U);
__declspec(dllimport) U __stdcall CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
__declspec(dllimport) U __stdcall DestroyWindow(U);
__declspec(dllimport) U __stdcall DefWindowProcA(U,U,U,U);
__declspec(dllimport) U __stdcall SendMessageA(U,U,U,U);
__declspec(dllimport) void __stdcall ExitProcess(U);
__declspec(dllimport) void* __stdcall LocalAlloc(U,U);
__declspec(dllimport) U __stdcall LocalFree(void*);
__declspec(dllimport) U __stdcall RegCreateKeyA(U,const char*,U*);
__declspec(dllimport) U __stdcall RegSetValueExA(U,const char*,U,U,const void*,U);
__declspec(dllimport) U __stdcall RegQueryValueExA(U,const char*,U*,U*,void*,U*);
__declspec(dllimport) U __stdcall RegCloseKey(U);
static U bar,registry_key,save_notes,restore_notes,restore_item,callback_error,save_mode,restore_mode,in_restore;
static char borrowed_text[]="Borrowed restore text";
static void check(int ok,U code){if(!ok)ExitProcess(code);}
static U __stdcall parent_proc(U h,U m,U w,U l){
 if(m==0x4e&&((HDR*)l)->code==0xfffffd44){
  int item=*(int*)(l+12);if(item>=4||restore_mode==5)return 0;if(in_restore&&restore_notes!=5)callback_error=7;static const U commands[4]={10,0,20,0};static const unsigned char states[4]={4,4,4,12},styles[4]={0,1,0,1};
  BUTTON*b=(BUTTON*)(l+16);b->command=commands[item];b->state=(item==0||item==2)?5:states[item];b->style=styles[item];b->data=0xfeed0000+(U)item;
#ifdef NATIVE_NEGATIVE
  if(restore_mode==4){b->string=(U)"Owned restore text";if(item==3){DestroyWindow(bar);bar=0;}}
#endif
  return 1;}
 if(m==0x4e){HDR*n=(HDR*)l;int item=*(int*)(l+24);int buttons=*(int*)(l+28);
  if(n->code==0xfffffd2e){
   if(item!=(int)(save_notes%5)-1||buttons!=4)callback_error=1;
   if(item<0&&save_mode){
#ifdef NATIVE_NEGATIVE
    if(SendMessageA(bar,0x416,0,0)||SendMessageA(bar,0x418,0,0)!=4)callback_error=6;
#endif
    *(U*)(l+12)=1;*(U*)(l+16)=1;*(U*)(l+20)=16;}
   else if(item<0){U data=(U)LocalAlloc(0,36);if(!data)ExitProcess(8625);*(U*)(l+12)=data;*(U*)(l+16)=data+4;*(U*)(l+20)=36;*(U*)data=0x12345678;}
   else {U current=*(U*)(l+16);if(!current)callback_error=3;else {*(U*)current=0xabc00000+(U)item;*(U*)(l+16)=current+4;}}
   ++save_notes;}
  if(n->code==0xfffffd2f){
   static const U commands[4]={10,0,20,0};static const unsigned char states[4]={4,4,4,12},styles[4]={0,1,0,1};
   if(item<0)restore_item=0;else if(item!=(int)restore_item++)callback_error=2;
   if(item<0&&restore_mode==3){*(U*)(l+16)=*(U*)(l+12);*(U*)(l+28)=0;restore_mode=0;}
   else if(item<0){if(*(U*)(l+20)!=36||**(U**)(l+12)!=0x12345678)callback_error=4;*(U*)(l+16)=*(U*)(l+12)+4;*(U*)(l+28)=4;*(U*)(l+32)+=4;
    if(restore_mode==2){U changed=999;RegSetValueExA(registry_key,"layout",0,3,&changed,4);restore_mode=0;}}
   else {U current=*(U*)(l+16);if(!current||*(U*)current!=0xabc00000+(U)item)callback_error=5;else *(U*)(l+16)=current+4;BUTTON*b=(BUTTON*)(l+36);b->command=commands[item];b->state=states[item];b->style=styles[item];
#ifdef NATIVE_NEGATIVE
    if(restore_mode==5)b->string=(U)borrowed_text;
#endif
   }
   ++restore_notes;if(item<0&&restore_mode==1)return 1;}}
 return DefWindowProcA(h,m,w,l);
}
static void add(U command,unsigned char state,unsigned char style){BUTTON b={0,command,state,style,0,0,0};check(SendMessageA(bar,0x414,1,(U)&b),8601);}
static BUTTON get(U index){BUTTON b={0};check(SendMessageA(bar,0x417,index,(U)&b),8602);return b;}
int run_toolbar_save_test(void){
 InitCommonControls();U instance=GetModuleHandleA(0),klass[10]={0,(U)parent_proc,0,0,instance};klass[9]=(U)"ToolbarSaveParent";
 check(RegisterClassA(klass),8603);U parent=CreateWindowExA(0,(char*)klass[9],"save",0,0,0,300,80,0,0,instance,0);check(parent,8604);
 bar=CreateWindowExA(0,"ToolbarWindow32","",0x40000000,0,0,280,30,parent,44,instance,0);check(bar,8605);SendMessageA(bar,0x41e,20,0);
 add(10,4,0);add(0,4,1);add(20,4,0);add(0,12,1);
 SAVE missing={0x80000001,"Software\\NEO98\\ToolbarSave","missing"};check(!SendMessageA(bar,0x41a,0,(U)&missing),8606);check(SendMessageA(bar,0x418,0,0)==4,8607);
 SAVE saved={0x80000001,"Software\\NEO98\\ToolbarSave","layout"};check(SendMessageA(bar,0x41a,1,(U)&saved),8608);check(save_notes==5&&!callback_error,8609);
 U key=0,type=0,size=40,data[10]={};check(!RegCreateKeyA(saved.root,saved.key,&key),8610);registry_key=key;check(!RegQueryValueExA(key,saved.value,0,&type,data,&size),8611);
 check(type==3&&size==36&&data[0]==0x12345678&&data[1]==10&&data[2]==0xabc00000&&data[3]==0xffffffff&&data[4]==0xabc00001&&data[5]==20&&data[6]==0xabc00002&&data[7]==0xfffffffe&&data[8]==0xabc00003,8612);
 while(SendMessageA(bar,0x418,0,0))check(SendMessageA(bar,0x416,0,0),8613);
 add(20,4,0);add(0,12,1);add(10,4,0);add(0,4,1);
 restore_mode=2;in_restore=1;check(SendMessageA(bar,0x41a,0,(U)&saved),8614);in_restore=0;check(restore_notes==5&&!callback_error,8615);
 check(SendMessageA(bar,0x418,0,0)==4,8624);
 BUTTON a=get(0),b=get(1),c=get(2),d=get(3);check(a.command==10&&!b.command&&c.command==20&&!d.command,8616);check(a.state==5&&c.state==5&&a.data==0xfeed0000&&c.data==0xfeed0002,8632);check(!(b.state&8)&&(d.state&8),8617);
 check(SendMessageA(bar,0x41a,1,(U)&saved),8626);U before_abort=get(0).command;restore_mode=1;check(!SendMessageA(bar,0x41a,0,(U)&saved)&&get(0).command==before_abort,8627);restore_mode=0;
#ifdef NATIVE_NEGATIVE
 SAVE invalid_save={saved.root,saved.key,"invalid-save"};save_mode=1;check(!SendMessageA(bar,0x41a,1,(U)&invalid_save)&&get(0).command==before_abort,8628);save_mode=0;
#endif
 U before=get(0).command;check(!RegSetValueExA(key,"bad-type",0,1,"x",2),8618);SAVE bad={saved.root,saved.key,"bad-type"};check(!SendMessageA(bar,0x41a,0,(U)&bad)&&get(0).command==before,8619);
 check(!RegSetValueExA(key,"bad-size",0,3,"xyz",3),8620);bad.value="bad-size";check(!SendMessageA(bar,0x41a,0,(U)&bad)&&get(0).command==before,8621);
#ifdef NATIVE_NEGATIVE
 restore_mode=5;check(SendMessageA(bar,0x41a,0,(U)&saved)&&!SendMessageA(bar,0x418,0,0),8633);check(borrowed_text[0]=='B'&&borrowed_text[8]==' '&&borrowed_text[20]=='t'&&borrowed_text[21]==0,8634);
 restore_mode=4;SendMessageA(bar,0x41a,0,(U)&saved);check(!bar,8631);
#endif
 RegCloseKey(key);check(DestroyWindow(parent),8622);check(UnregisterClassA((char*)klass[9],instance),8623);return 0;
}
void mainCRTStartup(void){ExitProcess(run_toolbar_save_test());}
