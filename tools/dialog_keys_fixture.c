// SPDX-License-Identifier: GPL-2.0-or-later
// Keys of a dialog that its controls handle themselves,
// driven from the keyboard by tools/test_dialog_keys_host.py.
//
// Dialog "Keys": 100 a multiline EDIT without, 101 one with ES_WANTRETURN,
// 102 an automatic check box, 103 a plain check box, OK (default) and Cancel.
// Dialog "Keys 2" is the same and is left with ESC. Each end appends a line to
// C:\DLGKEYS.TXT; a last line tells what a TAB character does to a multiline
// and a single-line EDIT outside a dialog.
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API U DialogBoxIndirectParamA(U,const void*,U,U,U);API U GetModuleHandleA(const char*);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);API U DestroyWindow(U);
API U GetDlgItem(U,U);API U GetWindowTextA(U,char*,int);API U EndDialog(U,U);API U SendMessageA(U,U,U,U);
API U CreateFileA(const char*,U,U,void*,U,U,U);API U WriteFile(U,const void*,U,U*,void*);API U CloseHandle(U);
API void ExitProcess(U);
static char out[1024];static U length,clicks;
static void put(const char*s){while(*s&&length<sizeof out-1)out[length++]=*s++;}
static void number(U v){char d[12];U n=0;do{d[n++]=(char)('0'+v%10);v/=10;}while(v);while(n)out[length++]=d[--n];}
static void text(U h,U id){
 char t[128];U n=GetWindowTextA(id?GetDlgItem(h,id):h,t,sizeof t);put("[");
 for(U i=0;i<n&&i<sizeof t;++i){if(t[i]=='\t')put("\\t");else if(t[i]=='\r')put("\\r");else if(t[i]=='\n')put("\\n");else{char c[2]={t[i],0};put(c);}}
 put("]");
}
static U __stdcall proc(U h,U m,U w,U l){
 (void)l;if(m==0x110)return 1;
 if(m==0x111){
  const U id=w&0xffff;
  if(id==103&&!(w>>16))++clicks;
  if(id==1||id==2){
   put("end=");number(id);put(" e100=");text(h,100);put(" e101=");text(h,101);
   put(" auto=");number(SendMessageA(GetDlgItem(h,102),0xf0,0,0));put(" plain=");number(SendMessageA(GetDlgItem(h,103),0xf0,0,0));
   put(" clicks=");number(clicks);put("\n");clicks=0;EndDialog(h,id);
  }
  return 1;
 }
 return 0;
}
static void word(unsigned char*b,U*at,U v){b[(*at)++]=(unsigned char)v;b[(*at)++]=(unsigned char)(v>>8);}
static void dword(unsigned char*b,U*at,U v){word(b,at,v);word(b,at,v>>16);}
static void string(unsigned char*b,U*at,const char*s){do{word(b,at,(unsigned char)*s);}while(*s++);}
static void item(unsigned char*b,U*at,U style,int x,int y,int cx,int cy,U id,U atom,const char*title){
 while(*at&3)b[(*at)++]=0;dword(b,at,style);dword(b,at,0);word(b,at,x);word(b,at,y);word(b,at,cx);word(b,at,cy);word(b,at,id);
 word(b,at,0xffff);word(b,at,atom);string(b,at,title);word(b,at,0);
}
static void make(unsigned char*b,const char*title){
 U at=0;dword(b,&at,0x90c80880);dword(b,&at,0);word(b,&at,6);word(b,&at,0);word(b,&at,0);word(b,&at,200);word(b,&at,110);
 word(b,&at,0);word(b,&at,0);string(b,&at,title);
 item(b,&at,0x50a10044,5,5,190,30,100,0x81,"");
 item(b,&at,0x50a11044,5,40,190,30,101,0x81,"");
 item(b,&at,0x50010003,5,75,60,12,102,0x80,"&Auto");
 item(b,&at,0x50010002,70,75,60,12,103,0x80,"&Plain");
 item(b,&at,0x50010001,5,92,50,14,1,0x80,"OK");
 item(b,&at,0x50010000,60,92,50,14,2,0x80,"Cancel");
}
void mainCRTStartup(void){
 static unsigned char data[1024];U instance=GetModuleHandleA(0);
 make(data,"Keys");DialogBoxIndirectParamA(instance,data,0,(U)proc,0);
 make(data,"Keys 2");DialogBoxIndirectParamA(instance,data,0,(U)proc,0);
 // A TAB character outside a dialog: the multiline body inserts it;
 // the single-line one beeps and leaves the text.
 U multi=CreateWindowExA(0,"EDIT","",0x44,0,0,100,40,0,0,instance,0);
 U single=CreateWindowExA(0,"EDIT","",0,0,0,100,20,0,0,instance,0);
 SendMessageA(multi,0x102,'x',0);SendMessageA(multi,0x102,9,0);SendMessageA(multi,0x102,'y',0);
 SendMessageA(single,0x102,'x',0);SendMessageA(single,0x102,9,0);SendMessageA(single,0x102,'y',0);
 put("multiline=");text(multi,0);put(" single=");text(single,0);put("\n");
 DestroyWindow(multi);DestroyWindow(single);
 U file=CreateFileA("C:\\DLGKEYS.TXT",0x40000000,0,0,2,0x80,0);U written=0;
 if(file!=0xffffffff){WriteFile(file,out,length,&written,0);CloseHandle(file);}
 ExitProcess(0);
}
