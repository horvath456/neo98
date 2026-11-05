// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_shell_copy.h"
// SHLWAPI 5.0 Win98: ordinal 83/105 input object and ordinal 80 output
// object. Addresses and lower-provider boundaries are recorded in the package.
namespace Win98ShellModules {
struct Input { unsigned pointer=0,length=0,local=0; };
template<class E> void Release(E&e,Input&o){if(o.pointer!=o.local&&(o.pointer>>16))e.Free(o.pointer);o.pointer=0;}
template<class E> Input Convert(E&e,unsigned input,unsigned local){
 Input o;o.local=local;if(!(input>>16)){o.pointer=input;return o;}
 unsigned n=e.Wide(input,0xffffffff,local,519);o.length=n;
 if((int)n>0){e.Byte(local+n,0);if(!e.Byte(local+n-1))--o.length;o.pointer=local;return o;}
 unsigned size=e.Wide(input,0xffffffff,0,0)+1;o.pointer=e.Alloc(size,0x40);
 if(!o.pointer){e.Byte(local,0);o.pointer=local;return o;}
 n=e.Wide(input,0xffffffff,o.pointer,size);o.length=n;e.Byte(o.pointer+n,0);
 // Original reads pointer-1 if its second conversion fails. Such a malformed
 // conversion is an explicit native provider boundary, not a successful name.
 if(!n){e.ConversionFailure();return o;}
 if(!e.Byte(o.pointer+n-1))--o.length;return o;
}
template<class E> unsigned InputCall(E&e,unsigned ordinal,unsigned input,unsigned file,unsigned flags,unsigned local){
 if(e.IsNt())return e.NativeWide(ordinal,input,file,flags);
 Input o=Convert(e,input,local);unsigned result=0;
 if(ordinal==105&&(int)o.length>=260)e.Error(161);
 else result=e.Module(ordinal,o.pointer,file,flags);
 Release(e,o);return result;
}
// Conversion retries through a temporary complete wide string on
// ERROR_INSUFFICIENT_BUFFER, then calls original StrCpyNW's bounded copy rule.
template<class E> unsigned OutputConvert(E&e,unsigned input,unsigned output,unsigned count){
 if(!output||!count)return 0;e.Word(output,0);unsigned bytes=e.Length(input)+1;
 unsigned n=e.Multi(input,bytes,output,count);if(n||e.Error()!=122)return n;
 unsigned needed=e.Multi(input,bytes,0,0);if(!needed)return 0;
 unsigned temp=e.Alloc(needed*2,0);if(!temp)return 0;
 n=e.Multi(input,bytes,temp,needed);
 if(n){Win98ShellCopy::CopyNW(e,output,temp,count);n=count;}
 e.Free(temp);return n;
}
template<class E> unsigned FileName(E&e,unsigned module,unsigned output,unsigned count,unsigned local){
 if(e.IsNt())return e.NativeWide(80,module,output,count);
 unsigned pointer=0;if(!output)count=0;
 else {e.Byte(local,0);pointer=local;unsigned bytes=count*2;
  if(bytes>520){pointer=e.Alloc(bytes,0x40);if(pointer)e.Byte(pointer,0);else {e.Word(output,0);count=0;pointer=local;}}}
 e.Module(80,module,pointer,count*2);
 unsigned result=pointer?OutputConvert(e,pointer,output,count):0;
 if(pointer!=local&&(pointer>>16))e.Free(pointer);
 return result;
}
}
