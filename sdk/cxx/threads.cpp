/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// Threads, locks and thread-local keys of libc++ for programs of the NEO98
// SDK. libc++ declares these functions for Windows with opaque types
// (__thread/support/windows.h) and implements them over SRWLOCK,
// CONDITION_VARIABLE, InitOnceExecuteOnce and FlsAlloc, which need Windows
// Vista. This file implements the same functions over what KERNEL32 has had
// since Windows 95: events, CreateThread, TlsAlloc. The same
// program runs on any later Windows. Own code. docs/ARM64-APPS.md, "C++".
//
// Taking and releasing a lock nobody else holds, a recursive one included,
// and signalling a condition nobody waits on call nothing of the system: a
// call costs a program more than the whole operation (docs/ARM64-APPS.md,
// "The allocator"). The records come from malloc, which works in the program.
#include <__thread/support/windows.h>
#include <chrono>
#include <cstdlib>
#include <new>
#include <windows.h>

_LIBCPP_BEGIN_NAMESPACE_STD

namespace {
// The compiler's atomic operations are instructions on ARM64; nothing of the
// system is involved.
template <class T>
T Load(T* at) { return __atomic_load_n(at, __ATOMIC_SEQ_CST); }
template <class T>
bool Swap(T* at, T expected, T desired) {
  return __atomic_compare_exchange_n(at, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

// The thread that runs, without asking the system: x18 holds the address of
// the thread's block (the TEB) in every ARM64 program for Windows, and no two
// threads that live share one.
void* Self() {
  void* block;
  __asm__("mov %0, x18" : "=r"(block));
  return block;
}

// A lock: 0 free, 1 held, 2 held and somebody may wait. A waiter sleeps on
// an event that resets itself; the one who releases a contended lock sets
// it. The record lies on the heap and is made the first time the lock is
// used, because libc++ needs a lock that is ready as a null pointer.
struct Lock {
  long state;
  HANDLE event;
};
Lock* LockOf(void** slot) {
  Lock* lock = static_cast<Lock*>(Load(slot));
  if (lock)
    return lock;
  Lock* made = static_cast<Lock*>(calloc(1, sizeof(Lock)));
  if (!made)
    return nullptr;
  made->event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (made->event && Swap(slot, static_cast<void*>(nullptr), static_cast<void*>(made)))
    return made;
  // Another thread was faster, or there is no event.
  if (made->event)
    CloseHandle(made->event);
  free(made);
  return static_cast<Lock*>(Load(slot));
}
bool TryAcquire(Lock* lock) { return Swap(&lock->state, 0L, 1L); }
void Acquire(Lock* lock) {
  if (TryAcquire(lock))
    return;
  // From here on the lock counts as contended, so whoever releases it wakes
  // a waiter; this thread too marks it so when it gets it.
  while (__atomic_exchange_n(&lock->state, 2L, __ATOMIC_SEQ_CST) != 0)
    WaitForSingleObject(lock->event, INFINITE);
}
void Release(Lock* lock) {
  if (__atomic_exchange_n(&lock->state, 0L, __ATOMIC_SEQ_CST) == 2)
    SetEvent(lock->event);
}
void Destroy(void** slot) {
  Lock* lock = static_cast<Lock*>(Load(slot));
  if (!lock)
    return;
  CloseHandle(lock->event);
  free(lock);
  *slot = nullptr;
}

// A recursive lock: the lock, the thread that holds it and how often.
struct Recursive {
  void* lock;
  void* owner;
  unsigned count;
};
static_assert(sizeof(Recursive) <= sizeof(__libcpp_recursive_mutex_t), "the opaque type of libc++ has room");

// A condition: the threads that wait, in the order they came. Each waits on
// an event of its own, so a wake-up reaches the thread it was meant for and
// no thread that starts to wait later can take it.
struct Waiter {
  Waiter* next;
  HANDLE event;
};
struct Condition {
  long guard;
  Waiter* first;
  Waiter* last;
};
Condition* ConditionOf(void** slot) {
  Condition* condition = static_cast<Condition*>(Load(slot));
  if (condition)
    return condition;
  Condition* made = static_cast<Condition*>(calloc(1, sizeof(Condition)));
  if (!made)
    return nullptr;
  if (Swap(slot, static_cast<void*>(nullptr), static_cast<void*>(made)))
    return made;
  free(made);
  return static_cast<Condition*>(Load(slot));
}
// The list is touched for a few instructions at a time.
void Guard(Condition* condition) {
  while (__atomic_exchange_n(&condition->guard, 1L, __ATOMIC_SEQ_CST))
    Sleep(0);
}
void Unguard(Condition* condition) { __atomic_store_n(&condition->guard, 0L, __ATOMIC_SEQ_CST); }
// The event a thread waits on: one per thread, kept for its next wait.
DWORD event_slot = 0xffffffffu;
long event_slot_state; // 0 none, 1 being made, 2 there
HANDLE EventOfThread() {
  while (Load(&event_slot_state) != 2) {
    if (Swap(&event_slot_state, 0L, 1L)) {
      event_slot = TlsAlloc();
      __atomic_store_n(&event_slot_state, 2L, __ATOMIC_SEQ_CST);
    } else
      Sleep(0);
  }
  HANDLE event = static_cast<HANDLE>(TlsGetValue(event_slot));
  if (!event) {
    event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    TlsSetValue(event_slot, event);
  }
  return event;
}
void CloseEventOfThread() {
  if (Load(&event_slot_state) != 2)
    return;
  HANDLE event = static_cast<HANDLE>(TlsGetValue(event_slot));
  if (event) {
    TlsSetValue(event_slot, nullptr);
    CloseHandle(event);
  }
}
int Wait(void** cv, void** mutex, DWORD milliseconds) {
  Condition* condition = ConditionOf(cv);
  Lock* lock           = LockOf(mutex);
  Waiter self          = {nullptr, EventOfThread()};
  if (!condition || !lock || !self.event)
    return ENOMEM;
  Guard(condition);
  if (condition->last)
    condition->last->next = &self;
  else
    condition->first = &self;
  condition->last = &self;
  Unguard(condition);
  Release(lock);
  int result = 0;
  if (WaitForSingleObject(self.event, milliseconds) != WAIT_OBJECT_0) {
    // Time is up. If this thread still stands in the list, it leaves;
    // otherwise somebody took it out to wake it, and the event is on its way.
    bool listed = false;
    Guard(condition);
    for (Waiter **link = &condition->first, *previous = nullptr; *link; previous = *link, link = &(*link)->next)
      if (*link == &self) {
        *link = self.next;
        if (condition->last == &self)
          condition->last = previous;
        listed = true;
        break;
      }
    Unguard(condition);
    if (listed)
      result = ETIMEDOUT;
    else
      WaitForSingleObject(self.event, INFINITE);
  }
  Acquire(lock);
  return result;
}
// Takes waiters out of the list and wakes them: one, or all.
void Wake(Condition* condition, bool all) {
  for (;;) {
    Guard(condition);
    Waiter* waiter = condition->first;
    HANDLE event   = nullptr;
    if (waiter) {
      condition->first = waiter->next;
      if (!condition->first)
        condition->last = nullptr;
      // The record lies on the stack of the waiting thread: nothing of it
      // is read once that thread may run.
      event = waiter->event;
    }
    Unguard(condition);
    if (!event)
      return;
    SetEvent(event);
    if (!all)
      return;
  }
}

// The keys of thread-local values that have a destructor.
struct Key {
  DWORD slot;
  void(_LIBCPP_TLS_DESTRUCTOR_CC* destroy)(void*);
};
constexpr int MaximumKeys = 64;
Key keys[MaximumKeys];
long key_count;
// What a thread leaves behind when it ends: its values, destroyed; a
// destructor may set values again, so a few rounds.
void DestroyValues() {
  for (int round = 0; round < 4; ++round) {
    bool any = false;
    const long count = Load(&key_count);
    for (long i = 0; i < count && i < MaximumKeys; ++i) {
      void* value = keys[i].destroy ? TlsGetValue(keys[i].slot) : nullptr;
      if (!value)
        continue;
      TlsSetValue(keys[i].slot, nullptr);
      keys[i].destroy(value);
      any = true;
    }
    if (!any)
      break;
  }
}

struct Thread {
  HANDLE handle;
  DWORD id;
};
struct Start {
  void* (*function)(void*);
  void* argument;
};
DWORD WINAPI Entry(LPVOID parameter) {
  Start* start = static_cast<Start*>(parameter);
  void* (*function)(void*) = start->function;
  void* argument           = start->argument;
  free(start);
  function(argument);
  return 0;
}
} // namespace

// Mutex
int __libcpp_recursive_mutex_init(__libcpp_recursive_mutex_t* m) {
  Recursive* r = reinterpret_cast<Recursive*>(m);
  r->lock      = nullptr;
  r->owner     = nullptr;
  r->count     = 0;
  return LockOf(&r->lock) ? 0 : ENOMEM;
}
int __libcpp_recursive_mutex_lock(__libcpp_recursive_mutex_t* m) {
  Recursive* r    = reinterpret_cast<Recursive*>(m);
  void* const self = Self();
  if (Load(&r->owner) == self) {
    ++r->count;
    return 0;
  }
  Lock* lock = LockOf(&r->lock);
  if (!lock)
    return ENOMEM;
  Acquire(lock);
  __atomic_store_n(&r->owner, self, __ATOMIC_SEQ_CST);
  r->count = 1;
  return 0;
}
bool __libcpp_recursive_mutex_trylock(__libcpp_recursive_mutex_t* m) {
  Recursive* r    = reinterpret_cast<Recursive*>(m);
  void* const self = Self();
  if (Load(&r->owner) == self) {
    ++r->count;
    return true;
  }
  Lock* lock = LockOf(&r->lock);
  if (!lock || !TryAcquire(lock))
    return false;
  __atomic_store_n(&r->owner, self, __ATOMIC_SEQ_CST);
  r->count = 1;
  return true;
}
int __libcpp_recursive_mutex_unlock(__libcpp_recursive_mutex_t* m) {
  Recursive* r = reinterpret_cast<Recursive*>(m);
  if (--r->count)
    return 0;
  __atomic_store_n(&r->owner, static_cast<void*>(nullptr), __ATOMIC_SEQ_CST);
  Release(static_cast<Lock*>(Load(&r->lock)));
  return 0;
}
int __libcpp_recursive_mutex_destroy(__libcpp_recursive_mutex_t* m) {
  Destroy(&reinterpret_cast<Recursive*>(m)->lock);
  return 0;
}

int __libcpp_mutex_lock(__libcpp_mutex_t* m) {
  Lock* lock = LockOf(m);
  if (!lock)
    return ENOMEM;
  Acquire(lock);
  return 0;
}
bool __libcpp_mutex_trylock(__libcpp_mutex_t* m) {
  Lock* lock = LockOf(m);
  return lock && TryAcquire(lock);
}
int __libcpp_mutex_unlock(__libcpp_mutex_t* m) {
  Release(static_cast<Lock*>(Load(m)));
  return 0;
}
int __libcpp_mutex_destroy(__libcpp_mutex_t* m) {
  Destroy(m);
  return 0;
}

// Condition Variable
int __libcpp_condvar_signal(__libcpp_condvar_t* cv) {
  if (Condition* condition = ConditionOf(cv))
    Wake(condition, false);
  return 0;
}
int __libcpp_condvar_broadcast(__libcpp_condvar_t* cv) {
  if (Condition* condition = ConditionOf(cv))
    Wake(condition, true);
  return 0;
}
int __libcpp_condvar_wait(__libcpp_condvar_t* cv, __libcpp_mutex_t* m) { return Wait(cv, m, INFINITE); }
int __libcpp_condvar_timedwait(__libcpp_condvar_t* cv, __libcpp_mutex_t* m, __libcpp_timespec_t* ts) {
  // The time is a point of the system clock.
  using namespace chrono;
  const auto until = seconds(ts->tv_sec) + nanoseconds(ts->tv_nsec);
  const auto now   = system_clock::now().time_since_epoch();
  if (now >= until)
    return Wait(cv, m, 0);
  const auto left = duration_cast<milliseconds>(until - now).count() + 1;
  return Wait(cv, m, left > 0x7fffffff ? 0x7fffffff : static_cast<DWORD>(left));
}
int __libcpp_condvar_destroy(__libcpp_condvar_t* cv) {
  Condition* condition = static_cast<Condition*>(Load(cv));
  if (condition) {
    free(condition);
    *cv = nullptr;
  }
  return 0;
}

// Execute Once: null not yet, 1 running, 2 done.
int __libcpp_execute_once(__libcpp_exec_once_flag* flag, void (*init_routine)()) {
  void* const running = reinterpret_cast<void*>(1);
  void* const done    = reinterpret_cast<void*>(2);
  for (;;) {
    void* state = Load(flag);
    if (state == done)
      return 0;
    if (!state && Swap(flag, static_cast<void*>(nullptr), running)) {
      init_routine();
      __atomic_store_n(flag, done, __ATOMIC_SEQ_CST);
      return 0;
    }
    // Another thread runs it.
    Sleep(1);
  }
}

// Thread ID
bool __libcpp_thread_id_equal(__libcpp_thread_id lhs, __libcpp_thread_id rhs) { return lhs == rhs; }
bool __libcpp_thread_id_less(__libcpp_thread_id lhs, __libcpp_thread_id rhs) { return lhs < rhs; }

// Thread. The opaque value of libc++ is the address of a record with the
// handle and the number: Windows 98 cannot name the thread behind a handle.
bool __libcpp_thread_isnull(const __libcpp_thread_t* t) { return *t == nullptr; }
int __libcpp_thread_create(__libcpp_thread_t* t, void* (*func)(void*), void* arg) {
  Thread* thread = static_cast<Thread*>(calloc(1, sizeof(Thread)));
  Start* start   = static_cast<Start*>(malloc(sizeof(Start)));
  if (thread && start) {
    start->function = func;
    start->argument = arg;
    thread->handle  = CreateThread(nullptr, 0, Entry, start, 0, &thread->id);
    if (thread->handle) {
      *t = thread;
      return 0;
    }
  }
  free(thread);
  free(start);
  return EAGAIN;
}
__libcpp_thread_id __libcpp_thread_get_current_id() { return static_cast<__libcpp_thread_id>(GetCurrentThreadId()); }
__libcpp_thread_id __libcpp_thread_get_id(const __libcpp_thread_t* t) {
  return *t ? static_cast<__libcpp_thread_id>(static_cast<Thread*>(*t)->id) : 0;
}
int __libcpp_thread_join(__libcpp_thread_t* t) {
  Thread* thread = static_cast<Thread*>(*t);
  if (!thread || WaitForSingleObject(thread->handle, INFINITE) == WAIT_FAILED)
    return EINVAL;
  CloseHandle(thread->handle);
  free(thread);
  return 0;
}
int __libcpp_thread_detach(__libcpp_thread_t* t) {
  Thread* thread = static_cast<Thread*>(*t);
  if (!thread)
    return EINVAL;
  CloseHandle(thread->handle);
  free(thread);
  return 0;
}
void __libcpp_thread_yield() { Sleep(0); }
void __libcpp_thread_sleep_for(const chrono::nanoseconds& ns) {
  // Rounded up: a thread never sleeps shorter than asked.
  long long milliseconds = (ns.count() + 999999) / 1000000;
  for (; milliseconds > 0x7fffffff; milliseconds -= 0x7fffffff)
    Sleep(0x7fffffff);
  Sleep(static_cast<DWORD>(milliseconds));
}

// Thread Local Storage. The SDK's thread-entry wrapper and DLL detach call
// the same cleanup, including for a direct CreateThread call.
int __libcpp_tls_create(__libcpp_tls_key* key, void(_LIBCPP_TLS_DESTRUCTOR_CC* at_exit)(void*)) {
  const DWORD slot = TlsAlloc();
  if (slot == 0xffffffffu)
    return EAGAIN;
  if (at_exit) {
    const long index = __atomic_fetch_add(&key_count, 1L, __ATOMIC_SEQ_CST);
    if (index >= MaximumKeys) {
      __atomic_fetch_sub(&key_count, 1L, __ATOMIC_SEQ_CST);
      TlsFree(slot);
      return EAGAIN;
    }
    keys[index].slot    = slot;
    keys[index].destroy = at_exit;
  }
  *key = static_cast<__libcpp_tls_key>(slot);
  return 0;
}
void* __libcpp_tls_get(__libcpp_tls_key key) { return TlsGetValue(static_cast<DWORD>(key)); }
int __libcpp_tls_set(__libcpp_tls_key key, void* p) { return TlsSetValue(static_cast<DWORD>(key), p) ? 0 : EINVAL; }

_LIBCPP_END_NAMESPACE_STD

extern "C" void __neo98_thread_cleanup()
{
  std::DestroyValues();
  std::CloseEventOfThread();
}

namespace {
struct WindowsStart {
  LPTHREAD_START_ROUTINE procedure;
  LPVOID argument;
};
DWORD WINAPI WindowsEntry(LPVOID data)
{
  WindowsStart start = *static_cast<WindowsStart*>(data);
  free(data);
  DWORD result = start.procedure(start.argument);
  __neo98_thread_cleanup();
  return result;
}
HANDLE WINAPI CreateWindowsThread(const SECURITY_ATTRIBUTES* attributes, SIZE_T bytes,
                                  LPTHREAD_START_ROUTINE procedure, LPVOID argument, DWORD flags, LPDWORD id)
{
  // Resolve the real export rather than going through our import pointer.
  auto create = reinterpret_cast<decltype(&CreateThread)>(GetProcAddress(GetModuleHandleA("KERNEL32.DLL"), "CreateThread"));
  if (!create) return nullptr;
  WindowsStart* start = static_cast<WindowsStart*>(malloc(sizeof(WindowsStart)));
  if (!start) { SetLastError(8); return nullptr; }
  *start = {procedure, argument};
  HANDLE handle = create(attributes, bytes, WindowsEntry, start, flags, id);
  if (!handle) { DWORD error = GetLastError(); free(start); SetLastError(error); }
  return handle;
}
void WINAPI ExitWindowsThread(DWORD code)
{
  auto exit = reinterpret_cast<decltype(&ExitThread)>(GetProcAddress(GetModuleHandleA("KERNEL32.DLL"), "ExitThread"));
  __neo98_thread_cleanup();
  exit(code);
}
}
// Every SDK call through these import pointers enters the CRT wrapper. This
// changes no KERNEL32 export and works on Windows 98 without TLS callbacks.
extern "C" {
decltype(&CreateThread) __imp_CreateThread = CreateWindowsThread;
decltype(&ExitThread) __imp_ExitThread = ExitWindowsThread;
}
