// SPDX-License-Identifier: GPL-2.0-or-later
#include "platform.h"
#include <gio/gio.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
namespace HostDeviceService
{
namespace
{
struct Variant
{
    GVariant *p=nullptr;
    Variant(GVariant *v=nullptr):p(v){}
    ~Variant(){if(p)g_variant_unref(p);}
    Variant(const Variant &)=delete; Variant &operator=(const Variant &)=delete;
    operator bool()const{return p!=nullptr;}
};
struct Network { AH_Network info{}; std::string ap, device, profile; };
class Linux final: public Platform
{
    GDBusConnection *bus=nullptr;
    GCancellable *cancel=g_cancellable_new();
    std::string failure, device, active;
    unsigned adapter=0; // the index of the Wi-Fi device Windows uses
    std::map<unsigned,Network> networks;
    std::map<unsigned,Medium> media;
    int camera=-1;
    unsigned width=0,height=0,stride=0,camera_sequence=0;
    struct Mapping { void *data=nullptr; unsigned bytes=0; } mappings[4];
    unsigned camera_id=0;
    std::map<unsigned,std::string> cameras;
    GVariant *Call(const std::string &name,const std::string &path,const char *iface,const char *method,GVariant *args=nullptr)
    {
        if(!bus){failure="The system bus is not available."; if(args){g_variant_ref_sink(args);g_variant_unref(args);}return nullptr;}
        GError *error=nullptr;
        auto *out=g_dbus_connection_call_sync(bus,name.c_str(),path.c_str(),iface,method,args,nullptr,
            G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,10000,cancel,&error);
        if(error){failure=error->message;g_error_free(error);}return out;
    }
    GVariant *NM(const std::string &path,const char *iface,const char *method,GVariant *args=nullptr)
    {return Call("org.freedesktop.NetworkManager",path,iface,method,args);}
    GVariant *Properties(const std::string &name,const std::string &path,const char *iface)
    {
        Variant reply(Call(name,path,"org.freedesktop.DBus.Properties","GetAll",g_variant_new("(s)",iface)));
        return reply ? g_variant_get_child_value(reply.p,0) : nullptr;
    }
    static unsigned Number(GVariant *dict,const char *key)
    {
        Variant v(dict?g_variant_lookup_value(dict,key,nullptr):nullptr);if(!v)return 0;
        if(g_variant_is_of_type(v.p,G_VARIANT_TYPE_BYTE))return g_variant_get_byte(v.p);
        if(g_variant_is_of_type(v.p,G_VARIANT_TYPE_UINT32))return g_variant_get_uint32(v.p);
        if(g_variant_is_of_type(v.p,G_VARIANT_TYPE_BOOLEAN))return g_variant_get_boolean(v.p);
        return 0;
    }
    static std::string String(GVariant *dict,const char *key)
    {
        Variant v(dict?g_variant_lookup_value(dict,key,nullptr):nullptr);if(!v)return {};
        if(g_variant_is_of_type(v.p,G_VARIANT_TYPE_STRING)||g_variant_is_of_type(v.p,G_VARIANT_TYPE_OBJECT_PATH))return g_variant_get_string(v.p,nullptr);
        if(g_variant_is_of_type(v.p,G_VARIANT_TYPE_BYTESTRING)){gsize n=0;auto *p=(const char*)g_variant_get_fixed_array(v.p,&n,1);return p?std::string(p,strnlen(p,n)):std::string();}
        return {};
    }
    static std::vector<std::string> Paths(GVariant *array)
    {
        std::vector<std::string> result;if(!array)return result;GVariantIter iter;g_variant_iter_init(&iter,array);const char *path;
        while(g_variant_iter_next(&iter,"&o",&path))result.emplace_back(path);return result;
    }
    static std::string Ssid(GVariant *dict)
    {
        Variant v(g_variant_lookup_value(dict,"Ssid",G_VARIANT_TYPE("ay")));if(!v)return {};
        gsize n=0;const char *s=(const char*)g_variant_get_fixed_array(v.p,&n,1);return s?std::string(s,std::min(gsize(32),n)):std::string();
    }
    void Wifi(AH_Status &out)
    {
        const char *nm="org.freedesktop.NetworkManager";
        Variant root(Properties(nm,"/org/freedesktop/NetworkManager",nm));
        if(!root){Text(out.message,failure);return;}out.features|=AH_WIFI;out.wifi_on=Number(root.p,"WirelessEnabled");
        Variant devices(g_variant_lookup_value(root.p,"Devices",G_VARIANT_TYPE("ao")));
        device.clear();active.clear();networks.clear();
        std::vector<std::string> adapters;
        for(auto &path:Paths(devices.p))
        {
            Variant base(Properties(nm,path,"org.freedesktop.NetworkManager.Device"));
            if(Number(base.p,"DeviceType")!=2||adapters.size()==AH_MAX_ADAPTERS)continue;
            Text(out.adapter_name[adapters.size()],String(base.p,"Interface"));adapters.push_back(path);
        }
        out.adapters=unsigned(adapters.size());if(adapter>=out.adapters)adapter=0;out.adapter=adapter;
        for(auto &path:adapters)
        {
            if(&path!=&adapters[adapter])continue; // only the adapter Windows uses
            Variant base(Properties(nm,path,"org.freedesktop.NetworkManager.Device"));
            device=path;active=String(base.p,"ActiveConnection");
            Variant wifi(Properties(nm,path,"org.freedesktop.NetworkManager.Device.Wireless"));if(!wifi)continue;
            const auto active_ap=String(wifi.p,"ActiveAccessPoint");
            const auto state=Number(base.p,"State");out.wifi_connected=state==100;
            if(out.wifi_connected)
            {
                out.wifi_bitrate=Number(wifi.p,"Bitrate");
                const auto config=String(base.p,"Ip4Config");
                Variant ip(config.size()>1?Properties(nm,config,"org.freedesktop.NetworkManager.IP4Config"):nullptr);
                Variant addresses(ip?g_variant_lookup_value(ip.p,"AddressData",G_VARIANT_TYPE("aa{sv}")):nullptr);
                if(addresses&&g_variant_n_children(addresses.p))
                {
                    Variant first(g_variant_get_child_value(addresses.p,0));in_addr address{};
                    if(inet_pton(AF_INET,String(first.p,"address").c_str(),&address)==1)memcpy(&out.wifi_address,&address,4);
                }
            }
            if(state>=40&&state<100)Text(out.message,"Connecting...");
            if(state==120)
            {
                Variant reason(g_variant_lookup_value(base.p,"StateReason",G_VARIANT_TYPE("(uu)")));
                unsigned code=0,ignored=0;if(reason)g_variant_get(reason.p,"(uu)",&ignored,&code);
                Text(out.message,"Windows could not connect to the network. Host reason "+std::to_string(code)+".");
            }
            Variant aps(g_variant_lookup_value(wifi.p,"AccessPoints",G_VARIANT_TYPE("ao")));
            for(auto &ap:Paths(aps.p))
            {
                Variant props(Properties(nm,ap,"org.freedesktop.NetworkManager.AccessPoint"));if(!props)continue;
                const auto ssid=Ssid(props.p);if(ssid.empty())continue;
                Network net;net.device=path;net.ap=ap;net.info.id=Id("wifi:"+ssid);net.info.ssid_bytes=unsigned(ssid.size());
                memcpy(net.info.ssid,ssid.data(),ssid.size());
                gchar *valid=g_utf8_make_valid(ssid.data(),ssid.size());Text(net.info.name,valid);g_free(valid);
                net.info.signal=Number(props.p,"Strength");
                const unsigned sec=Number(props.p,"WpaFlags")|Number(props.p,"RsnFlags");
                if(Number(props.p,"Flags")&1)net.info.flags|=AH_NET_SECURE;
                if(sec&0x200)net.info.flags|=AH_NET_ENTERPRISE;
                if(sec&0x400)net.info.flags|=AH_NET_SAE;
                if((net.info.flags&AH_NET_SECURE)&&!sec)net.info.flags|=AH_NET_WEP;
                if(ap==active_ap){net.info.flags|=AH_NET_ACTIVE;Text(out.wifi_name,net.info.name);}
                auto found=networks.find(net.info.id);
                if(found==networks.end() || (net.info.flags&AH_NET_ACTIVE) || (!(found->second.info.flags&AH_NET_ACTIVE)&&net.info.signal>found->second.info.signal))
                    networks[net.info.id]=net;
            }
            break;
        }
        // A hidden network's access point has no SSID; the active connection keeps its name.
        if(out.wifi_connected&&!out.wifi_name[0]&&active.size()>1)
        {Variant connection(Properties(nm,active,"org.freedesktop.NetworkManager.Connection.Active"));Text(out.wifi_name,String(connection.p,"Id"));}
        Variant saved(NM("/org/freedesktop/NetworkManager/Settings","org.freedesktop.NetworkManager.Settings","ListConnections"));
        Variant profiles(saved?g_variant_get_child_value(saved.p,0):nullptr);
        for(auto &path:Paths(profiles.p))
        {
            Variant reply(NM(path,"org.freedesktop.NetworkManager.Settings.Connection","GetSettings"));if(!reply)continue;
            Variant settings(g_variant_get_child_value(reply.p,0));
            Variant wireless(g_variant_lookup_value(settings.p,"802-11-wireless",G_VARIANT_TYPE("a{sv}")));if(!wireless)continue;
            Variant ss(g_variant_lookup_value(wireless.p,"ssid",G_VARIANT_TYPE("ay")));if(!ss)continue;
            gsize n=0;auto *bytes=(const char*)g_variant_get_fixed_array(ss.p,&n,1);if(!bytes||!n||n>32)continue;
            std::string ssid(bytes,n);auto &net=networks[Id("wifi:"+ssid)];
            if(!net.info.id){net.info.id=Id("wifi:"+ssid);net.device=device;net.info.ssid_bytes=unsigned(n);memcpy(net.info.ssid,bytes,n);gchar *valid=g_utf8_make_valid(bytes,n);Text(net.info.name,valid);g_free(valid);}
            net.info.flags|=AH_NET_SAVED;net.profile=path;
        }
        std::vector<AH_Network> ordered;for(auto &item:networks)ordered.push_back(item.second.info);
        std::sort(ordered.begin(),ordered.end(),[](const auto &a,const auto &b){
            if((a.flags&AH_NET_ACTIVE)!=(b.flags&AH_NET_ACTIVE))return bool(a.flags&AH_NET_ACTIVE);
            if(a.signal!=b.signal)return a.signal>b.signal;return strcmp(a.name,b.name)<0;
        });
        for(auto &item:ordered)if(out.networks<AH_MAX_NETWORKS)out.network[out.networks++]=item;
    }
    void Media(AH_Status &out,std::map<unsigned,Medium> &result)
    {
        Variant reply(Call("org.freedesktop.UDisks2","/org/freedesktop/UDisks2","org.freedesktop.DBus.ObjectManager","GetManagedObjects"));
        if(!reply)return;out.features|=AH_MEDIA;
        Variant objects(g_variant_get_child_value(reply.p,0));
        GVariantIter iter;g_variant_iter_init(&iter,objects.p);const char *path;GVariant *interfaces;
        media.clear();
        while(g_variant_iter_next(&iter,"{&o@a{sa{sv}}}",&path,&interfaces))
        {
            Variant owned(interfaces),block(g_variant_lookup_value(interfaces,"org.freedesktop.UDisks2.Block",G_VARIANT_TYPE("a{sv}")));
            if(!block||Number(block.p,"HintIgnore"))continue;
            std::string drive=String(block.p,"Drive");if(drive.empty()||drive=="/")continue;
            Variant drive_ifaces(g_variant_lookup_value(objects.p,drive.c_str(),G_VARIANT_TYPE("a{sa{sv}}")));
            Variant props(drive_ifaces?g_variant_lookup_value(drive_ifaces.p,"org.freedesktop.UDisks2.Drive",G_VARIANT_TYPE("a{sv}")):nullptr);
            if(!props)continue;
            const bool optical=Number(props.p,"Optical");const auto connection=String(props.p,"ConnectionBus"),compat=String(props.p,"Media");
            if(connection!="usb" && !Number(props.p,"Removable") && !Number(props.p,"MediaRemovable"))continue;
            Variant fs(g_variant_lookup_value(interfaces,"org.freedesktop.UDisks2.Filesystem",G_VARIANT_TYPE("a{sv}")));
            if(!fs)continue;
            Medium m;m.object=path;m.drive=drive;m.id=Id("media:"+m.object);m.name=String(block.p,"IdLabel");
            if(m.name.empty())m.name=String(props.p,"Vendor")+" "+String(props.p,"Model");
            if(m.name.find_first_not_of(' ')==std::string::npos)m.name=optical?"CD-ROM":"Removable Disk";
            if(optical)m.flags|=AH_MEDIA_OPTICAL|AH_MEDIA_READONLY;
            if(compat.find("floppy")!=std::string::npos)m.flags|=AH_MEDIA_FLOPPY;
            if(Number(block.p,"ReadOnly"))m.flags|=AH_MEDIA_READONLY;
            Variant mounts(g_variant_lookup_value(fs.p,"MountPoints",G_VARIANT_TYPE("aay")));
            if(mounts&&g_variant_n_children(mounts.p))
            {Variant first(g_variant_get_child_value(mounts.p,0));gsize n=0;const char *s=(const char*)g_variant_get_fixed_array(first.p,&n,1);if(s){m.path=std::string(s,strnlen(s,n));m.flags|=AH_MEDIA_MOUNTED;}}
            media[m.id]=m;result[m.id]=m;
            if(out.media<AH_MAX_MEDIA){auto &entry=out.medium[out.media++];entry.id=m.id;entry.flags=m.flags;Text(entry.name,m.name);}
        }
    }
    static std::string Read(const std::filesystem::path &path)
    {std::ifstream file(path);std::string value;std::getline(file,value);return value;}
    void Usb(AH_Status &out)
    {
        out.features|=AH_USB;std::error_code error;
        for(auto &entry:std::filesystem::directory_iterator("/sys/bus/usb/devices",error))
        {
            auto p=entry.path();const auto vendor=Read(p/"idVendor"),product=Read(p/"idProduct");if(vendor.empty()||product.empty())continue;
            const auto kind=Read(p/"bDeviceClass");if(kind=="09")continue;
            if(out.devices==AH_MAX_DEVICES)break;
            auto &d=out.device[out.devices++];d.id=Id(p.string());d.vendor=strtoul(vendor.c_str(),nullptr,16);d.product=strtoul(product.c_str(),nullptr,16);
            Text(d.name,Read(p/"manufacturer")+" "+Read(p/"product"));Text(d.kind,kind=="03"?"Input device":kind=="08"?"Drive":kind=="0e"?"Camera":"USB device");
        }
        cameras.clear();
        for(auto &entry:std::filesystem::directory_iterator("/sys/class/video4linux",error))
        {
            const auto path="/dev/"+entry.path().filename().string();const int fd=open(path.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC);if(fd<0)continue;
            v4l2_capability cap{};const bool okay=ioctl(fd,VIDIOC_QUERYCAP,&cap)==0;close(fd);
            unsigned flags=cap.capabilities&V4L2_CAP_DEVICE_CAPS?cap.device_caps:cap.capabilities;
            if(!okay||!(flags&V4L2_CAP_VIDEO_CAPTURE))continue;
            const auto id=Id("camera:"+path);cameras[id]=path;out.features|=AH_CAMERA;
            if(!out.camera_id)out.camera_id=id;
            if(out.devices<AH_MAX_DEVICES){auto &d=out.device[out.devices++];d.id=id;Text(d.name,(const char*)cap.card);Text(d.kind,"Camera");}
        }
    }
    void StopCamera()
    {
        if(camera<0)return;v4l2_buf_type type=V4L2_BUF_TYPE_VIDEO_CAPTURE;ioctl(camera,VIDIOC_STREAMOFF,&type);
        for(auto &m:mappings){if(m.data)munmap(m.data,m.bytes);m={};}close(camera);camera=-1;camera_id=0;
    }
    unsigned StartCamera(unsigned id)
    {
        StopCamera();auto found=cameras.find(id);if(found==cameras.end())return 1167;
        camera=open(found->second.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC);if(camera<0)return errno==EACCES?5:21;
        v4l2_format format{};format.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;format.fmt.pix.width=640;format.fmt.pix.height=480;
        format.fmt.pix.pixelformat=V4L2_PIX_FMT_YUYV;format.fmt.pix.field=V4L2_FIELD_ANY;
        if(ioctl(camera,VIDIOC_S_FMT,&format)||format.fmt.pix.pixelformat!=V4L2_PIX_FMT_YUYV||format.fmt.pix.width>640||format.fmt.pix.height>480){StopCamera();return 50;}
        width=format.fmt.pix.width;height=format.fmt.pix.height;stride=format.fmt.pix.bytesperline;
        if(!width||!height||(width&1)||stride<width*2){StopCamera();return 13;}
        v4l2_requestbuffers buffers{};buffers.count=4;buffers.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;buffers.memory=V4L2_MEMORY_MMAP;
        if(ioctl(camera,VIDIOC_REQBUFS,&buffers)||buffers.count<2){StopCamera();return 50;}
        for(unsigned i=0;i<std::min(buffers.count,4u);++i)
        {
            v4l2_buffer buffer{};buffer.type=buffers.type;buffer.memory=buffers.memory;buffer.index=i;
            if(ioctl(camera,VIDIOC_QUERYBUF,&buffer)){StopCamera();return 21;}
            auto *data=mmap(nullptr,buffer.length,PROT_READ|PROT_WRITE,MAP_SHARED,camera,buffer.m.offset);
            if(data==MAP_FAILED){StopCamera();return 8;}mappings[i]={data,buffer.length};
            if(ioctl(camera,VIDIOC_QBUF,&buffer)){StopCamera();return 21;}
        }
        auto type=V4L2_BUF_TYPE_VIDEO_CAPTURE;if(ioctl(camera,VIDIOC_STREAMON,&type)){StopCamera();return 21;}camera_id=id;return 0;
    }
public:
    Linux(){GError *error=nullptr;bus=g_bus_get_sync(G_BUS_TYPE_SYSTEM,nullptr,&error);if(error){failure=error->message;g_error_free(error);}}
    ~Linux()override{StopCamera();if(bus)g_object_unref(bus);g_object_unref(cancel);}
    void Cancel()override{g_cancellable_cancel(cancel);}
    void Refresh(AH_Status &out,std::map<unsigned,Medium> &result)override
    {if(wifi)Wifi(out);Media(out,result);Usb(out);}
    unsigned Mount(unsigned id,std::string &path,std::string &message)override
    {
        auto found=media.find(id);if(found==media.end())return 1167;
        if(!found->second.path.empty()){path=found->second.path;return 0;}
        GVariantBuilder options;g_variant_builder_init(&options,G_VARIANT_TYPE("a{sv}"));
        Variant reply(Call("org.freedesktop.UDisks2",found->second.object,"org.freedesktop.UDisks2.Filesystem","Mount",g_variant_new("(a{sv})",&options)));
        if(!reply){message=failure;return 5;}const char *mounted=nullptr;g_variant_get(reply.p,"(&s)",&mounted);path=mounted;return 0;
    }
    unsigned Eject(unsigned id,std::string &message)override
    {
        auto found=media.find(id);if(found==media.end())return 1167;
        GVariantBuilder options;
        if(!found->second.path.empty())
        {
            g_variant_builder_init(&options,G_VARIANT_TYPE("a{sv}"));
            Variant reply(Call("org.freedesktop.UDisks2",found->second.object,"org.freedesktop.UDisks2.Filesystem","Unmount",g_variant_new("(a{sv})",&options)));
            if(!reply){message=failure;return 5;}
        }
        Variant props(Properties("org.freedesktop.UDisks2",found->second.drive,"org.freedesktop.UDisks2.Drive"));
        if(Number(props.p,"Ejectable"))
        {g_variant_builder_init(&options,G_VARIANT_TYPE("a{sv}"));Variant eject(Call("org.freedesktop.UDisks2",found->second.drive,"org.freedesktop.UDisks2.Drive","Eject",g_variant_new("(a{sv})",&options)));if(!eject){message=failure;return 5;}}
        return 0;
    }
    unsigned Action(const AH_Request &request,std::string &message)override
    {
        if(request.action==AH_CONNECT&&!g_utf8_validate(request.password,-1,nullptr))return 87;
        if(request.action==AH_CAMERA_STOP){StopCamera();return 0;}
        if(request.action==AH_CAMERA_START)
        {
            const unsigned error=StartCamera(request.id);
            if(error==5)message="Linux does not let NEO98 open the camera. Add your user to the group video, log in again, and then open Camera again.";
            return error;
        }
        const char *root="/org/freedesktop/NetworkManager",*iface="org.freedesktop.NetworkManager";
        if(request.action==AH_WIFI_ON||request.action==AH_WIFI_OFF)
        {Variant r(NM(root,"org.freedesktop.DBus.Properties","Set",g_variant_new("(ssv)",iface,"WirelessEnabled",g_variant_new_boolean(request.action==AH_WIFI_ON))));if(r)return 0;}
        else if(request.action==AH_SCAN)
        {if(device.empty())return 1167;GVariantBuilder options;g_variant_builder_init(&options,G_VARIANT_TYPE("a{sv}"));Variant r(NM(device,"org.freedesktop.NetworkManager.Device.Wireless","RequestScan",g_variant_new("(a{sv})",&options)));if(r)return 0;}
        else if(request.action==AH_DISCONNECT)
        {if(device.empty())return 1167;Variant r(NM(device,"org.freedesktop.NetworkManager.Device","Disconnect"));if(r)return 0;}
        else if(request.action==AH_FORGET)
        {auto n=networks.find(request.id);if(n==networks.end()||n->second.profile.empty())return 1168;Variant r(NM(n->second.profile,"org.freedesktop.NetworkManager.Settings.Connection","Delete"));if(r)return 0;}
        else if(request.action==AH_SELECT_ADAPTER)
        {adapter=request.id<AH_MAX_ADAPTERS?request.id:0;return 0;}
        else if(request.action==AH_CONNECT_HIDDEN)
        {
            const auto length=strnlen(request.ssid,32);if(!length)return 87;
            if(device.empty())return 1167;
            GVariantBuilder config,connection,wireless,security,options;
            gchar *name=g_utf8_make_valid(request.ssid,gssize(length));
            g_variant_builder_init(&config,G_VARIANT_TYPE("a{sa{sv}}"));
            g_variant_builder_init(&connection,G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&connection,"{sv}","id",g_variant_new_string(name));g_free(name);
            g_variant_builder_add(&connection,"{sv}","type",g_variant_new_string("802-11-wireless"));
            g_variant_builder_add(&config,"{s@a{sv}}","connection",g_variant_builder_end(&connection));
            g_variant_builder_init(&wireless,G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&wireless,"{sv}","ssid",g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,request.ssid,length,1));
            g_variant_builder_add(&wireless,"{sv}","hidden",g_variant_new_boolean(true));
            g_variant_builder_add(&config,"{s@a{sv}}","802-11-wireless",g_variant_builder_end(&wireless));
            if(request.flags&AH_HIDDEN_SECURE)
            {
                if(!request.password[0])return 87;
                g_variant_builder_init(&security,G_VARIANT_TYPE("a{sv}"));
                g_variant_builder_add(&security,"{sv}","key-mgmt",g_variant_new_string("wpa-psk"));
                g_variant_builder_add(&security,"{sv}","psk",g_variant_new_string(request.password));
                g_variant_builder_add(&config,"{s@a{sv}}","802-11-wireless-security",g_variant_builder_end(&security));
            }
            g_variant_builder_init(&options,G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&options,"{sv}","persist",g_variant_new_string(request.flags&AH_HIDDEN_SAVE?"disk":"volatile"));
            Variant r(NM(root,iface,"AddAndActivateConnection2",g_variant_new("(@a{sa{sv}}ooa{sv})",g_variant_builder_end(&config),device.c_str(),"/",&options)));if(r)return 0;
        }
        else if(request.action==AH_CONNECT)
        {
            auto n=networks.find(request.id);if(n==networks.end())return 1167;const auto &net=n->second;
            if(net.info.flags&AH_NET_ACTIVE)return 0;
            if(net.device.empty())return 1167;
            if(!net.profile.empty()&&!request.password[0])
            {Variant r(NM(root,iface,"ActivateConnection",g_variant_new("(ooo)",net.profile.c_str(),net.device.c_str(),net.ap.empty()?"/":net.ap.c_str())));if(r)return 0;}
            else
            {
                if(net.info.flags&AH_NET_WEP){message="Set up this WEP network on the host first.";return 50;}
                if(net.info.flags&AH_NET_ENTERPRISE){message="Set up this enterprise network on the host first.";return 50;}
                GVariantBuilder config,connection,wireless,security,options;
                g_variant_builder_init(&config,G_VARIANT_TYPE("a{sa{sv}}"));
                g_variant_builder_init(&connection,G_VARIANT_TYPE("a{sv}"));
                g_variant_builder_add(&connection,"{sv}","id",g_variant_new_string(net.info.name));
                g_variant_builder_add(&connection,"{sv}","type",g_variant_new_string("802-11-wireless"));
                g_variant_builder_add(&config,"{s@a{sv}}","connection",g_variant_builder_end(&connection));
                g_variant_builder_init(&wireless,G_VARIANT_TYPE("a{sv}"));
                g_variant_builder_add(&wireless,"{sv}","ssid",g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,net.info.ssid,net.info.ssid_bytes,1));
                g_variant_builder_add(&config,"{s@a{sv}}","802-11-wireless",g_variant_builder_end(&wireless));
                if(net.info.flags&AH_NET_SECURE)
                {
                    if(!request.password[0])return 87;
                    g_variant_builder_init(&security,G_VARIANT_TYPE("a{sv}"));
                    g_variant_builder_add(&security,"{sv}","key-mgmt",g_variant_new_string(net.info.flags&AH_NET_SAE?"sae":"wpa-psk"));
                    g_variant_builder_add(&security,"{sv}","psk",g_variant_new_string(request.password));
                    g_variant_builder_add(&config,"{s@a{sv}}","802-11-wireless-security",g_variant_builder_end(&security));
                }
                g_variant_builder_init(&options,G_VARIANT_TYPE("a{sv}"));
                g_variant_builder_add(&options,"{sv}","persist",g_variant_new_string(request.flags&1?"disk":"volatile"));
                Variant r(NM(root,iface,"AddAndActivateConnection2",g_variant_new("(@a{sa{sv}}ooa{sv})",g_variant_builder_end(&config),net.device.c_str(),net.ap.empty()?"/":net.ap.c_str(),&options)));if(r)return 0;
            }
        }
        else return 50;
        message=failure;return 5;
    }
    void CameraTick(AH_Frame &out)override
    {
        if(camera<0)return;
        v4l2_buffer buffer{};buffer.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;buffer.memory=V4L2_MEMORY_MMAP;
        if(ioctl(camera,VIDIOC_DQBUF,&buffer))return;
        if(buffer.index<4&&mappings[buffer.index].data&&
           size_t(stride)*height<=buffer.bytesused&&buffer.bytesused<=mappings[buffer.index].bytes)
        {
            out.width=width;out.height=height;out.stride=width*4;out.bytes=width*height*4;out.sequence=++camera_sequence;
            auto clip=[](int c){return (unsigned char)std::clamp(c,0,255);};
            for(unsigned y=0;y<height;++y)
            {auto *src=(unsigned char*)mappings[buffer.index].data+y*stride;auto *dst=out.pixels+y*out.stride;
                for(unsigned x=0;x<width;x+=2)
                {int u=int(src[1])-128,v=int(src[3])-128;for(int n=0;n<2;++n){int c=298*(int(src[n*2])-16);dst[n*4]=clip((c+516*u+128)>>8);dst[n*4+1]=clip((c-100*u-208*v+128)>>8);dst[n*4+2]=clip((c+409*v+128)>>8);dst[n*4+3]=255;}src+=4;dst+=8;}
            }
        }
        ioctl(camera,VIDIOC_QBUF,&buffer);
    }
};
}
std::unique_ptr<Platform> MakePlatform(){return std::make_unique<Linux>();}
void RequestPermission(unsigned){}
}
