/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "host_devices/guest_win32.h"
#include "../src/host_devices_abi.h"
#define MORE(ret,name,args) __declspec(dllimport) ret __stdcall name args
MORE(H,LoadLibraryA,(S));MORE(H,GetProcAddress,(H,S));
MORE(U,GetTickCount,(void));MORE(void,Sleep,(U));
MORE(I,ReadFile,(H,void*,U,U*,void*));MORE(I,WriteFile,(H,const void*,U,U*,void*));
MORE(U,joyGetNumDevs,(void));MORE(U,joyGetDevCapsA,(U,void*,U));MORE(U,joyGetPos,(U,void*));MORE(U,joyGetPosEx,(U,void*));
MORE(I,WritePrivateProfileStringA,(S,S,S,S));
MORE(U,joyGetThreshold,(U,U*));MORE(U,joySetThreshold,(U,U));MORE(U,joySetCapture,(H,U,U,I));MORE(U,joyReleaseCapture,(U));
static U moves;
static H device;static AH_Status status;static AH_Frame frame;static U check,changes;
void *memset(void *p,I c,unsigned n){for(U i=0;i<n;++i)((char*)p)[i]=(char)c;return p;}
static void verify(I okay,U line){if(!okay)ExitProcess(line);}
#define expect(okay) verify(okay,__LINE__)
static void refresh(void){U done;expect(DeviceIoControl(device,AH_IOCTL_STATUS,0,0,&status,sizeof status,&done,0)&&done==sizeof status);}
static void action(U code,U flags)
{
    AH_Request r;memset(&r,0,sizeof r);r.version=AH_VERSION;r.action=code;r.id=99;r.flags=flags;U done;
    expect(DeviceIoControl(device,AH_IOCTL_ACTION,&r,sizeof r,0,0,&done,0));
    U start=GetTickCount();do{Sleep(5);refresh();expect(GetTickCount()-start<4000);}while(status.busy);
    expect(!status.error);
}
/* Video for Windows: AVICAP32 and its 16-bit AVICAP reach NEO98CAP.DRV
   through MSVIDEO; a grabbed frame saved as a DIB carries the fake camera's
   red left half (src/win16_capture.cpp). */
static void capture(void)
{
    expect(WritePrivateProfileStringA("drivers","msvideo","neo98cap.drv","system.ini"));
    H avicap=LoadLibraryA("AVICAP32.DLL");expect(avicap!=0);
    typedef I (__stdcall *DESC)(unsigned short,char*,I,char*,I);typedef H (__stdcall *CREATE)(S,U,I,I,I,I,H,I);
    DESC describe=(DESC)GetProcAddress(avicap,"capGetDriverDescriptionA");CREATE create=(CREATE)GetProcAddress(avicap,"capCreateCaptureWindowA");
    expect(describe&&create);
    char name[80],version[80];name[0]=version[0]=0;expect(describe(0,name,sizeof name,version,sizeof version));
    expect(name[0]=='N'&&name[6]=='C'); /* "NEO98 Camera" from the version of NEO98CAP.DRV */
    H window=create("capture",0,0,0,160,120,0,0);expect(window!=0);
    expect(SendMessageA(window,0x40a,0,0)); /* WM_CAP_DRIVER_CONNECT */
    U format[10];expect(SendMessageA(window,0x42c,sizeof format,(I)format)==40); /* WM_CAP_GET_VIDEOFORMAT */
    expect(format[1]==320&&format[2]==240&&(format[3]>>16)==24);
    format[1]=160;format[2]=120;format[5]=480*120;expect(SendMessageA(window,0x42d,40,(I)format)); /* WM_CAP_SET_VIDEOFORMAT */
    unsigned char pixel[3]={0,0,0};
    for(U tries=0;tries<100&&pixel[2]!=255;++tries)
    {
        Sleep(20);expect(SendMessageA(window,0x43c,0,0)); /* WM_CAP_GRAB_FRAME */
        expect(SendMessageA(window,0x419,0,(I)"C:\\GRAB.BMP")); /* WM_CAP_FILE_SAVEDIBA */
        H file=CreateFileA("C:\\GRAB.BMP",0x80000000,1,0,3,0,0);expect(file!=(H)~0u);
        unsigned char head[54];U done=0;expect(ReadFile(file,head,54,&done,0)&&done==54&&head[0]=='B'&&head[1]=='M');
        expect(*(I*)(head+18)==160&&*(I*)(head+22)==120&&*(unsigned short*)(head+28)==24);
        expect(ReadFile(file,pixel,3,&done,0)&&done==3);CloseHandle(file);
    }
    expect(pixel[0]==0&&pixel[1]==0&&pixel[2]==255);
    expect(SendMessageA(window,0x40b,0,0)); /* WM_CAP_DRIVER_DISCONNECT */
    DestroyWindow(window);
}
/* DirectInput finds the host's controllers through the joystick
   configuration the runtime writes and the axes DINPUT.VXD reports
   (src/x86_mm_devices.cpp, src/x86_dinput_vxd.cpp), and reads them through
   its joyGetPosEx emulation. */
typedef struct { U size; unsigned char guid[16],product[16]; U type; char name[260],product_name[260]; } DEVINST;
static U joysticks;static unsigned char first_joystick[16];
static I __stdcall each_joystick(const DEVINST *d,void *ref){if(!joysticks)for(U i=0;i<16;++i)first_joystick[i]=d->guid[i];++joysticks;return 1;}
static const unsigned char guid_x[16]={0xE0,0x02,0x6D,0xA3,0xF3,0xC9,0xCF,0x11,0xBF,0xC7,0x44,0x45,0x53,0x54,0,0};
static const unsigned char guid_y[16]={0xE1,0x02,0x6D,0xA3,0xF3,0xC9,0xCF,0x11,0xBF,0xC7,0x44,0x45,0x53,0x54,0,0};
static const unsigned char guid_pov[16]={0xF2,0x02,0x6D,0xA3,0xF3,0xC9,0xCF,0x11,0xBF,0xC7,0x44,0x45,0x53,0x54,0,0};
static const unsigned char iid_device2[16]={0x82,0xE6,0x44,0x59,0x2E,0xC9,0xCF,0x11,0xBF,0xC7,0x44,0x45,0x53,0x54,0,0};
typedef struct{const void *guid;U offset,type,flags;}OBJ;
typedef struct{U size,object_size,flags,data_size,count;const OBJ *objects;}FORMAT;
#define CALL(object,slot,type) ((type)(*(void***)(object))[slot])
static const unsigned char guid_constant[16]={0x20,0x1C,0x54,0x13,0x33,0x8E,0xD0,0x11,0x9A,0xD0,0x00,0xA0,0xC9,0xA0,0x6E,0x35};
static void directinput(H window)
{
    H dinput=LoadLibraryA("DINPUT.DLL");expect(dinput!=0);
    typedef I (__stdcall *CREATE)(H,U,void***,void*);CREATE create=(CREATE)GetProcAddress(dinput,"DirectInputCreateA");expect(create!=0);
    void **di=0;expect(create(GetModuleHandleA(0),0x0700,&di,0)>=0&&di);
    typedef I (__stdcall *ENUM)(void*,U,void*,void*,U);expect(CALL(di,4,ENUM)(di,4,(void*)each_joystick,0,1)>=0); /* DIDEVTYPE_JOYSTICK, attached */
    expect(joysticks==2);
    void **dev=0;typedef I (__stdcall *CREATEDEV)(void*,const void*,void***,void*);expect(CALL(di,3,CREATEDEV)(di,first_joystick,&dev,0)>=0&&dev);
    U caps[11];caps[0]=44;for(U i=1;i<11;++i)caps[i]=0;
    typedef I (__stdcall *CAPS)(void*,U*);expect(CALL(dev,3,CAPS)(dev,caps)>=0);
    expect(caps[3]==4&&caps[4]==12&&caps[5]==1); /* axes, buttons, POVs */
    expect(caps[1]&0x100); /* DIDC_FORCEFEEDBACK: the first stick rumbles */
    static OBJ objects[7];
    objects[0].guid=guid_x;objects[0].offset=0;objects[0].type=0x80ffff03;objects[0].flags=0x100;
    objects[1].guid=guid_y;objects[1].offset=4;objects[1].type=0x80ffff03;objects[1].flags=0x100;
    objects[2].guid=guid_pov;objects[2].offset=32;objects[2].type=0x80ffff10;objects[2].flags=0;
    for(U i=0;i<4;++i){objects[3+i].guid=0;objects[3+i].offset=48+i;objects[3+i].type=0x80ffff0c;objects[3+i].flags=0;}
    static const FORMAT format={24,16,1,80,7,objects};
    typedef I (__stdcall *SETFORMAT)(void*,const FORMAT*);expect(CALL(dev,11,SETFORMAT)(dev,&format)>=0);
    typedef I (__stdcall *LEVEL)(void*,H,U);expect(CALL(dev,13,LEVEL)(dev,window,1|8)>=0); /* exclusive, background */
    typedef I (__stdcall *ONE)(void*);expect(CALL(dev,7,ONE)(dev)>=0);
    void **dev2=0;typedef I (__stdcall *QI)(void*,const void*,void***);expect(CALL(dev,0,QI)(dev,iid_device2,&dev2)>=0&&dev2);
    expect(CALL(dev2,25,ONE)(dev2)>=0); /* Poll */
    I state[20];for(U i=0;i<20;++i)state[i]=0x5555;
    typedef I (__stdcall *GET)(void*,U,void*);expect(CALL(dev,9,GET)(dev,80,state)>=0);
    const unsigned char *buttons=(const unsigned char*)(state+12);
    /* The virtual stick holds X left, Y right, button 2 and the hat up. */
    expect(state[0]<27000&&state[1]>38000&&buttons[2]==0x80&&!buttons[0]&&state[8]==0);
    /* DINPUT applies the dead zone from JoystickUserValues, five percent
       when the value is missing as on Windows 98: raw 20423 is 12344 left
       of the centre and becomes 32767-(12344-1638)*32767/(32767-1638). */
    U zone[5]={20,16,0,1,~0u};typedef I (__stdcall *PROP)(void*,const void*,U*);
    expect(CALL(dev,5,PROP)(dev,(const void*)5,zone)>=0&&zone[4]==500); /* DIPROP_DEADZONE, X by offset */
    expect(state[0]==21498);
    expect(CALL(dev,8,ONE)(dev)>=0);zone[4]=0;expect(CALL(dev,6,PROP)(dev,(const void*)5,zone)>=0);
    expect(CALL(dev,7,ONE)(dev)>=0&&CALL(dev2,25,ONE)(dev2)>=0&&CALL(dev,9,GET)(dev,80,state)>=0&&state[0]==20423);
    /* Force feedback through NEO98FF.DLL: a constant force of 60 percent on
       X for one second, started, running, stopped. */
    static U axis_x=0;static I direction=0;static I constant=6000;
    U effect[13]={52,0x12,1000000,0,10000,0xffffffffu,0,1,(U)&axis_x,(U)&direction,0,4,(U)&constant};
    void **force=0;typedef I (__stdcall *MAKE)(void*,const void*,const U*,void***,void*);
    expect(CALL(dev2,18,MAKE)(dev2,guid_constant,effect,&force,0)>=0&&force);
    typedef I (__stdcall *START)(void*,U,U);expect(CALL(force,7,START)(force,1,0)>=0);
    Sleep(100);U playing=0;typedef I (__stdcall *STATUS)(void*,U*);expect(CALL(force,9,STATUS)(force,&playing)>=0&&(playing&1));
    expect(CALL(force,8,ONE)(force)>=0);Sleep(100);
    CALL(force,2,ONE)(force);
    CALL(dev2,2,ONE)(dev2);CALL(dev,8,ONE)(dev);CALL(dev,2,ONE)(dev);CALL(di,2,ONE)(di);
}
static I __stdcall proc(H hwnd,U msg,U wp,I lp)
{if(msg==0x3a0)++moves;if(msg==0x219&&(wp==0x8000||wp==0x8004))++changes;return DefWindowProcA(hwnd,msg,wp,lp);}
void mainCRTStartup(void)
{
    U security[3]={12,0,0};H key=0;U created=0;
    expect(!RegCreateKeyExA((H)0x80000001,"Software\\NEO98HardwareTest",0,0,0,0xf003f,security,&key,&created));expect(!RegCloseKey(key));
    AH_Power power;expect(GetSystemPowerStatus(&power));expect(!GetSystemPowerStatus(0)&&GetLastError()==87);
    device=CreateFileA("\\\\.\\NEO98HOST",0,0,0,3,0,0);expect(device!=(H)~0u);refresh();
    expect(status.version==AH_VERSION&&power.percent==status.power.percent&&power.ac==status.power.ac);
    U done=0;expect(!DeviceIoControl(device,AH_IOCTL_STATUS,0,0,&status,4,&done,0)&&GetLastError()==122);
    expect(!DeviceIoControl(device,0xdead,0,0,0,0,&done,0)&&GetLastError()==50);
    AH_Request bad;memset(&bad,0,sizeof bad);bad.version=999;expect(!DeviceIoControl(device,AH_IOCTL_ACTION,&bad,sizeof bad,0,0,&done,0)&&GetLastError()==87);
    H meter=LoadLibraryA("BATMETER.DLL");expect(meter!=0);
    typedef I (__stdcall *CAPS)(void*);CAPS capabilities=(CAPS)GetProcAddress(meter,"BatMeterCapabilities");expect(capabilities!=0);expect(capabilities(0));
    expect(joyGetNumDevs()==2);unsigned char caps[404];expect(!joyGetDevCapsA(0,caps,sizeof caps));expect(*(U*)(caps+60)==12&&*(U*)(caps+104)==4);
    U pos[4];expect(!joyGetPos(0,pos));expect(pos[0]==20423&&pos[1]==56224&&pos[3]==4);
    U ex[13]={52,255};expect(!joyGetPosEx(0,ex));expect(ex[2]==20423&&ex[3]==56224&&ex[8]==4&&ex[9]==3&&ex[10]==0);
    ex[1]=0x200;ex[10]=123;expect(!joyGetPosEx(0,ex)&&ex[10]==0);
    expect(!joyGetDevCapsA(1,caps,sizeof caps)&&!(*(U*)(caps+96)&16));
    ex[1]=0x40;expect(!joyGetPosEx(1,ex)&&ex[10]==65535);
    expect(joyGetPos(2,pos)==167);
    CLASS c={0,proc,0,0,GetModuleHandleA(0),0,0,0,0,"NEO98DevicesFixture"};RegisterClassA(&c);
    H window=CreateWindowExA(0,c.name,"hardware test",0,0,0,10,10,0,0,c.instance,0);
    U threshold;expect(!joySetThreshold(0,123));expect(!joyGetThreshold(0,&threshold)&&threshold==123);expect(!joySetCapture(window,0,10,0));
    action(AH_ATTACH,0);refresh();expect(status.medium[0].letter=='F');
    H file=CreateFileA("F:\\HELLO.TXT",0x80000000,1,0,3,0,0);expect(file!=(H)~0u);
    char bytes[32];expect(ReadFile(file,bytes,sizeof bytes,&done,0)&&done==11&&bytes[0]=='U');
    H write=CreateFileA("F:\\WRITE.TXT",0x40000000,0,0,2,0,0);expect(write==(H)~0u);
    action(AH_DETACH,0);expect(!ReadFile(file,bytes,1,&done,0)&&GetLastError()==21);CloseHandle(file);
    action(AH_ATTACH,1);refresh();expect(status.medium[0].letter=='F');
    write=CreateFileA("F:\\WRITE.TXT",0x40000000,0,0,2,0,0);expect(write!=(H)~0u);expect(WriteFile(write,"guest write",11,&done,0)&&done==11);CloseHandle(write);
    /* The camera has its own state and never makes the service busy: wait
       for the first frame, and after the stop for the frames to end. */
    action(AH_CAMERA_START,0);
    {U start=GetTickCount();while(refresh(),status.camera_state!=AH_CAMERA_RUNNING){expect(GetTickCount()-start<4000);Sleep(5);}}
    expect(DeviceIoControl(device,AH_IOCTL_FRAME,0,0,&frame,sizeof frame,&done,0));expect(frame.width==2&&frame.height==1&&frame.bytes==8&&frame.pixels[2]==255);
    action(AH_CAMERA_STOP,0);
    {U start=GetTickCount();while(DeviceIoControl(device,AH_IOCTL_FRAME,0,0,&frame,sizeof frame,&done,0)){expect(GetTickCount()-start<4000);Sleep(5);}}
    expect(GetLastError()==21&&status.camera_state==AH_CAMERA_OFF);
    capture();
    directinput(window);
    action(AH_SCAN,0);
    file=CreateFileA("F:\\HELLO.TXT",0x80000000,1,0,3,0,0);expect(file==(H)~0u);
    MSG m;while(PeekMessageA(&m,0,0,0,1))DispatchMessageA(&m);
    expect(changes>=4&&moves);expect(!joyReleaseCapture(0));DestroyWindow(window);CloseHandle(device);ExitProcess(0);
}
