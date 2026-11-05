/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef struct{int left,top,right,bottom;}R;typedef struct{int x,y;}S;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API U CreateCompatibleDC(U);API U DeleteDC(U);API U CreateDIBSection(U,const void*,U,U**,U,U);API U SelectObject(U,U);API U DeleteObject(U);API U SetTextColor(U,U);API int SetBkMode(U,int);API U GetTextMetricsA(U,void*);API U GetTextExtentPoint32A(U,const char*,int,S*);API int DrawTextA(U,const char*,int,R*,U);API int DrawTextExA(U,char*,int,R*,U,U*);API int SaveDC(U);API U RestoreDC(U,int);
static void check(int b,U code){if(!b)ExitProcess(code);}
void run_draw_text_test(void){
 U dc=CreateCompatibleDC(0),*pixels=0,bmi[10]={40,160,(U)-96,0x200001};check(dc!=0,23500);
 U bitmap=CreateDIBSection(dc,bmi,0,&pixels,0,0);check(bitmap&&pixels,23501);U old=SelectObject(dc,bitmap);U tm[14]={};check(GetTextMetricsA(dc,tm),23502);int h=tm[0];check(h>0&&h<32,23503);
 R r={4,5,150,90};U p[5]={20,8,0,0,99};char lines[]="A\r\nB";check(DrawTextExA(dc,lines,-1,&r,0x2e50,p)==2*(h+(int)tm[4]),23504);check(r.bottom==5+2*(h+(int)tm[4])&&p[4]==4,23505);
 r=(R){4,5,150,90};check(DrawTextA(dc,"A\n\rB",-1,&r,0x400)==2*h,23506);
 r=(R){4,5,150,90};check(DrawTextA(dc,"A\r\n",-1,&r,0x400)==2*h,23507);
 r=(R){4,5,150,90};check(DrawTextA(dc,"A\r\n",-1,&r,0x2400)==h,23508);
 S one={};check(GetTextExtentPoint32A(dc,"A",1,&one),23509);r=(R){0,0,one.x,90};p[4]=99;char word[]="AAAA";check(DrawTextExA(dc,word,-1,&r,0x2410,p)==4*h&&p[4]==4,23510);
 r=(R){0,0,160,96};p[2]=3;p[3]=7;p[4]=99;char text[]="A";check(DrawTextExA(dc,text,-1,&r,0x420,p)==h&&r.right==10+one.x&&p[4]==1,23511);
 r=(R){2,3,77,88};p[4]=99;check(DrawTextExA(dc,text,0,&r,0x400,p)==1&&r.right==77&&r.bottom==88&&p[4]==99,23512);
 p[0]=19;check(!DrawTextExA(dc,text,-1,&r,0x400,p),23513);p[0]=20;p[2]=p[3]=0;
 r=(R){0,0,160,96};check(!DrawTextA(dc,"ellipsis",-1,&r,0x8000)&&GetLastError()==50,23514);
 for(U i=0;i<160*96;++i)pixels[i]=0xffffff;SetTextColor(dc,0xff);SetBkMode(dc,1);
 r=(R){8,4,75,80};char draw[]="&Alpha beta\r\nGamma";check(DrawTextExA(dc,draw,-1,&r,0x2a50,p)>h,23515);
 U changed=0;for(U y=0;y<96;++y)for(U x=0;x<160;++x){U c=pixels[y*160+x]&0xffffff;if(c!=0xffffff){++changed;check(x>=8&&x<75&&y>=4&&y<80,23516);}}
 check(changed>8&&p[4]==18,23517);
 /* DT_END_ELLIPSIS without DT_SINGLELINE (the history bar of Explorer): 1224 draws the last line with the ellipsis. */
 for(U i=0;i<160*96;++i)pixels[i]=0xffffff;
 r=(R){8,4,150,4+h};check(DrawTextA(dc,"Today",5,&r,0x8804)==h,23519);
 changed=0;for(U y=0;y<96;++y)for(U x=0;x<160;++x)if((pixels[y*160+x]&0xffffff)!=0xffffff){++changed;check(x>=8&&x<150&&y>=4&&y<4+(U)h,23520);}
 check(changed>8,23521);
 r=(R){8,40,8+4*one.x,40+2*h};check(DrawTextA(dc,"AAAAAAAAAA\r\nAAAAAAAAAA",-1,&r,0x8800)==2*h,23522);
 check(SelectObject(dc,old)==bitmap&&DeleteObject(bitmap)&&DeleteDC(dc),23518);
}
#ifdef DRAW_TEXT_STANDALONE
void mainCRTStartup(void){run_draw_text_test();ExitProcess(0);}
#endif
