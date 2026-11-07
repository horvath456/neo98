// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit composite provider: unchanged DOS3 bytes plus native Windows UI.
#pragma once
#include "host_windows_state.h"
#include "host_dos_vm.h"
#include "../src/dos_vm.h"
#include "../src/dos_voodoo.h"
#include "../src/x86_state_dos_ui.h"
#include <chrono>
#include <thread>
#include <limits>
namespace HostDosUiState {
inline bool asked=false, frozen=false, loaded=false;
inline std::vector<unsigned char> pending, core_bytes;
inline DosVm::Launch launch;
inline DosVm::Session *staged=nullptr;
inline std::string load_error;
inline std::string Engine() { return "dos-ui-1:"+HostDosVm::StateEngine()+":"+HostWindowsState::Engine(); }
struct Writer {
 std::vector<unsigned char> bytes;
 void Word(unsigned long long n) { const auto *p=reinterpret_cast<const unsigned char*>(&n);bytes.insert(bytes.end(),p,p+8); }
 void Raw(const void *data,size_t size) { const auto *p=static_cast<const unsigned char*>(data);bytes.insert(bytes.end(),p,p+size); }
 void Text(const char *s) { const size_t n=strlen(s);Word(n);Raw(s,n); }
};
struct Reader {
 const std::vector<unsigned char> &bytes;size_t at=0;bool okay=true;
 bool Raw(void *to,size_t n) { if(!okay||n>bytes.size()-at) return okay=false;memcpy(to,bytes.data()+at,n);at+=n;return true; }
 unsigned long long Word() { unsigned long long n=0;Raw(&n,8);return n; }
 bool Text(char *to,size_t capacity) { auto n=Word();if(!okay||n>=capacity) return okay=false;Raw(to,(size_t)n);to[n]=0;return okay&&memchr(to,0,(size_t)n)==nullptr; }
};
inline void WriteLaunch(Writer &w,const DosVm::Launch &l) {
 w.Text(l.command);w.Text(l.directory);w.Word(l.memory_mb);w.Word(l.xms);w.Word(l.ems);w.Word(l.fullscreen);
 w.Word(l.close_on_exit);w.Word(l.idle_sensitivity);w.Text(l.keyboard_layout);w.Text(l.prelude);w.Word(l.lines);
 w.Word(l.fast_paste);w.Text(l.options);w.Word(l.voodoo_host);
}
inline bool ReadLaunch(Reader &r,DosVm::Launch &l) {
 if(!r.Text(l.command,sizeof l.command)||!r.Text(l.directory,sizeof l.directory)) return false;
 const auto memory=r.Word(),xms=r.Word(),ems=r.Word(),full=r.Word(),close=r.Word(),idle=r.Word();
 if(!r.okay||!memory||memory>256||xms>1||ems>1||full>1||close>1||idle>100) return false;
 l.memory_mb=(unsigned)memory;l.xms=xms;l.ems=ems;l.fullscreen=full;l.close_on_exit=close;l.idle_sensitivity=(unsigned)idle;
 if(!r.Text(l.keyboard_layout,sizeof l.keyboard_layout)||!r.Text(l.prelude,sizeof l.prelude)) return false;
 const auto lines=r.Word(),paste=r.Word();if(!r.okay||lines>100||paste>1) return false;l.lines=(unsigned)lines;l.fast_paste=paste;
 if(!r.Text(l.options,sizeof l.options)) return false;
 const auto voodoo=r.Word();l.voodoo_host=voodoo;return r.okay&&voodoo<=1&&l.command[0];
}
inline unsigned long Sum(const std::vector<unsigned char> &bytes) { return crc32(0,bytes.data(),(uInt)bytes.size()); }
inline bool Decode(const std::vector<unsigned char> &bytes,std::vector<unsigned char> &dos,std::vector<unsigned char> &ui,DosVm::Launch &start,std::string &error) {
 Reader r{bytes};char magic[8]={};char engine[256]={};
 if(!r.Raw(magic,8)||memcmp(magic,"A98DUI01",8)||r.Word()!=1||!r.Text(engine,sizeof engine)||Engine()!=engine||!ReadLaunch(r,start)) {
  error="The combined DOS/dialog state has an invalid signature or startup sequence.";return false;
 }
 const auto nd=r.Word(),nu=r.Word(),sd=r.Word(),su=r.Word();
 if(!r.okay||!nd||!nu||nd>(1ull<<28)||nu>(1ull<<28)||nd>bytes.size()-r.at||nu!=bytes.size()-r.at-nd) {
  error="Part of the combined state is missing or has an invalid length.";return false;
 }
 dos.resize((size_t)nd);ui.resize((size_t)nu);
 if(!r.Raw(dos.data(),dos.size())||!r.Raw(ui.data(),ui.size())||Sum(dos)!=sd||Sum(ui)!=su) {
  error="Part of the combined state is damaged.";return false;
 }
 HostWindowsState::Bytes raw;
 unsigned long long raw_size=0;
 if(ui.size()<12||memcmp(ui.data(),"A98Z",4)) { error="The dialog state is damaged.";return false; }
 memcpy(&raw_size,ui.data()+4,8);
 if(raw_size>(1ull<<29)||!HostWindowsState::Unpack(ui,raw.data)) { error="The dialog state is damaged.";return false; }
 char why[512]={};
 if(!MachineState::ValidateDosUiLaunch(raw,start,why,sizeof why)||raw.at!=raw.data.size()) {
  error=std::string("The dialog state is invalid: ")+why;return false;
 }
 return true;
}
inline bool Ready(std::string &reason,std::vector<std::pair<std::string,std::string>> *machine) {
 auto *a=HostWindowsState::first;
 if(!a||staged||!pending.empty()||X86::Application::LiveChildren()) { reason="A combined state requires exactly one running DOS game.";return false; }
 HostState::Layout();const auto v=MachineState::CanSaveDosUi(*a);
 if(!v.possible&&!v.transient) {
  if (getenv("NEO98_STATE_PROBE")) fprintf(stderr,"NEO98 DOS UI state refused: %s\n",v.reason);
  reason=strstr(v.reason,"Voodoo renderer")
   ? "Cannot yet save 3dfx graphics state while DOS settings are open."
   : "Cannot yet save this DOS session together with its Windows UI.";
  return false;
 }
 if(!HostDosVm::StateReady(reason,machine)) return false;
 if(machine) machine->emplace_back("dos-ui-format","1");return true;
}
inline void Save() { asked=true;frozen=false;core_bytes.clear(); }
inline void Load(std::vector<unsigned char> bytes) { pending=std::move(bytes);loaded=false;load_error.clear(); }
inline bool HoldUi() { return frozen; }
inline void StopStaged() {
 MachineState::WantSavePoint(false);
 if(staged) { const auto *p=DosVm::Current();if(p)p->Stop(p->owner,staged);staged=nullptr; }
 HostDosVm::StateRelease();
}
inline bool LoadNow(X86::Application &a,const unsigned short *clock,bool &failed,std::string &error) {
 if(pending.empty()) return false;
 failed=true;HostState::Layout();std::vector<unsigned char> ui;DosVm::Launch start;
 if(!Decode(pending,core_bytes,ui,start,error)) { pending.clear();return true; }
 pending.clear();
 // Stage the actual core under bootstrap write suppression, before any UI
 // global is assigned. Its own reader validates native frames and files.
 HostDosVm::StateLoad(std::move(core_bytes));
 const auto *provider=DosVm::Current();char why[512]={};
 staged=provider?provider->Start(provider->owner,start,why,sizeof why):nullptr;
 if(!staged) { error=why[0]?why:"Could not prepare the DOS core.";StopStaged();return true; }
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(40);
 int answer=0;
 // Bootstrap graphics belong to this staging owner, not the saved USER UI.
 // StopStaged runs before this object's GPU surfaces are destroyed on error.
 struct StagingGraphics {
  DosVoodoo::Renderer *renderer=nullptr;
  ~StagingGraphics() { delete renderer; }
 } graphics;
 do {
  answer=HostDosVm::StatePoll(nullptr,error);if(answer)break;
  if(start.voodoo_host&&provider&&X86::Application::gpu_backend&&
     !DosVoodoo::Drain(*provider,staged,*X86::Application::gpu_backend,0x40000000u,graphics.renderer)) {
   error="Could not render Voodoo graphics while preparing the DOS core.";
   StopStaged();return true;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
 while(std::chrono::steady_clock::now()<deadline);
 if(answer!=1) { if(error.empty())error="The DOS core did not reach its load boundary.";StopStaged();return true; }
 HostWindowsState::Bytes raw;
 if(!HostWindowsState::Unpack(ui,raw.data)) { error="Could not decompress the dialog state.";StopStaged();return true; }
 MachineState::Host host;host.first=&a;host.wall_clock=clock;
 if(!MachineState::LoadDosUi(raw,host,why,sizeof why)||!a.BindRestoredDosVm(staged,start)) {
  error=why[0]?why:"The DOS core does not match this dialog state.";StopStaged();return true;
 }
 staged=nullptr;loaded=true;frozen=true;failed=false;
 printf("NEO98 HOST combined DOS/UI machine built; both parts held\n");fflush(stdout);return true;
}
inline int Poll(std::vector<unsigned char> *bytes,std::string &error) {
 if(!bytes) { if(loaded)return 1;error=load_error.empty()?"The combined state was not loaded.":load_error;return -1; }
 auto *a=HostWindowsState::first;if(!asked||!a) { error="No combined state requested.";return -1; }
 if(!frozen) {
  const auto v=MachineState::CanSaveDosUi(*a);
  if(!v.possible) { if(v.transient) { MachineState::WantSavePoint(true);return 0; }error=v.reason;return -1; }
  MachineState::WantSavePoint(false);
  if(HostWindowsState::pause)HostWindowsState::pause(true,"combined DOS/UI capture");
  if(!a->ExportDosLaunch(launch)) { error="DOS startup parameters are missing.";return -1; }
  frozen=true;HostDosVm::StateSave();
 }
 a->DrainDosUiGraphics();
 char graphics_error[256]={};
 if(!a->DosUiStateReady(graphics_error,sizeof graphics_error)) { error=graphics_error;return -1; }
 const int answer=HostDosVm::StatePoll(&core_bytes,error);if(answer!=1)return answer;
 HostWindowsState::Bytes raw;char why[512]={};
 if(!MachineState::SaveDosUi(*a,raw,why,sizeof why)) { error=why;return -1; }
 HostWindowsState::Bytes check;check.data=raw.data;
 if(!MachineState::ValidateDosUiLaunch(check,launch,why,sizeof why)||check.at!=check.data.size()) {
  error=std::string("The DOS UI has no verified resume path: ")+why;return -1;
 }
 std::vector<unsigned char> packed;
 if(!HostWindowsState::Pack(raw.data,packed)) { error="Could not compress the dialog state.";return -1; }
 Writer w;w.Raw("A98DUI01",8);w.Word(1);w.Text(Engine().c_str());WriteLaunch(w,launch);
 w.Word(core_bytes.size());w.Word(packed.size());w.Word(Sum(core_bytes));w.Word(Sum(packed));
 w.Raw(core_bytes.data(),core_bytes.size());w.Raw(packed.data(),packed.size());*bytes=std::move(w.bytes);
 asked=false;return 1;
}
inline void Release() { MachineState::WantSavePoint(false);HostDosVm::StateRelease();asked=false;frozen=false;core_bytes.clear(); }
inline bool Picture(std::vector<unsigned> &pixels,unsigned &width,unsigned &height) {
 int w=0,h=0;const auto *p=User::Pixels();User::PixelsSize(w,h);
 if(!p||w<=0||h<=0)return false;width=(unsigned)w;height=(unsigned)h;
 pixels.assign(reinterpret_cast<const unsigned*>(p),reinterpret_cast<const unsigned*>(p)+(size_t)w*h);return true;
}
}
