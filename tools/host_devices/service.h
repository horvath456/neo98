// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "platform.h"
#include "power.h"
#include "camera_inventory.h"
#include "../../src/host_devices.h"
#include "../host_volume.h"
#include "../host_settings.h"
#include "../../src/x86_app.h"
#include "../../src/registry.h"
#include <condition_variable>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <mutex>
#include <thread>
#include <set>
#include <functional>
namespace HostDeviceService
{
// NEO98_TEST_CAMERA replaces the cameras of the host by one test camera, for
// runs without the Mac's camera and without its question for access:
// "on" (or any other value) a moving picture, left half red and right half
// green with a white bar; "dark" starts but sends no picture; "denied" is
// refused as macOS refuses without access; "ask" waits for an answer that
// never comes; "none" is a host without a camera.
class TestCamera final:public Platform
{
    std::unique_ptr<Platform> inner;
    std::string mode;
    bool running=false;unsigned step=0;
    std::chrono::steady_clock::time_point next{};
    std::atomic<bool> cancelled{false};
public:
    static unsigned Identity(){return Id("camera:neo98-test");}
    TestCamera(std::unique_ptr<Platform> wrapped,const char *how):inner(std::move(wrapped)),mode(how){}
    void Cancel()override{cancelled=true;inner->Cancel();}
    void Refresh(AH_Status &out,std::map<unsigned,Medium> &media)override
    {
        inner->wifi=wifi;inner->Refresh(out,media);
        unsigned kept=0;for(unsigned i=0;i<out.devices;++i)if(strcmp(out.device[i].kind,"Camera"))out.device[kept++]=out.device[i];out.devices=kept;
        out.camera_id=0;out.features&=~unsigned(AH_CAMERA);if(mode=="none")return;
        out.camera_id=Identity();out.features|=AH_CAMERA;
        if(out.devices<AH_MAX_DEVICES){auto &d=out.device[out.devices++];d={};d.id=out.camera_id;Text(d.name,"NEO98 Test Camera");Text(d.kind,"Camera");}
    }
    unsigned Action(const AH_Request &request,std::string &message)override
    {
        if(request.action==AH_CAMERA_STOP){running=false;return 0;}
        if(request.action!=AH_CAMERA_START)return inner->Action(request,message);
        if(mode=="none"||request.id!=Identity())return 1167;
        if(mode=="denied"){message="On the Mac, open System Settings, click Privacy & Security, click Camera, and turn on NEO98. Then close Camera and open it again.";return 5;}
        if(mode=="ask")
        {
            asking_camera=true;
            for(unsigned i=0;i<6000&&!cancelled;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            asking_camera=false;message="Camera access was not granted.";return 5;
        }
        running=true;step=0;next={};return 0;
    }
    unsigned Mount(unsigned id,std::string &path,std::string &message)override{return inner->Mount(id,path,message);}
    unsigned Eject(unsigned id,std::string &message)override{return inner->Eject(id,message);}
    void CameraTick(AH_Frame &out)override
    {
        if(!running||mode=="dark")return;
        const auto now=std::chrono::steady_clock::now();if(now<next)return;next=now+std::chrono::milliseconds(66);
        static unsigned char picture[320*240*4];const unsigned bar=(step++*8)%320;
        for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x)
        {
            unsigned char *p=picture+(y*320+x)*4;const bool white=x>=bar&&x<bar+16;
            p[0]=white?255:0;p[1]=white||x>=160?255:0;p[2]=white||x<160?255:0;p[3]=255;
        }
        CopyFrame(out,picture,320,240,320*4);
    }
};
inline std::unique_ptr<Platform> MakeHostPlatform()
{
    auto platform=MakePlatform();
    if(const char *test=getenv("NEO98_TEST_CAMERA");test&&*test)return std::make_unique<TestCamera>(std::move(platform),test);
    return platform;
}
class Service final: public HostDevices::Provider
{
    AH_Power (*power_reader)();
    std::mutex lock;
    std::condition_variable wake;
    std::thread worker;
    std::unique_ptr<Platform> platform;
    bool stopping=false;
    std::deque<AH_Request> requests;
    AH_Status status{};
    std::unique_ptr<AH_Frame> frame=std::make_unique<AH_Frame>();
    struct Pending { AH_Request request{}; Medium medium; unsigned error=0; std::string message; };
    std::deque<Pending> mounted;
    std::deque<AH_Request> local;
    std::map<unsigned,Medium> media;
    struct Attached { char letter; std::unique_ptr<HostVolume::Drive> volume; bool optical,writable; };
    unsigned observed_generation=0;
    std::map<unsigned,Attached> attached;
    std::map<SDL_JoystickID,SDL_Joystick *> joysticks;
    unsigned next_input=0, next_power=0;
    unsigned camera_generation=0,camera_wanted=0;
    // Whether the host has a camera, from the last look at its devices, and
    // what the registry shows (Neo98Hardware::CameraName).
    std::atomic<bool> camera_present{false},camera_inventory_ready{false};
    CameraInventory camera_inventory;
    unsigned next_tray=0,pending_view=~0u; bool tray_started=false;
    std::atomic<bool> wifi_enabled{false},refresh_now{false};
    // Busy: a wireless or drive request waits or runs. Camera requests have
    // their own state and never make the service busy.
    bool Waiting()const
    {
        if(!local.empty())return true;
        for(const auto &r:requests)if(r.action!=AH_CAMERA_START&&r.action!=AH_CAMERA_STOP)return true;
        return false;
    }
    void Result(unsigned action,unsigned error,const std::string &message)
    {std::lock_guard<std::mutex> guard(lock);status.last_action=action;status.error=error;status.busy=Waiting();Text(status.message,message.empty()?(error?"The request failed.":"Done."):message);}
    void Work()
    {
        {auto created=MakeHostPlatform();std::lock_guard<std::mutex> guard(lock);platform=std::move(created);if(stopping)platform->Cancel();}
        auto next=std::chrono::steady_clock::time_point{};
        while(true)
        {
            AH_Request request{};
            {std::unique_lock<std::mutex> guard(lock);wake.wait_for(guard,std::chrono::milliseconds(50),[&]{return stopping||!requests.empty();});if(stopping)break;if(!requests.empty()){request=requests.front();std::fill(std::begin(requests.front().password),std::end(requests.front().password),0);requests.pop_front();}}
            if(request.action)
            {
                std::string message;unsigned error=0;
                if(request.action==AH_ATTACH)
                {
                    Pending result;result.request=request;
                    auto found=media.find(request.id);
                    if(found==media.end())result.error=1167;
                    else{result.medium=found->second;result.error=platform->Mount(request.id,result.medium.path,result.message);}
                    std::lock_guard<std::mutex> guard(lock);mounted.push_back(std::move(result));
                }
                else
                {
                    const bool camera=request.action==AH_CAMERA_START||request.action==AH_CAMERA_STOP;
                    error=request.action==AH_EJECT?platform->Eject(request.id,message):platform->Action(request,message);
                    if(camera)
                    {
                        // What the Camera program shows instead of a black
                        // picture, and a line in the session log for a hand
                        // test.
                        std::lock_guard<std::mutex> guard(lock);frame->bytes=0;frame->sequence=0;
                        const bool start=request.action==AH_CAMERA_START,stale=start&&camera_wanted!=request.id;
                        if(!stale)
                        {
                            camera_generation=start&&!error?request.id:0;
                            status.camera_state=!start?AH_CAMERA_OFF:!error?AH_CAMERA_OPENING:error==5?AH_CAMERA_DENIED:AH_CAMERA_FAILED;
                            status.camera_error=error;Text(status.camera_message,message);
                        }
                        printf("NEO98 HOST camera %s error=%u%s%s\n",start?"start":"stop",error,message.empty()?"":" ",message.c_str());fflush(stdout);
                    }
                    else Result(request.action,error,message);
                }
                volatile char *secret=request.password;for(unsigned i=0;i<sizeof request.password;++i)secret[i]=0;
                next={};
            }
            if(refresh_now.exchange(false))next={};
            const auto now=std::chrono::steady_clock::now();
            if(now>=next)
            {
                AH_Status update{};update.version=AH_VERSION;std::map<unsigned,Medium> current;
                platform->wifi=wifi_enabled;platform->Refresh(update,current);
                DropClaimed(update,current);
                media=std::move(current);
                {std::lock_guard<std::mutex> guard(lock);
                    update.generation=status.generation+1;update.power=status.power;update.busy=status.busy;update.error=status.error;update.last_action=status.last_action;
                    if(status.message[0]&&(status.busy||status.error||!update.message[0]))Text(update.message,status.message);
                    update.features|=AH_JOYSTICK;update.joysticks=status.joysticks;
                    camera_present=update.camera_id!=0;
                    camera_inventory_ready=true;
                    update.camera_state=status.camera_state;update.camera_error=status.camera_error;Text(update.camera_message,status.camera_message);
                    memcpy(update.joystick,status.joystick,sizeof update.joystick);status=update;
                }
                next=now+std::chrono::seconds(3);
            }
            // Only this worker owns the capture device. The guest reads a copy.
            {std::lock_guard<std::mutex> guard(lock);platform->CameraTick(*frame);
             if(camera_generation&&frame->bytes&&status.camera_state==AH_CAMERA_OPENING)status.camera_state=AH_CAMERA_RUNNING;}
        }
    }
    void Inputs()
    {
        SDL_JoystickUpdate();
        for(auto i=joysticks.begin();i!=joysticks.end();)
            if(!SDL_JoystickGetAttached(i->second)){SDL_JoystickClose(i->second);i=joysticks.erase(i);}else++i;
        for(int i=0;i<SDL_NumJoysticks()&&joysticks.size()<AH_MAX_JOYS;++i)
        {const auto id=SDL_JoystickGetDeviceInstanceID(i);if(id>=0&&!joysticks.count(id))if(auto *j=SDL_JoystickOpen(i))joysticks[id]=j;}
        std::lock_guard<std::mutex> guard(lock);status.joysticks=0;status.features|=AH_JOYSTICK;
        for(auto &pair:joysticks)
        {
            auto *j=pair.second;auto &out=status.joystick[status.joysticks++];out={};out.id=unsigned(pair.first);
            if(SDL_JoystickHasRumble(j))out.flags|=AH_JOY_RUMBLE;
            out.axes=std::min(6,SDL_JoystickNumAxes(j));out.buttons=std::min(32,SDL_JoystickNumButtons(j));
            Text(out.name,SDL_JoystickName(j)?SDL_JoystickName(j):"Controller");
            for(unsigned a=0;a<6;++a)out.axis[a]=a<out.axes?unsigned(int(SDL_JoystickGetAxis(j,a))+32768):32767;
            for(unsigned b=0;b<out.buttons;++b)if(SDL_JoystickGetButton(j,b))out.pressed|=1u<<b;
            out.pov=SDL_JoystickNumHats(j)?65535:~0u;
            if(SDL_JoystickNumHats(j))switch(SDL_JoystickGetHat(j,0))
            {case SDL_HAT_UP:out.pov=0;break;case SDL_HAT_RIGHTUP:out.pov=4500;break;case SDL_HAT_RIGHT:out.pov=9000;break;case SDL_HAT_RIGHTDOWN:out.pov=13500;break;case SDL_HAT_DOWN:out.pov=18000;break;case SDL_HAT_LEFTDOWN:out.pov=22500;break;case SDL_HAT_LEFT:out.pov=27000;break;case SDL_HAT_LEFTUP:out.pov=31500;break;}
        }
    }
    void CaptureInput()
    {
        std::lock_guard<std::mutex> guard(lock);const auto now=User::MessageClock();
        for(unsigned i=0;i<2;++i)
        {
            auto &capture=HostDevices::captures[i];if(!capture.window)continue;
            if(i>=status.joysticks||!User::IsWindow(capture.window)){capture={};continue;}
            if(capture.seen&&int(now-capture.next)<0)continue;capture.next=now+capture.period;
            const auto &joy=status.joystick[i];const unsigned buttons=joy.pressed&15,delta=(buttons^capture.previous[3])&15;
            auto moved=[&](unsigned a){const auto old=capture.previous[a],value=joy.axis[a];return (old>value?old-value:value-old)>HostDevices::thresholds[i];};
            const unsigned xy=joy.axis[0]|(joy.axis[1]<<16);
            if(!capture.changed_only||!capture.seen||moved(0)||moved(1))User::PostMessage(capture.window,0x3a0+i,buttons,xy);
            if(joy.axes>2&&(!capture.changed_only||!capture.seen||moved(2)))User::PostMessage(capture.window,0x3a2+i,buttons,joy.axis[2]);
            if(delta&buttons)User::PostMessage(capture.window,0x3b5+i,buttons|((delta&buttons)<<8),xy);
            if(delta&~buttons)User::PostMessage(capture.window,0x3b7+i,buttons|((delta&~buttons)<<8),xy);
            for(unsigned a=0;a<3;++a)capture.previous[a]=joy.axis[a];capture.previous[3]=buttons;capture.seen=1;
        }
    }
    unsigned Detach(unsigned id,X86::Application &app)
    {
        auto found=attached.find(id);if(found==attached.end())return 1167;
        auto &a=found->second;
        // The host menu may have replaced this medium in the shared CD slot.
        // Forget our provider without ejecting the replacement.
        if(Volume::Mounted(a.letter)!=a.volume.get()){attached.erase(found);return 0;}
        Volume::Mount(a.letter,nullptr,a.optical?Volume::Cdrom:2,false);
        if(!app.ChangeMedium(a.letter))
        {Volume::Mount(a.letter,a.volume.get(),a.optical?Volume::Cdrom:2,a.writable);return 170;}
        if(media_changed)media_changed(a.letter,"");
        attached.erase(found);return 0;
    }
public:
    std::function<void(char,const std::string&)> media_changed;
    explicit Service(AH_Power (*read_power)()=ReadPower):power_reader(read_power){status.version=AH_VERSION;status.power=power_reader();}
    ~Service()override
    {
        if(HostDevices::current==this)HostDevices::current=nullptr;
        {std::lock_guard<std::mutex> guard(lock);stopping=true;if(platform)platform->Cancel();for(auto &r:requests)std::fill(std::begin(r.password),std::end(r.password),0);}
        wake.notify_all();if(worker.joinable())worker.join();
        for(auto &j:joysticks)SDL_JoystickClose(j.second);
    }
    void Attach(){HostDevices::current=this;}
    // Network > WLAN in Windows. Off, Windows has no wireless adapter, no
    // tray icon, and the Mac is not asked about its wireless networks.
    void SetWifi(bool on){wifi_enabled=on;refresh_now=true;wake.notify_all();}
    void Install()
    {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");SDL_InitSubSystem(SDL_INIT_JOYSTICK);
        HostDevices::current=this;Inputs();worker=std::thread([this]{Work();});
    }
    bool Camera()override{return camera_present;}
    AH_Power Power()override {std::lock_guard<std::mutex> guard(lock);return status.power;}
    unsigned Status(AH_Status &out)override
    {
        std::lock_guard<std::mutex> guard(lock);out=status;
        if(out.camera_state==AH_CAMERA_OPENING&&platform&&platform->asking_camera)out.camera_state=AH_CAMERA_ASKING;
        for(unsigned i=0;i<out.media;++i)if(auto p=attached.find(out.medium[i].id);p!=attached.end())out.medium[i].letter=p->second.letter;
        return 0;
    }
    unsigned Action(const AH_Request &request)override
    {
        if(request.version!=AH_VERSION||!memchr(request.password,0,sizeof request.password)||!memchr(request.ssid,0,sizeof request.ssid)||request.action<AH_SCAN||request.action>AH_RUMBLE)return 87;
        if(request.action==AH_RUMBLE)
        {
            if(request.id>=AH_MAX_JOYS)return 87;
            std::lock_guard<std::mutex> guard(lock);
            if(request.id>=status.joysticks||!(status.joystick[request.id].flags&AH_JOY_RUMBLE))return 50;
            // Guest IOCTLs run on the same host thread that owns the SDL
            // joysticks. Apply the command before returning: an effect can
            // stop and the guest exit before the next 16 ms input pump.
            const auto found=joysticks.find(SDL_JoystickID(status.joystick[request.id].id));
            if(found==joysticks.end()||!SDL_JoystickGetAttached(found->second))return 1167;
            return SDL_JoystickRumble(found->second,request.low,request.high,request.flags)==0?0:50;
        }
        // The camera has its own state and is never refused because a
        // wireless or drive request is still running: a refused
        // start left the capture driver with black frames.
        if(request.action==AH_CAMERA_START||request.action==AH_CAMERA_STOP)
        {
            RequestPermission(request.action);
            std::lock_guard<std::mutex> guard(lock);
            const bool start=request.action==AH_CAMERA_START;
            camera_wanted=start?request.id:0;
            status.camera_state=start?AH_CAMERA_OPENING:AH_CAMERA_OFF;status.camera_error=0;status.camera_message[0]=0;
            requests.push_back(request);wake.notify_all();return 0;
        }
        const bool wireless=request.action<=AH_FORGET||request.action==AH_CONNECT_HIDDEN||request.action==AH_SELECT_ADAPTER;
        if(wireless&&!wifi_enabled)return 1167;
        RequestPermission(request.action);
        std::lock_guard<std::mutex> guard(lock);
        if(status.busy&&request.action!=AH_CAMERA_STOP)return 170;
        status.busy=1;status.error=0;status.message[0]=0;status.last_action=request.action;
        if(request.action==AH_DETACH||request.action==AH_EJECT)local.push_back(request);else requests.push_back(request);
        wake.notify_all();return 0;
    }
    unsigned Frame(void *out,unsigned bytes,unsigned &done)override
    {
        std::lock_guard<std::mutex> guard(lock);done=0;if(!camera_generation||!frame->bytes)return 21;
        const auto needed=unsigned(offsetof(AH_Frame,pixels))+frame->bytes;if(bytes<needed)return 122;
        memcpy(out,frame.get(),needed);done=needed;return 0;
    }
    // view: 0 Unplug or Eject Hardware, 1 power, 2 wireless networks, 3 the Camera program,
    // 4 the Game Controllers panel.
    bool Open(bool show=true,unsigned view=0)
    {
        if(const auto hwnd=User::FindWindow("NEO98Hardware",nullptr))
        {if(show)User::PostMessage(hwnd,0x402,view,0);return true;}
        if(!SeedFiles())return false;
        X86::Application::RequestOpen("C:\\WINDOWS\\NEO98HW.EXE");pending_view=show?view:~0u;return true;
    }
    // NEO98HW.EXE and the Camera program CAMERA.EXE to C:\WINDOWS, the version
    // file of the camera driver NEO98CAP.DRV (src/win16_capture.cpp) and the
    // force feedback driver NEO98FF.DLL to C:\WINDOWS\SYSTEM, from the folder
    // of the host program or its package. The shortcut of Camera goes to
    // Programs > Accessories > Multimedia once, as Setup leaves a shortcut:
    // one the user deleted stays deleted only while the program is there.
    static constexpr char CameraShortcut[]="C:\\WINDOWS\\Start Menu\\Programs\\Accessories\\Multimedia\\Camera.lnk";
    static bool SeedFiles()
    {
        const auto exe=std::filesystem::path(HostSettings::Executable()).parent_path();
        for(const auto &folder:{exe/"../share/neo98",exe/"../Resources",exe/"../host-devices"})
        {
            bool seeded=false;
            unsigned node=0;const bool camera_known=!Files::System().Lookup("C:\\WINDOWS\\CAMERA.EXE",node);
            for(const auto &[file,target]:{std::pair<const char*,const char*>{"NEO98HW.EXE","C:\\WINDOWS\\NEO98HW.EXE"},{"NEO98CAP.DRV","C:\\WINDOWS\\SYSTEM\\NEO98CAP.DRV"},
                                              {"NEO98FF.DLL","C:\\WINDOWS\\SYSTEM\\NEO98FF.DLL"},{"CAMERA.EXE","C:\\WINDOWS\\CAMERA.EXE"},{"CAMERA.LNK",CameraShortcut}})
            {
                if(target==CameraShortcut&&(camera_known||!Files::System().Lookup(CameraShortcut,node)))continue;
                std::ifstream input(folder/file,std::ios::binary);if(!input)continue;
                std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
                if(bytes.empty())return false;
                if(target==CameraShortcut)
                {
                    // Setup may call its media group Entertainment, or the user
                    // may have removed it. Install our documented shortcut group
                    // before the link, just as an installer creates its folders.
                    const std::string path=CameraShortcut;
                    for(size_t end=path.find('\\',3);end!=std::string::npos;end=path.find('\\',end+1))
                    {
                        const auto error=Files::System().MakeDirectory(path.substr(0,end).c_str());
                        if(error&&error!=183)return false;
                    }
                }
                const auto error=Files::System().Seed(target,bytes.data(),unsigned(bytes.size()));
                if(error&&error!=32&&target!=CameraShortcut)return false;
                seeded|=!strcmp(file,"NEO98HW.EXE");
            }
            if(seeded)return true;
        }
        return false;
    }
    void Pump(X86::Application &app,bool desktop=true)
    {
        // The worker still refreshes host status, but a paused guest keeps
        // its registry, input queue and mounted drives unchanged. This also
        // preserves a freshly loaded state until the user resumes it.
        if(X86::Application::MachinePaused())return;
        const unsigned now=SDL_GetTicks();
        if(int(now-next_tray)>=0)
        {
            next_tray=now+1000;
            if(pending_view!=~0u)if(const auto hwnd=User::FindWindow("NEO98Hardware",nullptr))
            {User::PostMessage(hwnd,0x402,pending_view,0);pending_view=~0u;}
            if(const auto tray=User::FindWindow("Shell_TrayWnd",nullptr);desktop&&!tray_started&&tray&&User::IsVisible(tray))
            {tray_started=true;Open(false);}
        }
        if(int(now-next_input)>=0){next_input=now+16;Inputs();CaptureInput();}
        // Publish the initial inventory without inventing a hotplug during
        // DLL initialization. Subsequent arrivals/removals notify Windows.
        const bool camera_ready=camera_inventory_ready.load();
        const bool camera_now=camera_present.load();
        const auto camera_update=camera_inventory.Observe(camera_ready,camera_now);
        if(camera_update!=CameraInventory::Unchanged)
        {
            Registry::PublishDevnodes(Registry::System());
            unsigned windows[User::WindowCapacity],count=0;
            if(camera_update==CameraInventory::Changed&&User::WindowSnapshot(0,windows,User::WindowCapacity,count))for(unsigned i=0;i<count;++i)User::PostMessage(windows[i],0x219,7,0); // DBT_DEVNODES_CHANGED
        }
        if(int(now-next_power)>=0)
        {
            next_power=now+2000;const auto power=power_reader();bool changed=false;
            {std::lock_guard<std::mutex> guard(lock);changed=memcmp(&power,&status.power,4)!=0;status.power=power;}
            if(changed){unsigned windows[User::WindowCapacity],count=0;if(User::WindowSnapshot(0,windows,User::WindowCapacity,count))for(unsigned i=0;i<count;++i)User::PostMessage(windows[i],0x218,0xa,0);}
        }
        std::vector<unsigned> gone;
        {std::lock_guard<std::mutex> guard(lock);
            if(observed_generation!=status.generation&&(status.features&AH_MEDIA))
            {
                observed_generation=status.generation;
                for(const auto &item:attached)
                {bool present=false;for(unsigned i=0;i<status.media;++i)if(status.medium[i].id==item.first)present=true;if(!present)gone.push_back(item.first);}
            }
        }
        for(unsigned id:gone)Detach(id,app);
        std::deque<Pending> ready;std::deque<AH_Request> actions;
        {std::lock_guard<std::mutex> guard(lock);ready.swap(mounted);actions.swap(local);}
        for(auto &action:actions)
        {
            unsigned error=attached.count(action.id)?Detach(action.id,app):action.action==AH_EJECT?0:1167;
            if(!error&&action.action==AH_EJECT){std::lock_guard<std::mutex> guard(lock);requests.push_back(action);wake.notify_all();}
            else Result(action.action,error,error?"Windows could not stop the drive because a program is still using it.":"The drive has been stopped.");
        }
        for(auto &p:ready)
        {
            if(p.error){Result(p.request.action,p.error,p.message);continue;}
            if(attached.count(p.request.id)){Result(p.request.action,0,"The drive is already in use.");continue;}
            char letter=0;
            for(char c=p.medium.flags&AH_MEDIA_FLOPPY?'A':p.medium.flags&AH_MEDIA_OPTICAL?'E':'F';c<='Z';++c)
                if(c!='C'&&c!='D'&&(!Volume::Exists(c)||((p.medium.flags&AH_MEDIA_OPTICAL)&&Volume::Type(c)==Volume::Cdrom&&!Volume::Mounted(c)))){letter=c;break;}
            if(!letter){Result(p.request.action,4,"No drive letter is available.");continue;}
            const bool writable=(p.request.flags&1)&&!(p.medium.flags&AH_MEDIA_READONLY);
            auto volume=std::make_unique<HostVolume::Drive>();
            if(!volume->Open(p.medium.path.c_str(),writable,p.medium.name.c_str())){Result(p.request.action,21,"The disk cannot be read.");continue;}
            const bool optical=p.medium.flags&AH_MEDIA_OPTICAL;
            Volume::Mount(letter,volume.get(),optical?Volume::Cdrom:2,writable);
            if(!app.ChangeMedium(letter)){Volume::Mount(letter,nullptr,optical?Volume::Cdrom:2,false);Result(p.request.action,170,"The drive letter is in use.");continue;}
            attached.emplace(p.request.id,Attached{letter,std::move(volume),optical,writable});
            if(media_changed)media_changed(letter,p.medium.path);
            Result(p.request.action,0,std::string("Drive ")+letter+": is ready.");
        }
    }
};
}
