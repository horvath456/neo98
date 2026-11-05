// SPDX-License-Identifier: GPL-2.0-or-later
// Include after process children/legacy to reuse the native checked guest memory provider.
#include "win98_shell_control.h"
namespace X86 {
struct Application::ShellControlState:Win98ShellControl::State{
 Application*owner=nullptr;
 static ShellControlState*Find(Application*app,bool create){static ShellControlState slots[64];ShellControlState*empty=nullptr;for(auto&slot:slots){if(slot.owner==app)return &slot;if(!slot.owner&&!empty)empty=&slot;}if(create&&empty){empty->owner=app;return empty;}return nullptr;}
};
struct Application::ShellControlEnvironment {
 Application&app;ProcessLaunchEnvironment probe;unsigned literals[4]={};
 explicit ShellControlEnvironment(Application&a):app(a),probe{a}{}
 ~ShellControlEnvironment(){for(unsigned p:literals)if(p)app.Free(p);}
 struct Scratch{ShellControlEnvironment&e;unsigned address,old;Scratch(ShellControlEnvironment&v,unsigned n):e(v),old(e.app.Register(Reg::Esp)){address=(old-n-32)&~15u;e.app.Register(Reg::Esp,address);}~Scratch(){e.app.Register(Reg::Esp,old);}};
 unsigned R8(unsigned p){unsigned char c=0;app.Read(p,&c,1);return c;}
 unsigned R16(unsigned p){unsigned short c=0;app.Read(p,&c,2);return c;}
 unsigned R32(unsigned p){return app.Read32(p);}
 void W8(unsigned p,unsigned v){unsigned char c=v;app.Store(p,&c,1);}
 void W16(unsigned p,unsigned v){unsigned short c=v;app.Store(p,&c,2);}
 void W32(unsigned p,unsigned v){app.Write32(p,v);}
 unsigned Literal(unsigned i,const char*s){if(!literals[i])literals[i]=app.String(s);return literals[i];}
 unsigned SetupText(){return Literal(0,"Setup");}unsigned NameText(){return Literal(1,"CPLName");}unsigned FlagsText(){return Literal(2,"CPLFlags");}
 bool Lead(unsigned c){return app.NlsApi(537,&c)!=0;}
 unsigned Next(unsigned p){return app.CharacterApi(4,&p);}
 unsigned Previous(unsigned start,unsigned p){unsigned a[]={start,p};return app.CharacterApi(5,a);}
 unsigned FindComma(unsigned p){unsigned a[]={p,','};return app.ShellSearchApi("StrChrA",a);}
 unsigned SpanDelimiter(unsigned p){for(unsigned i=0;i<32768&&R8(p);++i){unsigned c=R8(p);if(c==','||c==' ')return p;p=Next(p);}return 0;}
 void CopyString(unsigned out,unsigned from,unsigned n){if(!out||!n)return;unsigned i=0;for(;i+1<n&&i<32768;++i){unsigned c=R8(from+i);W8(out+i,c);if(!c)return;}W8(out+i,0);}
 unsigned Integer(unsigned p){return app.CommonControlApi(357,&p);}
 int Compare(unsigned a,unsigned b){unsigned v[]={a,b};return int(app.Api("lstrcmpiA",v));}
 int CompareExact(unsigned a,unsigned b){unsigned v[]={a,b};return int(app.Api("lstrcmpA",v));}
 unsigned Applet(unsigned legacy,unsigned,unsigned proc,unsigned h,unsigned m,unsigned a,unsigned b){if(legacy){app.Fail("Control_RunDLL requires unavailable Win16 CPL thunk");return 0;}if(app.state==Failed||app.state==Exited)return 0;unsigned args[]={h,m,a,b};return app.Call(proc,args,4);}
 unsigned Procedure(unsigned legacy,unsigned module){if(legacy){app.last_error=50;return 0;}unsigned p=Literal(3,"CPlApplet"),a[]={module,p};return app.ModuleApi("GetProcAddress",a);}
 unsigned LoadIcon(unsigned module,unsigned id){unsigned a[]={module,id,1,0,0,0x40};return app.ImageApi("LoadImageA",a);}
 void DestroyIcon(unsigned icon){app.ImageApi("DestroyIcon",&icon);}
 void LoadString(unsigned module,unsigned id,unsigned out,unsigned n){W8(out,0);unsigned a[]={module,id,out,n};app.Api("LoadStringA",a);}
 unsigned SetString(unsigned out,unsigned text){unsigned a[]={out,text};return app.CommonControlApi(234,a);}
 void WideInquiry(unsigned p){Scratch copy(*this,0x1d4);Win98ShellControl::Copy(*this,copy.address,p,0x1d4);const unsigned offsets[]={20,52,116},sizes[]={32,64,128};for(unsigned i=0;i<3;++i){unsigned a[]={0,0,copy.address+(i==0?20:i==1?84:212),sizes[i],p+offsets[i],sizes[i],0,0};app.NlsApi(811,a);}}
 unsigned ArrayCreate(unsigned size,unsigned grow){if(grow>1024){app.last_error=8;return 0;}unsigned a[]={size,grow};return app.CommonControlApi(320,a);}
 unsigned ArrayGet(unsigned list,unsigned index){unsigned a[]={list,index};return app.CommonControlApi(323,a);}
 unsigned ArrayInsert(unsigned list,unsigned index,unsigned p){unsigned a[]={list,index,p};return app.CommonControlApi(324,a);}
 void ArrayDelete(unsigned list,unsigned index){unsigned a[]={list,index};app.CommonControlApi(326,a);}
 void ArrayClear(unsigned list){app.CommonControlApi(327,&list);}void ArrayDestroy(unsigned list){app.CommonControlApi(321,&list);}
 unsigned LoadLegacy(unsigned){return 21;} // Explicit PE32 selection; ModuleApi rejects NE images.
 unsigned LoadLibrary(unsigned path){return app.ModuleApi("LoadLibraryA",&path);}
 unsigned ProcessId(){return app.process_id;}
 unsigned OpenSelf(unsigned){return 0;} // Application lifetime already retains the current process.
 void Close(unsigned h){if(h)app.ThreadApi("CloseHandle",&h);}
 void Unload(unsigned legacy,unsigned module){if(legacy){app.last_error=50;return;}app.ModuleApi("FreeLibrary",&module);}
 void ModuleName(unsigned legacy,unsigned module,unsigned out,unsigned n){if(legacy){app.last_error=50;return;}unsigned a[]={module,out,n};app.ModuleApi("GetModuleFileNameA",a);}
 unsigned AddAtom(unsigned text){return app.GlobalAtomApi("GlobalAddAtomA",&text);}
 void DeleteAtom(unsigned atom){app.GlobalAtomApi("GlobalDeleteAtom",&atom);}
 unsigned GetProp(unsigned h,unsigned key){unsigned a[]={h,key};return app.PropertyApi("GetPropA",a);}
 unsigned SetProp(unsigned h,unsigned key,unsigned value){unsigned a[]={h,key,value};return app.PropertyApi("SetPropA",a);}
 void RemoveProp(unsigned h,unsigned key){unsigned a[]={h,key};app.PropertyApi("RemovePropA",a);}
 void ClassName(unsigned h,unsigned out,unsigned n){unsigned a[]={h,out,n};app.ClassApi("GetClassNameA",a);}
 bool Resolve(unsigned path){unsigned a[]={path,0};if(app.ShellPathSyntaxApi("PathIsFileSpecA",a))return app.ShellCommandApi(479,a)!=0;return app.ShellCommandApi(471,a)!=0;}
 unsigned FindExisting(Win98ShellControl::State&s,unsigned owner,unsigned atoms){if(!Win98ShellControl::GlobalAtoms(*this,s))return 0;Scratch local(*this,32);W8(local.address,0);if(owner)ClassName(owner,local.address,32);unsigned windows[193],count=0;if(!User::WindowSnapshot(0,windows,193,count))return 0;for(unsigned i=0;i<count;++i){if(Win98ShellControl::Match(*this,s,windows[i],atoms,local.address))return windows[i];}return 0;}
 unsigned LastPopup(unsigned h){return app.ActivationApi("GetLastActivePopup",&h);}bool IsWindow(unsigned h){return User::IsWindow(h);}
 void Foreground(unsigned h){app.ActivationApi("SetForegroundWindow",&h);}
 void NotifyOwner(unsigned h,unsigned icon,unsigned caption){Scratch local(*this,20);unsigned note[]={h,User::ControlID(h),unsigned(-500),icon,caption};app.Store(local.address,note,sizeof note);app.CallWindow(h,0x4e,note[1],local.address);}
 void OpenPanel(unsigned,unsigned,unsigned){app.Fail("Control_RunDLL empty command requires Control Panel namespace execution");}
};
unsigned Application::ShellControlApi(ApiName,const unsigned*a){
 ShellControlEnvironment e(*this);char input[32768];if(a[2]&&!e.probe.Text(a[2],input,sizeof input))return 0;
 if(a[0]&&User::WindowProcess(a[0])!=process_id){last_error=50;return 0;}
 auto*shell_control=ShellControlState::Find(this,true);if(!shell_control){last_error=8;return 0;}
 unsigned text=a[2]?String(input):0;if(a[2]&&!text)return 0;
 Win98ShellControl::Run(e,*shell_control,a[0],a[1],text,a[3],1);if(text)Free(text);return 0; // void public export
}
void Application::FreeShellControl(){auto*shell_control=ShellControlState::Find(this,false);if(!shell_control)return;ShellControlEnvironment e(*this);
 while(shell_control->cache&&e.R32(shell_control->cache)){unsigned index=e.R32(shell_control->cache)-1,m=e.ArrayGet(shell_control->cache,index);e.W32(m,1);Win98ShellControl::ReleaseIndex(e,*shell_control,index);}
 if(shell_control->cache)e.ArrayDestroy(shell_control->cache);
 if(shell_control->name_atom)e.DeleteAtom(shell_control->name_atom);if(shell_control->flags_atom)e.DeleteAtom(shell_control->flags_atom);
 *shell_control=ShellControlState{};
}
}
