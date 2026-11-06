// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_devices/platform.h"
#include "host_devices/power.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace HostDeviceService;
int main(int argc,char **argv)
{
    assert(argc==2);
    auto unknown=PowerStatus(SDL_POWERSTATE_UNKNOWN,-1,-1);
    assert(unknown.ac==255&&unknown.flags==255&&unknown.percent==255&&unknown.seconds==~0u);
    auto none=PowerStatus(SDL_POWERSTATE_NO_BATTERY,0,0);assert(none.ac==1&&none.flags==128&&none.percent==255);
    auto low=PowerStatus(SDL_POWERSTATE_ON_BATTERY,1800,4);assert(low.ac==0&&low.flags==4&&low.percent==4&&low.seconds==1800);
    auto charging=PowerStatus(SDL_POWERSTATE_CHARGING,99,70);assert(charging.ac==1&&charging.flags==9&&charging.seconds==~0u);
    auto normal=PowerStatus(SDL_POWERSTATE_ON_BATTERY,-1,50);assert(normal.flags==0&&normal.seconds==~0u);
    char text[5];Text(text,"ab\xc3\xa4z");assert(!strcmp(text,"ab\xc3\xa4"));Text(text,"abc\xc3\xa4");assert(!strcmp(text,"abc"));
    // Exposing Wi-Fi is opt-in (the host's --wifi); this test exercises it.
    auto platform=MakePlatform();platform->wifi=true;AH_Status status{};std::map<unsigned,Medium> media;
    auto refresh=[&]{status={};media.clear();platform->Refresh(status,media);};refresh();
    assert(status.features&AH_WIFI);assert(status.features&AH_MEDIA);assert(status.wifi_on&&status.wifi_connected);
    assert(!strcmp(status.wifi_name,"NEO98 Active"));assert(status.networks==6&&status.media==1&&media.size()==1);
    assert(status.adapters==2&&status.adapter==0&&!strcmp(status.adapter_name[0],"wlp1s0")&&!strcmp(status.adapter_name[1],"wlx0011"));
    assert(status.wifi_bitrate==866700&&status.wifi_address==(10u|42u<<24));
    auto id=[&](const char *name){for(unsigned i=0;i<status.networks;++i)if(!strcmp(status.network[i].name,name))return status.network[i].id;assert(false);return 0u;};
    std::string message;
    auto action=[&](unsigned what,unsigned net=0,const char *password="",unsigned flags=0){AH_Request r{};r.version=AH_VERSION;r.action=what;r.id=net;r.flags=flags;Text(r.password,password);return platform->Action(r,message);};
    assert(!action(AH_SCAN));assert(!action(AH_DISCONNECT));assert(!action(AH_WIFI_OFF));assert(!action(AH_WIFI_ON));
    assert(!action(AH_CONNECT,id("NEO98 Saved")));
    assert(action(AH_CONNECT,id("NEO98 Secure"),"\xff")==87);
    assert(!action(AH_CONNECT,id("NEO98 Secure"),"pr\xc3\xbc" "fpasswort",1));
    assert(!action(AH_CONNECT,id("NEO98 Open")));
    assert(action(AH_CONNECT,id("NEO98 Enterprise"),"irrelevant")==50);
    assert(action(AH_CONNECT,id("NEO98 WEP"),"irrelevant")==50);
    assert(action(AH_CONNECT,0xdead)==1167);
    assert(!action(AH_FORGET,id("NEO98 Saved")));
    {AH_Request r{};r.version=AH_VERSION;r.action=AH_CONNECT_HIDDEN;r.flags=AH_HIDDEN_SAVE|AH_HIDDEN_SECURE;Text(r.ssid,"NEO98 Hidden");Text(r.password,"hiddenkey1");assert(!platform->Action(r,message));}
    {AH_Request r{};r.version=AH_VERSION;r.action=AH_CONNECT_HIDDEN;r.flags=AH_HIDDEN_SECURE;Text(r.ssid,"NEO98 Hidden");assert(platform->Action(r,message)==87);}
    assert(!action(AH_SELECT_ADAPTER,1));refresh();assert(status.adapter==1&&!status.wifi_connected&&!status.wifi_bitrate);
    assert(!action(AH_SELECT_ADAPTER,0));refresh();assert(status.adapter==0&&status.wifi_connected);
    const unsigned medium=status.medium[0].id;std::string path;
    assert(!platform->Mount(medium,path,message)&&path==argv[1]);refresh();
    assert(status.medium[0].flags&AH_MEDIA_MOUNTED);
    assert(!platform->Eject(medium,message));
    assert(platform->Mount(0xdead,path,message)==1167);
    assert(!action(AH_CAMERA_STOP));
    std::cout<<"HOST DEVICES PASS: battery mapping, UTF-8, NetworkManager actions, hidden networks, adapter choice, bitrate and IPv4, UDisks mount/eject and internal-disk filtering\n";
}
