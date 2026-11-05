// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include <cstdio>
#include <vector>
#include <cstdlib>
static std::vector<unsigned char> load(const char*path){FILE*f=fopen(path,"rb");if(!f)return{};fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);if(fread(b.data(),1,b.size(),f)!=b.size())b.clear();fclose(f);return b;}
static void log(const char*s){puts(s);}
int main(int argc,char**argv){if(argc!=3)return 2;auto exe=load(argv[1]),font=load(argv[2]);X86::Application a(log);if(!a.Start(exe.data(),exe.size(),font.data(),font.size(),"COMBOMSE.EXE"))return 3;
 auto step=[&](){for(unsigned i=0;i<16;++i){a.Pump();if(a.Status()==X86::Application::Failed){fprintf(stderr,"FAIL %s\n",a.Error());exit(35202);}}};step();
 unsigned p=User::FindWindow("ComboMouseFixture",nullptr),c=User::GetDlgItem(p,12),list=0;
 auto require=[&](bool ok,unsigned code){if(!ok){fprintf(stderr,"FAIL %u capture=%u focus=%u state=%u\n",code,User::CaptureOperation(0),User::GetFocus(),a.Status());exit(code);}};
 auto read=[&](unsigned offset){unsigned value=0;User::WindowLong(p,offset,value,false);return value;};auto write=[&](unsigned offset,unsigned value){User::WindowLong(p,offset,value,true);};
 auto reset=[&](){for(unsigned offset=0;offset<=20;offset+=4)write(offset,0);write(28,0);};
 auto selected=[&](){return a.Send(c,0x147,0,0);};
 auto point=[&](unsigned index){Gdi::RECT r{};unsigned scratch=a.Allocate(16);require(scratch,35203);require(a.Send(list,0x198,index,scratch)!=~0u&&a.Read(scratch,&r,16),35204);a.Free(scratch);Gdi::POINT o{};User::ScreenToClient(list,&o);return Gdi::POINT{(r.left+r.right)/2-o.x,(r.top+r.bottom)/2-o.y};};
 auto open=[&](){Gdi::RECT r{};User::GetWindowRect(c,&r);a.Mouse(r.right-9,(r.top+r.bottom)/2,true,false);step();require(a.Send(c,0x157,0,0)&&User::CaptureOperation(0)==c,35205);a.Mouse(r.right-9,(r.top+r.bottom)/2,false,true);step();unsigned nodes[64],n=0;User::WindowSnapshot(0,nodes,64,n);list=0;for(unsigned i=0;i<n;++i)if(User::Owner(nodes[i])==c&&User::CommonControl(nodes[i])==4)list=nodes[i];require(list&&User::IsVisible(list)&&User::CaptureOperation(0)==list&&User::GetFocus()==c,35206);};
 auto clickrow=[&](unsigned index){auto pt=point(index);a.Mouse(pt.x,pt.y,false,false);step();a.Mouse(pt.x,pt.y,true,false);step();a.Mouse(pt.x,pt.y,false,true);step();};
 require(p&&c,35207);reset();open();clickrow(1);require(selected()==1&&!User::IsVisible(list)&&!User::CaptureOperation(0)&&User::GetFocus()==c,35208);unsigned n=read(0);require(n>=3&&read(4+(n-3)*4)==9&&read(4+(n-2)*4)==8&&read(4+(n-1)*4)==1&&read(28)==3,35209);printf("COMBO_MOUSE click PASS selection=1 capture=0 focus=%u notifications=9,8,1\n",c);
 reset();open();auto pt=point(0);a.Mouse(pt.x,pt.y,false,false);step();require(selected()==0,35210);a.RawKey(27,0,true);a.RawKey(27,0,false);step();require(selected()==1&&!User::IsVisible(list)&&!User::CaptureOperation(0),35211);printf("COMBO_MOUSE escape PASS selection_restored=1 capture=0\n");
 reset();open();a.Mouse(600,400,true,false);step();a.Mouse(600,400,false,true);step();require(!User::IsVisible(list)&&!User::CaptureOperation(0)&&selected()==1,35212);printf("COMBO_MOUSE outside PASS selection=1 capture=0\n");
 reset();open();User::CaptureOperation(1,p);step();require(!User::IsVisible(list)&&User::CaptureOperation(0)==p,35213);User::CaptureOperation(2);step();printf("COMBO_MOUSE capture-transfer PASS new_owner_preserved\n");
 reset();{Gdi::RECT r{};User::GetWindowRect(c,&r);a.Mouse(r.right-9,(r.top+r.bottom)/2,true,false);step();auto pt=point(0);a.Mouse(pt.x,pt.y,false,false);step();require(User::CaptureOperation(0)==list,35215);a.Mouse(pt.x,pt.y,false,true);step();require(selected()==0&&!User::IsVisible(list)&&!User::CaptureOperation(0)&&User::GetFocus()==c,35216);printf("COMBO_MOUSE drag PASS selection=0 capture=0\n");}
 reset();write(24,9);open();clickrow(0);require(!User::IsWindow(c)&&!User::IsWindow(list)&&!User::CaptureOperation(0),35214);printf("COMBO_MOUSE destruction PASS no_popup_or_capture\n");
 printf("PASS combo-mouse api_calls=%u callbacks=%u\n",a.ApiCalls(),a.Callbacks());return 0;
}
