// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// SHLWAPI thread creation and reference-counted thread objects.
// Scheduling, guest calls, COM and TLS are explicit providers.
namespace Win98ShellThreads {
struct StartData { unsigned procedure, preparation, event, parameter, flags, thread_ref, process_ref; };
static_assert(sizeof(StartData)==28,"original seven DWORD start record");
template<class E> unsigned GetRef(E&e,unsigned&object){object=e.TlsGet();e.Reference(object);if(!object)return 0x80004002u;e.AddRef(object);return 0;}
template<class E> unsigned SetRef(E&e,unsigned object){e.TlsSet(object);return 0;}
template<class E> unsigned Worker(E&e,StartData data){
 int initialized=0;if(data.flags&8)initialized=e.ComInitialize();
 if(data.preparation)e.Invoke(data.preparation,data.parameter);
 e.Signal(data.event);
 unsigned result=e.Invoke(data.procedure,data.parameter);
 if(data.thread_ref)e.Release(data.thread_ref);
 if(data.process_ref)e.Release(data.process_ref);
 if((data.flags&8)&&initialized>=0)e.ComUninitialize();
 return result;
}
template<class E> unsigned Create(E&e,unsigned proc,unsigned parameter,unsigned flags,unsigned preparation){
 StartData data={proc,preparation,0,parameter,flags,0,0};
 if(flags&2)GetRef(e,data.thread_ref);
 if(flags&4)e.ProcessRef(data.process_ref);
 data.event=e.Event();unsigned result=0;
 if(data.event){unsigned thread=e.Thread(data);if(thread){e.Wait(data.event);e.Close(thread);result=1;}e.Close(data.event);}
 if(!result){if(data.thread_ref)e.Release(data.thread_ref);if(data.process_ref)e.Release(data.process_ref);if(flags&1){e.Invoke(proc,parameter);result=1;}}
 return result;
}
}
