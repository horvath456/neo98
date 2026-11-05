// SPDX-License-Identifier: GPL-2.0-or-later
// Native ordinary-DC providers for the original coordinate state and thunks.
namespace {
struct MappingDevice
{
    const Context *dc=nullptr;
    // Explicit native display profile: equal 96-DPI axes. The original body
    // consumes driver-supplied WORD extents; no physical monitor is inferred.
    // A printer DC has the extents its driver wrote into GDIINFO+28h.
    Win98DcMap::Units DeviceUnits(unsigned mode)
    {
        if(dc&&dc->device_info&&0x28+mode*8<=dc->device_info_bytes){
            const unsigned char *p=dc->device_info+0x28+(mode-1)*8;
            auto word=[&](unsigned i){return int(short(p[i]|p[i+1]<<8));};
            return {{word(0),word(2)},{word(4),word(6)}};
        }
        static constexpr int unit[]={1,254,2540,100,1000,1440};
        return {{unit[mode-1],unit[mode-1]},{96,-96}};
    }
    Win98DcMap::Point DeviceAspect() { return {1,1}; }
    bool ObjectAffectsMapping() { return false; } // No mapped-pen realization yet.
};
struct MappingMemory
{
    BitmapIO &io;
    unsigned base;
    bool okay=true;
    unsigned Read16(unsigned at)
    { unsigned short v=0;if(!io.Read(base+at,&v,2))okay=false;return v; }
    unsigned Read32(unsigned at)
    { unsigned v=0;if(!io.Read(base+at,&v,4))okay=false;return v; }
    void Write16(unsigned at,unsigned value)
    { const unsigned short v=value;if(!io.Write(base+at,&v,2))okay=false; }
    void Write32(unsigned at,unsigned value)
    { if(!io.Write(base+at,&value,4))okay=false; }
    unsigned ReadPacked(unsigned at) { return Read16(at); }
    void WritePacked(unsigned at,unsigned value) { Write16(at,value); }
    void BeginPacked() {}
    void EndPacked() {}
};
bool MappingRange(BitmapIO &io,unsigned p,unsigned bytes)
{
    if(!p || !bytes || (unsigned long long)p+bytes>0x100000000ull)return false;
    for(unsigned i=0;i<bytes;i+=4096)
        if(!io.Probe(p+i,false) || !io.Probe(p+i,true))return false;
    return io.Probe(p+bytes-1,false) && io.Probe(p+bytes-1,true);
}
constexpr const char *MappingNames[]={
    "GetMapMode","SetMapMode","GetWindowOrgEx","GetViewportOrgEx",
    "GetWindowExtEx","GetViewportExtEx","GetDCOrgEx","SetWindowOrgEx",
    "SetViewportOrgEx","OffsetWindowOrgEx","OffsetViewportOrgEx",
    "SetWindowExtEx","SetViewportExtEx","ScaleWindowExtEx","ScaleViewportExtEx",
    "LPtoDP","DPtoLP"
};
}
constexpr ApiIdTable MappingIds(MappingNames);
bool IsMappingApi(ApiName name)
{
    return MappingIds.Has(name.id);
}
unsigned MapApi(ApiName name,const unsigned *a,BitmapIO &io)
{
    const unsigned operation=MappingIds.Index(name.id);
    auto *dc=DC(a[0]);
    if(!dc || operation>=17)return 0;
    auto &s=dc->current.mapping;MappingDevice device;device.dc=dc;
    if(operation==0)return s.mode;
    if(operation==1)return Win98DcMap::SetMode(device,s,a[1]);
    if(operation>=15)
    {
        const unsigned count=a[2];
        // Native mapper quota is one million points. Its packed selector has
        // offset zero, so the original WORD wrap is relative to this buffer.
        // For count zero the original LOOP touches a full 64 KiB; require that
        // complete backing range instead of silently writing outside a probe.
        const unsigned bytes=count ? count*8 : 65536;
        if(count>1048576 || !MappingRange(io,a[1],bytes) ||
           (!count && !io.Probe(a[1]-1,true))) {io.Error(87);return 0;}
        MappingMemory memory{io,a[1]};
        const auto result=Win98DcMap::ConvertPoints(memory,s,count,operation==16);
        if(!memory.okay){io.Error(87);return 0;}
        return result;
    }
    if(operation<=6)
    {
        const auto value=Win98DcMap::Get(s,Win98DcMap::Field(operation-2));
        const POINT out={value.x,value.y};
        if(a[1] && (!MappingRange(io,a[1],sizeof out) || !io.Write(a[1],&out,sizeof out)))
        {io.Error(87);return 0;}
        return 1;
    }
    const unsigned pointer=a[operation>=13 ? 5 : 3];
    if(pointer && !MappingRange(io,pointer,8)){io.Error(87);return 0;}
    const auto changed=Win98DcMap::ChangeEx(device,s,Win98DcMap::Change(operation-7),a+1);
    if(pointer && changed.written)
    {
        const POINT out={changed.previous.x,changed.previous.y};
        if(!io.Write(pointer,&out,sizeof out)){io.Error(87);return 0;}
    }
    // The PE thunk exposes uninitialized temporary bytes when the NE wrapper
    // declines its output. Native policy leaves that failed output untouched.
    return changed.result;
}
bool SetDeviceOrigin(HDC handle,int x,int y,bool local_surface)
{
    auto *dc=DC(handle);if(!dc)return false;
    auto &s=dc->current.mapping;
    s.device_origin=Win98DcMap::Narrow({x,y});
    const auto old=s.viewport_origin;
    Win98DcMap::SetOrigin(s,true,old);
    dc->current.surface_origin=local_surface ? POINT{s.device_origin.x,s.device_origin.y} : POINT{};
    return true;
}
bool GetDeviceOrigin(HDC handle,POINT *point)
{
    const auto *dc=DC(handle);if(!dc||!point)return false;
    const auto origin=dc->current.mapping.device_origin;
    *point={origin.x,origin.y};return true;
}
