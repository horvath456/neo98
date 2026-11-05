/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* bitmap wallpaper, clipping, replacement and lifetime across processes. */
typedef unsigned U;
void *memset(void *dst,int value,unsigned count){unsigned char *p=dst;while(count--)*p++=(unsigned char)value;return dst;}
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateFileA(const char *,U,U,void *,U,U,U);
API U WriteFile(U,const void *,U,U *,void *);
API U CloseHandle(U);
API U RegCreateKeyA(U,const char *,U *);
API U RegSetValueExA(U,const char *,U,U,const void *,U);
API U RegCloseKey(U);
API U SystemParametersInfoA(U,U,void *,U);
API U PaintDesktop(U);
API U GetSystemMetrics(U);
API U GetSysColor(U);
API U CreateCompatibleDC(U);
API U CreateBitmap(int,int,U,U,const void *);
API U SelectObject(U,U);
API U DeleteObject(U);
API U DeleteDC(U);
API U GetPixel(U,int,int);
API U SetPixel(U,int,int,U);
API U SaveDC(U);
API U RestoreDC(U,int);
API U IntersectClipRect(U,int,int,int,int);
API U GetModuleFileNameA(U,char *,U);
API char *GetCommandLineA(void);
API U CreateProcessA(const char *,char *,void *,void *,U,U,void *,const char *,void *,void *);
API U WaitForSingleObject(U,U);
API U GetExitCodeProcess(U,U *);
static char path[]="C:\\WALLTEST.BMP";
static void require(U ok,U code) { if(!ok) ExitProcess(code); }
static void setting(const char *name,const char *text) {
 U key=0,n=0;while(text[n])++n;
 require(!RegCreateKeyA(0x80000001u,"Control Panel\\Desktop",&key),10);
 require(!RegSetValueExA(key,name,0,1,text,n+1),11);RegCloseKey(key);
}
void mainCRTStartup(void) {
 char *cmd=GetCommandLineA();U i;
 for(i=0;cmd[i];++i) if(cmd[i]=='!' && cmd[i+1]=='!') {
  setting("TileWallpaper","1");
  require(SystemParametersInfoA(20,0,path,0),12);
  ExitProcess(0);
 }
 /* 32 x 32 bottom-up BMP repeating red/green and blue/white pairs.
    This exercises several tiles without hundreds of thousands of blits. */
 static unsigned char bmp[54+32*32*3]={
  'B','M',0x36,0x0c,0,0,0,0,0,0,54,0,0,0,40,0,0,0,32,0,0,0,32,0,0,0,
  1,0,24,0,0,0,0,0,0,0x0c,0,0};
 static const unsigned char colors[4][3]={{0,0,255},{0,255,0},{255,0,0},{255,255,255}};
 for(U y=0;y<32;++y)for(U x=0;x<32;++x)for(U c=0;c<3;++c)
  bmp[54+(y*32+x)*3+c]=colors[((31-y)&1)*2+(x&1)][c];
 U file=CreateFileA(path,0x40000000,0,0,2,0,0),written=0;
 require(file!=~0u && WriteFile(file,bmp,sizeof bmp,&written,0)&&written==sizeof bmp,20);CloseHandle(file);
 U width=GetSystemMetrics(0),height=GetSystemMetrics(1),dc=CreateCompatibleDC(0);
 U bitmap=CreateBitmap(width,height,1,32,0),old=SelectObject(dc,bitmap);
 require(dc&&bitmap&&old,21);
 setting("TileWallpaper","1");setting("WallpaperStyle","0");
 require(SystemParametersInfoA(20,0,path,0),22);
 require(PaintDesktop(dc),23);
 require(GetPixel(dc,0,0)==0xff && GetPixel(dc,1,0)==0xff00,24);
 require(GetPixel(dc,0,1)==0xff0000 && GetPixel(dc,1,1)==0xffffff,25);
 require(GetPixel(dc,100,100)==0xff,26);
 SetPixel(dc,0,0,0xff00ff);SaveDC(dc);IntersectClipRect(dc,4,4,8,8);
 require(PaintDesktop(dc),27);
 RestoreDC(dc,-1);
 require(GetPixel(dc,0,0)==0xff00ff&&GetPixel(dc,4,4)==0xff,27);
 setting("TileWallpaper","0");
 require(SystemParametersInfoA(20,0,path,0)&&PaintDesktop(dc),28);
 require(GetPixel(dc,(int)(width-32)/2,(int)(height-32)/2)==0xff,29);
 require(GetPixel(dc,0,0)==GetSysColor(1),30);
 require(!SystemParametersInfoA(20,0,"C:\\ABSENT-WALLPAPER.BMP",0),31);
 require(PaintDesktop(dc)&&GetPixel(dc,(int)(width-32)/2,(int)(height-32)/2)==GetSysColor(1),32);
 setting("Wallpaper",path);setting("TileWallpaper","1");
 require(SystemParametersInfoA(20,0,0,0)&&PaintDesktop(dc)&&GetPixel(dc,0,0)==0xff,33);
 require(SystemParametersInfoA(20,0,"",0)&&PaintDesktop(dc)&&GetPixel(dc,0,0)==GetSysColor(1),34);
 /* A child changes the system setting and exits. Its bitmap must survive. */
 char exe[260]={0},command[280]={0};U startup[17]={sizeof startup},process[4]={0},code=~0u;
 require(GetModuleFileNameA(0,exe,sizeof exe),35);
 command[0]='"';for(i=0;exe[i];++i)command[i+1]=exe[i];
 command[++i]='"';command[++i]=' ';command[++i]='!';command[++i]='!';command[++i]=0;
 require(CreateProcessA(exe,command,0,0,0,0,0,0,startup,process),36);
 require(WaitForSingleObject(process[0],10000)==0&&GetExitCodeProcess(process[0],&code)&&!code,37);
 CloseHandle(process[0]);CloseHandle(process[1]);
 require(PaintDesktop(dc)&&GetPixel(dc,0,0)==0xff&&GetPixel(dc,0,1)==0xff0000,38);
 SystemParametersInfoA(20,0,"",0);
 SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ExitProcess(0);
}
