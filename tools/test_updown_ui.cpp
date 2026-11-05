// SPDX-License-Identifier: GPL-2.0-or-later
// The shared integration links this once; these helpers do not run old tests.
#define main MessageBoxFixtureMain
#include "test_message_box_ui.cpp"
#undef main
// Original mouse release sends EM_SETSEL(0,-1), so the buddy is selected.
// Match the actual glyph mask in the native EDIT selection colors.
static bool selectedGlyphs(unsigned font,const std::string&text,Gdi::RECT area){
 auto g=glyphs(font,text);area=clipped(area);
 for(int y=area.top;y+g.height<=area.bottom;++y)for(int x=area.left;x+g.width<=area.right;++x){bool match=true;
  for(int yy=0;match&&yy<g.height;++yy)for(int xx=0;xx<g.width;++xx)
   if(color(x+xx,y+yy)!=(g.ink[yy*g.width+xx]?0xffffffu:0x000080u)){match=false;break;}
  if(match)return true;
 }return false;
}
int main(int argc,char**argv){
 require(argc==4,"usage: updown-runner UPDOWN.EXE fixedsys.fnt output-directory");std::string out=argv[3];trace=fopen((out+"/updown-ui.trace").c_str(),"wb");require(trace,"UpDown trace");auto exe=load(argv[1]),font=load(argv[2]);unsigned before=Gdi::LiveObjects();
 {App app(log);require(app.Start(exe.data(),exe.size(),font.data(),font.size(),"UPDOWN.EXE"),"start UpDown guest");settle(app);unsigned owner=User::FindWindow("UpDownPort98","updown ready");require(owner,"numeric/buddy/acceleration checks reach input phase");unsigned spin=User::GetDlgItem(owner,42),edit=User::GetDlgItem(owner,41);require(spin&&edit&&User::IsVisible(spin)&&User::GetFocus()==edit,"actual visible UpDown with focused buddy");Gdi::RECT r{},text{};require(User::GetWindowRect(spin,&r)&&User::GetWindowRect(edit,&text),"real control bounds");unsigned selected=User::ControlMessage(edit,0x31,0,0);require(!visibleRows(selected,"12",text).empty(),"buddy 12 is drawn from actual font");
  int cx=(r.left+r.right)/2,top=(3*r.top+r.bottom)/4,bottom=(r.top+3*r.bottom)/4;
  // Check arrow ink in both actual halves, not just the existence of a HWND.
  for(int center:{top,bottom}){unsigned ink=0;for(int y=center-4;y<center+5;++y)for(int x=cx-5;x<cx+5;++x)ink+=color(x,y)==0;require(ink>=10,"visible arrow pixels");}
  screenshot(out+"/updown-initial.ppm");
  auto upperClick=[&](){app.MouseButtons(cx,top,0);app.MouseButtons(cx,top,1);app.MouseButtons(cx,top,0);settle(app);};
  upperClick();require(title(owner)=="updown veto"&&title(edit)=="12","real first click is vetoed");
  std::this_thread::sleep_for(std::chrono::milliseconds(User::GetDoubleClickTime()+20));upperClick();require(title(owner)=="updown mouse"&&title(edit)=="14","mutable native delta and buddy write");screenshot(out+"/updown-mouse.ppm");require(selectedGlyphs(selected,"14",text),"selected changed buddy glyphs drawn");
  key(app,0x28);finished(app,"UpDown range/base/accel/buddy + mouse veto/mutable step + keyboard + teardown");
 }require(Gdi::LiveObjects()==before,"UpDown/buddy/window/GDI cleanup");fprintf(trace,"PASS visible UpDown arrows, buddy glyphs, mouse veto/mutable delta and key input\n");fclose(trace);return 0;
}
