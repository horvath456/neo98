// SPDX-License-Identifier: GPL-2.0-or-later
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#define private public
#include "../src/x86_app.h"
#undef private
#include "linux_session.h"
#include "host_devices/service.h"
#include <cassert>
namespace HostDeviceService
{
static std::string fixture_directory;
class Fake final:public Platform
{
    bool present=true,camera=false,connected=false;unsigned adapter=0;
public:
    void Refresh(AH_Status &out,std::map<unsigned,Medium> &media)override
    {
        out.features=AH_MEDIA|AH_CAMERA|AH_WIFI;out.camera_id=99;out.wifi_on=1;out.wifi_connected=connected;
        if(connected){Text(out.wifi_name,"NEO98 Testnetz");out.wifi_bitrate=54000;out.wifi_address=0x1701a8c0;}
        out.adapters=2;out.adapter=adapter;Text(out.adapter_name[0],"wlan0");Text(out.adapter_name[1],"wlan1");
        out.networks=1;out.network[0].id=72;out.network[0].signal=85;Text(out.network[0].name,"NEO98 Testnetz");
        if(connected)out.network[0].flags|=AH_NET_ACTIVE;
        if(present){out.media=1;out.medium[0].id=99;Text(out.medium[0].name,"Fixture USB");Medium m;m.id=99;m.path=fixture_directory;m.name="Fixture USB";media[99]=m;}
        // A second stick arrives while Windows runs, which AutoPlay opens.
        if(present&&connected){auto &late=out.medium[out.media++];late.id=100;Text(late.name,"Late USB");Medium m;m.id=100;m.path=fixture_directory;m.name="Late USB";media[100]=m;}
    }
    unsigned Action(const AH_Request &r,std::string&)override
    {if(r.action==AH_SCAN)present=false;else if(r.action==AH_CONNECT)connected=true;else if(r.action==AH_SELECT_ADAPTER)adapter=r.id;else if(r.action==AH_CAMERA_START)camera=true;else if(r.action==AH_CAMERA_STOP)camera=false;return 0;}
    unsigned Mount(unsigned,std::string &path,std::string&)override{path=fixture_directory;return 0;}
    unsigned Eject(unsigned,std::string&)override{return 0;}
    void CameraTick(AH_Frame &out)override
    {if(camera){out.width=2;out.height=1;out.stride=8;out.bytes=8;++out.sequence;out.pixels[2]=255;out.pixels[3]=255;}}
};
std::unique_ptr<Platform> MakePlatform(){return std::make_unique<Fake>();}
void RequestPermission(unsigned){}
}
// The motors of the first virtual controller, as NEO98FF.DLL drives them.
static unsigned short rumble_peak,rumble_last;static unsigned rumble_calls;static int rumble_success;
static int Rumble(void *,Uint16 low,Uint16 high){++rumble_calls;rumble_last=low>high?low:high;if(rumble_last>rumble_peak)rumble_peak=rumble_last;return rumble_success;}
int main(int argc,char **argv)
{
    assert(argc>=4);HostDeviceService::fixture_directory=argv[2];
    assert(!SDL_Init(SDL_INIT_JOYSTICK));
    SDL_VirtualJoystickDesc desc{};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes=4;desc.nbuttons=12;desc.nhats=1;desc.name="Virtual Controller";desc.Rumble=Rumble;
    int index=SDL_JoystickAttachVirtualEx(&desc);assert(index>=0);
    auto *joystick=SDL_JoystickOpen(index);assert(joystick);
    // SDL2 returns 0 from this callback; sdl2-compat 2.32.70 forwards it to
    // SDL3 as a bool without conversion. Probe the adapter so a rejected
    // virtual start cannot hide a subsequent stop from this regression test.
    if(SDL_JoystickRumble(joystick,1,1,1000)<0)
    {assert(rumble_calls==1);rumble_success=1;assert(!SDL_JoystickRumble(joystick,1,1,1000));}
    assert(!SDL_JoystickRumble(joystick,0,0,0));assert(!rumble_last);
    rumble_peak=rumble_last=0;rumble_calls=0;
    const int nohat=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN,2,2,0);assert(nohat>=0);
    assert(!SDL_JoystickSetVirtualAxis(joystick,0,-12345));assert(!SDL_JoystickSetVirtualAxis(joystick,1,23456));
    assert(!SDL_JoystickSetVirtualButton(joystick,2,1));assert(!SDL_JoystickSetVirtualHat(joystick,0,SDL_HAT_UP));
    HostDeviceService::Service hardware([]{return HostDeviceService::PowerStatus(SDL_POWERSTATE_ON_BATTERY,3600,42);});hardware.Install();
    // No Pump between the commands or before checking the motors: a guest
    // may stop and exit within one host tick.
    AH_Request force{};force.version=AH_VERSION;force.action=AH_RUMBLE;
    force.low=force.high=39321;force.flags=1000;
    assert(!hardware.Action(force));assert(rumble_last==39321);
    force.low=force.high=0;force.flags=0;
    assert(!hardware.Action(force));assert(!rumble_last&&rumble_calls==2);
    rumble_peak=rumble_last=0;rumble_calls=0;
    hardware.SetWifi(true); // Network > WLAN in Windows is off by default; the fixture drives the WLAN page
    const bool ui=!strcmp(argv[1],"--ui");
    LinuxHost::Options options;options.exe=ui?"C:\\WINDOWS\\EXPLORER.EXE":argv[1];options.desktop_width=800;options.desktop_height=600;options.quiet=!getenv("NEO98_VERBOSE");
    LinuxHost::Session session(argc,argv,options);assert(session.Start());
    assert(HostDeviceService::Service::SeedFiles()); // NEO98HW.EXE and NEO98CAP.DRV of build/host-devices
    if(ui)
    {
        unsigned stage=0,manager=0,power=0;auto next=std::chrono::steady_clock::now();
        auto deadline=next+std::chrono::seconds(100);
        while(std::chrono::steady_clock::now()<deadline)
        {
            hardware.Pump(*session.app);session.app->Pump(64);User::Render();SDL_Delay(1);
            assert(session.app->Status()!=X86::Application::Failed&&!X86::Application::FailedChildren());
            const auto now=std::chrono::steady_clock::now();if(now<next)continue;
            if(stage==0)
            {
                manager=User::FindWindow("NEO98Hardware",nullptr);
                if(!manager||!User::FindWindow("SystemTray_Main",nullptr))continue;
                assert(!User::IsVisible(manager));session.app->MouseButtons(730,587,0);User::PostMessage(manager,0x402,2,0);stage=1;next=now+std::chrono::seconds(1);
            }
            else if(stage==1)
            {
                const auto wifi=User::FindWindow("NEO98Wifi",nullptr);if(!wifi||!User::IsVisible(wifi))continue;
                if(!User::GetDlgItem(wifi,10))continue;
                const auto hidden=User::FindWindow("NEO98WifiHidden",nullptr);
                assert(hidden&&!User::IsVisible(hidden)&&User::GetDlgItem(hidden,31)&&(User::Style(User::GetDlgItem(hidden,33))&0x20));
                if(!User::IsVisible(User::GetDlgItem(User::FindWindow("NEO98Wifi",nullptr),36)))continue; // two adapters show the choice
                const auto key=User::FindWindow("NEO98WifiKey",nullptr);
                assert(key&&!User::IsVisible(key)&&(User::Style(User::GetDlgItem(key,11))&0x20)&&(User::Style(User::GetDlgItem(key,12))&0x20));
                char label[256]={};User::GetWindowText(User::GetDlgItem(wifi,21),label,sizeof label);
                if(strcmp(label,"Not connected"))continue;
                session.Screenshot(std::string(argv[2])+"/wifi.png");User::PostMessage(wifi,0x111,103,0);stage=2;
            }
            else if(stage==2)
            {
                // The tray program attaches the fixture medium without being asked.
                AH_Status status{};hardware.Status(status);if(status.busy||!status.wifi_connected||!status.media||status.medium[0].letter!='F')continue;
                User::PostMessage(User::FindWindow("NEO98Wifi",nullptr),0x10,0,0);
                User::PostMessage(manager,0x111,506,0);stage=20;next=now+std::chrono::seconds(2);
            }
            else if(stage==20)
            {
                // The status window and the note that the connection is up.
                const auto details=User::FindWindow("NEO98WifiStatus",nullptr);if(!details||!User::IsVisible(details))continue;
                char speed[64]={};User::GetWindowText(User::GetDlgItem(details,43),speed,sizeof speed);
                char address[64]={};User::GetWindowText(User::GetDlgItem(details,45),address,sizeof address);
                if(strcmp(speed,"54.0 Mbps")||strcmp(address,"192.168.1.23"))continue;
                assert(User::IsVisible(User::FindWindow("NEO98Notice",nullptr)));
                session.Screenshot(std::string(argv[2])+"/status.png");
                User::PostMessage(details,0x10,0,0);User::PostMessage(manager,0x402,4,0);stage=22;
            }
            else if(stage==22)
            {
                // The Game Controllers panel (JOY.CPL of DirectX 8 calls itself
                // Gaming Options) lists the virtual SDL controllers.
                const auto panel=User::FindWindow("#32770","Gaming Options");if(!panel||!User::IsVisible(panel))continue;
                session.Screenshot(std::string(argv[2])+"/controllers.png");
                User::PostMessage(panel,0x10,0,0);stage=21;next=now+std::chrono::seconds(1);
            }
            else if(stage==21)
            {
                AH_Status status{};hardware.Status(status);if(status.busy||status.media<2||status.medium[1].letter!='G')continue;
                // AutoPlay opens the new drive's folder in an Explorer window.
                const auto folder=User::FindWindow("CabinetWClass",nullptr);if(!folder||!User::IsVisible(folder))continue;
                session.Screenshot(std::string(argv[2])+"/autoplay.png");
                User::PostMessage(folder,0x10,0,0);User::PostMessage(manager,0x402,0,0);stage=3;next=now+std::chrono::seconds(1);
            }
            else if(stage==3)
            {
                const auto devices=User::FindWindow("NEO98Eject",nullptr);if(!devices||!User::IsVisible(devices))continue;
                session.Screenshot(std::string(argv[2])+"/devices.png");
                User::PostMessage(devices,0x10,0,0);User::PostMessage(manager,0x111,501,0);stage=4;
            }
            else if(stage==4)
            {
                power=User::FindWindow("#32770","Power Management Properties");if(!power)continue;
                if(!User::IsVisible(power)||!User::GetDlgItem(power,1))continue;
                stage=5;next=now+std::chrono::seconds(1);
            }
            else if(stage==5||stage==6)
            {
                Gdi::RECT rect{};assert(User::GetWindowRect(power,&rect));
                const int x=rect.left+178,y=rect.top+(stage==5?12:39);
                session.app->MouseButtons(x,y,0);session.app->MouseButtons(x,y,1);session.app->MouseButtons(x,y,0);
                ++stage;next=now+std::chrono::seconds(stage==6?1:2);
            }
            else
            {
                unsigned nodes[User::WindowCapacity],count=0;assert(User::WindowSnapshot(0,nodes,User::WindowCapacity,count));
                std::string text;for(unsigned i=0;i<count;++i)if(User::IsVisible(nodes[i]))
                {char value[512]={};User::GetWindowText(nodes[i],value,sizeof value);text+=value;text+='\n';}
                // Include descendants: WindowSnapshot(0) lists top-level windows.
                auto children=[&](auto &self,unsigned parent)->void {
                    for(unsigned child=User::GetWindow(parent,5);child;child=User::GetWindow(child,2))
                    {if(!User::IsVisible(child))continue;char value[512]={};User::GetWindowText(child,value,sizeof value);text+=value;text+='\n';self(self,child);}
                };children(children,power);
                std::ofstream(std::string(argv[2])+"/power-text.txt")<<text;
                session.Screenshot(std::string(argv[2])+"/original-power.png");
                assert(text.find("42%")!=std::string::npos||text.find("42 %")!=std::string::npos);
                puts("HOST DEVICES UI PASS: automatic tray startup, wireless network window and connect action, Game Controllers panel, automatic USB drive, original POWERCFG/BATMETER displays 42 percent");return 0;
            }
        }
        fprintf(stderr,"HARDWARE UI timed out in stage %u, status=%u error=%s\n",stage,session.app->Status(),session.app->Error());return 1;
    }
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(std::chrono::steady_clock::now()<until&&session.app->Status()!=X86::Application::Failed&&session.app->Status()!=X86::Application::Exited)
    {hardware.Pump(*session.app,false);session.app->Pump(32);User::Render();SDL_Delay(1);}
    fprintf(stderr,"HARDWARE GUEST state=%u exit=%u error=%s\n",session.app->Status(),session.app->ExitCode(),session.app->Error());
    assert(session.app->Status()==X86::Application::Exited&&!session.app->ExitCode());
    std::ifstream input(HostDeviceService::fixture_directory+"/WRITE.TXT");std::string data;std::getline(input,data);assert(data=="guest write");
    // A constant force of 60 percent ran, and stopping it stopped the motors.
    fprintf(stderr,"HARDWARE rumble calls=%u peak=%u last=%u\n",rumble_calls,rumble_peak,rumble_last);
    assert(rumble_peak>=39000&&rumble_peak<=40000&&!rumble_last);
    SDL_JoystickClose(joystick);SDL_JoystickDetachVirtual(nohat);SDL_JoystickDetachVirtual(index);
    puts("HOST DEVICES GUEST PASS: real Windows power/IOCTL/WinMM APIs, read-only/writable USB mount, notifications, old handles, unplug, camera frames, Video for Windows capture through AVICAP32, DirectInput joysticks and force feedback");
}
