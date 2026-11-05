// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "user_settings.h"
#include "win98_coordinates.h"
#include <string.h>
namespace X86 {
namespace {
struct Coordinates {
 void Lock(){}void Unlock(){} // Native desktop access is cooperative.
 unsigned Validate(unsigned h){return User::IsWindow(h)?h:0;}
 unsigned Desktop(){return User::GetDesktopWindow();}
 unsigned Version(){return User::SystemSettings().ExpectedVersion();}
 unsigned Metric(unsigned index){return User::SystemSettings().Word(0x2de + index*2);}
 Win98Coordinates::Window Read(unsigned h){auto w=User::HitWindowState(h);
  // PE32-owned native windows correspond to USER's Win32 window flag. Native
  // host windows with no owning process retain the legacy-window branch.
  return{{w.outer.left,w.outer.top,w.outer.right,w.outer.bottom},{w.client.left,w.client.top,w.client.right,w.client.bottom},w.style,User::WindowProcess(h)?0x40u:0u};
 }
};
}
unsigned Application::CoordinatesApi(ApiName name,const unsigned*a){
 using namespace Win98Coordinates;Coordinates e;
 if(API_IS(name,"MapWindowPoints")){
  // The native guest-buffer bound predates this port. USER32's arbitrary-size
  // segmented alias allocation and destructive in-place packing are not used.
  if(a[3]>4096){last_error=8;return 0;}
  if((a[0]&&!e.Validate(a[0]))||(a[1]&&!e.Validate(a[1])))return 0;
  if(a[3]&&(!a[2]||!virtual_memory.Access(a[2],a[3]*8,true))){last_error=87;return 0;}
  U delta=Win98Coordinates::Map(e,a[0],a[1],nullptr,0);
  for(U i=0;i<a[3];++i){Point p;if(!Read(a[2]+i*8,&p,sizeof p))return 0;Win98Coordinates::Map(e,a[0],a[1],&p,1);if(!Store(a[2]+i*8,&p,sizeof p))return 0;}
  return delta;
 }
 const bool point=API_IS(name,"ClientToScreen")||API_IS(name,"ScreenToClient");
 if(point){
  // Desktop fast path does not inspect the point pointer at all.
  if(!e.Validate(a[0])||a[0]==e.Desktop()||!a[1])return PointQuery(e,a[0],nullptr,API_IS(name,"ClientToScreen"));
  if(!virtual_memory.Access(a[1],8,true)){last_error=87;return 0;}
  Point p;if(!Read(a[1],&p,8))return 0;U result=PointQuery(e,a[0],&p,API_IS(name,"ClientToScreen"));return Store(a[1],&p,8)?result:0;
 }
 if(!e.Validate(a[0])||!a[1])return RectQuery(e,a[0],nullptr,API_IS(name,"GetClientRect"));
 if(!virtual_memory.Access(a[1],16,true)){last_error=87;return 0;}
 Rect rect;U result=RectQuery(e,a[0],&rect,API_IS(name,"GetClientRect"));return Store(a[1],&rect,sizeof rect)?result:0;
}
}
