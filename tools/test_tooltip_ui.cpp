// SPDX-License-Identifier: GPL-2.0-or-later
// Existing shared objects provide the runtime; this is one focused UI driver.
#define main MessageBoxFixtureMain
#include "test_message_box_ui.cpp"
#undef main
static unsigned waitTip(App&app,unsigned owner,const char*caption){
 auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);
 while(std::chrono::steady_clock::now()<end){settle(app);unsigned tip=User::FindWindow("tooltips_class32",nullptr);if(title(owner)==caption&&tip&&User::IsVisible(tip))return tip;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 require(false,"real tooltip timer shows expected tool");return 0;
}
int main(int argc,char**argv){
 require(argc==4,"usage: tooltip-runner TOOLTIP.EXE fixedsys.fnt output-directory");std::string out=argv[3];trace=fopen((out+"/tooltip-ui.trace").c_str(),"wb");require(trace,"tooltip trace");auto exe=load(argv[1]),font=load(argv[2]);unsigned before=Gdi::LiveObjects();
 {App app(log);require(app.Start(exe.data(),exe.size(),font.data(),font.size(),"TOOLTIP.EXE"),"start tooltip guest");settle(app);unsigned owner=User::FindWindow("TooltipPort98","tooltip ready");require(owner,"tool storage/text/delay APIs reach visible phase");Gdi::POINT p={0,0};require(User::ScreenToClient(owner,&p),"tool client rectangle mapping");app.MouseButtons(50-p.x,60-p.y,0);unsigned tip=waitTip(app,owner,"tooltip first");Gdi::RECT bounds{};require(User::GetWindowRect(tip,&bounds),"tooltip real popup bounds");unsigned actualfont=User::ControlMessage(tip,0x31,0,0);require(!visibleRows(actualfont,"Plain tip",bounds).empty(),"owned text is visibly drawn with original prefix removal");require(User::GetFocus()==owner,"tooltip did not take focus");screenshot(out+"/tooltip-plain.ppm");
  unsigned second=User::GetDlgItem(owner,42);Gdi::RECT target{};require(User::GetWindowRect(second,&target),"second tool HWND");app.MouseButtons((target.left+target.right)/2,(target.top+target.bottom)/2,0);tip=waitTip(app,owner,"tooltip second");require(User::GetWindowRect(tip,&bounds),"second tooltip bounds");require(!visibleRows(actualfont,"Callback tooltip",bounds).empty(),"guest callback text is visibly drawn");screenshot(out+"/tooltip-callback.ppm");key(app,13);finished(app,"tooltip A/W tool storage + real hover timers + callback/show/pop + cleanup");
 }require(Gdi::LiveObjects()==before,"tooltip and tool-window cleanup");fprintf(trace,"PASS two visible tooltip texts; real hover/reshow timers; guest callback caching and pop notices\n");fclose(trace);return 0;
}
