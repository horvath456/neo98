// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "user_settings.h"
#include "win98_monitors.h"
#include "neo98_hardware.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
namespace X86 {
namespace {
using Win98Monitors::Rect;
using U=unsigned;
// Opaque native HMONITOR. The original handle is the DGROUP offset of the
// monitor record (0x11A2 + index * 0x4E); we never hand a host address out.
constexpr U kHandle=0x4d310000;
struct Display{
 // One display: the framebuffer. Screen size, virtual screen and monitor count
 // come from the shared settings provider that also answers GetSystemMetrics;
 // the work area is the SPI work area at the original record offset +0x18.
 User::Settings&s=User::SystemSettings();
 U Metric(U index)const{return s.Word(0x2de + index * 2);}
 int Signed(U index)const{return short(Metric(index));}
 U Count()const{const U monitors=Metric(80);return monitors?monitors-1:0;}
 Rect Monitor(U)const{return{0,0,Signed(0),Signed(1)};}
 Rect Work(U)const{return{short(s.Word(0x11ba)),short(s.Word(0x11bc)),short(s.Word(0x11be)),short(s.Word(0x11c0))};}
 Rect VirtualScreen()const{return{Signed(76),Signed(77),Signed(76)+Signed(78),Signed(77)+Signed(79)};}
 // The original reads the primary name from the display VxD through USER
 // (int 2Fh/1684h, device 0x000A, function 0x8D). That driver call has
 // no counterpart here, so the framebuffer reports the standard first name.
 void Name(U index,char(&out)[32])const{memset(out,0,sizeof out);const char*p="\\\\.\\DISPLAY1";if(!index)memcpy(out,p,strlen(p));}
 U Handle(U index)const{return kHandle+index;}
 bool Index(U handle,U&index)const{if(handle<kHandle||handle-kHandle>Count())return false;index=handle-kHandle;return true;}
 U Sequence()const{return 0;} // The native monitor list never changes while a callback runs.
 U Validate(U hwnd)const{return User::IsWindow(hwnd)?hwnd:0;}
 // No native window carries the original per-window monitor property, so the
 // pinned-rectangle branch of USER stays unreachable here.
 bool HasProperties(U)const{return false;}
 bool MonitorProperty(U,Rect&)const{return false;}
 bool ParentIsDesktop(U hwnd)const{return User::Parent(hwnd)==User::GetDesktopWindow();}
 Rect WindowRect(U hwnd)const{auto w=User::HitWindowState(hwnd);return{short(w.outer.left),short(w.outer.top),short(w.outer.right),short(w.outer.bottom)};}
};
} // namespace
unsigned Application::MonitorsApi(ApiName name,const unsigned*a){
 using namespace Win98Monitors;Display e;
 // USER32 is the shared unimplemented wide stub; it zeroes EAX and
 // tails into KERNEL32 ordinal 17 with CL = 2, sharing its body with
 // MapVirtualKeyW. The reported error value is ours, not measured.
 switch (name.id)
 {
 API_CASE("GetMonitorInfoW"){last_error=120;return 0;}
 // The original wrapper zero-extends the 16-bit handle the thunk returns;
 // the opaque native handle stays full width, like the generation HWNDs.
 API_CASE("MonitorFromPoint")return FromPoint(e,short(a[0]),short(a[1]),a[2]&65535);
 // The original wrapper narrows the HWND to the 16-bit handle; native
 // generation HWNDs stay full width, as in the coordinate family.
 API_CASE("MonitorFromWindow")return FromWindow(e,a[0],a[1]&65535);
 API_CASE("MonitorFromRect"){
  const U flags=a[1]&65535;
  // The original packs the caller's four LONGs into WORDs before the 16-bit
  // body runs; a null rectangle reaches the body as a null far pointer, which
  // only the single-display default shortcut survives.
  if(!a[0]){if(!e.Count()&&(flags&3))return e.Handle(0);last_error=87;return 0;}
  int v[4];if(!Read(a[0],v,sizeof v)){last_error=87;return 0;}
  const Rect rc{short(v[0]),short(v[1]),short(v[2]),short(v[3])};
  return FromRect(e,rc,flags);
 }
 // USER32 loads CL with the index 0x13 and calls the 32-to-16 thunk
 // of USER.EXE; that 16-bit body asks the display VxD for the adapters and is
 // not implemented. What this port can answer for itself is its own display:
 // the compositor's frame buffer is one adapter, attached to the desktop and
 // primary, under the first standard device name. A second adapter does not
 // exist, and the monitors below an adapter (lpDevice) are not enumerated,
 // which is the answer of an adapter with no monitor record.
 //
 // The caller that matters is DDRAW.DLL: it walks the
 // adapters, keeps the DeviceName of the first with DISPLAY_DEVICE_PRIMARY,
 // and falls back to the literal "display" when the export is missing. It then
 // passes that name to CreateDCA, so src/x86_gdi.cpp accepts it.
 API_CASE("EnumDisplayDevicesA"){
  constexpr U NameAt=4,StringAt=36,FlagsAt=164,Largest=424,Smallest=FlagsAt+4;
  if(!a[2]){last_error=87;return 0;}
  U size=0;if(!Read(a[2],&size,4)){last_error=87;return 0;}
  if(size<Smallest||size>Largest){last_error=87;return 0;}
  constexpr U IdAt=168,KeyAt=296;
  static const char class_key[]="System\\CurrentControlSet\\Services\\Class\\";
  unsigned char record[Largest]={};
  if(a[0]){
   // The monitors of one adapter: the one monitor of the registry
   // (src/neo98_hardware.h), the device the DirectX diagnosis names on its
   // page Display. DeviceID is the path below Enum, DeviceKey the driver key.
   const char*wanted=ReadString(a[0]);bool known=false;
   for(U i=0;wanted&&i<=e.Count()&&!known;++i){char name[32];e.Name(i,name);known=!strcasecmp(wanted,name);}
   if(!known||a[1])return 0;
   snprintf((char*)record+NameAt,32,"%s\\Monitor0",wanted);
   memcpy(record+StringAt,Neo98Hardware::MonitorName,sizeof Neo98Hardware::MonitorName);
   snprintf((char*)record+IdAt,128,"%s",Neo98Hardware::MonitorNode+5);
   snprintf((char*)record+KeyAt,128,"%s%s",class_key,Neo98Hardware::MonitorDriver);
   const U flags=1u;
   memcpy(record+FlagsAt,&flags,4);
   if(!virtual_memory.Access(a[2],size,true)){last_error=87;return 0;}
   return Store(a[2]+4,record+4,size-4)?1u:0u;
  }
  if(a[1]>e.Count())return 0;       // past the last adapter
  snprintf((char*)record+IdAt,128,"%s",Neo98Hardware::DisplayNode+5);
  snprintf((char*)record+KeyAt,128,"%sDisplay\\0000",class_key);
  char device[32];e.Name(a[1],device);
  memcpy(record+NameAt,device,sizeof device);
  // The adapter is the port's own: the DIB engine on the compositor's frame
  // buffer. Its description is the DriverDesc the registry carries for it
  // (src/neo98_hardware.h, src/registry_hives.h), so the display properties
  // and the Device Manager name the same adapter.
  memcpy(record+StringAt,Neo98Hardware::DisplayName,sizeof Neo98Hardware::DisplayName);
  const U flags=1u|(a[1]?0u:4u);    // ATTACHED_TO_DESKTOP, PRIMARY_DEVICE
  memcpy(record+FlagsAt,&flags,4);
  if(!virtual_memory.Access(a[2],size,true)){last_error=87;return 0;}
  // cb itself stays as the caller wrote it, like every original that reads it.
  return Store(a[2]+4,record+4,size-4)?1u:0u;
 }
 API_CASE("GetMonitorInfoA"){
  U index;if(!e.Index(a[0],index)||!a[1]){last_error=87;return 0;}
  U size=0;if(!Read(a[1],&size,4)){last_error=87;return 0;}
  Info info;info.size=size;
  if(!Query(e,index,info))return 0;
  if(!virtual_memory.Access(a[1],size,true)){last_error=87;return 0;}
  unsigned char record[0x48]={};
  memcpy(record,&info.size,4);memcpy(record+4,&info.monitor,16);memcpy(record+20,&info.work,16);memcpy(record+36,&info.flags,4);
  if(size==0x48)memcpy(record+40,info.device,32);
  return Store(a[1]+4,record+4,size-4)?1:0;
 }
 default:
     break;
 }
 // USER: a USER display DCE starts with GetClipBox, intersects the
 // optional caller rectangle, then adds GetDCOrg before the monitor walk.
 // The port has one monitor, so reuses the caller's DC throughout.
 const U dc=a[0],window=dc?User::DisplayDCWindow(dc):0;
 if(dc&&!window){last_error=87;return 0;}
 Rect clip;bool clipped=false;
 if(a[1]){int v[4];if(!Read(a[1],v,sizeof v)){last_error=87;return 0;}clip={short(v[0]),short(v[1]),short(v[2]),short(v[3])};clipped=true;}
 if(!a[2]){last_error=87;return 0;}
 Gdi::POINT origin{};
 if(dc){
  Gdi::RECT bounds{};const U kind=Gdi::GetClipBox(dc,&bounds);
  if(kind==1)return 1;
  if(!kind||!Gdi::GetDeviceOrigin(dc,&origin))return 0;
  Rect area{bounds.left,bounds.top,bounds.right,bounds.bottom};
  if(clipped){Rect meet;if(!Meet(meet,area,clip))return 1;area=meet;}
  clip={W(area.left+origin.x),W(area.top+origin.y),W(area.right+origin.x),W(area.bottom+origin.y)};
  clipped=true;
 }
 const U lease=Allocate(16);if(!lease){last_error=8;return 0;}
 auto callback=[&](U handle,const Rect&area)->U{
  const int box[4]={W(area.left-origin.x),W(area.top-origin.y),W(area.right-origin.x),W(area.bottom-origin.y)};
  if(!Store(lease,box,sizeof box))return 0;
  // Windows 98 call USER to save/restore the application
  // clip only, including an early stop. Other DC attributes survive.
  Gdi::HGDIOBJ saved=0;
  if(dc){
   if(User::DisplayDCWindow(dc)!=window)return 0;
   if(!Gdi::CopyApplicationClip(dc,saved))return 0;
   Gdi::IntersectClipRect(dc,box[0],box[1],box[2],box[3]);
  }
  const U arguments[]={handle,dc,lease,a[3]};
  const U answer=Call(a[2],arguments,4)&65535;
  if(dc){Gdi::SelectClipRegion(dc,saved);if(saved)Gdi::DeleteObject(saved);}
  return answer;
 };
 const U result=Enumerate(e,clipped?&clip:nullptr,callback,dc!=0);
 Free(lease);
 return U(int(short(result))); // USER32 sign-extends the 16-bit result.
}
} // namespace X86
