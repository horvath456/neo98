// SPDX-License-Identifier: GPL-2.0-or-later
// Test stand-in for WSOCK32.DLL: CreateSocketHandle
// resolves recv, Arecv, send, Asend and closesockinfo by name before it
// creates a socket object. Nothing here is called; the addresses only have
// to exist. Exported cdecl so the names stay undecorated.
__declspec(dllexport) int recv(unsigned s, void *buffer, int length, int flags) { return -1; }
__declspec(dllexport) int Arecv(unsigned s, void *buffer, int length, int flags, void *overlapped) { return -1; }
__declspec(dllexport) int send(unsigned s, const void *buffer, int length, int flags) { return -1; }
__declspec(dllexport) int Asend(unsigned s, const void *buffer, int length, int flags, void *overlapped) { return -1; }
__declspec(dllexport) int closesockinfo(unsigned context) { return 0; }
unsigned __stdcall DllMain(unsigned instance, unsigned reason, void *reserved) { return 1; }
