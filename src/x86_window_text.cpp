// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_window_text.h"
#include <string.h>
namespace X86 {
struct Application::WindowTextMemory {
 Application&app;bool failed=false;
 explicit WindowTextMemory(Application&a):app(a){}
 bool Valid(unsigned h){return User::IsWindow(h);}
 unsigned FirstChild(unsigned h){return User::GetWindow(h,5);}
 unsigned NextSibling(unsigned h){return User::GetWindow(h,2);}
 unsigned ControlID(unsigned h){return User::ControlID(h);}
 bool SameProcess(unsigned h){unsigned pid=User::WindowProcess(h);return !pid||pid==app.process_id;}
 void Clear(unsigned p){unsigned char zero=0;if(!app.Store(p,&zero,1))failed=true;}
 unsigned Send(unsigned h,unsigned message,unsigned wp,unsigned lp){
  if(failed)return 0;
  if(User::CurrentProcess()&&User::CurrentProcess()!=app.process_id){app.last_error=50;failed=true;return 0;}
  unsigned result=app.CallWindow(h,message,wp,lp);if(app.state==Failed)failed=true;return result;
 }
 unsigned Default(unsigned h,unsigned message,unsigned wp,unsigned lp){
  if(failed)return 0;
  // The original takes DefWindowProc for foreign processes. Only the caption
  // read path is supplied here; mutation may emit owner-context shell callbacks.
  if(message==0xc){app.last_error=50;failed=true;return 0;}
  return app.WindowTextDefault(h,message,wp,lp);
 }
};
unsigned Application::WindowTextDefault(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 if(!User::IsWindow(hwnd)){last_error=1400;return 0;}
 if(message==0xe)return User::CaptionLength(hwnd);
 if(message==0xd){
  char caption[User::TitleCapacity]{};User::CaptionText(hwnd,caption,sizeof caption);
  const unsigned length = unsigned(strlen(caption)) + 1;
  const unsigned bytes = wp < length ? wp : length;
  if(bytes && !virtual_memory.Access(lp,bytes,true)){last_error=87;return 0;}
  return PutString(lp,caption,wp);
 }
 if(message==0xc){
  if((User::WindowProcess(hwnd)&&User::WindowProcess(hwnd)!=process_id)||(User::CurrentProcess()&&User::CurrentProcess()!=process_id)){last_error=50;return ~0u;}
  const char*text=lp?ReadString(lp):"";if(state==Failed)return ~0u;
  if(strlen(text)>=User::TitleCapacity){last_error=8;return ~0u;}
  if(!User::SetCaptionText(hwnd,text))return ~0u;
  // EVENT_OBJECT_NAMECHANGE after the caption has its text.
  if(User::WinEventListening())WinEventNotify(0x800c,hwnd,0,0,false);
  return 1;
 }
 return 0;
}
unsigned Application::WindowTextApi(ApiName name,const unsigned*a){
 using namespace Win98WindowText;WindowTextMemory e(*this);
 const bool dialog=strstr(name,"DlgItem")!=nullptr,get=!strncmp(name,"Get",3),length=strstr(name,"Length")!=nullptr;
 unsigned target=dialog?(e.Valid(a[0])?DialogItem(e,a[0],a[1]):0):a[0];
 unsigned output=length?0:a[dialog?2:1],capacity=get&&!length?(a[dialog?3:2]&65535):0;
 // Valid HWNDs reach an unconditional byte clear in USER. Preserve
 // that observable write without allowing a native access to an invalid span.
 if(get&&!length&&(capacity||e.Valid(target))&&(!output||!virtual_memory.Access(output,1,true))){last_error=87;return 0;}
 if(!get&&output){ReadString(output);if(state==Failed)return 0;}
 if(!e.Valid(a[0]))last_error=1400;
 unsigned result=get?(length?Length(e,a[0]):dialog?GetItem(e,a[0],a[1],output,capacity):Get(e,a[0],output,capacity)):(dialog?SetItem(e,a[0],a[1],output):Set(e,a[0],output));
 return e.failed?0:result;
}
} // namespace X86
