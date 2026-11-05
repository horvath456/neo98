// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98LibraryEx {
using U=unsigned;
struct State { U altered=0; };
// KERNEL32. Byte scan, including colon and either separator.
// DBCS recognition is absent in this original helper too.
template<class E> void Split(E&e,U text,U&leaf,U&stem){
 U at=text,extension=0;leaf=text;
 while(U c=e.Read8(at)){if(c=='.')extension=at;else if(c=='/'||c==':'||c=='\\'){leaf=at+1;extension=0;}++at;}
 stem=(extension?extension:at)-leaf;
}
// KERNEL32 LoadLibraryExA. hFile is never inspected;
// this version acts on flags 8 and 2 only, including when other bits are set.
// The lower loader owns successful altered-path cleanup before DLL callbacks.
template<class E> U Load(E&e,State&s,U input,U /*file*/,U flags){
 U name=input,ownership=2;const bool converted=e.ConvertMode();
 if(converted)name=e.Convert(name);
 if(!name){e.Error(87);return 0;}
 if(e.CompatibilityMode()&&!e.Compatibility(name,ownership,converted))return 0;
 if(flags&8){U leaf,stem;Split(e,name,leaf,stem);if(leaf!=name){s.altered=e.Duplicate(name);if(!s.altered)return 0;}else s.altered=0;}
 U result=flags&2?e.Datafile(name):e.Normal(name);
 if(!result&&s.altered){e.Free(s.altered);s.altered=0;}
 if(converted){e.FreeConverted(name);e.ClearConversionFlags();}
 else if(!ownership)e.Free(name);
 return result;
}
template<class E> void BeforeInitialize(E&e,State&s){if(s.altered){e.Free(s.altered);s.altered=0;}}
// 32-bit process branch of the search-context selector.
template<class T> T SearchSource(T altered,T ordinary){return altered?altered:ordinary;}
}
