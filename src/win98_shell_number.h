// SPDX-License-Identifier: GPL-2.0-or-later
// Original SHLWAPI StrToInt family.
#pragma once
namespace Win98ShellNumber{
using U=unsigned;
template<class E> U Simple(E&e,U p,U unit){U value=0;bool negative=e.Get(p,unit)=='-';if(negative)p+=unit;for(;;){U digit=e.Get(p,unit)-'0';if(digit>9)break;value=value*10+digit;p+=unit;}return negative?0-value:value;}
template<class E> U Extended(E&e,U p,U flags,U out){U value=0,c;bool negative=false;for(;;){c=e.Get(p,1);if(c!=' '&&c!='\n'&&c!='\t')break;p=e.Next(p);}c=e.Get(p,1);if(c=='+'||c=='-'){negative=c!='+';++p;}
 U next=e.Next(p),start=p;bool hex=false;if((flags&1)&&e.Get(p,1)=='0'){c=e.Get(next,1);if(c=='x'||c=='X'){hex=true;negative=false;p=e.Next(next);start=p;}}
 for(;;){c=e.Get(p,1);U digit=c-'0';if(hex){if(digit>9){if(c>='a'&&c<=0x7f)c=(c-32)&255;digit=U(int(static_cast<signed char>(c)))-'A'+10;if(digit<10||digit>15)break;}value=value*16+digit;}else{if(digit>9)break;value=value*10+digit;}p=e.Next(p);}
 e.Put(out,negative?0-value:value);return p!=start;
}
// The narrow conversion helper and wide wrapper share validation. Failed
// conversion after a successful local allocation does not enter cleanup.
template<class E> U Wide(E&e,U source,U flags,U out){
 U stack=e.Stack(),buffer=stack;if(!source)return Extended(e,source,flags,out);
 U count=e.Narrow(source,0,0),capacity=260;
 if(int(count)>260||!buffer){capacity=count+1;buffer=e.Allocate(capacity);}
 if(!buffer)return 0;if(int(e.Narrow(source,buffer,capacity))<=0)return 0;
 U result=Extended(e,buffer,flags,out);if(buffer!=stack)e.Free(buffer);return result;
}
struct Export{const char*name;U argc;};constexpr Export Exports[]={{"StrToIntA",1},{"StrToIntW",1},{"StrToIntExA",3},{"StrToIntExW",3}};
template<class E> U Call(E&e,U op,const U*a){if(op<2)return Simple(e,a[0],op?2:1);return op==2?Extended(e,a[0],a[1],a[2]):Wide(e,a[0],a[1],a[2]);}
}
