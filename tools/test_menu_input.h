// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
namespace TestMenuInput {
inline void Click(X86::Application&app,const Gdi::RECT&r){
 assert(r.right>r.left&&r.bottom>r.top);
 int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
 app.Mouse(x,y,true,false);app.Mouse(x,y,false,true);
}
// Query only for coordinates; opening and selection still enter the native input queue.
inline void Command(X86::Application&app,unsigned command,void(*wait)(X86::Application&),unsigned bar_hint=~0u){
 unsigned owner=app.MainWindow(),bar=User::GetMenu(owner),submenu=0,bar_index=0;
 if(bar_hint!=~0u){submenu=User::GetSubMenu(bar,bar_hint);bar_index=bar_hint;}
 for(unsigned n=0;n<User::MenuItemCount(bar)&&!submenu;++n){unsigned child=User::GetSubMenu(bar,n);
  if(child)for(unsigned k=0;k<User::MenuItemCount(child);++k)if(User::MenuItemID(child,k)==command){submenu=child;bar_index=n;break;}}
 assert(submenu);Gdi::RECT rect{};assert(User::MenuItemRect(owner,bar,bar_index,&rect));Click(app,rect);wait(app);
 assert(User::PopupActive());
 // Initialization callbacks may have inserted or removed entries.
 bool found=false;for(unsigned k=0;k<User::MenuItemCount(submenu);++k)if(User::MenuItemID(submenu,k)==command){assert(User::MenuItemRect(owner,submenu,k,&rect));found=true;break;}
 if(!found){for(unsigned k=0;k<User::MenuItemCount(submenu);++k)fprintf(stderr,"MENU index=%u id=%u\n",k,User::MenuItemID(submenu,k));}assert(found);assert(User::MenuItemFromPoint(owner,submenu,(rect.left+rect.right)/2,(rect.top+rect.bottom)/2)>=0);Click(app,rect);
}
}
