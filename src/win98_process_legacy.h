// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_path_family.h"
namespace Win98ProcessLegacy {
using U=unsigned;
// Preserve the memory-probe order; the adapter handles SEH.
template<class E> bool ProbeString(E&e,U text){U c,left=~0u;do{if(!e.ProbeRead(text++,c))return false;}while(--left&&c);return true;}
template<class E> bool ProbeLoad(E&e,U name,U block){U c;return ProbeString(e,name)&&e.ProbeRead(block,c)&&e.ProbeRead(block+15,c);}
template<class E> bool ProbeStartup(E&e,U p){return e.ProbeWrite(p)&&e.ProbeWrite(p+67);}
// The source is the current process's actual startup record.
template<class E> void Startup(E&e,U out){e.LockStartup();e.Copy(out,e.Startup(),68);e.UnlockStartup();}
template<class E> void Zero(E&e,U p,U bytes){for(U i=0;i<bytes;++i)e.Write8(p+i,0);}
template<class E> U Length(E&e,U p){U n=0;while(e.Read8(p+n))++n;return n;}
template<class E> void Append(E&e,U to,U from){to+=Length(e,to);U c;do{c=e.Read8(from++);e.Write8(to++,c);}while(c);}
// excluding SEH scaffolding around the optional Win16 TDB.
template<class E> U Finish(E&e,U kind,U created,U info){
 if(!created){U error=e.LastError();switch(error){case 2:case 3:case 4:case 5:case 8:case 11:return error;case 14:return 8;default:return 0;}}
 U tid=e.Read32(info+12),task=e.ThreadTask(tid)&65535;
 if(!kind){U process=e.ReferenceProcess(e.Read32(info),0x80000020,0);if(process){if(e.ProcessFlags(process)&8)kind=1;e.DereferenceProcess(process);}}
 if(kind!=1)e.WaitThread(tid);
 U result=task;
 if(e.SelectorExists(task)){U p=e.SelectorAddress(task<<16);if(e.Read16(p+0xfa)==0x4454)result=e.Read16(p+0x1c);}
 if(result<=32)result=42;
 e.Close(e.Read32(info+4));e.Close(e.Read32(info));return result;
}
// Finish is shared with the other legacy launch callers.
template<class E> U WinExec(E&e,Win98PathFamily::State&s,U command,U show){
 bool converted=Win98PathFamily::FileApiMode(s,0);if(converted)command=e.ConvertInput(command,0);
 typename E::Scratch local(e,344);U directory=local.address,si=directory+260,pi=si+68;
 e.GetDirectory(260,directory);e.SetDirectory(directory);Zero(e,si,68);e.Write32(si,68);e.Write32(si+44,1);e.Write16(si+48,show);
 const U args[]={0,command,0,0,0,0,0,directory,si,pi};U result=e.Create(args);
 if(converted){e.FreeInput(command,0);Win98PathFamily::ClearMode(s);}
 return e.Finish(0,result,pi);
}
// The one-byte command length is SIGNED for allocation;
// concatenation itself still reads through NUL. Native providers must bound it.
template<class E> U LoadModule(E&e,U name,U block){
 U buffer=0,command=0,result=0,show=e.Read32(block+8),tail=0;
 if(show&&e.Read16(show)==2&&(tail=e.Read32(block+4))){
  buffer=e.Allocate(260);
  if(buffer){U found=e.Search(name,buffer,260);
   if(found&&found<260){
    unsigned char byte=e.Read8(tail);bool ready=true;
    if(byte){U length=Length(e,buffer)+U(int(static_cast<signed char>(byte)))+2;command=e.Allocate(length);if(!command)ready=false;
     else{e.Write8(command,0);Append(e,command,name);U end=command+Length(e,command);e.Write8(end,' ');e.Write8(end+1,0);Append(e,command,tail+1);}}
    if(ready){typename E::Scratch local(e,84);U si=local.address,pi=si+68;Zero(e,si,68);e.Write32(si,68);e.Write32(si+44,e.Read32(si+44)|1);e.Write16(si+48,e.Read16(show+2));
     const U args[]={buffer,command,0,0,0,0,e.Read32(block),0,si,pi};
     if(e.Create(args)){e.Close(e.Read32(pi));e.Close(e.Read32(pi+4));result=32;}
     else{result=e.LastError();if(result>=32)result=0;}
    }
   }else{
    U prefix=e.Read16(name);
    if(prefix==0x5c5c||prefix==0x2f2f||e.Read16(name+1)==0x5c3a){U file=e.Open(name);if(file!=~0u){result=2;e.Close(file);}else result=e.LastError();}
    else result=2;
   }
  }
 }
 if(command)e.Free(command);if(buffer)e.Free(buffer);return result;
}
}
