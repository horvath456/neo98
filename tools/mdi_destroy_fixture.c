/* SPDX-License-Identifier: GPL-2.0-or-later */
/* WM_MDIDESTROY must notify a frame and its nested views. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U DefWindowProcA(U,U,U,U);
API U DefFrameProcA(U,U,U,U,U);
API U DefMDIChildProcA(U,U,U,U);
API U SendMessageA(U,U,U,U);
API U IsWindow(U);
static U client,frame,child,view,leaf,events[8],count;
static void check(int ok,U code){if(!ok)ExitProcess(code);}
static U __stdcall Proc(U h,U m,U w,U l){
    if((m==2||m==0x82)&&h!=frame){
        check(count<8,90);
        events[count++]=(m==2?0:10)+(h==child?1:h==view?2:3);
    }
    if(h==frame)return DefFrameProcA(h,client,m,w,l);
    if(h==child)return DefMDIChildProcA(h,m,w,l);
    return DefWindowProcA(h,m,w,l);
}
void mainCRTStartup(void){
    U cls[10]={0,(U)Proc,0,0,0,0,0,0,0,(U)"MdiDestroy98"},ccs[2]={0,100};
    U create[9]={(U)"MdiDestroy98",(U)"child",0x400000,0,0,200,150,0,0};
    U expected[6]={1,2,3,13,12,11},i,other;
    check(RegisterClassA(cls)!=0,1);
    frame=CreateWindowExA(0,"MdiDestroy98","frame",0x10cf0000,0,0,400,300,0,0,0,0);
    client=CreateWindowExA(0,"MDICLIENT",0,0x52000000,0,0,300,200,frame,0,0,ccs);
    child=SendMessageA(client,0x220,0,(U)create);
    view=CreateWindowExA(0,"MdiDestroy98","view",0x50000000,0,0,100,100,child,0,0,0);
    leaf=CreateWindowExA(0,"MdiDestroy98","leaf",0x50000000,0,0,50,50,view,0,0,0);
    check(child&&view&&leaf,2);
    other=SendMessageA(client,0x220,0,(U)create);
    check(other!=0,6);
    SendMessageA(client,0x221,child,0);
    check(count==6,100+count);
    for(i=0;i<6;++i)check(events[i]==expected[i],10+i);
    check(!IsWindow(child)&&!IsWindow(view)&&!IsWindow(leaf),4);
    check(IsWindow(client)&&IsWindow(frame),5);
    check(SendMessageA(client,0x229,0,0)==other,7);
    ExitProcess(0);
}
