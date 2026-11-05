/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Win98 USER32 packs window-position coordinates into signed WORDs. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
void *memset(void *p, int c, unsigned n) {
    volatile unsigned char *q=p; unsigned i;
    for(i=0;i<n;++i) q[i]=(unsigned char)c;
    return p;
}
static int recording, changing, seen[4];
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if(m==WM_WINDOWPOSCHANGING && recording) {
        WINDOWPOS *p=(WINDOWPOS*)l;
        seen[0]=p->x;seen[1]=p->y;seen[2]=p->cx;seen[3]=p->cy;
        if(changing) {p->x=65538;p->y=-65533;p->cx=65596;p->cy=65586;}
    }
    return DefWindowProcA(h,m,w,l);
}
void mainCRTStartup(void) {
    const int cases[][4]={{2,3,80,60},{65538,65539,65596,65586},
        {32768,-32769,32768,-32769},{-1,-2,-1,-2},
        {0x80000000,0xffffffff,0x10050,0x1003c},
        {0x12340000,0x56780000,0x12340000,0x56780000},
        {2,3,80,60}};
    WNDCLASSA cls={0};unsigned mode,i;DWORD done;char line[256];
    HANDLE file=CreateFileA("C:\\POSWORD.TXT",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(0);cls.lpszClassName="PositionWords98";
    if(!RegisterClassA(&cls)) ExitProcess(2);
    for(mode=0;mode<3;++mode) for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        HWND h=CreateWindowExA(0,cls.lpszClassName,"",WS_POPUP,10,20,80,60,0,0,cls.hInstance,0);
        RECT r;BOOL ok;const int *p=cases[i];
        if(!h) ExitProcess(3);
        seen[0]=seen[1]=seen[2]=seen[3]=0x7777;changing=(i==6);recording=1;
        if(mode==0) ok=MoveWindow(h,p[0],p[1],p[2],p[3],FALSE);
        else if(mode==1) ok=SetWindowPos(h,0,p[0],p[1],p[2],p[3],SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
        else {
            HDWP batch=BeginDeferWindowPos(1);
            batch=DeferWindowPos(batch,h,0,p[0],p[1],p[2],p[3],SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
            ok=batch?EndDeferWindowPos(batch):FALSE;
        }
        recording=0;GetWindowRect(h,&r);
        wsprintfA(line,"%u %u ok=%u changing=%d,%d,%d,%d rect=%ld,%ld,%ld,%ld\r\n",mode,i,!!ok,
            seen[0],seen[1],seen[2],seen[3],r.left,r.top,r.right,r.bottom);
        WriteFile(file,line,lstrlenA(line),&done,0);DestroyWindow(h);
    }
    CloseHandle(file);ExitProcess(0);
}
