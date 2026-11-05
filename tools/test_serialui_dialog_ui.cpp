// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include "fixture_native_modules.h"
#define main MessageBoxFixtureMain
#include "test_message_box_ui.cpp"
#undef main
static unsigned page(App&app,unsigned sheet){unsigned s=0;require(User::WindowLong(sheet,8,s,false)&&s,"sheet state");unsigned p=app.Read32(s+0x38);require(User::IsWindow(p)&&User::IsVisible(p),"native serial page");require(title(p)=="Port Settings","original page caption");return p;}
static unsigned instance(App&app){
 for(unsigned i=0;i<app.registry.store.node_capacity;++i){const Registry::Value*v=nullptr,*c=nullptr;
  if(!app.registry.store.Query(i,"PortName",v)&&v->size>=4&&!memcmp(v->data,"COM1",4)&&!app.registry.store.Query(i,"Class",c)&&c->size>=5&&(c->data[0]|32)=='p')return i;
 }require(false,"installed COM1 instance");return 0;
}
static void settings(App&app,unsigned node,unsigned flags,unsigned rx,unsigned tx){const Registry::Value*v=nullptr;require(!app.registry.store.Query(node,"Settings",v)&&v->type==3&&v->size==4,"binary FIFO settings");require(v->data[0]==flags&&v->data[1]==tx&&v->data[2]==0&&v->data[3]==rx,"original FIFO encoding");}
static void choose(App&app,unsigned p,unsigned id,unsigned value){unsigned h=User::GetDlgItem(p,id),n=app.Send(h,0x146,0,0);require(h&&n&&n<20,"original combo list");for(unsigned i=0;i<n;++i)if(app.Send(h,0x150,i,0)==value){app.Send(h,0x14e,i,0);return;}require(false,"requested original item data");}
int main(int argc,char**argv){
 require(argc==4,"serial dialog runner EXE FONT OUT");std::string out=argv[3];trace=fopen((out+"/dialog-ui.trace").c_str(),"wb");require(trace,"trace");auto exe=load(argv[1]),font=load(argv[2]);unsigned objects=Gdi::LiveObjects();
 // The implemented CommConfigDialogA builds its property sheet out of
 // COMCTL32, and that DLL runs as original x86 code since PLAN.md step 7.
 // This run tests our own sheet on the small RAM volume, so it keeps the
 // five implementations on their native route.
 {App app(log);require(FixtureModules::KeepNative(app),"native module override");require(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SERIALUI.EXE"),"start serial dialog guest");
 unsigned node=instance(app);const unsigned char initial[]={2,16,0,128};app.registry.store.Set(node,"Settings",3,initial,4);
 unsigned h=box(app);require(title(h)=="COM1 Properties","original sheet caption");unsigned p=page(app,h);screenshot(out+"/serialui-port-settings.ppm");
 click(app,User::GetDlgItem(p,0x423));unsigned advanced=box(app,h);require(title(advanced)=="Advanced Port Settings","original advanced caption");require(!User::IsEnabled(h),"advanced dialog disables property sheet");
 unsigned rx=User::GetDlgItem(advanced,0x3eb),tx=User::GetDlgItem(advanced,0x3ec);
 require(app.Send(rx,0x400,0,0)==2&&app.Send(tx,0x400,0,0)==3,"FIFO initial positions");
 unsigned rect=app.Allocate(16);app.Send(rx,0x419,0,rect);Gdi::RECT thumb{},outer{};app.Read(rect,&thumb,16);app.Free(rect);User::GetWindowRect(rx,&outer);
 int mx=outer.left+(thumb.left+thumb.right)/2,my=outer.top+(thumb.top+thumb.bottom)/2;
 app.MouseButtons(mx,my,0);app.MouseButtons(mx,my,1);settle(app);app.MouseButtons(outer.right-5,my,1);settle(app);app.MouseButtons(outer.right-5,my,0);settle(app);require(app.Send(rx,0x400,0,0)==3,"native trackbar drag reaches upper endpoint");
 screenshot(out+"/serialui-advanced.ppm");
 app.SetFocus(rx);key(app,0x24);key(app,0x27);require(app.Send(rx,0x400,0,0)==1,"trackbar keyboard input");click(app,User::GetDlgItem(advanced,2));settings(app,node,2,128,16);
 click(app,User::GetDlgItem(p,0x423));advanced=box(app,h);rx=User::GetDlgItem(advanced,0x3eb);app.SetFocus(rx);key(app,0x24);key(app,0x27);
 click(app,User::GetDlgItem(advanced,0x3f0));require(!User::IsEnabled(rx),"FIFO disable disables sliders");click(app,User::GetDlgItem(advanced,1));settings(app,node,0,64,16);
 click(app,User::GetDlgItem(p,0x423));advanced=box(app,h);click(app,User::GetDlgItem(advanced,1000));require(User::IsEnabled(User::GetDlgItem(advanced,0x3eb)),"Defaults re-enables FIFO");click(app,User::GetDlgItem(advanced,1));settings(app,node,2,128,16);
 choose(app,p,0x418,110);click(app,User::GetDlgItem(h,2));
 h=box(app);p=page(app,h);choose(app,p,0x418,115200);choose(app,p,0x41a,7);choose(app,p,0x41c,1);choose(app,p,0x41e,2);choose(app,p,0x420,2);click(app,User::GetDlgItem(h,1));
 h=box(app);p=page(app,h);click(app,User::GetDlgItem(p,0x422));click(app,User::GetDlgItem(h,1));
 h=box(app);p=page(app,h);choose(app,p,0x418,300);require(User::PostMessage(h,0x46e,0,0),"apply message");settle(app);choose(app,p,0x418,110);click(app,User::GetDlgItem(h,2));
 finished(app,"SERIALUI cancel, edit, defaults, Apply then Cancel");}
 require(Gdi::LiveObjects()==objects,"serial dialog object cleanup");fclose(trace);
}
