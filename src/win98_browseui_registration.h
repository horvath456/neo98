// SPDX-License-Identifier: GPL-2.0-or-later
// Generated from original BROWSEUI REGINST by import_browseui_registration.py.
#pragma once
namespace Win98BrowseUiRegistration {
struct Value { unsigned root;const char*key;const char*name;unsigned type;const char*value; };
inline constexpr Value Values[]={
 {0,"CLSID\\{ECD4FC4D-521C-11D0-B792-00A0C90312E1}","",1,"Shell Rebar BandSite"},
 {0,"CLSID\\{ECD4FC4D-521C-11D0-B792-00A0C90312E1}\\InProcServer32","",2,"C:\\WINDOWS\\SYSTEM\\BROWSEUI.DLL"},
 {0,"CLSID\\{ECD4FC4D-521C-11D0-B792-00A0C90312E1}\\InProcServer32","ThreadingModel",1,"Apartment"},
 {2,"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved","{ECD4FC4D-521C-11D0-B792-00A0C90312E1}",1,"Shell Rebar BandSite"},
};
// Native installation provider for these four original data rows.
template<class Store> unsigned Install(Store&store){
 for(const auto&v:Values){unsigned key=0;bool created=false;unsigned error=store.Open(store.roots[v.root],v.key,true,key,created);if(error)return error;unsigned bytes=0;while(v.value[bytes])++bytes;error=store.Set(key,v.name,v.type,v.value,bytes+1);if(error)return error;}return 0;
}
}
