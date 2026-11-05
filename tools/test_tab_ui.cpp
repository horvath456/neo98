// SPDX-License-Identifier: GPL-2.0-or-later
// Reuse the existing read-only glyph/screenshot and real-input test helpers.
// Renaming its main does not run the MessageBox fixtures or any old suite.
#define main MessageBoxFixtureMain
#include "test_message_box_ui.cpp"
#undef main
int main(int argc,char**argv){
 require(argc==4,"usage: tab-runner TAB.EXE fixedsys.fnt output-directory");std::string out=argv[3];trace=fopen((out+"/tab-ui.trace").c_str(),"wb");require(trace,"tab trace");auto exe=load(argv[1]),font=load(argv[2]);unsigned before=Gdi::LiveObjects();
 {App app(log);require(app.Start(exe.data(),exe.size(),font.data(),font.size(),"TAB.EXE"),"start tab guest");settle(app);
  unsigned owner=User::FindWindow("TabPort98","tabs ready");require(owner,"tab guest reaches visible input phase");unsigned tab=User::GetDlgItem(owner,42);require(tab&&User::IsVisible(tab)&&User::GetFocus()==tab,"actual focused visible tab control");
  const char*labels[]={"General","Display","Advanced"};unsigned actualfont=User::ControlMessage(tab,0x31,0,0);
  for(unsigned i=0;i<3;++i){unsigned marker=User::GetDlgItem(tab,800+i);Gdi::RECT rect{};require(marker&&!User::IsVisible(marker)&&User::GetWindowRect(marker,&rect),"fixture marker from actual TCM_GETITEMRECT");require(!visibleRows(actualfont,labels[i],rect).empty(),"actual visible tab-label glyphs");fprintf(trace,"TAB visible label=%s rect=[%d,%d,%d,%d]\n",labels[i],rect.left,rect.top,rect.right,rect.bottom);}
  screenshot(out+"/tabs-initial.ppm");unsigned second=User::GetDlgItem(tab,801);click(app,second);require(title(owner)=="tabs veto","parent veto blocks first mouse selection");
  // These are separate clicks. The original non-button tab WndProc ignores
  // WM_LBUTTONDBLCLK; USER correctly classifies a rapid retry as it.
  std::this_thread::sleep_for(std::chrono::milliseconds(User::GetDoubleClickTime()+20));
  click(app,second);require(title(owner)=="tabs mouse"&&User::GetFocus()==tab,"second real click commits tab selection");screenshot(out+"/tabs-selected.ppm");
  key(app,0x27);finished(app,"TAB item contract + veto/mouse/keyboard");
 }require(Gdi::LiveObjects()==before,"tab DPA/string/DC/window cleanup");fprintf(trace,"PASS visible tabs; real mouse veto/retry and keyboard selection\n");fclose(trace);return 0;
}
