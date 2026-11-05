// SPDX-License-Identifier: GPL-2.0-or-later
#define main MessageBoxFixtureMain
#include "test_message_box_ui.cpp"
#undef main
int main(int argc,char**argv){
 require(argc==4,"usage: multiline-runner MULTITAB.EXE fixedsys.fnt output-directory");std::string out=argv[3];trace=fopen((out+"/multiline-ui.trace").c_str(),"wb");require(trace,"multiline trace");auto exe=load(argv[1]),font=load(argv[2]);unsigned before=Gdi::LiveObjects();
 {App app(log);require(app.Start(exe.data(),exe.size(),font.data(),font.size(),"MULTITAB.EXE"),"start multiline guest");settle(app);
  unsigned owner=User::FindWindow("MultiTab98","multi ready");require(owner,"multiline guest reaches input phase");unsigned tab=User::GetDlgItem(owner,42);require(tab&&User::IsVisible(tab)&&User::GetFocus()==tab,"actual visible focused multiline control");
  const char*labels[]={"General","Display","Advanced","Fonts","Colors","Network","Sound","Devices"};unsigned actualfont=User::ControlMessage(tab,0x31,0,0);
  auto inspect=[&](){for(unsigned i=0;i<8;++i){unsigned marker=User::GetDlgItem(tab,800+i);Gdi::RECT rect{};require(marker&&!User::IsVisible(marker)&&User::GetWindowRect(marker,&rect),"fixture marker from actual item rectangle");require(!visibleRows(actualfont,labels[i],rect).empty(),"actual multiline label pixels");fprintf(trace,"LABEL %s [%d,%d,%d,%d]\n",labels[i],rect.left,rect.top,rect.right,rect.bottom);}};
  inspect();screenshot(out+"/multiline-initial.ppm");unsigned target=User::GetDlgItem(tab,802);click(app,target);require(title(owner)=="multi veto","first row selection vetoed");
  std::this_thread::sleep_for(std::chrono::milliseconds(User::GetDoubleClickTime()+20));click(app,target);require(title(owner)=="multi mouse","second click selects and rotates row");inspect();screenshot(out+"/multiline-selected.ppm");
  key(app,0x27);require(title(owner)=="multi right","Right chooses adjacent item");key(app,0x26);finished(app,"multiline row state, fixed width, mouse veto, Right/Up input");
 }require(Gdi::LiveObjects()==before,"multiline object/DC cleanup");fprintf(trace,"PASS multiline UI\n");fclose(trace);return 0;
}
