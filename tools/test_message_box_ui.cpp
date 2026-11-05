// SPDX-License-Identifier: GPL-2.0-or-later
// Link once against the parent's shared native runtime objects. No synthetic commands.
#include "../src/x86_app.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
using App=X86::Application;
static FILE*trace;
static void require(bool ok,const char*what){if(!ok){fprintf(stderr,"MESSAGEBOX UI FAILED: %s\n",what);abort();}}
static std::vector<unsigned char>load(const char*path){FILE*f=fopen(path,"rb");require(f,"open guest/font");fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);require(fread(b.data(),1,n,f)==size_t(n),"read guest/font");fclose(f);return b;}
static void log(const char*text){fprintf(trace,"APP %s\n",text);fflush(trace);}
static void healthy(App&app){if(app.Status()==App::Failed||(app.Status()==App::Exited&&app.ExitCode())){fprintf(stderr,"status=%u exit=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());require(false,"guest execution");}}
static void settle(App&app){for(unsigned n=0;n<512;++n){app.Pump(32);healthy(app);if(app.Status()==App::Idle||app.Status()==App::Exited)return;}require(false,"modal input did not suspend");}
static std::string title(unsigned hwnd){char s[512]{};User::GetWindowText(hwnd,s,sizeof s);return s;}
static unsigned box(App&app,unsigned previous=0){
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 while(std::chrono::steady_clock::now()<deadline){settle(app);unsigned nodes[192],count=0;require(User::WindowSnapshot(0,nodes,192,count),"window snapshot");
  for(unsigned i=0;i<count;++i){char name[64]{};User::GetClassName(nodes[i],name,sizeof name);if(nodes[i]!=previous&&User::IsVisible(nodes[i])&&!strcmp(name,"#32770"))return nodes[i];}
  if(app.Status()==App::Exited)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }require(false,"visible modal dialog");return 0;
}
static void key(App&app,unsigned code){app.RawKey(code,0,true);app.RawKey(code,0,false);settle(app);}
static void click(App&app,unsigned hwnd){Gdi::RECT r{};require(User::GetWindowRect(hwnd,&r),"button hit rectangle");int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;app.MouseButtons(x,y,0);app.MouseButtons(x,y,1);app.MouseButtons(x,y,0);settle(app);}
static unsigned color(int x,int y){const auto*p=User::Pixels()+(y*800+x)*4;return p[0]|(unsigned(p[1])<<8)|(unsigned(p[2])<<16);}
static Gdi::RECT clipped(Gdi::RECT r){r.left=std::max(r.left,0);r.top=std::max(r.top,0);r.right=std::min(r.right,800);r.bottom=std::min(r.bottom,600);return r;}
struct GlyphMask{int width=0,height=0;std::vector<unsigned char>ink;};
static GlyphMask glyphs(unsigned font,const std::string&text){
 unsigned dc=Gdi::CreateCompatibleDC();require(dc,"glyph reference DC");auto oldfont=Gdi::SelectObject(dc,font);Gdi::SIZE size{};require(Gdi::GetTextExtentPoint32A(dc,text.c_str(),text.size(),&size),"reference text metrics");require(size.cx>0&&size.cy>0,"nonempty glyph reference");
 Gdi::BITMAPINFO info{{40,size.cx,-size.cy,1,32,0,0,0,0,0,0}};void*bits=nullptr;unsigned bitmap=Gdi::CreateDIBSection(dc,&info,0,&bits);require(bitmap&&bits,"glyph reference bitmap");auto oldbitmap=Gdi::SelectObject(dc,bitmap);memset(bits,255,size.cx*size.cy*4);Gdi::SetTextColor(dc,0);Gdi::SetBkMode(dc,Gdi::TRANSPARENT);require(Gdi::TextOutA(dc,0,0,text.c_str(),text.size()),"reference glyph raster");
 auto*p=static_cast<unsigned*>(bits);int left=size.cx,top=size.cy,right=0,bottom=0;
 for(int y=0;y<size.cy;++y)for(int x=0;x<size.cx;++x)if(!(p[y*size.cx+x]&0xffffff)){left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}
 require(right>left&&bottom>top,"reference contains visible ink");GlyphMask out;out.width=right-left;out.height=bottom-top;
 for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)out.ink.push_back(!(p[y*size.cx+x]&0xffffff));
 Gdi::SelectObject(dc,oldbitmap);Gdi::SelectObject(dc,oldfont);require(Gdi::DeleteObject(bitmap)&&Gdi::DeleteDC(dc),"reference cleanup");return out;
}
// Search actual compositor pixels. This checks text content through glyph shapes;
// window data alone cannot prove that the body or button labels were drawn.
static std::vector<int>visibleRows(unsigned font,const std::string&text,Gdi::RECT area){
 auto g=glyphs(font,text);area=clipped(area);std::vector<int>rows;
 for(int y=area.top;y+g.height<=area.bottom;++y){bool row=false;
  for(int x=area.left;!row&&x+g.width<=area.right;++x){bool match=true;
   for(int yy=0;match&&yy<g.height;++yy)for(int xx=0;xx<g.width;++xx)if((color(x+xx,y+yy)==0)!=bool(g.ink[yy*g.width+xx])){match=false;break;}
   row=match;
  }if(row)rows.push_back(y);
 }return rows;
}
static std::string plain(const char*s){std::string t;for(;*s;++s)if(*s!='&')t+=*s;return t;}
struct Button{unsigned id;const char*label;};
struct View{unsigned hwnd,font;Gdi::RECT body;};
static View inspect(unsigned hwnd,const std::string&caption,std::initializer_list<Button>expected,unsigned focus){
 require(title(hwnd)==caption,"dialog caption text");require(User::IsVisible(hwnd),"dialog visibility");
 Gdi::RECT outer{},client{};require(User::GetWindowRect(hwnd,&outer)&&User::GetClientRect(hwnd,&client),"dialog bounds");Gdi::POINT origin{0,0};require(User::ScreenToClient(hwnd,&origin),"client origin");origin.x=-origin.x;origin.y=-origin.y;
 Gdi::RECT body{origin.x+2,origin.y+2,origin.x+client.right-2,origin.y+client.bottom-2};
 unsigned count=0,nodes[192],buttons=0,font=0;require(User::WindowSnapshot(hwnd,nodes,192,count),"dialog children");for(unsigned i=0;i<count;++i)buttons+=User::IsButton(nodes[i]);require(buttons==expected.size(),"original button count");
 for(auto b:expected){unsigned child=User::GetDlgItem(hwnd,b.id);require(child&&User::IsButton(child)&&User::IsVisible(child)&&User::IsEnabled(child),"visible enabled original button");require(title(child)==b.label,"button label");Gdi::RECT r{};require(User::GetWindowRect(child,&r),"button bounds");body.bottom=std::min(body.bottom,r.top-2);font=User::ControlMessage(child,0x31,0,0);require(font,"actual button font");r.left+=3;r.top+=3;r.right-=3;r.bottom-=3;require(!visibleRows(font,plain(b.label),r).empty(),"visible button label glyphs");}
 require(User::ControlID(User::GetFocus())==focus&&User::Parent(User::GetFocus())==hwnd,"original default button focus");
 if(!caption.empty()){unsigned ink=0;auto r=clipped({outer.left+8,outer.top+4,outer.right-40,origin.y-2});for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x)ink+=color(x,y)==0xffffff;require(ink>5,"visible caption ink");}
 fprintf(trace,"VISIBLE caption=%s hwnd=%u buttons=%u default=%u body=[%d,%d,%d,%d]\n",caption.c_str(),hwnd,buttons,focus,body.left,body.top,body.right,body.bottom);fflush(trace);return {hwnd,font,body};
}
static void body(const View&v,const std::string&text,unsigned lines=1){auto rows=visibleRows(v.font,text,v.body);require(rows.size()>=lines,"visible expected body glyphs/lines");fprintf(trace,"BODY caption=%s prefix=%s matched_rows=%zu first_y=%d\n",title(v.hwnd).c_str(),text.c_str(),rows.size(),rows.front());fflush(trace);}
static void emptyBody(const View&v){auto r=clipped(v.body);unsigned ink=0;for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x)ink+=color(x,y)==0;require(!ink,"empty resource produces empty body");}
static void screenshot(const std::string&path){FILE*f=fopen(path.c_str(),"wb");require(f,"open screenshot");fprintf(f,"P6\n800 600\n255\n");for(int y=0;y<600;++y)for(int x=0;x<800;++x){unsigned c=color(x,y);unsigned char rgb[]={static_cast<unsigned char>(c>>16),static_cast<unsigned char>(c>>8),static_cast<unsigned char>(c)};fwrite(rgb,1,3,f);}fclose(f);}
static void finished(App&app,const char*name){settle(app);require(app.Status()==App::Exited&&!app.ExitCode(),"guest completes");require(!User::LiveWindows(),"all native windows destroyed");fprintf(trace,"PASS %s calls=%u callbacks=%u exit=0\n",name,app.ApiCalls(),app.Callbacks());printf("MESSAGEBOX %s PASS calls=%u callbacks=%u\n",name,app.ApiCalls(),app.Callbacks());}
static void publicBoxes(const char*exe,const std::vector<unsigned char>&font,const std::string&out){
 auto bytes=load(exe);App app(log);require(app.Start(bytes.data(),bytes.size(),font.data(),font.size(),"MBPUBLIC.EXE"),"start MBPUBLIC");
 unsigned h=box(app);auto v=inspect(h,"Public resource caption",{{2,"OK"}},2);body(v,"Body from the");key(app,13);
 h=box(app,h);v=inspect(h,std::string(79,'C'),{{1,"OK"},{2,"Cancel"}},2);body(v,std::string(12,'T'),2);screenshot(out+"/message-box-public-long.ppm");key(app,13);
 h=box(app,h);v=inspect(h,"",{{2,"OK"}},2);emptyBody(v);key(app,13);
 h=box(app,h);v=inspect(h,"Wide",{{6,"&Yes"},{7,"&No"},{2,"Cancel"}},7);body(v,"Gr\xfc\xdf" "e \x80");key(app,13);
 h=box(app,h);v=inspect(h,"Wide",{{2,"OK"}},2);body(v,"Gr\xfc\xdf" "e \x80");key(app,13);
 h=box(app,h);v=inspect(h,"Error",{{2,"OK"}},2);emptyBody(v);key(app,13);finished(app,"MBPUBLIC six boxes");
}
static void nativeBoxes(const char*exe,const std::vector<unsigned char>&font,const std::string&out){
 auto bytes=load(exe);App app(log);require(app.Start(bytes.data(),bytes.size(),font.data(),font.size(),"MBUI.EXE"),"start MBUI");
 unsigned h=box(app),owner=User::FindWindow("MessageBoxOwner98","native owner"),sibling=User::FindWindow("MessageBoxOwner98","native sibling");require(owner&&sibling&&!User::IsEnabled(owner)&&User::IsEnabled(sibling),"owner-only modality");
 auto v=inspect(h,"Native OK",{{2,"OK"}},2);body(v,"Escape returns");key(app,27);
 h=box(app,h);v=inspect(h,"Native callback",{{6,"&Yes"},{7,"&No"},{2,"Cancel"},{9,"&Help"}},7);body(v,"Native callback");body(v,"Second line wraps");body(v,"Third line remains");screenshot(out+"/message-box-native-multiline.ppm");
 key(app,0x70);require(title(owner)=="callback help 1"&&User::IsWindow(h),"F1 invokes guest Help callback without closing");fprintf(trace,"HELP callback F1 validated HELPINFO28/context/owner disabled\n");
 click(app,User::GetDlgItem(h,9));require(title(owner)=="callback help 2"&&User::IsWindow(h),"mouse Help invokes second guest callback");fprintf(trace,"HELP callback mouse validated control9/box HWND/pointer coordinates\n");click(app,User::GetDlgItem(h,7));
 h=box(app,h);v=inspect(h,"Native owner help",{{1,"OK"},{2,"Cancel"},{9,"&Help"}},1);body(v,"Help reaches the");click(app,User::GetDlgItem(h,9));require(title(owner)=="owner help 1"&&User::IsWindow(h),"owner receives real WM_HELP");fprintf(trace,"HELP owner WM_HELP validated context ABCDEF01\n");key(app,27);
 h=box(app,h);v=inspect(h,"Native no Cancel",{{6,"&Yes"},{7,"&No"}},6);body(v,"Escape must keep");key(app,0x70);require(title(owner)=="owner help 2","F1 without MB_HELP uses owner and zero context");fprintf(trace,"HELP no MB_HELP: owner receives zero context; configured guest callback ignored\n");key(app,27);require(User::IsWindow(h)&&User::IsVisible(h),"Escape cannot close box without Cancel");click(app,User::GetDlgItem(h,6));
 h=box(app,h);v=inspect(h,"Native task modal",{{4,"&Retry"},{2,"Cancel"}},2);body(v,"Both task windows");require(!User::IsEnabled(owner)&&!User::IsEnabled(sibling),"task modality disables both real windows");key(app,13);finished(app,"MBUI five boxes + four Help deliveries");
}
int main(int argc,char**argv){
 require(argc==5,"usage: runner MBPUBLIC.EXE MBUI.EXE fixedsys.fnt output-directory");std::string out=argv[4];trace=fopen((out+"/message-box-ui.trace").c_str(),"wb");require(trace,"open trace");auto font=load(argv[3]);
 unsigned before=Gdi::LiveObjects();publicBoxes(argv[1],font,out);require(Gdi::LiveObjects()==before,"public guest resource cleanup");nativeBoxes(argv[2],font,out);require(Gdi::LiveObjects()==before,"native guest resource cleanup");
 fprintf(trace,"PASS eleven visible boxes; real input; glyph checks; four Help deliveries\n");fclose(trace);return 0;
}
