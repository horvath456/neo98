// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98PathProbes {
using U=unsigned;
struct Resource {U type=0,display=0;};
// ANSI and Unicode path-existence probes.
// Count separators through CharNext for ANSI, UTF-16 units for W. No nonempty
// server/share-component validation exists in these original predicates.
template<class E,class C>U Unc(E&e,const C*p,U separators){if(!p||p[0]!='\\'||p[1]!='\\')return 0;U n=0;while(*p){if(*p=='\\')++n;p=e.Next(p);}return n==separators;}
// SHLWAPI PathFileExistsA70BEB9A9 /W70BEB919, including NULL early return.
template<class E,class C>U Exists(E&e,const C*p){if(!p)return 0;U previous=e.SetMode(1);U result=e.Attributes(p)!=~0u;e.SetMode(previous);return result;}
// SHELL32 #45 /80CD..812C +. Network success or
// ERROR_MORE_DATA establishes existence while leaving attributes at -1.
template<class E,class C>U ShellExists(E&e,const C*p,U&attributes){attributes=~0u;U previous=e.SetMode(1),result=0;
 if(Unc(e,p,2)||Unc(e,p,3)){Resource info;U status=e.Network(p,info,true);if(!status||status==234)result=1;}
 if(!result){attributes=e.Attributes(p);result=attributes!=~0u;}e.SetMode(previous);return result;
}
// PathIsDirectory handles ANSI and Unicode paths.
// Local BOOL retains the directory bit16; network share success returns1.
template<class E,class C>U Directory(E&e,const C*p){if(!p||Unc(e,p,2))return 0;
 if(Unc(e,p,3)){Resource info;U status=e.Network(p,info,false);if(!status&&info.display)return info.display==3&&(info.type==0||info.type==1);}
 U attributes=e.Attributes(p);return attributes==~0u?0:attributes&16;
}
// Apply the Windows 98 path attribute checks.
template<class C>bool Dot(const C*p){return p[0]=='.'&&(!p[1]||(p[1]=='.'&&!p[2]));}
// PathIsDirectoryEmptyA70BF85D8..8683 /W70BFF18F..F239. Find failure is
// false; only dot entries followed by end-of-enumeration count as empty.
template<class E,class C>U Empty(E&e,const C*p){if(!p||!Directory(e,p))return 0;C pattern[260]={},name[260]={};if(!e.Pattern(p,pattern))return 0;U find=e.First(pattern,name);if(find==~0u)return 0;U result=0;for(;;){if(!Dot(name))break;if(!e.More(find,name)){result=1;break;}}e.Close(find);return result;}
}
