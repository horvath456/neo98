// SPDX-License-Identifier: GPL-2.0-or-later
// The registry value a Windows 98 display driver's INF writes for its OpenGL
// driver: under HKLM\Software\Microsoft\Windows\CurrentVersion\OpenGLDrivers
// the name OPENGL_GETINFO hands out, with the DLL as REG_SZ. OPENGL32 opens
// the key and loads the DLL it names. src/x86_opengl.cpp,
//
#pragma once
namespace Win98OpenGLRegistration {
struct Value { unsigned root;const char*key;const char*name;unsigned type;const char*value; };
inline constexpr Value Values[]={
 {2,"Software\\Microsoft\\Windows\\CurrentVersion\\OpenGLDrivers","NEO98",1,"NEO98GL32.DLL"},
};
template<class Store> unsigned Install(Store&store){
 for(const auto&v:Values){unsigned key=0;bool created=false;unsigned error=store.Open(store.roots[v.root],v.key,true,key,created);if(error)return error;unsigned bytes=0;while(v.value[bytes])++bytes;error=store.Set(key,v.name,v.type,v.value,bytes+1);store.Release(key);if(error)return error;}return 0;
}
}
