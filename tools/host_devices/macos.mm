// SPDX-License-Identifier: GPL-2.0-or-later
#include "platform.h"
#import <AppKit/AppKit.h>
// CoreWLANUtil.h declares its C functions without extern "C" in some SDKs.
extern "C" {
#import <CoreWLAN/CoreWLANUtil.h>
}
#import <CoreWLAN/CoreWLAN.h>
#import <CoreLocation/CoreLocation.h>
#include <atomic>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <IOKit/IOKitLib.h>
#include <algorithm>
#include <cstring>
#include <future>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <mutex>
#include <sys/mount.h>
@interface AHLocationPermission : NSObject<CLLocationManagerDelegate>
@property(nonatomic,strong) CLLocationManager *manager;
@end
@implementation AHLocationPermission
- (void)locationManagerDidChangeAuthorization:(CLLocationManager *)manager {}
@end
@interface AHCameraReceiver : NSObject<AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic,assign) void *owner;
@property(nonatomic,assign) void (*receive)(void *,CMSampleBufferRef);
@end
@implementation AHCameraReceiver
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)buffer fromConnection:(AVCaptureConnection *)connection
{if(self.owner&&self.receive)self.receive(self.owner,buffer);}
@end
namespace HostDeviceService
{
namespace
{
std::string String(NSString *s){return s.UTF8String?s.UTF8String:"";}
NSString *Native(const std::string &s){return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];}
// A Finder-mounted image and its ISO provider are the same medium. Find
// the backing file through the disk's IOKit parents, without mounting it.
std::string ImageSource(const std::string &mount)
{
    struct statfs volume{};
    if (statfs(mount.c_str(), &volume) || strncmp(volume.f_mntfromname, "/dev/", 5)) return {};
    io_service_t disk = IOServiceGetMatchingService(kIOMainPortDefault,
        IOBSDNameMatching(kIOMainPortDefault, 0, volume.f_mntfromname + 5));
    if (!disk) return {};
    CFTypeRef source = IORegistryEntrySearchCFProperty(disk, kIOServicePlane,
        CFSTR("image-path"), kCFAllocatorDefault,
        kIORegistryIterateRecursively | kIORegistryIterateParents);
    IOObjectRelease(disk);
    if (!source) return {};
    std::string path;
    if (CFGetTypeID(source) == CFDataGetTypeID()) {
        auto data = static_cast<CFDataRef>(source);
        const char *bytes = reinterpret_cast<const char *>(CFDataGetBytePtr(data));
        path.assign(bytes, strnlen(bytes, size_t(CFDataGetLength(data))));
    } else if (CFGetTypeID(source) == CFStringGetTypeID()) {
        path = String((__bridge NSString *)source);
    }
    CFRelease(source);
    return path;
}
// What the Camera program of Windows says when macOS refuses the camera.
constexpr char CameraAccess[]="On the Mac, open System Settings, click Privacy & Security, click Camera, and turn on NEO98. Then close Camera and open it again.";
class Mac final:public Platform
{
    std::atomic<bool> cancelled{false};
    std::map<unsigned,Medium> media;
    std::map<unsigned,std::string> ssids,cameras;
    unsigned adapter=0; // the index into the sorted interface names Windows uses
    static NSArray<AVCaptureDevice *> *Cameras()
    {
        // Built-in and external cameras; the external type got its new name with macOS 14.
        AVCaptureDeviceType external;
        if (@available(macOS 14.0, *)) external = AVCaptureDeviceTypeExternal;
        else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            external = AVCaptureDeviceTypeExternalUnknown;
#pragma clang diagnostic pop
        }
        return [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera, external]
            mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified].devices;
    }
    std::vector<std::string> Adapters()
    {
        std::vector<std::string> names;for(NSString *name in [CWWiFiClient.sharedWiFiClient interfaceNames])names.push_back(String(name));
        std::sort(names.begin(),names.end());if(names.size()>AH_MAX_ADAPTERS)names.resize(AH_MAX_ADAPTERS);return names;
    }
    CWInterface *Interface()
    {
        const auto names=Adapters();if(names.empty())return CWWiFiClient.sharedWiFiClient.interface;
        if(adapter>=names.size())adapter=0;return [CWWiFiClient.sharedWiFiClient interfaceWithName:Native(names[adapter])];
    }
    static unsigned Address(NSString *name)
    {
        unsigned value=0;ifaddrs *list=nullptr;if(getifaddrs(&list))return 0;
        for(auto *item=list;item;item=item->ifa_next)
            if(item->ifa_addr&&item->ifa_addr->sa_family==AF_INET&&name&&!strcmp(item->ifa_name,name.UTF8String))
            {memcpy(&value,&((sockaddr_in*)item->ifa_addr)->sin_addr,4);break;}
        freeifaddrs(list);return value;
    }
    std::mutex frame_lock;
    std::unique_ptr<AH_Frame> camera_frame=std::make_unique<AH_Frame>();
    AVCaptureSession *session=nil;
    AHCameraReceiver *receiver=nil;
    dispatch_queue_t queue=nullptr;
    void Stop()
    {
        [session stopRunning];
        for(AVCaptureOutput *output in session.outputs)if([output isKindOfClass:AVCaptureVideoDataOutput.class])[(AVCaptureVideoDataOutput*)output setSampleBufferDelegate:nil queue:nullptr];
        if(queue)dispatch_sync(queue,^{});
        receiver.owner=nullptr;receiver=nil;session=nil;queue=nullptr;
        std::lock_guard<std::mutex> guard(frame_lock);camera_frame->bytes=0;
    }
    static void Receive(void *owner,CMSampleBufferRef buffer)
    {
        auto &self=*static_cast<Mac*>(owner);CVImageBufferRef image=CMSampleBufferGetImageBuffer(buffer);if(!image)return;
        CVPixelBufferLockBaseAddress(image,kCVPixelBufferLock_ReadOnly);
        const auto width=CVPixelBufferGetWidth(image),height=CVPixelBufferGetHeight(image),stride=CVPixelBufferGetBytesPerRow(image);
        if(CVPixelBufferGetBaseAddress(image)&&CVPixelBufferGetPixelFormatType(image)==kCVPixelFormatType_32BGRA)
        {std::lock_guard<std::mutex> guard(self.frame_lock);CopyFrame(*self.camera_frame,(const unsigned char*)CVPixelBufferGetBaseAddress(image),unsigned(width),unsigned(height),unsigned(stride));}
        CVPixelBufferUnlockBaseAddress(image,kCVPixelBufferLock_ReadOnly);
    }
    template<class T> bool Ready(std::future<T> &future,unsigned seconds)
    {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
        while(!cancelled&&std::chrono::steady_clock::now()<deadline)
            if(future.wait_for(std::chrono::milliseconds(100))==std::future_status::ready)return true;
        return false;
    }
    unsigned Start(unsigned id,std::string &message)
    {
        Stop();auto found=cameras.find(id);if(found==cameras.end())return 1167;
        const auto permission=[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
        if(permission==AVAuthorizationStatusNotDetermined)
        {
            auto promise=std::make_shared<std::promise<bool>>();auto future=promise->get_future();
            asking_camera=true;[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted){promise->set_value(granted);}];
            const bool answered=Ready(future,60);asking_camera=false;
            if(!answered||!future.get()){message=CameraAccess;return 5;}
        }
        else if(permission!=AVAuthorizationStatusAuthorized){message=CameraAccess;return 5;}
        AVCaptureDevice *camera=[AVCaptureDevice deviceWithUniqueID:Native(found->second)];NSError *error=nil;
        AVCaptureDeviceInput *input=[AVCaptureDeviceInput deviceInputWithDevice:camera error:&error];
        if(!input){message="The camera cannot be opened."+(error?" (error "+std::to_string((long)error.code)+")":std::string());return 21;}
        session=[AVCaptureSession new];if([session canSetSessionPreset:AVCaptureSessionPreset640x480])session.sessionPreset=AVCaptureSessionPreset640x480;
        if(![session canAddInput:input]){Stop();return 50;}[session addInput:input];
        AVCaptureVideoDataOutput *output=[AVCaptureVideoDataOutput new];output.videoSettings=@{(NSString*)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA)};output.alwaysDiscardsLateVideoFrames=YES;
        receiver=[AHCameraReceiver new];receiver.owner=this;receiver.receive=Receive;queue=dispatch_queue_create("neo98.camera",DISPATCH_QUEUE_SERIAL);
        [output setSampleBufferDelegate:receiver queue:queue];if(![session canAddOutput:output]){Stop();return 50;}[session addOutput:output];[session startRunning];return 0;
    }
public:
    ~Mac()override{@autoreleasepool{Stop();}}
    void Cancel()override{cancelled=true;}
    void Refresh(AH_Status &out,std::map<unsigned,Medium> &result)override
    {@autoreleasepool{
        out.features|=AH_MEDIA|AH_USB;
        if(wifi)
        {
        out.features|=AH_WIFI;
        const auto names=Adapters();out.adapters=unsigned(names.size());for(unsigned i=0;i<out.adapters;++i)Text(out.adapter_name[i],names[i]);
        CWInterface *wifi=Interface();out.adapter=adapter;out.wifi_on=wifi.powerOn;out.wifi_connected=wifi.ssid.length>0;Text(out.wifi_name,String(wifi.ssid));
        if(out.wifi_connected){out.wifi_bitrate=unsigned(wifi.transmitRate*1000);out.wifi_address=Address(wifi.interfaceName);}
        NSError *error=nil;NSSet<CWNetwork*> *networks=[wifi cachedScanResults];
        ssids.clear();
        for(CWNetwork *network in networks)
        {
            NSData *ssid=network.ssidData;if(!ssid.length||ssid.length>32)continue;
            std::string raw((const char*)ssid.bytes,ssid.length);unsigned id=Id("wifi:"+raw);
            bool duplicate=false;for(unsigned i=0;i<out.networks;++i)if(out.network[i].id==id)duplicate=true;
            if(duplicate||out.networks==AH_MAX_NETWORKS)continue;
            auto &n=out.network[out.networks++];n.id=id;n.ssid_bytes=unsigned(ssid.length);memcpy(n.ssid,ssid.bytes,ssid.length);Text(n.name,String(network.ssid));
            n.signal=unsigned(std::clamp(2*(int(network.rssiValue)+100),0,100));
            if([network.ssid isEqualToString:wifi.ssid])n.flags|=AH_NET_ACTIVE;
            if(![network supportsSecurity:kCWSecurityNone])n.flags|=AH_NET_SECURE;
            if([network supportsSecurity:kCWSecurityWPA2Enterprise]||[network supportsSecurity:kCWSecurityWPAEnterprise])n.flags|=AH_NET_ENTERPRISE;
            for(CWNetworkProfile *profile in wifi.configuration.networkProfiles)if([profile.ssid isEqualToString:network.ssid])n.flags|=AH_NET_SAVED;
            ssids[id]=raw;
        }
        if(!wifi)Text(out.message,"No wireless network adapter was found.");
        else if(!out.networks&&wifi.powerOn)Text(out.message,"Showing wireless networks may need Location Services for NEO98 in macOS.");
        (void)error;
        }
        media.clear();
        NSArray<NSURL*> *volumes=[NSFileManager.defaultManager mountedVolumeURLsIncludingResourceValuesForKeys:@[NSURLVolumeIsRemovableKey,NSURLVolumeIsEjectableKey,NSURLVolumeIsReadOnlyKey,NSURLVolumeNameKey,NSURLVolumeLocalizedFormatDescriptionKey,NSURLVolumeTotalCapacityKey] options:0];
        for(NSURL *url in volumes)
        {
            NSNumber *removable=nil,*ejectable=nil,*readonly=nil;NSString *name=nil,*format=nil;
            [url getResourceValue:&removable forKey:NSURLVolumeIsRemovableKey error:nil];[url getResourceValue:&ejectable forKey:NSURLVolumeIsEjectableKey error:nil];
            if(!removable.boolValue&&!ejectable.boolValue)continue;
            [url getResourceValue:&readonly forKey:NSURLVolumeIsReadOnlyKey error:nil];[url getResourceValue:&name forKey:NSURLVolumeNameKey error:nil];[url getResourceValue:&format forKey:NSURLVolumeLocalizedFormatDescriptionKey error:nil];
            Medium m;m.path=String(url.path);m.image_source=ImageSource(m.path);m.id=Id("media:"+m.path);m.name=String(name);m.flags=AH_MEDIA_MOUNTED|(readonly.boolValue?AH_MEDIA_READONLY:0);
            if([format rangeOfString:@"UDF" options:NSCaseInsensitiveSearch].location!=NSNotFound||[format rangeOfString:@"ISO 9660" options:NSCaseInsensitiveSearch].location!=NSNotFound)m.flags|=AH_MEDIA_OPTICAL|AH_MEDIA_READONLY;
            // macOS names no floppy drive; a medium of at most 2.88 MB is a
            // floppy disk and becomes A: (FAT12 of 720 KB, 1.44 MB, 2.88 MB).
            NSNumber *capacity=nil;[url getResourceValue:&capacity forKey:NSURLVolumeTotalCapacityKey error:nil];
            if(!(m.flags&AH_MEDIA_OPTICAL)&&capacity&&capacity.unsignedLongLongValue&&capacity.unsignedLongLongValue<=2949120)m.flags|=AH_MEDIA_FLOPPY;
            media[m.id]=m;result[m.id]=m;if(out.media<AH_MAX_MEDIA){auto &v=out.medium[out.media++];v.id=m.id;v.flags=m.flags;Text(v.name,m.name);}
        }
        io_iterator_t iterator=0;
        if(IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching("IOUSBHostDevice"),&iterator)==KERN_SUCCESS)
        {for(io_object_t item;(item=IOIteratorNext(iterator));)
            {CFMutableDictionaryRef props=nullptr;if(IORegistryEntryCreateCFProperties(item,&props,kCFAllocatorDefault,0)==KERN_SUCCESS)
                {NSDictionary *p=CFBridgingRelease(props);if(out.devices<AH_MAX_DEVICES){auto &d=out.device[out.devices++];uint64_t registry_id=0;IORegistryEntryGetRegistryEntryID(item,&registry_id);d.id=Id("usb:"+std::to_string(registry_id));d.vendor=[p[@"idVendor"] unsignedIntValue];d.product=[p[@"idProduct"] unsignedIntValue];Text(d.name,String(p[@"USB Product Name"]?:p[@"USB Vendor Name"]?:@"USB device"));Text(d.kind,"USB device");}}
                IOObjectRelease(item);}IOObjectRelease(iterator);}
        cameras.clear();
        for(AVCaptureDevice *camera in Cameras())
        {const auto path=String(camera.uniqueID);unsigned id=Id("camera:"+path);cameras[id]=path;out.features|=AH_CAMERA;if(!out.camera_id)out.camera_id=id;if(out.devices<AH_MAX_DEVICES){auto &d=out.device[out.devices++];d.id=id;Text(d.name,String(camera.localizedName));Text(d.kind,"Camera");}}
    }}
    unsigned Mount(unsigned id,std::string &path,std::string &)override{auto found=media.find(id);if(found==media.end())return 1167;path=found->second.path;return 0;}
    unsigned Eject(unsigned id,std::string &message)override
    {@autoreleasepool{
        auto found=media.find(id);if(found==media.end())return 1167;
        auto promise=std::make_shared<std::promise<std::string>>();auto future=promise->get_future();NSURL *url=[NSURL fileURLWithPath:Native(found->second.path)];
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{NSError *error=nil;BOOL okay=[NSWorkspace.sharedWorkspace unmountAndEjectDeviceAtURL:url error:&error];promise->set_value(okay?std::string():error?"The device cannot be stopped right now. (error "+std::to_string((long)error.code)+")":std::string("The device cannot be stopped right now."));});
        if(!Ready(future,30))return 1460;message=future.get();return message.empty()?0:5;
    }}
    unsigned Action(const AH_Request &request,std::string &message)override
    {@autoreleasepool{
        if(request.action==AH_CONNECT&&request.password[0]&&!Native(request.password))return 87;
        if(request.action==AH_CAMERA_START)return Start(request.id,message);
        if(request.action==AH_CAMERA_STOP){Stop();return 0;}
        if(request.action==AH_SELECT_ADAPTER){adapter=request.id<AH_MAX_ADAPTERS?request.id:0;return 0;}
        CWInterface *wifi=Interface();if(!wifi)return 1167;NSError *error=nil;BOOL okay=NO;
        if(request.action==AH_WIFI_ON||request.action==AH_WIFI_OFF)okay=[wifi setPower:request.action==AH_WIFI_ON error:&error];
        else if(request.action==AH_DISCONNECT){[wifi disassociate];okay=YES;}
        else if(request.action==AH_SCAN)okay=[wifi scanForNetworksWithSSID:nil error:&error]!=nil;
        else if(request.action==AH_CONNECT_HIDDEN)
        {
            const auto length=strnlen(request.ssid,32);if(!length)return 87;
            NSData *ssid=[NSData dataWithBytes:request.ssid length:length];
            // A scan for the name probes for a network that does not announce itself.
            CWNetwork *network=[wifi scanForNetworksWithSSID:ssid error:&error].anyObject;
            if(!network){message="Windows cannot find this network.";return 1168;}
            okay=[wifi associateToNetwork:network password:(request.flags&AH_HIDDEN_SECURE)?Native(request.password):nil error:&error];
        }
        else
        {
            auto found=ssids.find(request.id);if(found==ssids.end())return 1167;
            NSData *ssid=[NSData dataWithBytes:found->second.data() length:found->second.size()];
            if(request.action==AH_CONNECT)
            {
                NSSet<CWNetwork*> *matches=[wifi scanForNetworksWithSSID:ssid error:&error];CWNetwork *network=matches.anyObject;
                if(network)
                {
                    if([network supportsSecurity:kCWSecurityWPA2Enterprise]||[network supportsSecurity:kCWSecurityWPAEnterprise])
                    {message="Connect to enterprise networks in macOS first.";return 50;}
                    NSString *password=request.password[0]?Native(request.password):nil;
                    if(!password)CWKeychainFindWiFiPassword(kCWKeychainDomainUser,ssid,&password);
                    // macOS keeps the key of a network it joined itself in the
                    // System keychain, which a program may not read without an
                    // administrator. Windows asks for the key instead, as it
                    // does for a network without saved settings.
                    if(!password&&[network supportsSecurity:kCWSecurityNone]==NO)
                    {message="The network key is needed to connect to this network.";return 86;}
                    okay=[wifi associateToNetwork:network password:password error:&error];
                    if(!okay&&password&&!request.password[0])
                    {message="The network key is needed to connect to this network.";return 86;}
                    if(okay&&(request.flags&1))
                    {
                        CWMutableConfiguration *configuration=[wifi.configuration mutableCopy];
                        NSMutableOrderedSet *profiles=[configuration.networkProfiles mutableCopy];
                        for(CWNetworkProfile *p in [profiles copy])if([p.ssidData isEqualToData:ssid])[profiles removeObject:p];
                        CWMutableNetworkProfile *profile=[CWMutableNetworkProfile new];profile.ssidData=ssid;
                        profile.security=[network supportsSecurity:kCWSecurityWPA3Personal]?kCWSecurityWPA3Personal:[network supportsSecurity:kCWSecurityWPA2Personal]?kCWSecurityWPA2Personal:[network supportsSecurity:kCWSecurityWPAPersonal]?kCWSecurityWPAPersonal:kCWSecurityNone;
                        [profiles addObject:profile];configuration.networkProfiles=profiles;
                        okay=[wifi commitConfiguration:configuration authorization:nil error:&error];
                        if(okay&&password&&CWKeychainSetWiFiPassword(kCWKeychainDomainUser,ssid,password)!=0)
                        {message="Connected. The network key could not be saved in the keychain.";return 5;}
                    }
                }
            }
            else if(request.action==AH_FORGET)
            {CWMutableConfiguration *configuration=[wifi.configuration mutableCopy];NSMutableOrderedSet *profiles=[configuration.networkProfiles mutableCopy];for(CWNetworkProfile *profile in [profiles copy])if([profile.ssidData isEqualToData:ssid])[profiles removeObject:profile];configuration.networkProfiles=profiles;okay=[wifi commitConfiguration:configuration authorization:nil error:&error];if(okay)CWKeychainDeleteWiFiPassword(kCWKeychainDomainUser,ssid);}
            else return 50;
        }
        if(!okay)
        {
            // The Mac's own text is in the language of macOS; Windows 98 speaks English.
            if(request.action==AH_CONNECT||request.action==AH_CONNECT_HIDDEN)
                message=error&&error.code==kCWInvalidPMKErr?"The network key is not correct.":"Windows cannot connect to this network.";
            else message="The wireless request failed.";
            if(error)message+=" (error "+std::to_string((long)error.code)+")";
            return 5;
        }
        return 0;
    }}
    void CameraTick(AH_Frame &out)override{std::lock_guard<std::mutex> guard(frame_lock);if(camera_frame->bytes&&camera_frame->sequence!=out.sequence)memcpy(&out,camera_frame.get(),offsetof(AH_Frame,pixels)+camera_frame->bytes);}
};
}
std::unique_ptr<Platform> MakePlatform(){return std::make_unique<Mac>();}
void RequestPermission(unsigned action)
{
    if(action!=AH_SCAN&&action!=AH_CONNECT)return;
    dispatch_async(dispatch_get_main_queue(),^{
        static AHLocationPermission *permission;
        if(!permission){permission=[AHLocationPermission new];permission.manager=[CLLocationManager new];permission.manager.delegate=permission;}
        [permission.manager requestWhenInUseAuthorization];
    });
}
}
