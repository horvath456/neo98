// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef DEP_VALUE
__declspec(dllexport) unsigned __stdcall Dependency(void){return DEP_VALUE;}
#else
__declspec(dllimport) unsigned __stdcall Dependency(void);
static unsigned attached;
__declspec(dllexport) unsigned __stdcall Value(void){return Dependency();}
__declspec(dllexport) unsigned __stdcall Attached(void){return attached;}
#endif
unsigned __stdcall DllMain(unsigned instance,unsigned reason,void*reserved){
#ifndef DEP_VALUE
 if(reason==1)++attached;
#endif
 return 1;
}
