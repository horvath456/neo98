// SPDX-License-Identifier: GPL-2.0-or-later
// Runs the implemented SHELL32 drag-and-drop family for
// tools/test_shell_dragdrop_original.py. The provider callback carries every
// loader and OLE32 call into Python, which answers it for both sides.
#include "../src/win98_shell_dragdrop.h"
#include <stdint.h>
#include <initializer_list>
using U = unsigned;
using P = unsigned long long;
struct Wire
{
    U op, window, target, data, source, effects, result_pointer, result;
    Win98ShellDragDrop::State state;
};
static U (*provider)(U, const P *, U);
struct E
{
    U Call(U op, std::initializer_list<P> a) { return provider(op, a.begin(), a.size()); }
    U LoadLibrary(const char *name) { return Call(1, {(uintptr_t)name}); }
    U Procedure(U module, const char *name) { return Call(2, {module, (uintptr_t)name}); }
    void FreeLibrary(U module) { Call(3, {module}); }
    U Invoke(U procedure, const U *arguments, U count)
    {
        return Call(4, {procedure, count, count > 0 ? arguments[0] : 0, count > 1 ? arguments[1] : 0,
                        count > 2 ? arguments[2] : 0, count > 3 ? arguments[3] : 0});
    }
    U DefaultSource(U data) { return Call(5, {data}); }
    void ReleaseSource(U source) { Call(6, {source}); }
};
extern "C" void callbacks(U (*p)(U, const P *, U)) { provider = p; }
extern "C" void run(Wire *w)
{
    E e;
    using namespace Win98ShellDragDrop;
    switch (w->op)
    {
    case 0: w->result = Register(e, w->state, w->window, w->target); break;
    case 1: w->result = Revoke(e, w->state, w->window); break;
    case 2:
        w->result = Drag(e, w->state, w->window, w->data, w->source, w->effects, w->result_pointer);
        break;
    case 3: w->result = Initialize(e, w->state); break;
    case 4: w->result = Uninitialize(e, w->state); break;
    }
}
