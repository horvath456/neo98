/* SPDX-License-Identifier: GPL-2.0-or-later */
/* retained screen DCs must not exhaust USER's paint stack. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(void *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U GetDC(U);
API U GetWindowDC(U);
API U ReleaseDC(U,U);
API U SetFocus(U);
API U GetFocus(void);
API U SetTimer(U,U,U,U);
API int GetMessageA(void *,U,U,U);
API U TranslateMessage(const void *);
API U DispatchMessageA(const void *);
API U GetStockObject(U);
API U SetTextColor(U,U);
API U GetTextColor(U);
API int SaveDC(U);
API int RestoreDC(U,int);
API int GetClipBox(U,int *);
API int IntersectClipRect(U,int,int,int,int);
API U SetPixel(U,int,int,U);
API U GetPixel(U,int,int);
static U held[5], box, paints, settled, ticks;
static void check(int okay,U code) { if(!okay)ExitProcess(code); }
static U __stdcall procedure(U h,U m,U wp,U lp)
{
    if(m==0x138) { ++paints; return GetStockObject(5); } /* HOLLOW_BRUSH */
    if(m==0x113) {
        check(GetFocus()==box,61020);
        if(++ticks==1)settled=paints;
        if(ticks==4) {
            check(settled>0,61019);
            check(paints-settled<=2,61021);
            /* Release out of acquisition order, preserving the other states. */
            const U order[5]={1,3,0,4,2};
            for(U i=0;i<5;++i) {
                const U n=order[i];
                /* USER reclaims the oldest of five held cache DCs
                   when window painting needs another. The remaining four
                   retain their attributes; the reclaimed lease is gone. */
                if(n==0) {
                    check(GetTextColor(held[n])==0,61026);
                    check(!ReleaseDC(0,held[n]),61027);
                } else {
                    check(GetTextColor(held[n])==0x101010u*(n+1),61022);
                    check(ReleaseDC(0,held[n]),61023);
                }
            }
            const U dc=GetDC(0);
            check(dc && GetTextColor(dc)==0 && SaveDC(dc)==1,61024);
            check(ReleaseDC(0,dc),61025);
            ExitProcess(0);
        }
        return 0;
    }
    return DefWindowProcA(h,m,wp,lp);
}
void mainCRTStartup(void)
{
    for(U i=0;i<5;++i) {
        held[i]=i&1?GetWindowDC(0):GetDC(0);
        check(held[i]!=0,61000);
        for(U k=0;k<i;++k)check(held[k]!=held[i],61001);
    }
    for(U i=0;i<5;++i) {
        check(SaveDC(held[i])==1,61002);
        check(RestoreDC(held[i],-1),61003);
        SetTextColor(held[i],0x101010u*(i+1));
    }
    int clip[4];
    IntersectClipRect(held[0],0,0,10,10);
    GetClipBox(held[1],clip);
    check(clip[2]>100 && clip[3]>100,61004);
    /* Separate state, shared framebuffer, including pixels under a window. */
    SetPixel(held[0],2,2,0x123456);
    check(GetPixel(held[1],2,2)==0x123456,61005);
    U fields[10]={0,(U)procedure,0,0,GetModuleHandleA(0),0,0,16,0,(U)"ScreenDCFocus"};
    check(RegisterClassA(fields),61006);
    U window=CreateWindowExA(0,"ScreenDCFocus","Retained screen DCs",0x10cf0000,
                             50,50,400,240,0,0,fields[4],0);
    check(window!=0,61007);
    box=CreateWindowExA(0,"BUTTON","Stable focus",0x50010003,30,50,180,24,
                       window,100,fields[4],0);
    check(box!=0,61008);
    SetFocus(box);
    SetPixel(held[1],60,60,0x123456);
    check(GetPixel(held[2],60,60)==0x123456,61009);
    check(SetTimer(window,1,1000,0),61010);
    U msg[7];
    while(GetMessageA(msg,0,0,0)>0) { TranslateMessage(msg);DispatchMessageA(msg); }
    ExitProcess(61099);
}
