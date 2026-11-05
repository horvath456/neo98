// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport) __stdcall
#define U unsigned
API void ExitProcess(U);API U GetModuleHandleA(const char*);API U GlobalFindAtomA(const char*);API U GetLastError(void);API U GetClassInfoW(U,U,U);
API U RegisterClassA(const U*);API U RegisterClassExA(const U*);API U GetClassInfoA(U,const char*,U*);API U GetClassInfoExA(U,const char*,U*);API U UnregisterClassA(const char*,U);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DestroyWindow(U);API U DefWindowProcA(U,U,U,U);
API U CallWindowProcA(U,U,U,U,U);API U SendMessageA(U,U,U,U);
API U GetClassLongA(U,int);API U SetClassLongA(U,int,U);API U GetClassWord(U,int);API U SetClassWord(U,int,U);API U LoadIconA(U,U);API U GetClassNameA(U,char*,U);API U GetMenu(U);API U GetMenuItemCount(U);
static U __stdcall proc(U h,U m,U w,U l){return DefWindowProcA(h,m,w,l);}
#define CHECK(x,n) do{if(!(x))return n;}while(0)
/* A superclass of BUTTON, the way Delphi builds TButton: the class procedure
 * of BUTTON comes from GetClassInfo and gets every message through
 * CallWindowProc. The button state has to work through that path. */
static U button_procedure, forwarded;
static U __stdcall super_button(U h,U m,U w,U l){++forwarded;return CallWindowProcA(button_procedure,h,m,w,l);}
static int superclassed_button(U instance){
 U info[10]={0},plain[10]={0};
 CHECK(GetClassInfoA(0,"BUTTON",info)&&info[1],8950);CHECK(GetClassInfoA(0,"STATIC",plain)&&plain[1],8951);
 /* BUTTON has a procedure of its own, not the one every plain class shares. */
 CHECK(info[1]!=plain[1],8952);
 button_procedure=info[1];info[1]=(U)super_button;info[4]=instance;info[9]=(U)"Neo98SuperButton";
 CHECK(RegisterClassA(info),8953);
 /* BS_AUTOCHECKBOX: the check state lives in the BUTTON procedure. */
 U h=CreateWindowExA(0,"Neo98SuperButton","super",3,0,0,80,24,0,0,instance,0);CHECK(h&&forwarded,8954);
 CHECK(SendMessageA(h,0xf0,0,0)==0,8955);              /* BM_GETCHECK */
 SendMessageA(h,0xf1,1,0);CHECK(SendMessageA(h,0xf0,0,0)==1,8956); /* BM_SETCHECK */
 SendMessageA(h,0xf3,1,0);CHECK(SendMessageA(h,0xf2,0,0)&4,8957);  /* BM_SETSTATE, BST_PUSHED */
 CHECK(SendMessageA(h,0x87,0,0)&0x2000,8958);          /* WM_GETDLGCODE: DLGC_BUTTON */
 CHECK(DestroyWindow(h)&&UnregisterClassA("Neo98SuperButton",instance),8959);
 return 0;
}
/* The same for STATIC, the way Delphi builds TStaticText: STATIC has
 * a procedure of its own, not that of the dialog class, and a window whose
 * procedure forwards to it is a static control. */
static U static_procedure, static_forwarded;
static U __stdcall super_static(U h,U m,U w,U l){++static_forwarded;return CallWindowProcA(static_procedure,h,m,w,l);}
static int superclassed_static(U instance){
 U info[10]={0},dialog[10]={0};
 CHECK(GetClassInfoA(0,"STATIC",info)&&info[1],8960);CHECK(GetClassInfoA(0,"#32770",dialog)&&dialog[1],8961);
 CHECK(info[1]!=dialog[1],8962);
 static_procedure=info[1];info[1]=(U)super_static;info[4]=instance;info[9]=(U)"Neo98SuperStatic";
 CHECK(RegisterClassA(info),8963);
 U h=CreateWindowExA(0,"Neo98SuperStatic","Score:",0x100,0,0,80,24,0,0,instance,0);CHECK(h&&static_forwarded,8964);
 CHECK(SendMessageA(h,0x87,0,0)==0x100,8965);           /* WM_GETDLGCODE: DLGC_STATIC */
 CHECK(DestroyWindow(h)&&UnregisterClassA("Neo98SuperStatic",instance),8966);
 return 0;
}
int run_class_family_test(void){
 CHECK(!GetClassInfoW(0,0,0)&&GetLastError()==120,8928);
 U instance=GetModuleHandleA(0),icon=LoadIconA(0,32512);CHECK(icon,8900);
 U f[12]={48,3,(U)proc,12,8,instance,icon,0,0,73,0,0};f[10]=(U)"Neo98OriginalClass";
 CHECK(!GlobalFindAtomA((const char*)f[10]),8901);U atom=RegisterClassExA(f);CHECK(atom>=0xc000,8902);CHECK(!GlobalFindAtomA((const char*)f[10]),8903);CHECK(!RegisterClassExA(f),8904);
 U out[12]={0x11223344};CHECK(GetClassInfoExA(instance,(const char*)atom,out)==atom,8905);CHECK(out[0]==0x11223344&&out[1]==3&&out[2]==(U)proc&&out[3]==12&&out[4]==8&&out[5]==instance&&out[6]==icon&&out[9]==73&&out[10]==atom,8906);
 CHECK(GetClassInfoA(instance,"Neo98OriginalClass",out)==1&&out[0]==3&&out[1]==(U)proc,8907);
 U h=CreateWindowExA(0,(const char*)atom,"class fixture",0,0,0,80,60,0,0,instance,0);CHECK(h,8908);CHECK(!UnregisterClassA((const char*)atom,instance),8909);
 CHECK(GetClassLongA(h,-14)==icon&&GetClassLongA(h,-16)==instance&&GetClassLongA(h,-32)==atom,8910);
 CHECK(!SetClassLongA(h,4,0x12345678)&&GetClassWord(h,4)==0x5678&&GetClassWord(h,6)==0x1234,8911);
 CHECK(SetClassWord(h,6,0xabcd)==0x1234&&GetClassLongA(h,4)==0xabcd5678,8912);CHECK(!SetClassLongA(h,10,0xffffffff)&&!GetClassLongA(h,10),8913);
 CHECK(SetClassLongA(h,-26,8)==3&&GetClassLongA(h,-26)==8,8914);char text[64];CHECK(GetClassNameA(h,text,sizeof text)==18,8915);
 CHECK(DestroyWindow(h)&&UnregisterClassA((const char*)atom,instance),8916);CHECK(!GetClassInfoA(instance,"Neo98OriginalClass",out),8917);
 f[3]=41;CHECK(!RegisterClassExA(f),8918);f[3]=4;f[4]=41;CHECK(!RegisterClassExA(f),8919);f[4]=4;
 for(U i=0;i<300;++i){atom=RegisterClassExA(f);CHECK(atom&&UnregisterClassA((const char*)atom,instance),8920);}
 CHECK(GetClassInfoExA(0,"EDIT",out)&&out[2]&&out[4]==6&&(out[1]&0x88)==0x88,8921);CHECK(GetClassInfoExA(0,"BUTTON",out)&&out[4]==8,8922);CHECK(GetClassInfoExA(0,"STATIC",out)&&out[4]==6,8923);CHECK(GetClassInfoExA(0,"#32770",out)&&out[4]==30,8924);
 U a=CreateWindowExA(0,"STATIC","a",0,0,0,10,10,0,0,instance,0),b=CreateWindowExA(0,"STATIC","b",0,0,0,10,10,0,0,instance,0);CHECK(a&&b,8925);CHECK(SetClassLongA(a,-26,0x80)==0x88&&GetClassLongA(b,-26)==0x80,8926);CHECK(DestroyWindow(a)&&DestroyWindow(b),8927);
 char long_name[256],long_out[256];for(U i=0;i<255;++i)long_name[i]='a'+i%26;long_name[255]=0;f[10]=(U)long_name;f[9]=0;
 atom=RegisterClassExA(f);CHECK(atom,8929);h=CreateWindowExA(0,(const char*)atom,"long class",0,0,0,10,10,0,0,instance,0);CHECK(h,8930);CHECK(GetClassNameA(h,long_out,256)==255,8931);for(U i=0;i<256;++i)CHECK(long_name[i]==long_out[i],8932);CHECK(!GetClassNameA(h,long_out,65536),8933);CHECK(DestroyWindow(h)&&UnregisterClassA((const char*)atom,instance),8934);
 f[10]=(U)"ClassMenuOwner";f[9]=(U)"CLASSFAMILYNAMEDMENU";atom=RegisterClassExA(f);CHECK(atom,8935);
 for(U i=0;i<60;++i){h=CreateWindowExA(0,(const char*)atom,"menu class",0,0,0,10,10,0,0,instance,0);CHECK(h,8936);U menu=GetMenu(h);CHECK(menu&&GetMenuItemCount(menu)==1,8937);CHECK(DestroyWindow(h),8938);}
 CHECK(UnregisterClassA((const char*)atom,instance),8939);
 f[5]=0;f[9]=0;f[10]=(U)"ClassZeroInstance";
 for(U extended=0;extended<2;++extended){
  atom=extended?RegisterClassExA(f):RegisterClassA(f+1);CHECK(atom,8940);
  CHECK(GetClassInfoA(instance,(const char*)atom,out)&&out[4]==instance,8941);
  CHECK(UnregisterClassA((const char*)atom,0),8942);
 }
 {int failed=superclassed_button(instance);if(failed)return failed;}
 {int failed=superclassed_static(instance);if(failed)return failed;}
 return 0;
}
#ifdef CLASS_FAMILY_STANDALONE
void mainCRTStartup(void){ExitProcess(run_class_family_test());}
#endif
