// SPDX-License-Identifier: GPL-2.0-or-later
// SHELL32 loads OLE32 on first use, resolves thirteen entry points, and
// counts references. RegisterDragDrop and DoDragDrop acquire a reference;
// RevokeDragDrop releases one. Loading and initialization failures propagate
// their HRESULTs. Release unloads the module at zero references; the next
// load refreshes cached pointers. DoDragDrop may construct and release a
// default drop source. OLE operations remain external providers.
#pragma once
namespace Win98ShellDragDrop {
using U = unsigned;
enum : U {
 Ok = 0,
 Unexpected = 0x8000ffffu,
 OutOfMemory = 0x8007000eu,  //
};
// Resolve procedures in this order. The stored cell follows each
// name because every store writes the previous call's result.
enum Entry : U {
 RegisterDragDrop,        //
 RevokeDragDrop,          //
 OleInitialize,           //
 OleUninitialize,         //
 StgCreateDocfile,        //
 StgOpenStorage,          //
 OleQueryLinkFromData,    //
 OleQueryCreateFromData,  //
 OleGetClipboard,         //
 OleSetClipboard,         //
 OleFlushClipboard,       //
 DoDragDrop,              //
 GetClassFile,            //
 EntryCount,
};
inline const char *Name(U entry)
{
 static const char *const names[EntryCount] = {
  "RegisterDragDrop", "RevokeDragDrop", "OleInitialize", "OleUninitialize",
  "StgCreateDocfile", "StgOpenStorage", "OleQueryLinkFromData",
  "OleQueryCreateFromData", "OleGetClipboard", "OleSetClipboard",
  "OleFlushClipboard", "DoDragDrop", "GetClassFile"};
 return entry < EntryCount ? names[entry] : "";
}
inline const char *Module() { return "OLE32.DLL"; }  //
// The shared cells. A single object holds both, because every routine here
// reads or writes both of them.
struct State {
 U module = 0;                 //
 U references = 0;             //
 U entries[EntryCount] = {};   //
};
inline bool Failed(U hr) { return static_cast<int>(hr) < 0; }
// E supplies LoadLibrary, Procedure, FreeLibrary and Invoke.
template <class E> U Gateway(E &e, State &s)
{
 U hr = Ok;
 if (!s.module)
 {
  const U module = e.LoadLibrary(Module());
  if (!module)
   hr = OutOfMemory;
  else
  {
   for (U i = 0; i < EntryCount; ++i)
    s.entries[i] = e.Procedure(module, Name(i));
   const U previous = s.module;
   s.module = module;
   if (previous)
    e.FreeLibrary(module);
  }
 }
 if (!Failed(hr))
  ++s.references;
 return hr;
}
// Always S_OK, even when it frees nothing.
template <class E> U ReleaseGateway(E &e, State &s)
{
 // The original decrements without a floor: a count of zero wraps and keeps
 // the module. Only Uninitialize calls this, and it checks the count first.
 if (--s.references)
  return Ok;
 e.FreeLibrary(s.module);
 s.module = 0;
 return Ok;
}

inline U Present(const State &s) { return s.module ? Ok : Unexpected; }

template <class E> U Initialize(E &e, State &s)
{
 const U hr = Gateway(e, s);
 if (Failed(hr))
  return hr;
 const U argument = 0;
 return e.Invoke(s.entries[OleInitialize], &argument, 1);
}

template <class E> U Uninitialize(E &e, State &s)
{
 if (s.module)
  e.Invoke(s.entries[OleUninitialize], nullptr, 0);
 if (!s.references)
  return Unexpected;
 return ReleaseGateway(e, s);
}
// ordinal 86.
template <class E> U Register(E &e, State &s, U window, U target)
{
 const U hr = Initialize(e, s);
 if (Failed(hr))
  return hr;
 const U arguments[] = {window, target};
 return e.Invoke(s.entries[RegisterDragDrop], arguments, 2);
}
// ordinal 87.
template <class E> U Revoke(E &e, State &s, U window)
{
 const U present = Present(s);
 if (Failed(present))
  return present;
 const U arguments[] = {window};
 const U hr = e.Invoke(s.entries[RevokeDragDrop], arguments, 1);
 Uninitialize(e, s);
 return hr;
}
// The default drop source is supplied by a provider:
// E::DefaultSource returns zero when it cannot build one, and E::ReleaseSource
// calls IUnknown::Release.
template <class E> U Drag(E &e, State &s, U window, U data, U source, U effects, U result)
{
 (void)window;  //  never reads [ebp+8].
 const U hr = Initialize(e, s);
 if (Failed(hr))
  return hr;
 U created = 0, used = source;
 if (!used)
 {
  created = e.DefaultSource(data);
  used = created;
 }
 const U arguments[] = {data, used, effects, result};
 const U dragged = e.Invoke(s.entries[DoDragDrop], arguments, 4);
 if (created)
  e.ReleaseSource(created);
 return dragged;
}
}
