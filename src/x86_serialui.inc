// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_serialui_dialog.h"
#include "win98_serialui_pages.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/serialui-assets/templates.h"
#endif
namespace X86 {
namespace {
struct SerialUiFrame {
 Application*app;SerialUiFrame*next;unsigned key,page=0,advanced=0,name,instance,module=0,config=0;bool applied=false,persistent=false;
 unsigned char dcb[28]{};static SerialUiFrame*head;
 SerialUiFrame(Application*a,unsigned k,unsigned n,unsigned i):app(a),next(head),key(k),name(n),instance(i){head=this;}
 ~SerialUiFrame(){auto**p=&head;while(*p&&*p!=this)p=&(*p)->next;if(*p)*p=next;}
};
SerialUiFrame*SerialUiFrame::head=nullptr;
}
unsigned Application::SerialUiDialog(unsigned name,unsigned owner,unsigned config,unsigned instance){
 const bool gate=TakeFresh();
 unsigned library=String("COMCTL32.DLL");if(!library)return 8;
 unsigned module=ModuleApi("LoadLibraryA",&library);Free(library);if(!module)return last_error;
 unsigned memory=Allocate(4096);if(!memory){ModuleApi("FreeLibrary",&module);return 8;}
 auto*frame=new SerialUiFrame(this,memory,name,instance);frame->module=module;frame->config=config;Read(config+8,frame->dcb,28);
 const bool kept=gate&&PushHostFrame(HostSerialDialog,memory);
 struct Release{Application&app;unsigned key;bool kept;~Release(){app.FinishSerialUiDialog(key,0,false);if(kept)app.PopHostFrame();}}release{*this,memory,kept};
 using namespace OriginalSerialUiAssets;
 Store(memory+256,Template119,sizeof Template119);
 // PSH_PROPTITLE | PSH_PROPSHEETPAGE; PSP_DLGINDIRECT. Native frame owns the
 // state formerly released by SERIALUI's PSP_USECALLBACK callback.
 unsigned header[]={40,9,owner,image.Base(),0,name,1,0,memory+64,0};
 unsigned page[]={40,1,image.Base(),memory+256,0,0,0x71000013,memory,0,0};
 Store(memory,header,sizeof header);Store(memory+64,page,sizeof page);
 if(kept)GiveTail(TailFresh);
 PropertySheetApi("PropertySheetA",&memory);
 return frame->applied?0:1223;
}
unsigned Application::FinishSerialUiDialog(unsigned key,unsigned result,bool advanced){
 auto*f=SerialUiFrame::head;while(f&&(f->app!=this||f->key!=key))f=f->next;
 if(!f)return result;
 if(!advanced){result=f->applied?0:1223;if(f->applied)Store(f->config+8,f->dcb,28);}
 Free(f->key);unsigned module=f->module;delete f;
 if(module)ModuleApi("FreeLibrary",&module);
 return result;
}
unsigned Application::ResumeSerialUiDialog(unsigned key,bool advanced){
 ++threads[current_thread].host_resume_at;
 unsigned result=ResumeInner();
 result=FinishSerialUiDialog(key,result,advanced);PopHostFrame();return result;
}
unsigned Application::SerialUiEnum(unsigned name,unsigned instance,unsigned config,unsigned callback,unsigned parameter){
 unsigned memory=Allocate(4096);if(!memory)return 0;
 unsigned copy=String(ReadString(name));if(!copy){Free(memory);return 0;}
 auto*frame=new SerialUiFrame(this,memory,copy,instance);frame->persistent=true;Read(config+8,frame->dcb,28);
 Store(memory+256,OriginalSerialUiAssets::Template119,sizeof OriginalSerialUiAssets::Template119);
 unsigned char cc[52];Read(config,cc,52);Store(memory+128,cc,52);
 unsigned gate=0;
 for(unsigned i=0;i<import_count;++i)if(!strcmp(imports[i].name,"__NEO98_SerialUiPageCallback")){gate=GateBase+i*16;break;}
 if(!gate)gate=NewImport("SERIALUI","__NEO98_SerialUiPageCallback",3);
 if(!gate){Free(copy);Free(memory);delete frame;return 1;}
 unsigned page[]={40,0x81,image.Base(),memory+256,0,0,0x71000013,memory,gate,0};Store(memory,page,sizeof page);
 struct Offer{
  Application&app;unsigned memory,callback,parameter,handle=0;
  unsigned Create(){return handle=app.PropertyPageApi("CreatePropertySheetPageA",&memory);}
  unsigned Callback(unsigned page){
   unsigned stack=memory+64;app.Write32(stack,parameter);app.Write32(stack+4,page);unsigned result=0,words=0;
   if(!app.NativeWin16Dispatch(callback,stack,8,result,words)&&app.state!=Failed)app.ThunkTargetFail("SERIALUI EnumPropPages callback",callback);
   return app.state==Failed?0:result;
  }
  void Destroy(unsigned page){app.PropertyPageApi("DestroyPropertySheetPage",&page);}
 }offer{*this,memory,callback,parameter};
 Win98SerialUi::OfferPage(offer);
 if(!offer.handle){Free(copy);Free(memory);delete frame;}
 // SERIALUI Windows 98 returns preparation's BOOL, ignoring OfferPage's status.
 return 1;
}
unsigned Application::SerialUiPageCallback(unsigned reason,unsigned page){
 if(reason!=1)return 1;
 unsigned key=Read32(page+28);SerialUiFrame*frame=nullptr;
 for(auto*p=SerialUiFrame::head;p;p=p->next)if(p->app==this&&p->persistent&&p->key==key){frame=p;break;}
 if(!frame)return 1;
 struct Release{
  Application&app;SerialUiFrame*frame;bool freed=false;
  void CopyDcb(){app.Store(frame->key+136,frame->dcb,28);}
  void Save(){unsigned args[]={frame->name,frame->key+128,app.Read32(frame->key+128)};app.CommApi("SerialUiSet16",args);}
  void FreeState(){app.Free(frame->name);app.Free(frame->key);delete frame;freed=true;}
 }release{*this,frame};
 unsigned result=Win98SerialUi::ReleasePage(release,reason,1,frame->applied);
 // Native ownership also reclaims cancelled/rejected pages. The NE routine
 // leaks that unreachable LocalAlloc state when its applied WORD is zero.
 if(!release.freed)release.FreeState();return result;
}
void Application::FreeSerialUiPages(){
 for(auto*p=SerialUiFrame::head;p;){auto*next=p->next;if(p->app==this){
  if(p->persistent){Free(p->name);Free(p->key);delete p;}
  else FinishSerialUiDialog(p->key,0,p->config==0);
 }p=next;}
}
unsigned Application::SerialUiMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 SerialUiFrame*f=nullptr;unsigned key=message==0x110&&lp?Read32(lp+28):0;
 for(auto*p=SerialUiFrame::head;p;p=p->next)if(p->app==this&&(p->page==hwnd||(!p->page&&p->key==key))){f=p;break;}
 if(!f)return 0;
 using namespace Win98SerialUi;
 const unsigned ids[]={0x418,0x41a,0x41c,0x41e,0x420};
 auto choose=[&](unsigned i,unsigned value){unsigned h=User::GetDlgItem(hwnd,ids[i]),count=Send(h,0x146,0,0);for(unsigned j=0;j<count;++j)if(Send(h,0x150,j,0)==value){Send(h,0x14e,j,0);break;}};
 if(message==0x110){
  f->page=hwnd;unsigned selected[5];DialogSelection(f->dcb,selected);
  const Choice*lists[]={Baud,Data,Parities,Stops,Flows};const unsigned counts[]={13,5,5,3,3};
  for(unsigned i=0;i<5;++i){unsigned h=User::GetDlgItem(hwnd,ids[i]);
   for(unsigned j=0;j<counts[i];++j){auto c=lists[i][j];unsigned text=String(OriginalSerialUiAssets::String(c.string));unsigned index=Send(h,0x143,0,text);Free(text);Send(h,0x151,index,c.value);}
   choose(i,selected[i]);
  }return 1;
 }
 if(message==0x4e&&lp&&Read32(lp+8)==0xffffff36u){
  unsigned values[5];for(unsigned i=0;i<5;++i){unsigned h=User::GetDlgItem(hwnd,ids[i]);values[i]=Send(h,0x150,Send(h,0x147,0,0),0);}
  DialogApply(f->dcb,values);f->applied=true;unsigned result=0;User::WindowLong(hwnd,0,result,true);return 1;
 }
 if(message==0x111){
  if((wp&65535)==0x422){const unsigned defaults[]={9600,8,3,0,1};for(unsigned i=0;i<5;++i)choose(i,defaults[i]);return 1;}
  if((wp&65535)==0x423){
   const bool kept=TakeTail()==TailRecord&&PushHostFrame(HostSerialMessage);
   if(kept)GiveTail(TailFresh);
   SerialUiAdvanced(hwnd,f->name,f->instance);if(kept)PopHostFrame();return 1;
  }
 }
 return 0;
}
unsigned Application::SerialUiAdvanced(unsigned owner,unsigned name,unsigned instance){
 const bool gate=TakeFresh();
 unsigned memory=Allocate(64);if(!memory)return ~0u;
 new SerialUiFrame(this,memory,name,instance);
 const bool kept=gate&&PushHostFrame(HostSerialAdvanced,memory);
 const unsigned frame_at=threads[current_thread].host_frame_count?threads[current_thread].host_frame_count-1:0;
 struct Release{Application&app;unsigned key;bool kept;~Release(){app.FinishSerialUiDialog(key,0,true);if(kept)app.PopHostFrame();}}release{*this,memory,kept};
 while(owner&&(User::Style(owner)&0x40000000u))owner=User::Parent(owner);
 unsigned hwnd=CreateDialogTemplate(OriginalSerialUiAssets::Template101,sizeof OriginalSerialUiAssets::Template101,owner,0x71000014,memory,true);
 if(kept){auto&record=threads[current_thread].host_frames[frame_at];record.b=hwnd;record.d=1;GiveTail(TailRecord);}
 return hwnd?RunModal(hwnd,kept):~0u;
}
unsigned Application::SerialUiAdvancedMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 SerialUiFrame*f=nullptr;for(auto*p=SerialUiFrame::head;p;p=p->next)if(p->app==this&&(p->advanced==hwnd||(!p->advanced&&message==0x110&&p->key==lp))){f=p;break;}
 if(!f)return 0;
 auto setup=[&](bool enabled,unsigned rx,unsigned tx,bool positions){
  const unsigned ids[]={0x3ed,0x3ee,0x3ef,0x3eb,0x3f1,0x3f2,0x3f3,0x3ec};
  for(unsigned id:ids)User::EnableWindow(User::GetDlgItem(hwnd,id),enabled);
  Send(User::GetDlgItem(hwnd,0x3f0),0xf1,enabled,0);
  if(positions){for(unsigned id=0x3eb;id<=0x3ec;++id)Send(User::GetDlgItem(hwnd,id),0x406,0,3u<<16);
   if(enabled){Send(User::GetDlgItem(hwnd,0x3eb),0x405,1,rx);Send(User::GetDlgItem(hwnd,0x3ec),0x405,1,tx>>2);}}
 };
 auto profile=[&](bool enabled){
  const Registry::Value*v=nullptr;if(registry.store.Query(f->instance,"PortName",v)||!v->size||v->size>250)return;
  char port[256]{};memcpy(port,v->data,v->size);unsigned n=strlen(port);if(n+5>sizeof port)return;memcpy(port+n,"Fifo",5);
  unsigned section=String("386Enh"),key=String(port),value=enabled?0:String("0"),file=String("system.ini");
  unsigned a[]={section,key,value,file};ProfileApi("WritePrivateProfileStringA",a);if(value)Free(value);Free(section);Free(key);Free(file);
 };
 if(message==0x110){
  f->advanced=hwnd;const Registry::Value*v=nullptr;unsigned char settings[]={2,16,0,128};
  if(!registry.store.Query(f->instance,"Settings",v)&&v->size<=4)memcpy(settings,v->data,v->size);
  bool enabled;unsigned rx,tx;Win98SerialUi::FifoRead(settings,enabled,rx,tx);setup(enabled,rx,tx,true);profile(enabled);return 0;
 }
 if(message==0x111){
  unsigned id=wp&65535;
  if(id==0x3f0){bool enabled=Send(User::GetDlgItem(hwnd,id),0xf0,0,0)!=0;setup(enabled,0,0,!enabled);}
  else if(id==1000)setup(true,2,12,true);
  else if(id==1||id==2){
   if(id==1){bool enabled=Send(User::GetDlgItem(hwnd,0x3f0),0xf0,0,0)!=0;
    unsigned rx=Send(User::GetDlgItem(hwnd,0x3eb),0x400,0,0),tx=Send(User::GetDlgItem(hwnd,0x3ec),0x400,0,0);
    unsigned char settings[4];Win98SerialUi::FifoWrite(settings,enabled,rx,tx);
    registry.store.Set(f->instance,"Settings",3,settings,4);profile(enabled);
   }EndDialog(hwnd,id==1?1:0);
  }return 0;
 }
 return 0;
}
} // namespace X86
