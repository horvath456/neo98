/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* initialize the installed providers before a display broadcast. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char*);
API U GetProcAddress(U,const char*);
API U FreeLibrary(U);
API U SendMessageA(U,U,U,U);
API void ExitProcess(U);
void mainCRTStartup(void) {
 U module=LoadLibraryA("TSP3216L.TSP"),lines=0,phones=0,options=0;
 if(!module)ExitProcess(1);
 U (__stdcall *enumerate)(U,U*,U*,U,U,U)=(void*)GetProcAddress(module,"TSPI_providerEnumDevices");
 U (__stdcall *initialize)(U,U,U,U,U,U,U,U*)=(void*)GetProcAddress(module,"TSPI_providerInit");
 if(!enumerate||!initialize)ExitProcess(2);
 if(enumerate(1,&lines,&phones,0,0,0))ExitProcess(3);
 U result=initialize(0x10004,1,0,0,lines,phones,0,&options);
 if(result)ExitProcess(result==0x80000048u?4:6);
 SendMessageA(0xffff,0x7e,16,800|(600<<16));
 U (__stdcall *shutdown)(U,U)=(void*)GetProcAddress(module,"TSPI_providerShutdown");
 if(!shutdown||shutdown(0x10004,1))ExitProcess(5);
 FreeLibrary(module);
 ExitProcess(0);
}
