// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../../src/host_devices_abi.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdlib.h>
#include <string>
#include <vector>
namespace HostDeviceService
{
inline unsigned Id(const std::string &name)
{
    unsigned value=2166136261u; for(unsigned char c:name) value=(value^c)*16777619u;
    return value ? value : 1;
}
template <unsigned long N> inline void Text(char (&out)[N], const std::string &in)
{
    auto bytes=in.size()<N-1 ? in.size() : N-1;
    // Keep a truncated UTF-8 name valid. SSID bytes travel separately.
    if(bytes<in.size())while(bytes&&((unsigned char)in[bytes]&0xc0)==0x80)--bytes;
    for(unsigned long i=0;i<bytes;++i) out[i]=in[i]; out[bytes]=0;
}
// One camera picture of 32-bit BGRA rows into the frame Windows reads. A
// picture larger than 640x480 is reduced to fit, keeping its proportions,
// with the nearest pixel: a camera that offers no 640x480 mode (or ignores
// the session preset) delivered 1280x720 and the frame stayed empty, the
// preview black.
inline bool CopyFrame(AH_Frame &frame, const unsigned char *bytes, unsigned width, unsigned height, unsigned stride)
{
    if(!bytes||!width||!height||stride<width*4)return false;
    unsigned w=width,h=height;
    if(w>640||h>480)
    {
        if((unsigned long long)width*480>(unsigned long long)height*640){w=640;h=unsigned((unsigned long long)height*640/width);}
        else{h=480;w=unsigned((unsigned long long)width*480/height);}
        if(!w)w=1;if(!h)h=1;
    }
    frame.width=w;frame.height=h;frame.stride=w*4;frame.bytes=frame.stride*h;++frame.sequence;
    for(unsigned y=0;y<h;++y)
    {
        const unsigned char *row=bytes+(unsigned long long)(y*height/h)*stride;unsigned char *out=frame.pixels+y*frame.stride;
        if(w==width){for(unsigned x=0;x<w*4;++x)out[x]=row[x];continue;}
        for(unsigned x=0;x<w;++x){const unsigned char *p=row+(x*width/w)*4;out[x*4]=p[0];out[x*4+1]=p[1];out[x*4+2]=p[2];out[x*4+3]=p[3];}
    }
    return true;
}
struct Medium { unsigned id=0, flags=0; std::string name, path, object, drive, image_source; };
// The folders the host already serves as drives, set by the host whenever a
// drive changes. A disc image the host attached with hdiutil for "Open with"
// is a mounted, ejectable volume of the Mac, and without this the service
// gave the same disc a second letter (E: and F:). Win98 has the disc
// once, in its CD-ROM drive.
inline std::mutex claimed_lock;
inline std::set<std::string> claimed_paths;
inline std::string Canonical(const std::string &path)
{
    char real[4096];
    return realpath(path.c_str(),real)?std::string(real):path;
}
inline void Claim(const std::string *roots,unsigned count)
{
    std::set<std::string> paths;
    for(unsigned i=0;i<count;++i)if(!roots[i].empty())paths.insert(Canonical(roots[i]));
    std::lock_guard<std::mutex> guard(claimed_lock);claimed_paths=std::move(paths);
}
inline bool Claimed(const std::string &path)
{
    const auto real=Canonical(path);
    std::lock_guard<std::mutex> guard(claimed_lock);return claimed_paths.count(real)!=0;
}
// Takes the claimed volumes out of what a platform found.
inline void DropClaimed(AH_Status &out,std::map<unsigned,Medium> &media)
{
    for(auto i=media.begin();i!=media.end();)
    {
        if(!Claimed(i->second.path) && (i->second.image_source.empty() || !Claimed(i->second.image_source))){++i;continue;}
        for(unsigned k=0;k<out.media;++k)
            if(out.medium[k].id==i->first){for(unsigned m=k;m+1<out.media;++m)out.medium[m]=out.medium[m+1];--out.media;break;}
        i=media.erase(i);
    }
}
struct Platform
{
    virtual ~Platform() = default;
    // Network > WLAN in Windows: off, Refresh leaves the wireless adapter
    // alone and Windows sees none.
    bool wifi = false;
    // True while the host asks the user for camera access (macOS); the
    // Camera program says so instead of showing a black picture.
    std::atomic<bool> asking_camera{false};
    virtual void Cancel() {}
    virtual void Refresh(AH_Status &, std::map<unsigned,Medium> &) = 0;
    virtual unsigned Action(const AH_Request &, std::string &) = 0;
    virtual unsigned Mount(unsigned id, std::string &, std::string &) = 0;
    virtual unsigned Eject(unsigned id, std::string &) = 0;
    virtual void CameraTick(AH_Frame &) = 0;
};
std::unique_ptr<Platform> MakePlatform();
void RequestPermission(unsigned action);
}
