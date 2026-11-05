// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetOpenFileNameA(void*);API U GetSaveFileNameA(void*);API U CommDlgExtendedError(void);
API U GetModuleHandleA(const char*);API U CreateDirectoryA(const char*,void*);API U SetCurrentDirectoryA(const char*);API U GetCurrentDirectoryA(U,char*);
API U CreateFileA(const char*,U,U,void*,U,U,U);API U WriteFile(U,const void*,U,U*,void*);API U CloseHandle(U);
API U GetParent(U);API U SetWindowLongA(U,int,U);API U SendMessageA(U,U,U,U);API U DestroyWindow(U);
struct OFN{U size,owner,instance;const char*filter;char*custom;U maxcustom,index;char*file;U maxfile;char*filetitle;U maxtitle;const char*initial,*title;U flags;W offset,extension;const char*defext;U data,hook;const char*resource;};
static void check(int yes,U code){if(!yes)ExitProcess(code);}
static int same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static void copy(char*a,const char*b){while((*a++=*b++));}
static U init,notify,fileok,veto,destroy;static U __stdcall hook(U hwnd,U msg,U wp,U lp){
 (void)wp;
 if(msg==0x110){struct OFN*p=(struct OFN*)lp;check(p&&p->size==76&&GetParent(hwnd),31600);++init;return 1;}
 if(msg==0x4e){U*n=(U*)lp;check(n[0]==GetParent(hwnd)&&n[3],31601);if(n[2]==(U)-601)++notify;
  if(n[2]==(U)-606){++fileok;if(destroy){DestroyWindow(GetParent(hwnd));return 1;}if(veto&&fileok==1){SetWindowLongA(hwnd,0,1);return 1;}}
 }
 return 0;
}
static struct OFN setup(char*file,U cap,const char*title){struct OFN p;for(U i=0;i<sizeof p;++i)((volatile unsigned char*)&p)[i]=0;p.size=76;p.instance=GetModuleHandleA(0);p.filter="Rich text\0*.rtf\0Text\0*.txt\0\0";p.index=2;p.file=file;p.maxfile=cap;p.title=title;p.flags=0x880826;p.defext="rtf";p.hook=(U)hook;return p;}
void run_file_dialog_test(void){
 check(!GetOpenFileNameA(0)&&CommDlgExtendedError()==2,31602);struct OFN bad;bad.size=80;check(!GetSaveFileNameA(&bad)&&CommDlgExtendedError()==1,31603);
 CreateDirectoryA("C:\\FDTEST",0);check(SetCurrentDirectoryA("C:\\FDTEST"),31604);
 char name[260],title[5],cwd[260];name[0]=title[0]=0;struct OFN p=setup(name,sizeof name,"File family save");p.filetitle=title;p.maxtitle=5;veto=1;
 check(GetSaveFileNameA(&p),31605);check(init==1&&notify==1&&fileok==2,31606);check(same(name,"C:\\FDTEST\\written.txt")&&same(title,"writ"),31607);check(p.index==2&&p.offset==10&&p.extension==18&&(p.flags&0x400),31608);
 U h=CreateFileA(name,0x40000000,3,0,2,0x80,0),written=0;check(h!=(U)-1&&WriteFile(h,"real file",9,&written,0)&&written==9&&CloseHandle(h),31609);
 name[0]=0;p=setup(name,sizeof name,"File family open");p.flags|=0x1000;veto=0;init=notify=fileok=0;check(GetOpenFileNameA(&p)&&same(name,"C:\\FDTEST\\written.txt")&&fileok==1,31610);
 copy(name,"keep.rtf");p=setup(name,sizeof name,"File family cancel");init=notify=fileok=0;check(!GetSaveFileNameA(&p)&&CommDlgExtendedError()==0&&same(name,"keep.rtf"),31611);
 name[0]=0;p=setup(name,3,"File family small");check(!GetSaveFileNameA(&p)&&CommDlgExtendedError()==0x3003&&(unsigned char)name[0]>3&&name[1]==0&&name[2]==0,31612);
 name[0]=0;p=setup(name,sizeof name,"File family destroy");destroy=1;check(!GetSaveFileNameA(&p),31613);check(GetCurrentDirectoryA(260,cwd)&&same(cwd,"C:\\FDTEST"),31614);
}
#ifdef FILE_DIALOG_STANDALONE
void mainCRTStartup(void){run_file_dialog_test();ExitProcess(0);}
#endif
