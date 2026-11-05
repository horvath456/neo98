// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#define main MessageBoxFixtureMain
#include "test_message_box_ui.cpp"
#undef main
static unsigned serial_page(App&app,unsigned h){unsigned s=0;require(User::WindowLong(h,8,s,false)&&s,"sheet state");unsigned p=app.Read32(s+0x38);require(User::IsWindow(p)&&title(p)=="Port Settings","enumerated native page");return p;}
static void baud(App&app,unsigned p,unsigned value){unsigned combo=User::GetDlgItem(p,0x418),n=app.Send(combo,0x146,0,0);for(unsigned i=0;i<n;++i)if(app.Send(combo,0x150,i,0)==value){app.Send(combo,0x14e,i,0);return;}require(false,"baud item");}
int main(int argc,char**argv){
 require(argc==4,"enum runner EXE FONT OUT");std::string out=argv[3];trace=fopen((out+"/enum-ui.trace").c_str(),"wb");require(trace,"trace");auto exe=load(argv[1]),font=load(argv[2]);unsigned objects=Gdi::LiveObjects();
 {App app(log);require(app.Start(exe.data(),exe.size(),font.data(),font.size(),"SERIALENUM.EXE"),"start enum guest");
 bool configured=false;
 for(unsigned i=0;i<app.registry.store.node_capacity;++i){const Registry::Value*v=nullptr,*c=nullptr;
  if(!app.registry.store.Query(i,"PortName",v)&&v->size>=4&&!memcmp(v->data,"COM1",4)&&!app.registry.store.Query(i,"Class",c)&&c->size>=5&&(c->data[0]|32)=='p'){
   const char desc[]="NEO98 enum test";require(!app.registry.store.Set(i,"DeviceDesc",1,desc,sizeof desc),"set fixture DeviceDesc");configured=true;break;
  }
 }
 require(configured,"installed port instance");
 unsigned h=box(app);baud(app,serial_page(app,h),300);click(app,User::GetDlgItem(h,2));
 h=box(app);baud(app,serial_page(app,h),115200);click(app,User::GetDlgItem(h,1));
 finished(app,"SERIALUI EnumPropPages, callback rejection, deferred save, cancellation");}
 require(Gdi::LiveObjects()==objects,"enum page objects released");fclose(trace);
}
