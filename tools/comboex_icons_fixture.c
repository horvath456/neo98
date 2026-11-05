/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API void ExitProcess(U);
API U RegisterClassA(void *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U GetMessageA(void *,U,U,U);
API U DispatchMessageA(void *);
API U SetWindowTextA(U,const char *);
API U SendMessageA(U,U,U,U);
API U SetFocus(U);
API U GetDC(U);
API U ReleaseDC(U,U);
API U FillRect(U,const int *,U);
API U CreateCompatibleDC(U);
API U CreateCompatibleBitmap(U,int,int);
API U SelectObject(U,U);
API U CreateSolidBrush(U);
API U DeleteObject(U);
API U DeleteDC(U);
API U InitCommonControlsEx(void *);
API U ImageList_Create(int,int,U,int,int);
API U ImageList_AddMasked(U,U,U);
static U combo;
static U __stdcall proc(U h,U m,U wp,U lp)
{
    if(m==0x4e && lp){
        U *info=(U *)lp;
        if(info[2]==(U)-800 || info[2]==(U)-807){
            info[7]=info[8]=info[4];
            return 0;
        }
    }
    if(m==0x100 && (wp=='D'||wp=='C')){
        U inner=SendMessageA(combo,0x406,0,0);
        SendMessageA(inner,0x14f,wp=='D',0);
        SetWindowTextA(h,wp=='D'?"icons open":"icons closed");
        return 0;
    }
    if(m==2)ExitProcess(0);
    return DefWindowProcA(h,m,wp,lp);
}
void mainCRTStartup(void)
{
    U instance=GetModuleHandleA(0),init[]={8,0x200};
    U wc[]={0,(U)proc,0,0,instance,0,0,6,0,(U)"ComboExIcons"};
    if(!InitCommonControlsEx(init)||!RegisterClassA(wc))ExitProcess(1);
    U parent=CreateWindowExA(0,"ComboExIcons","icons ready",0x10cf0000,40,40,440,320,0,0,instance,0);
    combo=CreateWindowExA(0,"ComboBoxEx32","",0x50010042,10,25,300,200,parent,100,instance,0);
    U screen=GetDC(parent),dc=CreateCompatibleDC(screen),bitmap=CreateCompatibleBitmap(screen,48,16);
    U old=SelectObject(dc,bitmap);
    for(int i=0;i<3;++i){
        int rect[]={i*16,0,(i+1)*16,16};
        U brush=CreateSolidBrush(0xffu<<(8*i));
        FillRect(dc,rect,brush);DeleteObject(brush);
    }
    SelectObject(dc,old);DeleteDC(dc);ReleaseDC(parent,screen);
    U images=ImageList_Create(16,16,0x19,3,1);
    ImageList_AddMasked(images,bitmap,0xffffff);DeleteObject(bitmap);
    SendMessageA(combo,0x402,0,images);
    const char *labels[]={"Desktop","My Computer","Drive C"};
    for(U i=0;i<3;++i){
        U item[]={0x17,i,(U)labels[i],0,(U)-1,(U)-1,0,i,0};
        SendMessageA(combo,0x401,0,(U)item);
    }
    SendMessageA(combo,0x14e,0,0);
    SetFocus(parent);
    U msg[7];while(GetMessageA(msg,0,0,0))DispatchMessageA(msg);
    ExitProcess(0);
}
