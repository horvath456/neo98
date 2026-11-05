// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98ShellProcessReference {
struct State { unsigned object=0; };
// SHELL32 #176. Borrows the pointer; EAX retains it.
inline unsigned Set(State&s,unsigned object){s.object=object;return object;}
// SHGetInstanceExplorer/#284. Publish before AddRef.
template<class E> unsigned Get(E&e,const State&s,unsigned output){
 const unsigned object=s.object;e.Publish(output,object);
 if(!object)return 0x80004005u;
 e.AddRef(object);return 0;
}
}
