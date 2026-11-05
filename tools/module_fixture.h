// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#define API __declspec(dllimport)
#define W __stdcall
typedef unsigned U;
API U W GetModuleHandleA(const char *);
API U W GetModuleFileNameA(U, char *, U);
API U W GetProcAddress(U, const char *);
API U W LoadLibraryA(const char *);
API U W FreeLibrary(U);
API U W DisableThreadLibraryCalls(U);
API U W GetLastError(void);
API U W GetVersion(void);
API void W SetLastError(U);
API void W Sleep(U);
API void W ExitProcess(U);
API U W CreateThread(void *, U, U(W *)(void *), void *, U, U *);
API U W WaitForSingleObject(U, U);
API U W CloseHandle(U);
API U W GetExitCodeThread(U, U *);

API U W EnumResourceTypesA(U, U, U);
API U W FindResourceA(U, const char *, const char *);

API U W FindResourceExA(U, const char *, const char *, U);
API U W LoadResource(U, U);
API U W LockResource(U);
API U W SizeofResource(U, U);
API U W FreeResource(U);
API U W LoadStringA(U, U, char *, U);
typedef struct {
  U base, allocation, allocation_protection, size, state, protection, type;
} MemoryInfo;
API U W VirtualQuery(const void *, MemoryInfo *, U);
