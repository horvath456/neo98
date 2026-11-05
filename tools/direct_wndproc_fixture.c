// SPDX-License-Identifier: GPL-2.0-or-later
// Test direct calls to system-class window procedures. Windows 98 returns
// callable procedures from GetWindowLong, GetClassLong, and GetClassInfo.
// Plus! 98 uses this when subclassing its wizard. Write checks to
// C:\DIRECTWP.TXT, ending with done or failed N.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef U (__stdcall *PROC)(U,U,U,U);
API U CreateDialogIndirectParamA(U,const void*,U,U,U);API U GetModuleHandleA(const char*);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U GetDlgItem(U,U);API U GetWindowLongA(U,int);API U SetWindowLongA(U,int,U);API U GetClassLongA(U,int);
API U GetClassInfoA(U,const char*,void*);API U SendMessageA(U,U,U,U);API U GetWindowTextA(U,char*,int);
API U DestroyWindow(U);API U CreateFileA(const char*,U,U,void*,U,U,U);API U WriteFile(U,const void*,U,U*,void*);
API U CloseHandle(U);API U SetFilePointer(U,int,int*,U);API void ExitProcess(U);
static void note(const char*s){
 U h=CreateFileA("C:\\DIRECTWP.TXT",0x40000000,3,0,4,0x80,0),n=0,done=0;
 if(h==~0u)return;while(s[n])++n;SetFilePointer(h,0,0,2);WriteFile(h,s,n,&done,0);CloseHandle(h);
}
static void check(int okay,U number){
 char line[24]="failed ";U at=7,digits=1000;
 if(okay)return;
 for(;digits;digits/=10)line[at++]=(char)('0'+number/digits%10);
 line[at++]='\r';line[at++]='\n';line[at]=0;note(line);ExitProcess(number);
}
static int same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static U old_proc,sub_calls,dialog_calls;
static U __stdcall proc(U h,U m,U w,U l){(void)w;(void)l;if(m==0x110)return 1;if(m==0x8001){++dialog_calls;SetWindowLongA(h,0,0x2468ace0);return 1;}return 0;}
static U __stdcall subclass(U h,U m,U w,U l){if(m==0x8001)++sub_calls;return ((PROC)old_proc)(h,m,w,l);}
static void word(unsigned char*b,U*at,U v){b[(*at)++]=(unsigned char)v;b[(*at)++]=(unsigned char)(v>>8);}
static void dword(unsigned char*b,U*at,U v){word(b,at,v);word(b,at,v>>16);}
static void string(unsigned char*b,U*at,const char*s){do{word(b,at,(unsigned char)*s);}while(*s++);}
static void make(unsigned char*b){U at=0;dword(b,&at,0x90c80080);dword(b,&at,0);word(b,&at,1);word(b,&at,0);word(b,&at,0);word(b,&at,100);word(b,&at,60);word(b,&at,0);word(b,&at,0);string(b,&at,"Direct");while(at&3)b[at++]=0;dword(b,&at,0x50010000);dword(b,&at,0);word(b,&at,5);word(b,&at,5);word(b,&at,40);word(b,&at,14);word(b,&at,11);word(b,&at,0xffff);word(b,&at,0x80);string(b,&at,"Button");word(b,&at,0);}
void mainCRTStartup(void){
 unsigned char data[256];char text[32];U instance=GetModuleHandleA(0),info[10];
 make(data);
 U h=CreateDialogIndirectParamA(instance,data,0,(U)proc,0);check(h!=0,1);
 // The dialog class: subclass and forward with a plain call.
 old_proc=GetWindowLongA(h,-4);check(old_proc!=0,2);
 check(SetWindowLongA(h,-4,(U)subclass)==old_proc,3);
 check(SendMessageA(h,0x8001,0,0)==0x2468ace0&&sub_calls==1&&dialog_calls==1,4);
 check(((PROC)old_proc)(h,0xc,0,(U)"Called")==1,5);
 check(GetWindowTextA(h,text,sizeof text)==6&&same(text,"Called"),6);
 note("dialog\r\n");
 // BUTTON by the procedure of the window.
 U button=GetDlgItem(h,11),button_proc=GetWindowLongA(button,-4);check(button&&button_proc,7);
 ((PROC)button_proc)(button,0xf3,1,0);check((SendMessageA(button,0xf2,0,0)&4)!=0,8);
 ((PROC)button_proc)(button,0xf3,0,0);check(!(SendMessageA(button,0xf2,0,0)&4),9);
 note("button\r\n");
 // EDIT by the procedure of the class.
 U edit=CreateWindowExA(0,"EDIT","abc",0x50000000,5,25,60,14,h,12,instance,0);check(edit!=0,10);
 U edit_proc=GetClassLongA(edit,-24);check(edit_proc!=0,11);
 check(((PROC)edit_proc)(edit,0xe,0,0)==3,12);
 check(((PROC)edit_proc)(edit,0xc,0,(U)"longer")==1&&((PROC)edit_proc)(edit,0xd,sizeof text,(U)text)==6&&same(text,"longer"),13);
 note("edit\r\n");
 // STATIC by GetClassInfo.
 check(GetClassInfoA(0,"STATIC",info)!=0&&info[1]!=0,14);
 U label=CreateWindowExA(0,"STATIC","xy",0x50000000,5,45,60,10,h,13,instance,0);check(label!=0,15);
 check(((PROC)info[1])(label,0xe,0,0)==2,16);
 note("static\r\n");
 // LISTBOX and COMBOBOX.
 U list=CreateWindowExA(0,"LISTBOX","",0x50000000,70,5,40,40,h,14,instance,0);check(list!=0,17);
 U list_proc=GetWindowLongA(list,-4);check(list_proc!=0,18);
 check(((PROC)list_proc)(list,0x180,0,(U)"one")==0&&((PROC)list_proc)(list,0x18b,0,0)==1,19);
 U combo=CreateWindowExA(0,"COMBOBOX","",0x50000003,70,45,40,60,h,15,instance,0);check(combo!=0,20);
 U combo_proc=GetWindowLongA(combo,-4);check(combo_proc!=0,21);
 check(((PROC)combo_proc)(combo,0x143,0,(U)"one")==0&&((PROC)combo_proc)(combo,0x146,0,0)==1,22);
 note("lists\r\n");
 check(DestroyWindow(h),23);note("done\r\n");ExitProcess(0);
}
