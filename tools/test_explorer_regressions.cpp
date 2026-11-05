// SPDX-License-Identifier: GPL-2.0-or-later
// Reuse the original-app probe's file loader, snapshots and stall diagnostics.
#define main original_probe_main
#include "probe_application.cpp"
#undef main
#include <cassert>
#include <string>
using App=X86::Application;
static App *shell,*calculator,*browser;
// --trace-start-cycles: the API trace of the current Start cycle only, held
// in memory without the blit calls and written to build/user-regressions/
// start-cycle.trace when a step fails. A full --trace-calls run changes the
// timing so much that the failure no longer shows.
static std::string cycle_trace;static bool cycle_tracing=false;
static void cycle_logger(const char*s){if(!cycle_tracing||strstr(s,"!BitBlt ")||strstr(s,"!StretchBlt ")||strstr(s,"!PatBlt "))return;cycle_trace+=s;cycle_trace+='\n';}
static void write_cycle_trace(){if(cycle_trace.empty())return;if(FILE*f=fopen("build/user-regressions/start-cycle.trace","w")){fwrite(cycle_trace.data(),1,cycle_trace.size(),f);fclose(f);}}
static void require(bool ok,const char*why){if(ok)return;fprintf(stderr,"EXPLORER REGRESSION FAILED: %s\n",why);write_cycle_trace();if(shell)sampling::stall_dump(*shell);if(calculator)sampling::stall_dump(*calculator);for(unsigned i=0;i<User::ProcessContextCount();++i)if(auto*other=static_cast<App*>(User::ProcessContextAt(i)))if(other!=shell&&other!=calculator){printf("PROBE_PROCESS program=%s\n",other->program_name);sampling::stall_dump(*other);}snapshot_windows();screenshot("build/user-regressions/failure.ppm");fflush(stdout);exit(1);}
static void pump(unsigned milliseconds){
 auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
 do{shell->Pump(32);if(calculator)calculator->Pump(32);if(browser)browser->Pump(32);
 require(shell->Status()!=App::Failed,shell->Error());
 if(calculator)require(calculator->Status()!=App::Failed,calculator->Error());
 if(browser)require(browser->Status()!=App::Failed,browser->Error());
 }while(std::chrono::steady_clock::now()<end);
}
// The limits are wall-clock time, and the host suite runs two ASan tests at
// once on a machine that other builds share: at a load of 15 to 20 Explorer
// took longer than 40 s to its tray and the test failed at a step that
// passes on every run alone. Each limit gets three times its time; what is
// waited for stays the same.
constexpr unsigned Patience=3;
template<class F> static void until(F condition,const char*why,unsigned limit=5000){
 auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(limit*Patience);
 while(!condition()&&std::chrono::steady_clock::now()<end)pump(20);
 require(condition(),why);
}
static unsigned find(const char*name,const char*title=nullptr){unsigned nodes[User::WindowCapacity],n=0;User::WindowSnapshot(User::GetDesktopWindow(),nodes,User::WindowCapacity,n);for(unsigned i=0;i<n;++i){char c[128],t[128];User::GetClassName(nodes[i],c,sizeof c);User::GetWindowText(nodes[i],t,sizeof t);if(User::IsVisible(nodes[i])&&(!name||!strcmp(c,name))&&(!title||!strcmp(t,title)))return nodes[i];}return 0;}
static void click(unsigned h){Gdi::RECT r{};require(User::GetWindowRect(h,&r),"click window");int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;shell->MouseButtons(x,y,0);shell->MouseButtons(x,y,1);until([&]{return User::ControlMessage(h,0xf2,0,0)&4;},"button paints pressed state");shell->MouseButtons(x,y,0);pump(120);}
// The original modules stand in front of the options, so the list ends at the
// first "--". A value of an option must never be taken for a module name.
static bool option_start(const char*text){return text[0]=='-'&&text[1]=='-';}
int main(int argc,char**argv){
 if(argc<5)return 2;
 // The Win98 tree has to be mounted before the first use of the RAM volume.
 const auto tree_options=SystemTreeHost::Scan(argc,argv);
 if(tree_options.tree)require(SystemTreeHost::Mount(tree_options.root),"mount the imported Win98 tree");
 // The writable C: above the tree. It carries the SYSTEM.INI whose [boot]
 // Shell names Explorer.exe, which is what makes EXPLORER.EXE the shell of
 // this machine; tools/win98_folder.py writes it, the setup preserves shell startup semantics.
 auto drive_options=HostVolume::Scan(argc,argv);
 drive_options.ram=drive_options.drive_c==nullptr;
 require(HostVolume::Mount(drive_options),"mount the writable C: drive");
 if(argv[4][0]!='-'||argv[4][1])require(seed_manifest(argv[4]),"seed manifest");
 auto exe=load(argv[1]),font=load(argv[2]),calc=load(argv[3]);
 // --trace-calls writes the same APITRACE lines the probe writes, so a
 // failing step here can be read against the guest's own call sequence.
 bool trace_calls=false;for(int i=5;i<argc;++i)if(!strcmp(argv[i],"--trace-calls"))trace_calls=true;
 bool skip_show_desktop=false;
 for(int i=5;i<argc;++i)if(!strcmp(argv[i],"--skip-show-desktop"))skip_show_desktop=true;
 bool trace_cycles=false;for(int i=5;i<argc;++i)if(!strcmp(argv[i],"--trace-start-cycles"))trace_cycles=true;
 shell=new App(trace_calls?logger:trace_cycles?cycle_logger:nullptr);shell->trace_calls=trace_calls;for(int i=5;i<argc&&!option_start(argv[i]);++i)require(shell->AddNativeModule(argv[i]),"native module override");
 require(shell->Start(exe.data(),exe.size(),font.data(),font.size(),"EXPLORER.EXE"),"explorer start");
 // What "startup finished" means is the shell itself, not the end of its
 // children. Win98 runs the Run keys of the installation at every boot and
 // some of those programs stay for the whole session; this installation has
 // rundll32.exe 3dfxCmn.dll,CMNUpdateOnBoot among them, which never ends.
 // The state this test needs is the tray with its Start button, the
 // notification area with a clock that shows a time, the desktop of Progman
 // with its view, no pending RUNDLL error box and a quiet message loop.
 // From the pristine C: the first shell start also runs the per-user Active
 // Setup of the installation, about 35 stubs (setupx InstallHinfSection,
 // advpack, runonce, grpconv), each synchronously; at the load of the host
 // suite beside other builds that took 145 to 210 s on 28 September 2026,
 // with and without the screen-user changes, so the startup gets 300 s.
 unsigned tray=0,start=0;
 until([&]{if(auto h=visible_dialog("RUNDLL")){auto b=User::GetDlgItem(h,2);if(!b)b=User::GetDlgItem(h,1);if(b)click(b);}
  tray=find("Shell_TrayWnd");start=tray?User::GetDlgItem(tray,0x130):0;
  unsigned clock=find("TrayClockWClass");Gdi::RECT face{};
  if(clock)User::GetWindowRect(clock,&face);
  return tray&&start&&find("TrayNotifyWnd")&&clock&&face.right>face.left&&face.bottom>face.top&&
   find("Progman")&&find("SHELLDLL_DefView")&&find("SysListView32")&&
   !visible_dialog("RUNDLL")&&shell->WaitingForInput();},"Explorer finishes startup",100000);
 printf("EXPLORER shell up children=%u calls=%u\n",App::LiveChildren(),shell->ApiCalls());fflush(stdout);
 require(start,"Start button");
 for(unsigned i=0;i<3;++i){
  printf("EXPLORER Start cycle=%u\n",i);fflush(stdout);
  if(trace_cycles){cycle_trace.clear();cycle_tracing=true;shell->trace_calls=true;}
  click(start);until([&]{return find("BaseBar")&&!User::Rendering();},"Start menu opens");
  require(User::ControlMessage(start,0xf2,0,0)&4,"Start remains pressed while menu holds capture");
  shell->RawKey(27,0,true);shell->RawKey(27,0,false);
  until([&]{return !find("BaseBar")&&!(User::ControlMessage(start,0xf2,0,0)&4);},"Start releases on Escape");
 }
 if(trace_cycles){cycle_tracing=false;shell->trace_calls=false;}
 puts("EXPLORER Start menu repeated open/close PASS");fflush(stdout);
 browser=new App(trace_calls?logger:nullptr);browser->trace_calls=trace_calls;for(int i=5;i<argc&&!option_start(argv[i]);++i)require(browser->AddNativeModule(argv[i]),"browser native module override");
 require(browser->Start(exe.data(),exe.size(),font.data(),font.size(),"EXPLORER.EXE","EXPLORER.EXE /e,C:\\"),"browser start");
 until([&]{return find("SysTreeView32")&&!User::Rendering();},"Explorer tree opens",15000);
 // C:\ of the whole installation is 35 entries with their icons, and the
 // folder window fills both panes before it answers input again. Drive the
 // mouse only once it is back in its message loop and nothing is pending.
 // The second EXPLORER.EXE only hands the folder over to the running shell,
 // so it is the shell that fills both panes. Drive the mouse once the shell
 // is back in its message loop and no window still owes a repaint.
 until([&]{return shell->WaitingForInput()&&!User::Rendering()&&!User::PendingPaintWindows(nullptr);},
       "folder window finishes its first view",20000);
 pump(500);screenshot("build/user-regressions/tree.ppm");
 auto pixel=[](int x,int y){auto*p=User::Pixels()+4*(y*800+x);return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16);};
 // The bottom left of the tree's own client area. C:\ of the whole
 // installation fills the pane and gives it both bars, so the window rect no
 // longer ends in the pane; the client rect does. Item row or empty space,
 // the original TreeView has erased that point white.
 const unsigned tree_pane=find("SysTreeView32");
 Gdi::RECT tree_client{};require(User::GetClientRect(tree_pane,&tree_client),"tree client rect");
 Gdi::POINT tree_origin{0,0};User::ScreenToClient(tree_pane,&tree_origin);
 require(pixel(-tree_origin.x+2,-tree_origin.y+tree_client.bottom-3)==0xffffff,
         "original TreeView erases unused area white");

 // the original folder window must size at its corner and
 // scroll at its bar. The frame drag is the modal loop of USER, the
 // bar the tracker of USER, both on Explorer's own fiber.
 {
  auto tree=find("SysTreeView32");require(tree,"Explorer tree");
  unsigned folder=tree;for(unsigned n=0;n<16&&User::Parent(folder);++n)folder=User::Parent(folder);
  require(folder&&folder!=tree,"Explorer folder window");
  require(User::Style(folder)&0x40000,"folder window has a sizing frame");
  Gdi::RECT before{};require(User::GetWindowRect(folder,&before),"folder rect");
  const int width=before.right-before.left,height=before.bottom-before.top;
  require(width>240&&height>180,"folder window large enough to shrink");
  // Grab the bottom right corner and pull it in by 60 by 40.
  shell->MouseButtons(before.right-2,before.bottom-2,0);
  shell->MouseButtons(before.right-2,before.bottom-2,1);pump(80);
  for(int i=1;i<=6;++i){shell->MouseButtons(before.right-2-i*10,before.bottom-2-i*7,1);pump(30);}
  Gdi::RECT during{};User::GetWindowRect(folder,&during);
  require(during.right-during.left==width&&during.bottom-during.top==height,
          "the folder window itself does not move while the outline is dragged");
  shell->MouseButtons(before.right-62,before.bottom-44,0);pump(200);
  until([&]{Gdi::RECT r{};User::GetWindowRect(folder,&r);
   return r.right-r.left==width-60&&r.bottom-r.top==height-42;},"folder window sizes at its corner");
  until([&]{return shell->WaitingForInput()&&!User::Rendering()&&!User::PendingPaintWindows(nullptr);},
        "folder window repaints after sizing",15000);
  Gdi::RECT sized{};User::GetWindowRect(folder,&sized);
  Gdi::RECT tree_after{};require(User::GetWindowRect(tree,&tree_after),"tree rect after sizing");
  require(tree_after.right<=sized.right&&tree_after.bottom<=sized.bottom,
          "the tree child stays inside the smaller folder window");
  screenshot("build/user-regressions/folder-sized.ppm");
  puts("EXPLORER folder window corner resize PASS");fflush(stdout);
  // The list pane's own bar, if the seeded folder fills it.
  unsigned list=find("SysListView32");
  if(!list||!(User::Style(list)&0x200000))list=tree;
  if(list&&(User::Style(list)&0x200000)){
   Gdi::RECT lr{};User::GetWindowRect(list,&lr);
   Gdi::RECT lc{};User::GetClientRect(list,&lc);
   Gdi::POINT lo{0,0};User::ScreenToClient(list,&lo);
   const int bar=-lo.x+lc.right+8,top=-lo.y,bottom=-lo.y+lc.bottom;
   Win98Scroll::Info info{28,0x17,0,0,0,0,0};
   if(User::ScrollState(list,0,1,&info,0)&&info.maximum>info.minimum){
    const unsigned start=info.position;
    // The arrow at the bottom of the bar, held past the first repeat delay.
    shell->MouseButtons(bar,bottom-6,0);shell->MouseButtons(bar,bottom-6,1);pump(700);
    shell->MouseButtons(bar,bottom-6,0);pump(150);
    // The track below the thumb.
    shell->MouseButtons(bar,(top+bottom)/2+30,0);shell->MouseButtons(bar,(top+bottom)/2+30,1);pump(120);
    auto pressed=User::FrameSerial();(void)pressed;
    shell->MouseButtons(bar,(top+bottom)/2+30,0);pump(150);
    // The thumb, dragged down and released.
    shell->MouseButtons(bar,top+20,0);shell->MouseButtons(bar,top+20,1);pump(60);
    for(int i=1;i<=4;++i){shell->MouseButtons(bar,top+20+i*14,1);pump(30);}
    shell->MouseButtons(bar,top+76,0);pump(200);
    Win98Scroll::Info now{28,0x17,0,0,0,0,0};User::ScrollState(list,0,1,&now,0);
    printf("EXPLORER list bar start=%u end=%u\n",start,now.position);fflush(stdout);
   }
   until([&]{return !User::Rendering();},"list pane repaints after scrolling");
   puts("EXPLORER folder window bar tracking PASS");fflush(stdout);
  } else puts("EXPLORER folder window has no vertical pane bar in this seed");
  User::MoveWindow(folder,before.left,before.top,width,height);pump(150);
 }

 calculator=new App(trace_calls?logger:nullptr);calculator->trace_calls=trace_calls;require(calculator->Start(calc.data(),calc.size(),font.data(),font.size(),"CALC.EXE"),"calculator start");
 until([&]{return find(nullptr,"Calculator");},"calculator starts",10000);
 auto window=find(nullptr,"Calculator");require(window,"calculator window");
 // The first Win98 desktop can lack the optional Quick Launch Show Desktop
 // shortcut. Exercise that path only when the selected tree supplies it.
 if(!skip_show_desktop){
  auto toggle_desktop=[](){
   shell->MouseButtons(75,588,0);shell->MouseButtons(75,588,1);pump(30);
   shell->MouseButtons(75,588,0);pump(120);
  };
  toggle_desktop();
  until([&]{return !User::IsVisible(window)||(User::Style(window)&0x20000000);},"Show Desktop hides Calculator");
  pump(600); // Separate clicks, not a toolbar double-click.
  toggle_desktop();
  until([&]{return User::IsVisible(window)&&!(User::Style(window)&0x20000000);},"Show Desktop restores Calculator");
  puts("EXPLORER Show Desktop toggle PASS");fflush(stdout);
 }else puts("EXPLORER Show Desktop shortcut absent from selected tree; step skipped");
 // A shell draws icons returned by another process, using the same HICON.
 unsigned class_icon=User::ClassAccess(window,-14,0,2);
 require(class_icon,"calculator class icon");
 require(calculator->FindIcon(class_icon)&&shell->FindIcon(class_icon)==calculator->FindIcon(class_icon),"calculator HICON resolves identically in Explorer");
 Gdi::RECT calc_rect{};User::GetWindowRect(window,&calc_rect);
 User::MoveWindow(window,450,20,calc_rect.right-calc_rect.left,calc_rect.bottom-calc_rect.top);pump(100);
 Gdi::RECT address{};require(User::GetWindowRect(find("ComboBox"),&address),"Explorer address combo");


 for(unsigned i=0;i<12;++i){
  Gdi::RECT r{};User::GetWindowRect(window,&r);int x=r.left+70,y=r.top+10;
  shell->MouseButtons(x,y,0);shell->MouseButtons(x,y,1);pump(30);
  shell->MouseButtons(x+(i&1?-12:12),y+(i&1?-8:8),1);pump(30);shell->MouseButtons(x+(i&1?-12:12),y+(i&1?-8:8),0);pump(80);
  until([&]{Gdi::RECT moved{};User::GetWindowRect(window,&moved);return moved.left==r.left+(i&1?-12:12)&&moved.top==r.top+(i&1?-8:8);},"calculator caption drag completes");
  User::PostMessage(window,0x112,0xf020,0);until([&]{return User::Style(window)&0x20000000;},"calculator minimizes");
  User::PostMessage(window,0x112,0xf120,0);until([&]{return !(User::Style(window)&0x20000000)&&calculator->WaitingForInput()&&!User::Rendering();},"calculator restores");
  auto display=[&](const char*want){char value[32]{};User::GetWindowText(User::GetDlgItem(window,403),value,sizeof value);return !strcmp(value,want);};
  click(User::GetDlgItem(window,81));
  until([&]{return display("0.");},"calculator clears after restore");
  click(User::GetDlgItem(window,125)); // 1 in the original CALC dialog.
  until([&]{return display("1.");},"calculator responds after restore");
  until([&]{return !User::Rendering();},"composition completes after drag/minimize/restore");
  User::InvalidateRect(find("SysTreeView32"),nullptr,true);pump(100);
  until([&]{return !User::Rendering();},"foreign process finishes repaint");
  Gdi::RECT digit{};User::GetWindowRect(User::GetDlgItem(window,125),&digit);
  unsigned blue=0;for(int y=digit.top;y<digit.bottom;++y)for(int x=digit.left;x<digit.right;++x)if(pixel(x,y)==0xff)++blue;
  require(blue>0,"Calculator owner-drawn digit remains visible after Explorer repaint");
  unsigned icon_pixels=0;for(int y=address.top+3;y<address.top+15;++y)for(int x=address.left+3;x<address.left+17;++x)if(pixel(x,y)!=0xffffff)++icon_pixels;
  require(icon_pixels>10,"Explorer address icon remains drawn while Calculator runs");

  printf("EXPLORER calculator cycle=%u calls=%u\n",i,calculator->ApiCalls());fflush(stdout);
 }
 screenshot("build/user-regressions/calculator.ppm");
 // End the session the way Win98 ends one: the autostart programs that ran
 // for its whole length are terminated, then reaped. Without the tree there
 // are none and the count is zero.
 const unsigned running=App::LiveChildren();
 const unsigned terminated=App::TerminateChildren(0);
 for(unsigned i=0;i<64&&App::LiveChildren();++i)App::PumpChildren(8);
 printf("EXPLORER autostart children running=%u terminated=%u left=%u\n",running,terminated,App::LiveChildren());fflush(stdout);
 require(!App::LiveChildren(),"every child process ends with the session");
 puts("EXPLORER REGRESSIONS PASS");
 // Process exit drops the independently running guest fibers; do not execute
 // application shutdown callbacks from this host test's stack.
 return 0;
}
