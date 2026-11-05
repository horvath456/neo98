// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98ShellControl {
using U=unsigned;
struct State {U cache=0,name_atom=0,flags_atom=0;};
template<class E>void Zero(E&e,U p,U n){for(U i=0;i<n;++i)e.W8(p+i,0);}
template<class E>void Copy(E&e,U to,U from,U n){for(U i=0;i<n;++i)e.W8(to+i,e.R8(from+i));}
// Original temporarily replaces quote/delimiter bytes.
template<class E>U Token(E&e,U out,U source,U capacity,U file){
 if(!source){if(out)e.W8(out,0);return 0;}while(e.R8(source)==' ')++source;
 U quote=0,delimiter=source;
 if(e.R8(source)=='"'){++source;quote=source;while(e.R8(quote)&&e.R8(quote)!='"')++quote;
  if(e.R8(quote)){e.W8(quote,0);delimiter=quote+1;}else quote=0;}
 delimiter=file?e.SpanDelimiter(delimiter):e.FindComma(delimiter);
 if(delimiter)e.W8(delimiter,0);
 if(out){e.CopyString(out,source,capacity);e.W8(out+capacity-1,0);}
 if(delimiter)e.W8(delimiter,',');if(quote)e.W8(quote,'"');return delimiter?delimiter+1:0;
}
// accelerator and East Asian caption marker removal.
template<class E>void Normalize(E&e,U text){U source=text,out=text;
 do{
  if(e.R8(source)=='('&&e.R8(source+1)=='&'&&e.R8(source+2)&&e.R8(source+3)==')')source+=4;
  if(e.R8(source)==0x1e){++source;if(e.R8(source)){out=e.Previous(text,out);source=e.Next(source);
   if(e.R8(source)){if(e.R8(source)==0x1f){++source;if(e.R8(source))source=e.Next(source);}source=e.Next(source);}}}
  U c=e.R8(source);if(c!='&')e.W8(out++,c);if(e.Lead(c)){e.W8(out++,e.R8(++source));}
 }while(e.R8(source++));
}
// Public RunDLL uses legacy=0.
template<class E>void Parse(E&e,U record,U command,U legacy){U next=Token(e,record+4,command,260,1);
 if(legacy){typename E::Scratch temp(e,8);next=Token(e,temp.address,next,8,0);e.W32(record,e.Integer(temp.address));}else e.W32(record,0);
 e.W32(record+0x128,Token(e,record+0x108,next,32,0));Normalize(e,record+0x108);
}
//  normal path. SEH recovery and Win16 thunk are providers.
template<class E>U Invoke(E&e,U module,U window,U message,U first,U second){return e.Applet(e.R32(module+4),e.R32(module+8),e.R32(module+0x118),window,message,first,second);}
// complete ANSI/Wide inquiry decision tree.
template<class E>U Inquire(E&e,U module,U item){typename E::Scratch stack(e,0x1e8);U modern=stack.address,old=modern+0x1d4;
 e.W32(old,0);U icon=0;Invoke(e,module,0,3,e.R32(item),old);
 if(e.R32(old)&&!e.R32(module+4)){icon=e.LoadIcon(e.R32(module+8),e.R16(old));if(!icon)e.W32(old,~0u);}
 e.W32(item+8,e.R32(old));Zero(e,modern,0x1d4);Invoke(e,module,0,8,e.R32(item),modern);
 U size=e.R32(modern);if(size==0x1d4)e.WideInquiry(modern);
 else if(size!=0xf4){e.W32(modern+16,e.LoadIcon(e.R32(module+8),e.R16(old)));e.LoadString(e.R32(module+8),e.R32(old+4),modern+20,32);e.LoadString(e.R32(module+8),e.R32(old+8),modern+52,64);e.W8(modern+116,0);e.W32(modern+8,0);e.W32(modern+12,e.R32(old+12));}
 e.W32(item+4,e.R32(modern+16));if(icon)e.DestroyIcon(icon);
 U result=e.SetString(item+12,modern+20)&&e.SetString(item+16,modern+52)&&e.SetString(item+20,modern+116);
 e.W32(item+24,e.R32(modern+12));e.W32(item+28,e.R32(modern+8));return result;
}
// resource release precedes CPL_STOP.
template<class E>void DestroyItem(E&e,U module,U item){U icon=e.R32(item+4);if(icon){e.DestroyIcon(icon);e.W32(item+4,0);}e.SetString(item+12,0);e.SetString(item+16,0);e.SetString(item+20,0);Invoke(e,module,0,6,e.R32(item),e.R32(item+24));}
// DSA allocation and partial-failure cleanup.
template<class E>U Initialize(E&e,U module){U proc=e.Procedure(e.R32(module+4),e.R32(module+8));e.W32(module+0x118,proc);
 if(!proc||!Invoke(e,module,0,1,0,0)){e.W32(module+0x118,0);return 0;}
 U count=Invoke(e,module,0,2,0,0);if(int(count)<=0)return 0;
 U list=e.ArrayCreate(32,count);e.W32(module+0x11c,list);if(!list)return 0;
 typename E::Scratch local(e,32);for(U i=0;i<count;++i){Zero(e,local.address,32);e.W32(local.address,i);
  if(Inquire(e,module,local.address)){Normalize(e,e.R32(local.address+12));if(int(e.ArrayInsert(list,0x7fffffff,local.address))>=0)continue;}
  DestroyItem(e,module,local.address);return 0;
 }return 1;
}
// All applets STOP, then EXIT, then module release.
template<class E>void Destroy(E&e,U module){if(e.R32(module+8)){if(e.R32(module+0x118)){U list=e.R32(module+0x11c);if(list){U n=e.R32(list);for(U i=0;int(i)<int(n);++i)DestroyItem(e,module,e.ArrayGet(list,i));e.ArrayClear(list);e.ArrayDestroy(list);e.W32(module+0x11c,0);}Invoke(e,module,0,7,0,0);e.W32(module+0x118,0);}e.Unload(e.R32(module+4),e.R32(module+8));e.W32(module+8,0);}e.W32(module+12,~0u);if(e.R32(module+16)){e.Close(e.R32(module+16));e.W32(module+16,0);}}
// cache lookup and insertion use the existing DSA service.
template<class E>U Find(E&e,State&s,U key){if(s.cache)for(U i=e.R32(s.cache);i--;){U m=e.ArrayGet(s.cache,i);if(e.R32(m+8)==e.R32(key+4)&&e.R32(m+4)==e.R32(key)&&e.R32(m+12)==e.R32(key+8))return i;}return ~0u;}
template<class E>U Insert(E&e,State&s,U record){if(!s.cache)s.cache=e.ArrayCreate(0x120,4);return s.cache?e.ArrayInsert(s.cache,0x7fffffff,record):~0u;}
// reference count and last-reference destruction.
template<class E>U ReleaseIndex(E&e,State&s,U index){U m=e.ArrayGet(s.cache,index);if(!m)return ~0u;U refs=e.R32(m)-1;e.W32(m,refs);if(refs)return refs;Destroy(e,m);e.ArrayDelete(s.cache,index);if(!e.R32(s.cache)){e.ArrayDestroy(s.cache);s.cache=0;}return 0;}
template<class E>void Release(E&e,State&s,U module){U index=Find(e,s,module+4);if(int(index)>=0)ReleaseIndex(e,s,index);}
// For the PE32 branch, providers handle Win16-first loading and process references.
template<class E>U Load(E&e,State&s,U path){typename E::Scratch scratch(e,0x130);U record=scratch.address;U key=record+0x120;
 U mode=1,module=e.LoadLegacy(path),pid=e.ProcessId(),process=e.OpenSelf(pid);
 if(module<32){if(module!=21)return ~0u;module=e.LoadLibrary(path);if(!module){e.Close(process);return ~0u;}mode=0;}
 e.W32(key,mode);e.W32(key+4,module);e.W32(key+8,pid);e.W32(key+12,process);
 U index=Find(e,s,key);if(int(index)>=0){U found=e.ArrayGet(s.cache,index);e.W32(found,e.R32(found)+1);e.Unload(mode,module);e.Close(process);return index;}
 Zero(e,record,0x120);e.W32(record,1);Copy(e,record+4,key,16);e.ModuleName(mode,module,record+20,260);
 if(Initialize(e,record))index=Insert(e,s,record);if(int(index)<0)Destroy(e,record);return index;
}
// Empty name defaults to zero, invalid @ falls through to name.
template<class E>U Select(E&e,State&s,U module_out,U index_out,U parsed){U index=Load(e,s,parsed+4);if(int(index)<0)return 0;U module=e.ArrayGet(s.cache,index),choice=0,name=parsed+0x108;
 if(e.R8(name)){U count=e.R32(e.R32(module+0x11c));bool numeric=false;if(e.R8(name)=='@'){choice=e.Integer(name+1);numeric=int(choice)>=0&&int(choice)<int(count);}
  if(!numeric){if(!e.Compare(e.SetupText(),e.R32(parsed+0x128)))Invoke(e,module,0,200,0,0);
   typename E::Scratch local(e,32);for(choice=0;int(choice)<int(count);++choice){U item=e.ArrayGet(e.R32(module+0x11c),choice);e.CopyString(local.address,e.R32(item+12),~0u);Normalize(e,local.address);if(!e.Compare(name,local.address))break;}
   if(int(choice)>=int(count)){Release(e,s,module);return 0;}
  }
 }
 e.W32(module_out,module);e.W32(index_out,choice);return 1;
}
//  atom ownership for one invocation.
template<class E>U PrepareAtoms(E&e,U atoms,U parsed,U owner){e.W16(atoms+2,0);e.W32(atoms+8,0);e.W32(atoms+4,owner);U a=e.AddAtom(parsed+4)&65535;e.W16(atoms,a);if(!a)return 0;if(e.R8(parsed+0x108)){a=e.AddAtom(parsed+0x108)&65535;e.W16(atoms+2,a);if(!a)return 0;}else e.W32(atoms+8,1);return 1;}
template<class E>void FreeAtoms(E&e,U atoms){for(U i=0;i<4;i+=2)if(e.R16(atoms+i)){e.DeleteAtom(e.R16(atoms+i));e.W16(atoms+i,0);}e.W32(atoms+4,0);e.W32(atoms+8,0);}
// matched owner class and shared USER property checks.
template<class E>bool Match(E&e,State&s,U hwnd,U atoms,U klass){typename E::Scratch local(e,32);e.ClassName(hwnd,local.address,32);if(e.CompareExact(local.address,klass)||!e.R16(atoms))return false;U file=e.GetProp(hwnd,s.name_atom)&65535;if(!file||file!=e.R16(atoms))return false;U item=e.GetProp(hwnd,file)&65535;if(item&&item==e.R16(atoms+2))return true;return(e.R32(atoms+8)&1)&&(e.GetProp(hwnd,s.flags_atom)&1);}
template<class E>bool GlobalAtoms(E&e,State&s){if(!s.name_atom){s.name_atom=e.AddAtom(e.NameText())&65535;s.flags_atom=e.AddAtom(e.FlagsText())&65535;}return s.name_atom&&s.flags_atom;}
// full command execution decisions with desktop providers.
template<class E>U Run(E&e,State&s,U owner,U instance,U command,U show,U execute){typename E::Scratch local(e,0x178);U parsed=local.address,atoms=parsed+0x164,modout=parsed+0x160,indexout=parsed+0x174,caption=parsed+0x12c;
 Parse(e,parsed,command,0);if(!e.R8(parsed+4)){e.OpenPanel(owner,show,3);return 1;}
 if(!e.Resolve(parsed+4))return 0;if(!PrepareAtoms(e,atoms,parsed,owner))return 0;
 U previous=e.FindExisting(s,owner,atoms),result=0;
 if(previous){previous=e.LastPopup(previous);if(previous&&e.IsWindow(previous)){e.Foreground(previous);result=1;goto done;}}
 if(!execute)goto done;
 if(Select(e,s,modout,indexout,parsed)){
  U module=e.R32(modout),choice=e.R32(indexout),item=e.ArrayGet(e.R32(module+0x11c),choice);
  e.CopyString(caption,e.R32(item+12),~0u);Normalize(e,caption);
  if(!e.R16(atoms+2)){e.W32(atoms+8,e.R32(atoms+8)|1);e.W16(atoms+2,e.AddAtom(caption));}
  if(e.R16(atoms+2)&&GlobalAtoms(e,s)){
   if(!choice)e.W32(atoms+8,e.R32(atoms+8)|1);
   if(e.SetProp(owner,s.name_atom,e.R16(atoms))&&e.SetProp(owner,e.R16(atoms),e.R16(atoms+2))){
    if(e.R32(atoms+8))e.SetProp(owner,s.flags_atom,e.R32(atoms+8));
    if(owner)e.NotifyOwner(owner,e.R32(item+4),caption);
    U tail=e.R32(parsed+0x128);if(tail)result=Invoke(e,module,owner,9,choice,tail);
    if(!result)Invoke(e,module,owner,5,choice,e.R32(item+24));
    e.RemoveProp(owner,e.R16(atoms));result=1;
   }
  }
  Release(e,s,module);
 }
 done:FreeAtoms(e,atoms);return result;
}
}
