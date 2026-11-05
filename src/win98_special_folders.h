// SPDX-License-Identifier: GPL-2.0-or-later
// SHELL32 special-folder decisions. PIDLs and filesystem/registry operations
// remain explicit providers; native path tokens are never exported as PIDLs.
#pragma once
#include "win98_special_folder_table.h"
namespace Win98SpecialFolders {
using U=unsigned;
struct State {U cached[30]={},epoch=0;int long_names=-1;};
// Exact IDs, no speculative masking of new flags.
inline int Index(U id){for(U i=0;i<30;++i)if(Table[i].id==id)return int(i);return -1;}
//  /. Keep seven namespace roots on invalidation.
template<class E>void Refresh(E&e,State&s){if(s.epoch==e.Epoch())return;e.Lock();for(U i=7;i<30;++i){if(s.cached[i]){e.ReleaseShared(s.cached[i]);s.cached[i]=0;}}s.epoch=e.Epoch();e.Unlock();}
//  /. Volume LFN capability selects original resources.
template<class E>void Default(E&e,State&s,U i,char*out){if(s.long_names==-1){e.Windows(out);s.long_names=e.LongNames(out)?1:0;}e.DefaultName(i,s.long_names!=0,out);}
// Cache path in Shell Folders, not User Shell Folders.
template<class E>void Write(E&e,U i,const char*path){U key=e.Open(Table[i].root,"Shell Folders");if(key){e.Set(key,Table[i].value,path);if(Table[i].id==5)e.PersonalChanged();e.Close(key);}}
// Registry fallback, repair/create and cache.
template<class E>U Build(E&e,State&s,U owner,U i,U create){
 U shared=0;if(!s.cached[i]){
  U temporary=0,id=Table[i].id;
  if(id==3||id==4||id==10)temporary=e.Virtual(id);
  else {
   bool fallback=true;char path[260]={},leaf[260]={};U key=e.Open(Table[i].root,"User Shell Folders");
   if(key){U type=0;if(!e.Query(key,Table[i].value,&type,path)&&type==1)fallback=false;e.Close(key);}
   for(U attempt=0;;++attempt){
    if(fallback){if(id==5||(!create&&((id>=22&&id<=25)||(id>=29&&id<=31))))return 0;e.Windows(path);Default(e,s,i,leaf);e.CombineDefault(path,leaf);}
    if(!e.Canonicalize(path))return 0;
    U attributes=e.Attributes(path);
    if(attributes!=~0u&&!(attributes&16)){
     // Original renames an obstructing file to.0,.1,... until successful.
     for(U suffix=0;;++suffix){if(e.MoveCollision(path,suffix))break;if(suffix>=63){e.Limit();return 0;}}
     attributes=e.Attributes(path);
    }
    if(create){
     if(owner&&attributes==~0u)attributes=e.OwnerMedia(owner,path,attributes);
     if(attributes==~0u){e.CreateDirectories(path);attributes=e.Attributes(path);
      if(attributes==~0u){if(fallback)break;fallback=true;if(attempt>=1){e.Limit();return 0;}continue;}
      if(id==8||id==19||id==27)e.SetHidden(path);
     }
    }
    if(attributes==~0u)return 0;
    Write(e,i,path);temporary=e.PathItem(path);break;
   }
  }
  shared=e.Share(temporary);e.FreeTemporary(temporary);
 }
 e.Lock();if(!s.cached[i])s.cached[i]=shared;else e.ReleaseShared(shared);e.Unlock();return s.cached[i];
}
// refresh then fast cache lookup; fCreate is ignored on a hit.
template<class E>U Get(E&e,State&s,U owner,U id,U create){int i=Index(id);if(i<0)return 0;Refresh(e,s);return s.cached[i]?s.cached[i]:Build(e,s,owner,U(i),create);}
// Always empty caller output before lookup.
template<class E>U Path(E&e,State&s,U owner,char*out,U id,U create){out[0]=0;U item=Get(e,s,owner,id,create);return item?e.PathFromItem(item,out):0;}
}
