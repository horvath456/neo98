/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U CreateProcessA(const char *, char *, U, U, U, U, U, U, U *, U *);
API U CloseHandle(U);
API U VirtualAlloc(void *, U, U, U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U FindWindowA(const char *,const char *);
API U FindWindowExA(U,U,const char *,const char *);
API U SendMessageA(U,U,U,U);
API U SendMessageTimeoutA(U,U,U,U,U,U,U *);
API U GetWindowTextA(U,char *,U);
API U GetMessageA(U *,U,U,U);
API U DispatchMessageA(U *);
API U PostMessageA(U,U,U,U);
static const char text[]="Cross process text";
static void check(int ok,U code){if(!ok){U h=FindWindowA("CrossText","caption");if(h)PostMessageA(h,0x8002,code,0);ExitProcess(code);}}
static U __stdcall proc(U h,U m,U w,U l){
    if(m==0xd){U n=0;char *p=(char *)l;if(!w)return 0;
        while(text[n]&&n+1<w){p[n]=text[n];++n;}p[n]=0;return n;}
    if(m==0x8001)ExitProcess(0);
    if(m==0x8002)ExitProcess(w);
    return DefWindowProcA(h,m,w,l);
}
static void verify(char *p,U n,U cap){
    check(n==(cap<sizeof text?cap-1:sizeof text-1),10);
    for(U i=0;i<n;++i)check(p[i]==text[i],11);
    check(p[n]==0&&p[cap]=='!',12);
}
void mainCRTStartup(void){
    char *cmd=GetCommandLineA();int child=0;
    for(U i=0;cmd[i];++i)if(cmd[i]=='!')child=1;
    if(child){
        U owner=FindWindowA("CrossText","caption");check(owner!=0,1);
        U edit=FindWindowExA(owner,0,"EDIT",0);check(edit!=0,2);
        char *p=(char *)VirtualAlloc((void *)0x30000000,4096,0x3000,4);check(p!=0,3);
        for(U mode=0;mode<3;++mode)for(U cap=1;cap<=32;cap+=cap<5?1:27){
            for(U i=0;i<64;++i)p[i]='!';
            U n=0,h=mode==2?edit:owner;
            if(mode==1)check(SendMessageTimeoutA(h,0xd,cap,(U)p,0,5000,&n)!=0,4);
            else n=SendMessageA(h,0xd,cap,(U)p);
            verify(p,n,cap);
        }
        p[0]='!';check(SendMessageA(owner,0xd,0,(U)p)==0&&p[0]=='!',5);
        /* USER uses DefWindowProc for a foreign GetWindowTextA. */
        check(GetWindowTextA(owner,p,64)==7,6);
        const char *caption="caption";for(U i=0;i<8;++i)check(p[i]==caption[i],7);
        PostMessageA(owner,0x8001,0,0);ExitProcess(0);
    }
    struct {U style,proc,a,b,instance,icon,cursor,brush;const char *menu,*name;} cls={0,(U)proc,0,0,0,0,0,0,0,"CrossText"};
    check(RegisterClassA(&cls)!=0,20);
    U owner=CreateWindowExA(0,cls.name,"caption",0,0,0,100,100,0,0,0,0);check(owner!=0,21);
    check(CreateWindowExA(0,"EDIT",text,0x40000000,0,0,90,50,owner,0,0,0)!=0,22);
    U si[17]={68},pi[4]={0};char command[]="D:\\GETTEXT.EXE !";
    check(CreateProcessA("D:\\GETTEXT.EXE",command,0,0,0,0,0,0,si,pi)!=0,23);
    CloseHandle(pi[0]);CloseHandle(pi[1]);
    U message[8];while(GetMessageA(message,0,0,0))DispatchMessageA(message);
    ExitProcess(24);
}
