/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The few things of COM the DirectX headers of the NEO98 SDK need: GUID,
   HRESULT, IUnknown and the macros an interface is declared with. Own file of
   the SDK; the names and values are those of the
   Windows headers, checked against mingw-w64 by tools/test_arm64_dx_headers.py. */
#ifndef NEO98_OBJBASE_H
#define NEO98_OBJBASE_H

#include <windows.h>
#include <string.h>

#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID
{
    unsigned long Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID;
#endif
typedef GUID IID;
typedef GUID CLSID;
typedef GUID *LPGUID;
typedef const GUID *LPCGUID;
typedef IID *LPIID;
typedef CLSID *LPCLSID;

#ifdef __cplusplus
#define EXTERN_C extern "C"
#define REFGUID const GUID &
#define REFIID const IID &
#define REFCLSID const IID &
inline int IsEqualGUID(REFGUID a, REFGUID b) { return !memcmp(&a, &b, sizeof(GUID)); }
inline bool operator==(REFGUID a, REFGUID b) { return !memcmp(&a, &b, sizeof(GUID)); }
inline bool operator!=(REFGUID a, REFGUID b) { return !(a == b); }
#else
#define EXTERN_C extern
#define REFGUID const GUID *
#define REFIID const IID *
#define REFCLSID const IID *
#define IsEqualGUID(a, b) (!memcmp((a), (b), sizeof(GUID)))
#endif
#define IsEqualIID(a, b) IsEqualGUID(a, b)
#define IsEqualCLSID(a, b) IsEqualGUID(a, b)

/* DEFINE_GUID declares a GUID; with INITGUID defined before the header it
   defines it. The GUIDs of DirectX are in dxguid.lib of the SDK. */
#ifdef INITGUID
#ifdef __cplusplus
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    EXTERN_C const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
#else
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
#endif
#else
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) EXTERN_C const GUID name
#endif

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define MAKE_HRESULT(sev, fac, code) \
    ((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))
#define HRESULT_CODE(hr) ((hr) & 0xFFFF)
#define HRESULT_FACILITY(hr) (((hr) >> 16) & 0x1fff)
#endif
#ifndef S_OK
#define S_OK ((HRESULT)0x00000000L)
#define S_FALSE ((HRESULT)0x00000001L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_ABORT ((HRESULT)0x80004004L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#define E_ACCESSDENIED ((HRESULT)0x80070005L)
#define E_HANDLE ((HRESULT)0x80070006L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110L)
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)
#define CO_E_NOTINITIALIZED ((HRESULT)0x800401F0L)
#define REGDB_E_CLASSNOTREG ((HRESULT)0x80040154L)
#endif

#define STDMETHODCALLTYPE __stdcall
#define STDAPICALLTYPE __stdcall
#define STDAPI EXTERN_C HRESULT STDAPICALLTYPE
#define STDAPI_(type) EXTERN_C type STDAPICALLTYPE

/* An interface is declared once and read by C and by C++. In C it is a
   pointer to a table of functions that take the object first; in C++ a struct
   of pure virtual functions in the same order. */
#if defined(__cplusplus) && !defined(CINTERFACE)
#define interface struct
#define STDMETHOD(method) virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type, method) virtual type STDMETHODCALLTYPE method
#define PURE = 0
#define THIS_
#define THIS void
#define DECLARE_INTERFACE(name) interface name
#define DECLARE_INTERFACE_(name, base) interface name : public base
#else
#define interface struct
#define STDMETHOD(method) HRESULT (STDMETHODCALLTYPE *method)
#define STDMETHOD_(type, method) type (STDMETHODCALLTYPE *method)
#define PURE
#define THIS_ INTERFACE *This,
#define THIS INTERFACE *This
#define DECLARE_INTERFACE(name) \
    typedef interface name { const struct name##Vtbl *lpVtbl; } name; \
    typedef struct name##Vtbl name##Vtbl; \
    struct name##Vtbl
#define DECLARE_INTERFACE_(name, base) DECLARE_INTERFACE(name)
#endif

DEFINE_GUID(IID_IUnknown, 0x00000000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

#undef INTERFACE
#define INTERFACE IUnknown
DECLARE_INTERFACE(IUnknown)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;
};
#undef INTERFACE
typedef IUnknown *LPUNKNOWN;

#if !defined(__cplusplus) || defined(CINTERFACE)
#define IUnknown_QueryInterface(p, a, b) (p)->lpVtbl->QueryInterface(p, a, b)
#define IUnknown_AddRef(p) (p)->lpVtbl->AddRef(p)
#define IUnknown_Release(p) (p)->lpVtbl->Release(p)
#else
#define IUnknown_QueryInterface(p, a, b) (p)->QueryInterface(a, b)
#define IUnknown_AddRef(p) (p)->AddRef()
#define IUnknown_Release(p) (p)->Release()
#endif

#endif
