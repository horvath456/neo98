/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CAMERA.EXE, the Camera program of NEO98 (Start > Programs > Accessories >
   Multimedia > Camera). It shows the picture of the host camera the way a
   Windows 98 program does: the capture window of AVICAP32 on the Video for
   Windows driver NEO98CAP.DRV (src/win16_capture.cpp). Where there is no
   picture it says why, from the camera state the host reports through
   \\.\NEO98HOST, instead of showing a black window. */
#include "guest_win32.h"
#include "../../src/host_devices_abi.h"
#define API(dll,ret,name,args) __declspec(dllimport) ret __stdcall name args
API(USER32,H,CreateMenu,(void));
API(USER32,H,LoadIconA,(H,S));
API(USER32,I,MoveWindow,(H,I,I,I,I,I));
API(SHELL32,I,ShellAboutA,(H,S,S,H));
API(AVICAP32,H,capCreateCaptureWindowA,(S,U,I,I,I,I,H,I));
#undef API

void *memset(void *p,I c,unsigned n){unsigned char *b=p;for(U i=0;i<n;++i)b[i]=(unsigned char)c;return p;}
void *memcpy(void *d,const void *s,unsigned n){for(U i=0;i<n;++i)((unsigned char*)d)[i]=((const unsigned char*)s)[i];return d;}
static U len(S s){U n=0;while(s[n])++n;return n;}
static void cat(char *d,S s){U n=len(d);while(*s)d[n++]=*s++;d[n]=0;}
static I same(S a,S b){while(*a&&*a==*b){++a;++b;}return *a==*b;}

/* WM_CAP_* from WM_USER (vfw.h). */
enum { CAP_CONNECT=0x40a, CAP_DISCONNECT=0x40b, CAP_GET_FORMAT=0x42c, CAP_SET_FORMAT=0x42d,
       CAP_PREVIEW=0x432, CAP_PREVIEW_RATE=0x434, CAP_SCALE=0x435 };
enum { MENU_EXIT=100, MENU_ABOUT=200 };

static H instance,window,capture,picture,words,bridge,icon;
static AH_Status status;
static U connected,showing=~0u;
static char message[640],shown[640];

/* The host's own words (UTF-8) in the ANSI code page. */
static S ansi(S text)
{
    static char converted[512];unsigned short wide[512];
    I n=MultiByteToWideChar(65001,0,text,-1,wide,512);
    if(n>0&&WideCharToMultiByte(0,0,wide,-1,converted,sizeof converted,0,0))return converted;
    return text;
}

/* Video for Windows finds the host camera as the capture driver NEO98CAP.DRV
   through SYSTEM.INI [drivers] msvideo=, as the INF of a capture card writes
   it, unless the line names another driver already. */
static void register_driver(void)
{
    char driver[64];GetPrivateProfileStringA("drivers","msvideo","",driver,sizeof driver,"system.ini");
    if(!driver[0])WritePrivateProfileStringA("drivers","msvideo","neo98cap.drv","system.ini");
}

static void layout(void)
{
    RECT r;GetClientRect(window,&r);
    if(capture)MoveWindow(capture,0,0,r.right,r.bottom,1);
    I top=r.bottom/2-70;if(top<8)top=8;
    MoveWindow(picture,(r.right-32)/2,top,32,32,1);
    MoveWindow(words,24,top+48,r.right-48>0?r.right-48:0,r.bottom-top-56>0?r.bottom-top-56:0,1);
}

/* The picture, or the reason there is none. */
static void show_picture(U on)
{
    if(on==showing&&(on||same(message,shown)))return;
    showing=on;U i=0;for(;message[i];++i)shown[i]=message[i];shown[i]=0;
    SetWindowTextA(words,shown);
    if(capture)ShowWindow(capture,on?5:0);
    ShowWindow(picture,on?0:5);ShowWindow(words,on?0:5);
}
static void say(S text,S detail)
{
    message[0]=0;cat(message,text);
    if(detail&&detail[0]){cat(message,"\r\n\r\n");cat(message,detail);}
    show_picture(0);
}

static void connect(void)
{
    if(connected)return;
    register_driver();
    if(!SendMessageA(capture,CAP_CONNECT,0,0))return;
    connected=1;
    /* 24-bit RGB at 640x480, the largest size the driver offers, scaled to the
       window. A BITMAPINFOHEADER: size, width, height, planes, bits,
       compression, image size. */
    DIB format;memset(&format,0,sizeof format);
    format.size=sizeof format;format.width=640;format.height=480;format.planes=1;format.bits=24;format.bytes=640*480*3;
    SendMessageA(capture,CAP_SET_FORMAT,sizeof format,(I)&format);
    SendMessageA(capture,CAP_SCALE,1,0);
    SendMessageA(capture,CAP_PREVIEW_RATE,66,0);
    SendMessageA(capture,CAP_PREVIEW,1,0);
}
static void disconnect(void)
{
    if(!connected)return;
    SendMessageA(capture,CAP_PREVIEW,0,0);SendMessageA(capture,CAP_DISCONNECT,0,0);connected=0;
}

static void update(void)
{
    U done=0;
    if(bridge==(H)~0u)
    {
        /* Without the host integration Camera is a plain capture window. */
        connect();
        if(connected)show_picture(1);else say("Windows could not find a camera.",0);
        return;
    }
    if(!DeviceIoControl(bridge,AH_IOCTL_STATUS,0,0,&status,sizeof status,&done,0))
    {say("Camera cannot read the state of the camera.",0);return;}
    if(!status.camera_id)
    {
        disconnect();
        say("No camera is connected to this computer.","Connect a camera to the computer. Camera shows its picture as soon as Windows finds it.");
        return;
    }
    connect();
    if(!connected){say("Windows could not connect to the camera driver.",0);return;}
    switch(status.camera_state)
    {
    case AH_CAMERA_RUNNING:show_picture(1);break;
    case AH_CAMERA_ASKING:
        say("Waiting for permission to use the camera.","The computer asks whether NEO98 may use the camera. Allow it there, and the picture appears here.");
        break;
    case AH_CAMERA_DENIED:
        say("NEO98 is not allowed to use the camera.",status.camera_message[0]?ansi(status.camera_message):"Allow NEO98 to use the camera in the settings of the computer. Then close Camera and open it again.");
        break;
    case AH_CAMERA_FAILED:
        say("The camera could not be started.",status.camera_message[0]?ansi(status.camera_message):"Close other programs that use the camera, and then open Camera again.");
        break;
    default:say("Starting the camera...",0);break;
    }
}

static I __stdcall proc(H hwnd,U msg,U wp,I lp)
{
    switch(msg)
    {
    case 0x05:layout();return 0;
    case 0x113:update();return 0;
    case 0x111:
        if((wp&65535)==MENU_EXIT)DestroyWindow(hwnd);
        else if((wp&65535)==MENU_ABOUT)ShellAboutA(hwnd,"Camera","Shows the picture of the camera of this computer.",icon);
        return 0;
    case 0x02:KillTimer(hwnd,1);disconnect();PostQuitMessage(0);return 0;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}

void mainCRTStartup(void)
{
    instance=GetModuleHandleA(0);icon=LoadIconA(instance,(S)1);
    bridge=CreateFileA("\\\\.\\NEO98HOST",0,0,0,3,0,0);
    CLASS c={0,proc,0,0,instance,icon,LoadCursorA(0,(S)32512),(H)16,0,"NEO98CameraViewer"};RegisterClassA(&c);
    H menu=CreateMenu(),file=CreatePopupMenu(),help=CreatePopupMenu();
    AppendMenuA(file,0,MENU_EXIT,"E&xit");AppendMenuA(menu,0x10,(U)file,"&File");
    AppendMenuA(help,0,MENU_ABOUT,"&About Camera");AppendMenuA(menu,0x10,(U)help,"&Help");
    /* A client area of 640x480, the size of the picture. */
    const U style=0x00cf0000u;RECT r={0,0,640,480};AdjustWindowRectEx(&r,style,1,0);
    window=CreateWindowExA(0,"NEO98CameraViewer","Camera",style,(I)0x80000000,(I)0x80000000,r.right-r.left,r.bottom-r.top,0,menu,instance,0);
    capture=capCreateCaptureWindowA("Camera",0x40000000u,0,0,640,480,window,1);
    /* The reason for no picture: the icon of Camera and a centred text. */
    picture=CreateWindowExA(0,"STATIC","",0x40000003u,0,0,32,32,window,(H)3,instance,0);SendMessageA(picture,0x170,(U)icon,0);
    words=CreateWindowExA(0,"STATIC","",0x40000081u,0,0,10,10,window,(H)2,instance,0);SendMessageA(words,0x30,(U)GetStockObject(17),0);
    layout();say("Starting the camera...",0);
    ShowWindow(window,5);update();SetTimer(window,1,250,0);
    MSG m;
    while(GetMessageA(&m,0,0,0)>0){TranslateMessage(&m);DispatchMessageA(&m);}
    if(bridge!=(H)~0u)CloseHandle(bridge);
    ExitProcess(0);
}
