// SPDX-License-Identifier: GPL-2.0-or-later
// Native storage/atom providers for the implemented Win98 USER property core.
#include "win98_properties.h"
#include "x86_app.h"
#include <string.h>
namespace X86
{
struct Application::PropertyEnvironment
{
    using U = unsigned;
    static constexpr U Slots = 193, MaximumRows = 8191, MaximumBytes = MaximumRows * 8 + 2;
    struct Buffer
    {
        unsigned char *data = nullptr;
        U bytes = 0, capacity = 0, window = 0;
    } buffers[Slots];
    Application &app;
    // The process whose call is running. USER keeps the list at the window
    // (WND+44h, in USER's own heap) and takes the name from the caller: a
    // string is looked up in the caller's address space, and the caller gets
    // the error. For a window of this process it is `app` itself.
    Application *caller = nullptr;
    Win98Properties::State state{};
    U active_window = 0;
    bool setting = false;
    explicit PropertyEnvironment(Application &owner) : app(owner), caller(&owner) {}
    ~PropertyEnvironment()
    {
        for (auto &buffer : buffers)
            delete[] buffer.data;
    }
    void Invalid(const char *message) { caller->Fail(message); }
    void Exhausted() { caller->last_error = 8; }
    U Token(const Buffer &buffer) const { return U(&buffer - buffers + 1) << 16; }
    Buffer *Find(U token)
    {
        const U index = token >> 16;
        return index && index <= Slots && buffers[index - 1].data ? &buffers[index - 1] : nullptr;
    }
    unsigned char *Access(U address, U bytes)
    {
        auto *buffer = Find(address);
        const U offset = address & 0xffff;
        if (!buffer || offset > buffer->bytes || bytes > buffer->bytes - offset)
        {
            Invalid("USER property storage access is out of bounds");
            return nullptr;
        }
        return buffer->data + offset;
    }
    U Read16(U address)
    {
        const auto *p = Access(address, 2);
        return p ? p[0] | U(p[1]) << 8 : 0;
    }
    U Read32(U address)
    {
        const auto *p = Access(address, 4);
        return p ? p[0] | U(p[1]) << 8 | U(p[2]) << 16 | U(p[3]) << 24 : 0;
    }
    void Write16(U address, U value)
    {
        if (auto *p = Access(address, 2))
        {
            p[0] = value;
            p[1] = value >> 8;
        }
    }
    void Write32(U address, U value)
    {
        if (auto *p = Access(address, 4))
            for (U i = 0; i < 4; ++i)
                p[i] = value >> (i * 8);
    }
    bool ValidList(U window)
    {
        const U list = User::PropertyList(window);
        if (!list)
            return true;
        auto *buffer = Find(list);
        if (!buffer || (list & 0xffff) || buffer->window != window || buffer->bytes < 2)
        {
            Invalid("USER property list has no matching native owner");
            return false;
        }
        const U count = Read16(list);
        if (!count || count > MaximumRows || count * 8 + 2 > buffer->bytes)
        {
            Invalid("USER property list count exceeds its native allocation");
            return false;
        }
        for (U i = 0; i < count; ++i)
            if (Read16(list + 4 + i * 8) & 1)
            {
                Invalid("Private USER property cleanup is not ported");
                return false;
            }
        return true;
    }
    U PropertyList(U window) { return User::PropertyList(window); }
    void SetPropertyList(U window, U list)
    {
        if (list)
        {
            auto *buffer = Find(list);
            if (!buffer || (list & 0xffff) || buffer->window != window)
            {
                Invalid("USER property list assignment has no matching native owner");
                return;
            }
        }
        User::SetPropertyList(window, list);
    }
    U Allocate(U flags, U bytes)
    {
        if (flags != 0x40 || bytes != 10)
        {
            Invalid("Unexpected USER property allocation request");
            return 0;
        }
        for (auto &buffer : buffers)
            if (!buffer.data)
            {
                auto *data = new unsigned char[bytes]{};
                if (!data)
                {
                    Exhausted();
                    return 0;
                }
                buffer.data = data;
                buffer.bytes = buffer.capacity = bytes;
                buffer.window = active_window;
                return Token(buffer);
            }
        Exhausted();
        return 0;
    }
    U Reallocate(U list, U bytes, U flags)
    {
        auto *buffer = Find(list);
        if (!buffer || (list & 0xffff) || flags != 2 || buffer->window != active_window)
        {
            Invalid("Unexpected USER property reallocation request");
            return 0;
        }
        const U count = Read16(list);
        // Set passes a truncated WORD allocation size. Reject the next row
        // before that truncation can masquerade as a successful shrink.
        if (setting && count >= MaximumRows)
        {
            Exhausted();
            return 0;
        }
        const U expected = count * 8 + (setting ? 10 : 2);
        if (bytes != expected || bytes < 2 || bytes > MaximumBytes)
        {
            Invalid("USER property reallocation would wrap its row storage");
            return 0;
        }
        if (bytes > buffer->capacity)
        {
            auto *data = new unsigned char[bytes]{};
            if (!data)
            {
                Exhausted();
                return 0;
            }
            memcpy(data, buffer->data, buffer->bytes);
            delete[] buffer->data;
            buffer->data = data;
            buffer->capacity = bytes;
        }
        else if (bytes > buffer->bytes)
            memset(buffer->data + buffer->bytes, 0, bytes - buffer->bytes);
        // Native storage policy: newly exposed bytes are zero; logical tokens
        // stay stable even when backing moves. Shrink never changes either.
        buffer->bytes = bytes;
        return list;
    }
    void Free(U list)
    {
        auto *buffer = Find(list);
        if (!buffer || (list & 0xffff))
        {
            Invalid("USER property free does not name an owned buffer");
            return;
        }
        delete[] buffer->data;
        *buffer = {};
    }
    void Move(U destination, U source, U bytes)
    {
        if (!bytes)
            return;
        auto *out = Access(destination, bytes);
        const auto *in = Access(source, bytes);
        if (out && in)
            memmove(out, in, bytes);
    }
    U FindAtom(U name) { return caller->GlobalAtomSearch(name, false); }
    U AddAtom(U name) { return caller->GlobalAtomSearch(name, true); }
    void DeleteAtom(U atom) { caller->GlobalAtomDelete(atom); }
    void SystemCleanup(U, U) { Invalid("Private USER property cleanup is not ported"); }
};

// The process that holds the property list of a window: the one the window
// belongs to. USER itself keeps the list in its own heap, reachable from every
// process (GetProp and SetProp Windows 98 load USER's data segment and read
// WND+44h; neither looks at the calling task). Here each process keeps the
// lists of its own windows, and a call for a foreign window works on the
// owner's list.
Application *Application::PropertyOwner(unsigned hwnd)
{
    const unsigned process = User::WindowProcess(hwnd);
    if (!process || process == process_id)
        return this;
    auto *owner = static_cast<Application *>(User::ProcessContextOf(process));
    return owner && owner->state != Failed && owner->state != Exited ? owner : nullptr;
}

unsigned Application::PropertyApi(ApiName name, const unsigned *a)
{
    const bool set = API_IS(name,"SetPropA"), get = API_IS(name,"GetPropA");
    if (!set && !get && !API_IS(name,"RemovePropA"))
    {
        Fail("Unrecognized USER property API");
        return 0;
    }
    if (!User::IsWindow(a[0]))
    {
        last_error = 1400; // Native HWND validation boundary.
        return 0;
    }
    auto *owner = PropertyOwner(a[0]);
    if (!owner)
    {
        last_error = 1400; // The window's process is ending.
        return 0;
    }
    if (!owner->properties)
    {
        if (!set)
        {
            if (User::PropertyList(a[0]))
                Fail("USER property list exists without its native environment");
            return 0;
        }
        owner->properties = new PropertyEnvironment(*owner);
        if (!owner->properties)
        {
            last_error = 8;
            return 0;
        }
    }
    auto &e = *owner->properties;
    auto *const previous = e.caller;
    e.caller = this;
    unsigned result = 0;
    if (e.ValidList(a[0]))
    {
        e.active_window = a[0];
        e.setting = set;
        const bool string = a[1] >> 16;
        result = set   ? Win98Properties::Set(e, e.state, a[0], a[1], string, a[2], 2)
                 : get ? Win98Properties::Get(e, e.state, a[0], a[1], string)
                       : Win98Properties::Remove(e, e.state, a[0], a[1], string);
        e.setting = false;
        e.active_window = 0;
    }
    e.caller = previous;
    return state == Failed ? 0 : result;
}

// USER, reached from USER32 EnumPropsA (CL 2Ah) and EnumPropsExA
// (CL 29h) through the second flat-thunk table. The rows are walked forward in
// insertion order, a row USER marked as its own is skipped, and the callback
// stops the walk by answering zero. A name whose atom is below C000h reaches
// the callback as the atom itself in the low word of the pointer, which is the
// convention the caller has to look for; a real atom is spelled out.
//
// The answer is minus one for a window without a property list, zero when the
// callback stopped the walk and one when every row was offered. Both entries
// are 32-bit, so they take the thirty-two-bit form of the data word.
unsigned Application::EnumProperties(unsigned hwnd, unsigned callback, unsigned parameter,
                                     bool extended)
{
    if (!User::IsWindow(hwnd)) { last_error = 1400; return 0; }
    if (!callback) { last_error = 87; return 0; }
    // The rows are the owner's; names and the callback are the caller's.
    auto *owner = PropertyOwner(hwnd);
    if (!owner) { last_error = 1400; return 0; }
    if (!owner->properties) return 0xffffffffu;
    auto &e = *owner->properties;
    struct Caller
    {
        PropertyEnvironment &e; Application *previous;
        Caller(PropertyEnvironment &environment, Application *now) : e(environment), previous(environment.caller) { e.caller = now; }
        ~Caller() { e.caller = previous; }
    };
    const unsigned owner_process = User::WindowProcess(hwnd);
    const unsigned list = User::PropertyList(hwnd);
    if (!list || !e.Find(list)) return 0xffffffffu;
    unsigned result = 0xffffffffu;
    const unsigned name = Allocate(256);
    if (!name) { last_error = 8; return 0; }
    e.state.enumerated_list = list;
    for (unsigned row = list + 2; ; )
    {
        unsigned count;
        {
            Caller reading(e, this);
            count = e.Read16(list);
        }
        if (row >= list + 2 + count * 8) break;
        e.state.enumerated_item = row;
        unsigned atom, flags, data;
        {
            Caller reading(e, this);
            atom = e.Read16(row); flags = e.Read16(row + 2);
            data = e.Read32(row + 4);
        }
        if (flags & 1) { row += 8; continue; }   // skips USER's own rows
        unsigned pointer = atom;
        if (atom >= 0xc000)
        {
            const unsigned query[] = {atom, name, 256};
            if (!Api("GlobalGetAtomNameA", query)) { row += 8; continue; }
            pointer = name;
        }
        const unsigned with_parameter[] = {hwnd, pointer, data, parameter};
        const unsigned plain[] = {hwnd, pointer, data};
        const unsigned answer = extended ? Call(callback, with_parameter, 4)
                                         : Call(callback, plain, 3);
        // The callback may have ended the window or, for a foreign window,
        // its process; then the rows are gone with their environment.
        if (owner == this ? properties != &e
                          : User::ProcessContextOf(owner_process) != owner || owner->properties != &e)
        {
            Free(name);
            return answer ? 1 : 0;
        }
        if (state == Failed || state == Exited) break;
        if (!answer) { result = 0; break; }
        result = 1;
        // a callback that removed this property left the array
        // compacted, so the same slot holds the next row.
        if (e.state.enumerated_item == row) row += 8;
        if (!User::PropertyList(hwnd)) break;
    }
    e.state.enumerated_list = 0;
    e.state.enumerated_item = 0;
    Free(name);
    return result;
}

void Application::DestroyProperties(unsigned hwnd)
{
    if (!properties)
        return;
    auto &e = *properties;
    if (!e.ValidList(hwnd))
        return;
    e.active_window = hwnd;
    e.setting = false;
    Win98Properties::Destroy(e, e.state, hwnd);
    e.active_window = 0;
}

void Application::FreeProperties()
{
    // The shared desktop retains its owner-routed cleanup callback.
    if (!properties)
        return;
    auto &e = *properties;
    for (auto &buffer : e.buffers)
        if (buffer.data)
        {
            const unsigned hwnd = buffer.window, list = e.Token(buffer);
            if (User::PropertyList(hwnd) == list)
            {
                DestroyProperties(hwnd);
                // If a native invariant failed, detach without claiming that
                // unported private USER payload cleanup has been performed.
                if (buffer.data && User::PropertyList(hwnd) == list)
                    User::SetPropertyList(hwnd, 0);
            }
            else
                Fail("USER property buffer lost its window owner before cleanup");
        }
    // No extra atom-reference correction: the original Set failure and Remove
    // with an integer name can retain references. Only Destroy's rows release.
    delete properties;
    properties = nullptr;
}
} // namespace X86
