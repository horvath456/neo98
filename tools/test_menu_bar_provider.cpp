// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/user.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "../src/gdi_truetype.h"
// This menu fixture uses raster fonts only. Keep an unexpected outline-font
// dependency (edit margins) fatal rather than supplying fake metrics.
namespace GdiTrueType {
bool AbcWidths(const Font &, unsigned, unsigned, int *)
{
    std::fputs("Unexpected TrueType query in menu bar fixture\n", stderr);
    std::abort();
}
}
struct State {unsigned owner,bar,popup,expected_menu,expected_index,expected_loop,entered=0,exited=0,initialized=0,selected=0,command=0;} state;
unsigned send(void*,unsigned h,unsigned m,unsigned w,unsigned l){assert(h==state.owner);if(m==0x211){assert(w==state.expected_loop);++state.entered;}if(m==0x212){assert(w==state.expected_loop);++state.exited;}if(m==0x116){assert(w==state.expected_menu);++state.initialized;if(w==state.bar&&User::MenuItemCount(state.popup)==1)assert(User::InsertMenu(state.popup,0,0x400,912,"&Modify"));}if(m==0x117){assert(w==state.popup&&l==state.expected_index);++state.selected;}if(m==0x111)state.command=w;return 1;}
unsigned loop(void*){assert(User::PopupActive());User::MouseButtons(0,0,0);User::KeyboardRaw('M','m',true);User::KeyboardRaw('M',0,false);assert(!User::PopupActive());return User::PopupCommand();}
// A command from a popup of the bar is posted, not sent (USER,
// ): the program runs it after WM_EXITMENULOOP, so the test takes it
// from the owner's queue.
// The owner is a plain frame of its own class: a STATIC answers WM_NCHITTEST
// with HTTRANSPARENT, and since the input router asks the window under the
// point a press on a STATIC's menu bar goes to the window behind it.
int main(){assert(User::Initialize());assert(User::RegisterClass("MenuBarOwner",0,0,0));state.owner=User::CreateWindow("MenuBarOwner","Bar",0x10cf0000,20,20,400,250,0,0,0,1);assert(state.owner);state.bar=User::CreateMenu();state.popup=User::CreateMenu(true);unsigned first=User::CreateMenu(true);assert(User::AppendMenu(first,0,1,"One",0));assert(User::AppendMenu(state.popup,0,5,"Other",0));assert(User::AppendMenu(state.bar,0,0,"&File",first));assert(User::AppendMenu(state.bar,0,0,"&Edit",state.popup));assert(User::SetMenu(state.owner,state.bar));User::SetMenuCallbacks(nullptr,send,loop,[](void*){return 1u;});state.expected_menu=state.bar;state.expected_index=1;state.expected_loop=0;Gdi::RECT r{};assert(User::MenuItemRect(state.owner,state.bar,1,&r));User::Mouse((r.left+r.right)/2,(r.top+r.bottom)/2,true,false);assert(!state.command);{User::Message posted{};assert(User::PeekMessage(&posted,state.owner,0x111,0x111,true)&&posted.wparam==912&&!posted.lparam);state.command=posted.wparam;}assert(state.command==912&&state.entered==1&&state.exited==1&&state.initialized==1&&state.selected==1);state.expected_menu=state.popup;state.expected_index=0;state.expected_loop=1;state.command=0;assert(User::TrackPopup(state.popup,0x100,100,100,state.owner)==912);assert(!state.command&&state.entered==2&&state.exited==2&&state.initialized==2&&state.selected==2);assert(!User::PopupActive()&&User::LiveWindows()==1);User::DestroyWindow(state.owner);User::DestroyMenu(state.bar);User::Shutdown();assert(!User::LiveWindows()&&!Gdi::LiveObjects());puts("MENU BAR PROVIDER PASS: root/index callbacks, dynamic item insertion, mouse opening, mnemonic selection, direct popup unchanged");}
