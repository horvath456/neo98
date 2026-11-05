// SPDX-License-Identifier: GPL-2.0-or-later
// Inspection reads controller/tab bytes only; all interaction is real input.
#define private public
#include "../src/x86_app.h"
#undef private
#define main MessageBoxFixtureMain
#include "test_message_box_ui.cpp"
#undef main
static unsigned sheetState(App&app,unsigned hwnd){unsigned state=0;require(User::WindowLong(hwnd,8,state,false)&&state,"original sheet state in dialog extra bytes");require(app.Read32(state)==hwnd,"controller owns actual sheet HWND");return state;}
static unsigned tabSelection(App&app,unsigned tab){unsigned state=0;require(User::WindowLong(tab,0,state,false)&&state,"tab state");return app.Read32(state+0x3c);}
static Gdi::RECT tabRect(App&app,unsigned tab,unsigned index){unsigned state=0;User::WindowLong(tab,0,state,false);unsigned array=app.Read32(state+0x28);require(index<app.Read32(array),"valid item index");unsigned item=app.Read32(app.Read32(array+4)+index*4);Gdi::RECT r{};app.Read(item,&r,16);Gdi::POINT origin{0,0};User::ScreenToClient(tab,&origin);r.left-=origin.x;r.right-=origin.x;r.top-=origin.y;r.bottom-=origin.y;return r;}
static void clickTab(App&app,unsigned tab,unsigned index){auto r=tabRect(app,tab,index);int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;app.MouseButtons(x,y,0);app.MouseButtons(x,y,1);app.MouseButtons(x,y,0);settle(app);}
static unsigned inspectSheet(App&app,unsigned hwnd,const char*caption,unsigned selected){
 require(title(hwnd)==caption,"actual original-template sheet caption");unsigned tab=User::GetDlgItem(hwnd,0x3020);require(tab&&User::IsVisible(tab)&&(User::Style(tab)&0x200),"original multiline tab style preserved");require(tabSelection(app,tab)==selected,"original requested start index");
 unsigned font=User::ControlMessage(tab,0x31,0,0);require(!visibleRows(font,"Clock",tabRect(app,tab,0)).empty()&&!visibleRows(font,"Zone",tabRect(app,tab,1)).empty(),"two visible tab labels");
 const unsigned ids[]={1,2,0x3021};const char*labels[]={"OK","Cancel","&Apply"};
 for(unsigned i=0;i<3;++i){unsigned button=User::GetDlgItem(hwnd,ids[i]);require(button&&User::IsVisible(button)&&title(button)==labels[i],"actual original button label");require(User::IsEnabled(button)==(i!=2),"Apply initially disabled");Gdi::RECT r{};User::GetWindowRect(button,&r);if(i!=2)require(!visibleRows(font,labels[i],r).empty(),"visible button glyphs");}
 unsigned help=User::GetDlgItem(hwnd,9);require(help&&!User::IsVisible(help),"Help hidden without a help page");unsigned s=sheetState(app,hwnd),page=app.Read32(s+0x38);require(page&&User::IsVisible(page)&&User::Parent(page)==hwnd,"actual selected page dialog");fprintf(trace,"SHEET caption=%s hwnd=%u tab=%u page=%u start=%u\n",caption,hwnd,tab,page,selected);return tab;
}
int main(int argc,char**argv){
 require(argc==4,"usage: sheet-runner SHEET.EXE fixedsys.fnt output-directory");std::string out=argv[3];trace=fopen((out+"/property-sheet-ui.trace").c_str(),"wb");require(trace,"property sheet trace");auto exe=load(argv[1]),font=load(argv[2]);unsigned before=Gdi::LiveObjects();
 {App app(log);require(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SHEET.EXE"),"start unchanged prepared sheet guest");unsigned hwnd=box(app),tab=inspectSheet(app,hwnd,"Original property sheet",1);unsigned state=sheetState(app,hwnd),zone=app.Read32(state+0x38);screenshot(out+"/property-sheet-zone.ppm");
  clickTab(app,tab,0);require(tabSelection(app,tab)==1&&app.Read32(state+0x38)==zone&&User::IsVisible(zone),"page KILLACTIVE veto preserves selection and page");
  std::this_thread::sleep_for(std::chrono::milliseconds(User::GetDoubleClickTime()+20));clickTab(app,tab,0);require(tabSelection(app,tab)==0&&app.Read32(state+0x38)!=zone&&!User::IsVisible(zone),"second click creates and shows Clock");screenshot(out+"/property-sheet-clock.ppm");
  click(app,User::GetDlgItem(hwnd,1));unsigned wide=box(app,hwnd);inspectSheet(app,wide,"Original wide sheet",0);click(app,User::GetDlgItem(wide,2));finished(app,"SHEET original template, ANSI/Unicode pages, veto, Apply/Cancel, release");
 }require(Gdi::LiveObjects()==before,"property sheet fonts/windows cleanup");fprintf(trace,"PASS unchanged SHEET guest\n");fclose(trace);return 0;
}
