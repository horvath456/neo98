// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string.h>
namespace Win98ProcessCommand {
using U=unsigned;
// KERNEL32 CreateProcessA. The initial internal creation
// attempt has already run. E::Create preserves all other launch arguments.
// No process success is implemented here: only the provider can create one.
template<class E> U Retry(E&e,const char*application,const char*command,U result){
 if(result||application)return result;
 const U saved=e.LastError(),length=strlen(command);char*temporary=e.Allocate(length+3);
 if(temporary){const char*at=command;
  for(;;){const char c=*at;if(c==' '||c=='\t'||!c){U prefix=at-command;temporary[0]='"';memcpy(temporary+1,command,prefix);temporary[prefix+1]='"';strcpy(temporary+prefix+2,at);
    result=e.Create(nullptr,temporary);if(result||!c)break;
    do{++at;}while(*at==' '||*at=='\t');--at;
   }++at;
  }
  e.Free(temporary);
 }
 if(!result)e.Error(saved);return result;
}
// Original internal process initialization.
// Returned pointers are still in the parent's address space. The launcher
// provider must copy the complete environment before publishing the child.
inline U Environment(U supplied,U parent){return supplied?supplied:parent;}
}
