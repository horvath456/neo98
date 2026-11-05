// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <set>
#include <vector>
#include <cstdlib>
static unsigned menu_fail_at=0,menu_alloc_calls=0;
static void*MenuTestMalloc(size_t size){if(++menu_alloc_calls==menu_fail_at)return nullptr;return std::malloc(size);}
#define private public
#include "../src/x86_app.h"
#undef private
#define malloc MenuTestMalloc
#include "../src/user.cpp"
#undef malloc
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);
 for(unsigned profile=0;profile<2;++profile){X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"MENUFAMILY.EXE"));for(unsigned n=0;n<10000&&app.Status()!=X86::Application::Failed&&app.Status()!=X86::Application::Exited;++n)app.Pump(32);if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"menu state=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());printf("MENU GUEST PASS: calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());assert(!User::menu_state.head);assert(!User::menu_environment.text_bytes);
 auto failure=User::CreateMenu();assert(failure);
 for(unsigned stage=1;stage<=2;++stage){menu_alloc_calls=0;menu_fail_at=stage;assert(!User::AppendMenu(failure,0,7,"allocation failure",0));assert(!User::MenuItemCount(failure));menu_fail_at=0;assert(!User::menu_environment.text_bytes);}
 assert(User::AppendMenu(failure,0,7,"old",0));Win98Menu::Info change;change.mask=0x42;change.id=8;change.text=reinterpret_cast<Win98Menu::P>("new");menu_alloc_calls=0;menu_fail_at=1;assert(!User::MenuItemInfo(failure,0,1,change,1));menu_fail_at=0;assert(User::MenuItemID(failure,0)==8);char old[8]={};change.mask=0x40;change.text=reinterpret_cast<Win98Menu::P>(old);change.length=8;assert(User::MenuItemInfo(failure,0,1,change,0)&&!strcmp(old,"old"));
 for(unsigned n=1;n<11;++n)assert(User::AppendMenu(failure,0,20+n,"resize",0));menu_alloc_calls=0;menu_fail_at=1;assert(!User::RemoveMenu(failure,0,true,false)&&User::MenuItemCount(failure)==10);menu_fail_at=0;assert(User::DestroyMenu(failure)&&!User::menu_environment.text_bytes);
 // The table has to carry the shell plus several programs, so it takes at
 // least 512 menus before it reports exhaustion, and every handle it hands
 // out is a distinct 16-bit value other than zero.
 std::vector<unsigned> handles;for(unsigned h;(h=User::CreateMenu());handles.push_back(h))assert(h<65536);
 assert(handles.size()>=512);std::set<unsigned> seen(handles.begin(),handles.end());assert(seen.size()==handles.size());
 for(auto h:handles)assert(User::DestroyMenu(h));assert(!User::menu_state.head);
 // A slot only comes back after the table has been walked, and then with a
 // new generation, so a freed handle never names the menu that follows it.
 for(auto h:handles)assert(!User::IsMenu(h));
 std::vector<unsigned> again;for(unsigned n=0;n<handles.size();++n){unsigned h=User::CreateMenu();assert(h&&!seen.count(h));again.push_back(h);}
 for(auto h:again)assert(User::DestroyMenu(h));assert(!User::menu_state.head);
 auto root=User::CreateMenu(),sub=User::CreateMenu();assert(User::AppendMenu(sub,0,1,"shutdown owned text",0));assert(User::AppendMenu(root,0x10,sub,"shutdown subtree",sub));
 }
 assert(!User::menu_state.head&&!User::menu_environment.text_bytes);puts("MENU NATIVE PASS: capacity, subtree ownership, application teardown");}
