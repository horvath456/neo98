// SPDX-License-Identifier: GPL-2.0-or-later
// Guest program for the SystemParametersInfo write branches, the work-area
// consumption in maximize and the HWND_BROADCAST WM_SETTINGCHANGE delivery.
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
struct WndClass{U style,proc,cbcls,cbwin,instance,icon,cursor,brush,menu,name;};
struct Rect{int left,top,right,bottom;};
API void S ExitProcess(U);API U S GetLastError(void);
API U S RegisterClassA(struct WndClass*);API U S UnregisterClassA(const char*,U);
API U S CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U S DestroyWindow(U);API U S DefWindowProcA(U,U,U,U);API U S SendMessageA(U,U,U,U);
API U S ShowWindow(U,int);API U S GetWindowRect(U,struct Rect*);API int S GetSystemMetrics(int);
API U S SystemParametersInfoA(U,U,void*,U);
API U S PostMessageA(U,U,U,U);API U S PeekMessageA(void*,U,U,U,U);API U S DispatchMessageA(const void*);
API U S RegOpenKeyExA(U,const char*,U,U,U*);API U S RegQueryValueExA(U,const char*,U*,U*,void*,U*);API U S RegCloseKey(U);

static U main_window,setting_changes,setting_action,minmax_seen,setting_name_seen,placing_seen,placing_edit;

static U S wndproc(U h,U m,U w,U l)
{
 if(m==0x1a){++setting_changes;setting_action=w;if(l&&*(const char*)l)++setting_name_seen;return 0;}
 if(m==0x24){++minmax_seen;return DefWindowProcA(h,m,w,l);}
 // WM_WINDOWPOSCHANGING: hwnd, after, x, y, cx, cy, flags. A maximize asks
 // here too (USER places through DeferWindowPos).
 if(m==0x46){U r=DefWindowProcA(h,m,w,l);int*p=(int*)l;++placing_seen;
  if(placing_edit){p[2]=7;p[3]=9;p[4]=300;p[5]=200;p[6]&=~3;}return r;}
 return DefWindowProcA(h,m,w,l);
}
static void check(int ok,U code){if(!ok)ExitProcess(code);}

U run_spi_write_test(void)
{
 struct WndClass klass={0};struct Rect r,frame;int i;
 unsigned char metrics[340],again[340];
 klass.proc=(U)wndproc;klass.instance=0x400000;klass.name=(U)"Neo98SpiWrite";
 check(RegisterClassA(&klass)!=0,30200);
 main_window=CreateWindowExA(0,"Neo98SpiWrite","spi",0x00cf0000u,20,20,200,150,0,0,0x400000,0);
 check(main_window!=0,30201);

 // The implemented read branch still reports the full monitor.
 check(SystemParametersInfoA(48,0,&r,0)!=0,30202);
 check(r.left==0&&r.top==0&&r.right==800&&r.bottom==600,30203);
 const int full_before=GetSystemMetrics(17),max_before=GetSystemMetrics(62);

 // SPI_SETWORKAREA with SPIF_SENDCHANGE reserves 28 pixels for a taskbar.
 {struct Rect work={0,0,800,572};
  check(SystemParametersInfoA(47,0,&work,2)!=0,30204);}
 check(setting_changes==1,30205);
 check(setting_action==47,30206);
 check(SystemParametersInfoA(48,0,&r,0)!=0,30207);
 check(r.right==800&&r.bottom==572,30208);
 check(GetSystemMetrics(17)==full_before-28,30209);
 check(GetSystemMetrics(62)==max_before-28,30210);

 // A maximized window now uses the work area, not the whole screen.
 ShowWindow(main_window,3);
 check(minmax_seen>=1,30212);
 check(GetWindowRect(main_window,&frame)!=0,30213);
 check(frame.left==frame.top&&frame.left<0,30214);
 check(frame.right==800-frame.left,30215);
 check(frame.bottom==572-frame.top,30216);
 check(frame.bottom<600,30217);

 // A work area that leaves the monitor is refused and changes nothing.
 {struct Rect bad={0,0,900,600};
  check(SystemParametersInfoA(47,0,&bad,0)==0,30218);}
 check(SystemParametersInfoA(48,0,&r,0)!=0&&r.right==800&&r.bottom==572,30219);

 // A NULL rectangle resets the work area to the monitor rectangle.
 check(SystemParametersInfoA(47,0,0,0)!=0,30220);
 check(SystemParametersInfoA(48,0,&r,0)!=0&&r.bottom==600,30221);

 // A work area whose top is not the monitor's (a taskbar at the top, the
 // bars of a phone shell): the maximized frame lies inside it, one frame
 // beyond each edge (USER compares with rcMonitor).
 ShowWindow(main_window,9);
 {struct Rect inset={0,44,800,560};
  check(SystemParametersInfoA(47,0,&inset,0)!=0,30260);}
 ShowWindow(main_window,3);
 check(GetWindowRect(main_window,&frame)!=0,30261);
 check(frame.left<0&&frame.right==800-frame.left,30262);
 check(frame.top==44+frame.left,30263);
 check(frame.bottom==560-frame.left,30264);
 ShowWindow(main_window,9);
 check(SystemParametersInfoA(47,0,0,0)!=0,30265);
 // The window is asked before a maximize is placed and may move the place.
 placing_seen=0;ShowWindow(main_window,3);
 check(placing_seen>=1,30266);
 ShowWindow(main_window,9);
 placing_edit=1;ShowWindow(main_window,3);placing_edit=0;
 check(GetWindowRect(main_window,&frame)!=0,30267);
 check(frame.left==7&&frame.top==9&&frame.right==307&&frame.bottom==209,30268);
 ShowWindow(main_window,9);

 // Writing the unchanged work area succeeds without a broadcast.
 {U before=setting_changes;struct Rect same={0,0,800,600};
  check(SystemParametersInfoA(47,0,&same,2)!=0,30222);
  check(setting_changes==before,30223);}

 // SPI_SETNONCLIENTMETRICS round trip through the shared settings.
 for(i=0;i<340;++i)metrics[i]=0;
 *(U*)metrics=340;
 check(SystemParametersInfoA(41,340,metrics,0)!=0,30224);
 *(int*)(metrics+0x14)=*(int*)(metrics+0x14)+2; // iCaptionHeight
 check(SystemParametersInfoA(42,340,metrics,3)!=0,30225);
 for(i=0;i<340;++i)again[i]=0;
 *(U*)again=340;
 check(SystemParametersInfoA(41,340,again,0)!=0,30226);
 check(*(int*)(again+0x14)==*(int*)(metrics+0x14),30227);
 // A rejected cbSize keeps the original failure.
 *(U*)again=12;
 check(SystemParametersInfoA(42,340,again,0)==0,30228);

 // SPI_SETICONTITLELOGFONT broadcasts with the original "Desktop" section name.
 {unsigned char font[60],back[60];
  check(SystemParametersInfoA(31,60,font,0)!=0,30232);
  *(int*)(font+16)=700; // lfWeight
  setting_changes=0;setting_action=0;setting_name_seen=0;
  check(SystemParametersInfoA(34,60,font,3)!=0,30233);
  check(setting_changes==1&&setting_action==34,30234);
  check(setting_name_seen==1,30235);
  check(SystemParametersInfoA(31,60,back,0)!=0,30236);
  check(*(int*)(back+16)==700,30237);
  // A wrong uiParam keeps the original rejection.
  check(SystemParametersInfoA(34,8,font,0)==0,30238);}

 // HWND_BROADCAST reaches this top-level window.
 setting_changes=0;setting_action=0;
 check(SendMessageA(0xffffffffu,0x1a,4242,0)!=0,30229);
 check(setting_changes==1&&setting_action==4242,30230);
 check(setting_name_seen==1,30231); // A null lParam carries no section name.
 // USER32 narrows a window handle to 16 bits, so 0FFFFh is HWND_BROADCAST
 // as well, for SendMessage and PostMessage (the Jugglor launcher).
 setting_changes=0;setting_action=0;
 check(SendMessageA(0xffffu,0x1a,4243,0)!=0,30239);
 check(setting_changes==1&&setting_action==4243,30240);
 {U msg[7];setting_changes=0;setting_action=0;
  check(PostMessageA(0xffffu,0x1a,4244,0)!=0,30241);
  while(PeekMessageA(msg,0,0,0,1))DispatchMessageA(msg);
  check(setting_changes==1&&setting_action==4244,30242);}

 // The screen saver and the power features of the display: the
 // timeout keeps its word's sign, "active" flips it, the power pair needs the
 // display's power states and writes HKCU\Control Panel\Desktop every time.
 {U value=0,key=0,type=0,size=16;char text[16]={0};
  check(SystemParametersInfoA(15,120,0,3)!=0,30250);
  check(setting_action==15,30251);
  check(SystemParametersInfoA(14,0,&value,0)!=0&&value==120,30252);
  check(SystemParametersInfoA(17,1,0,0)!=0,30253);
  check(SystemParametersInfoA(16,0,&value,0)!=0&&value==1,30254);
  check(SystemParametersInfoA(17,0,0,0)!=0,30255);
  check(SystemParametersInfoA(16,0,&value,0)!=0&&value==0,30256);
  check(SystemParametersInfoA(14,0,&value,0)!=0&&value==120,30257);
  check(SystemParametersInfoA(0x51,300,0,0)!=0,30258);
  check(SystemParametersInfoA(0x55,1,0,0)!=0,30259);
  check(SystemParametersInfoA(0x4f,0,&value,0)!=0&&value==300,30260);
  check(SystemParametersInfoA(0x53,0,&value,0)!=0&&value==1,30261);
  check(SystemParametersInfoA(0x52,600,0,0)!=0&&SystemParametersInfoA(0x56,0,0,0)!=0,30262);
  check(SystemParametersInfoA(0x50,0,&value,0)!=0&&value==600,30263);
  check(SystemParametersInfoA(0x54,0,&value,0)!=0&&value==0,30264);
  check(SystemParametersInfoA(0x61,1,&value,0)!=0&&value==0,30265);
  check(SystemParametersInfoA(0x61,0,&value,0)!=0&&value==1,30266);
  check(RegOpenKeyExA(0x80000001u,"Control Panel\\Desktop",0,0x20019,&key)==0,30267);
  check(RegQueryValueExA(key,"ScreenSaveLowPowerTimeout",0,&type,text,&size)==0&&type==1,30268);
  check(text[0]=='3'&&text[1]=='0'&&text[2]=='0'&&!text[3],30269);
  size=16;check(RegQueryValueExA(key,"ScreenSaveTimeOut",0,&type,text,&size)==0,30270);
  check(text[0]=='1'&&text[1]=='2'&&text[2]=='0'&&!text[3],30271);
  RegCloseKey(key);}

 DestroyWindow(main_window);
 UnregisterClassA("Neo98SpiWrite",0x400000);
 return 0;
}
#ifdef SPI_WRITE_STANDALONE
void mainCRTStartup(void){ExitProcess(run_spi_write_test());}
#endif
