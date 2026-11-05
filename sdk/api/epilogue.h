// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Horvath
/* Macros of the Win32 headers that are not functions. */
#define MAKEINTRESOURCEA(i) ((LPSTR)(ULONG_PTR)(WORD)(i))
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l) ((WORD)(((DWORD_PTR)(l) >> 16) & 0xffff))
#define LOBYTE(w) ((BYTE)((DWORD_PTR)(w) & 0xff))
#define HIBYTE(w) ((BYTE)(((DWORD_PTR)(w) >> 8) & 0xff))
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKELRESULT(l, h) ((LRESULT)(DWORD)MAKELONG(l, h))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)(((WORD)(c)) >> 8))
#define GetBValue(c) ((BYTE)((c) >> 16))
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))
#define HWND_TOP ((HWND)0)
#define HWND_BOTTOM ((HWND)1)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)
#define HWND_DESKTOP ((HWND)0)
#define HWND_BROADCAST ((HWND)0xffff)
#define IDC_ARROW MAKEINTRESOURCE(32512)
#define IDC_IBEAM MAKEINTRESOURCE(32513)
#define IDC_WAIT MAKEINTRESOURCE(32514)
#define IDC_CROSS MAKEINTRESOURCE(32515)
#define IDI_APPLICATION MAKEINTRESOURCE(32512)
#define IDI_HAND MAKEINTRESOURCE(32513)
#define IDI_QUESTION MAKEINTRESOURCE(32514)
#define IDI_EXCLAMATION MAKEINTRESOURCE(32515)
#define IDI_ASTERISK MAKEINTRESOURCE(32516)
#define RT_MENU MAKEINTRESOURCE(4)
#define RT_DIALOG MAKEINTRESOURCE(5)
#define RT_STRING MAKEINTRESOURCE(6)
#define RT_RCDATA MAKEINTRESOURCE(10)
#define RT_ICON MAKEINTRESOURCE(3)
#define RT_GROUP_ICON MAKEINTRESOURCE(14)
#define RT_VERSION MAKEINTRESOURCE(16)
#define RT_MANIFEST MAKEINTRESOURCE(24)
/* The headers of an image under the names without a width. */
typedef IMAGE_NT_HEADERS64 IMAGE_NT_HEADERS, *PIMAGE_NT_HEADERS;
typedef IMAGE_OPTIONAL_HEADER64 IMAGE_OPTIONAL_HEADER, *PIMAGE_OPTIONAL_HEADER;
typedef IMAGE_DOS_HEADER *PIMAGE_DOS_HEADER;
typedef IMAGE_SECTION_HEADER *PIMAGE_SECTION_HEADER;
#define IMAGE_FIRST_SECTION(nt) ((PIMAGE_SECTION_HEADER)((ULONG_PTR)(nt) + offsetof(IMAGE_NT_HEADERS, OptionalHeader) + \
                                 ((nt))->FileHeader.SizeOfOptionalHeader))
#define TEXT(s) s
#define CreateWindowA(c, n, s, x, y, w, h, p, m, i, l) CreateWindowExA(0, c, n, s, x, y, w, h, p, m, i, l)
#define CreateWindow CreateWindowA
#define DialogBoxA(i, t, p, f) DialogBoxParamA(i, t, p, f, 0)
#define DialogBox DialogBoxA
#define CreateDialogA(i, t, p, f) CreateDialogParamA(i, t, p, f, 0)
#define CreateDialog CreateDialogA
#define ZeroMemory(p, n) memset((p), 0, (n))
#define CopyMemory(d, s, n) memcpy((d), (s), (n))
#define UNREFERENCED_PARAMETER(p) (void)(p)

/* The interlocked operations: under Windows on ARM64 the compiler does them
   in place, they are no exports of KERNEL32. Full barriers, as there. */
#ifndef NEO98_INTERLOCKED
#define NEO98_INTERLOCKED
static __inline__ LONG InterlockedIncrement(LONG volatile *value) { return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST); }
static __inline__ LONG InterlockedDecrement(LONG volatile *value) { return __atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST); }
static __inline__ LONG InterlockedExchange(LONG volatile *target, LONG value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
static __inline__ LONG InterlockedExchangeAdd(LONG volatile *target, LONG value)
{
    return __atomic_fetch_add(target, value, __ATOMIC_SEQ_CST);
}
static __inline__ LONG InterlockedCompareExchange(LONG volatile *target, LONG exchange, LONG comparand)
{
    __atomic_compare_exchange_n(target, &comparand, exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}
static __inline__ PVOID InterlockedExchangePointer(PVOID volatile *target, PVOID value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
static __inline__ PVOID InterlockedCompareExchangePointer(PVOID volatile *target, PVOID exchange, PVOID comparand)
{
    __atomic_compare_exchange_n(target, &comparand, exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}
#endif

/* The compiler calls these by itself; sdk/start supplies them. */
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);

/* A W function the system below does not have and the helper library of the
   SDK (sdk/lib/widenames.c) answers through its A function: the C++ runtime
   asks for the module of KERNEL32 by a wide name. */
HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName);

/* The entry of a program of the Windows subsystem; sdk/start calls it. A
   console program has main, which the headers of Windows do not declare. */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd);
