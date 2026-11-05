// SPDX-License-Identifier: GPL-2.0-or-later
// Native DINPUT.VXD support for DirectX mouse and keyboard access. Without
// it, DINPUT.DLL falls back to hooks with stricter acquisition modes.
// Device controls release instances, configure object mappings and buffers,
// acquire/unacquire devices, set notification events, create mouse/keyboard
// instances, initialize unacquired state and query sequence counters or
// button counts. Joysticks retain DLL emulation.
// The DLL accesses instance flags, device state and ring-buffer pointers
// directly. Store these in the creating process's guest memory. Convert host
// mouse positions to motion; guest SetCursorPos warps are not physical
// movement (DinputWarp).
#include "x86_app.h"
#include "host_devices.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned InstanceSize = 0x44;
constexpr unsigned StateSize = 20;          // DIMOUSESTATE2; older DLLs use 16 bytes
struct Code { unsigned in, out; };
// Per-operation argument sizes.
constexpr Code codes[0x12] = {{0, 0}, {4, 0}, {12, 0}, {4, 0}, {4, 0}, {8, 0}, {8, 0}, {20, 4}, {4, 0},
                              {20, 4}, {8, 0}, {20, 4}, {4, 0}, {4, 76}, {8, 4}, {0, 4}, {4, 16}, {0, 4}};
int last_x = 0, last_y = 0;
bool have_last = false;
} // namespace

bool Application::DinputInstance(unsigned instance) const
{
    for (unsigned i = 0; i < dinput_vxd.count; ++i)
        if (dinput_vxd.instances[i] == instance) return true;
    return false;
}

void Application::DinputDropInstance(unsigned instance)
{
    for (unsigned i = 0; i < dinput_vxd.count; ++i)
        if (dinput_vxd.instances[i] == instance)
        {
            const unsigned flags = Read32(instance);
            if (flags & 2)
            {
                auto &acquired = Read32(instance + 0x2c) == 2 ? dinput_vxd.keyboard_acquired : dinput_vxd.acquired;
                auto &exclusive = Read32(instance + 0x2c) == 2 ? dinput_vxd.keyboard_exclusive : dinput_vxd.exclusive;
                if (acquired) --acquired;
                if ((flags & 1) && exclusive) --exclusive;
            }
            if (const unsigned ring = Read32(instance + 8)) Free(ring);
            if (const unsigned table = Read32(instance + 0x3c)) Free(table);
            Free(instance);
            dinput_vxd.instances[i] = dinput_vxd.instances[--dinput_vxd.count];
            return;
        }
}

void Application::FreeDinputVxd()
{
    while (dinput_vxd.count) DinputDropInstance(dinput_vxd.instances[0]);
    dinput_vxd = DinputVxd{};
}

unsigned Application::DinputVxdControl(const unsigned *a)
{
    const unsigned code = a[1];
    if (code == ~0u) { FreeDinputVxd(); return 0; }   // DIOC_CLOSEHANDLE
    if (!code) return 0;                               // DIOC_OPEN
    if (code < 0x100 || code >= 0x112) return 1;
    const Code &size = codes[code - 0x100];
    if (a[3] != size.in || a[5] != size.out) return 87;
    if ((size.in && !virtual_memory.Access(a[2], size.in, false)) ||
        (size.out && !virtual_memory.Access(a[4], size.out, true)))
        return 87;
    if (a[6] && virtual_memory.Access(a[6], 4, true)) Write32(a[6], size.out);
    const unsigned in = a[2], out = a[4];
    if (log && trace_calls) { char t[160]; snprintf(t, sizeof t, "DINPUTVXD control %x in=%08x,%08x,%08x flags=%08x", code, size.in >= 4 ? Read32(in) : 0, size.in >= 8 ? Read32(in + 4) : 0, size.in >= 12 ? Read32(in + 8) : 0, size.in >= 4 && DinputInstance(Read32(in)) ? Read32(Read32(in)) : 0); log(t); }
    auto instance = [&]() { const unsigned i = Read32(in); return DinputInstance(i) ? i : 0u; };
    switch (code)
    {
    case 0x101:
        if (const unsigned i = instance())
        {
            const unsigned references = Read32(i + 0x34);
            if (references > 1) Write32(i + 0x34, references - 1);
            else DinputDropInstance(i);
        }
        return 0;
    case 0x102:
    {
        const unsigned i = instance();
        if (!i) return 87;
        if (Read32(i) & 2) return 0xaa;
        const unsigned count = Read32(in + 4), source = Read32(in + 8);
        if (count > 256 || (count && !virtual_memory.Access(source, count * 4, false))) return 87;
        if (const unsigned old = Read32(i + 0x3c)) Free(old);
        Write32(i + 0x3c, 0);
        Write32(i + 0x38, count);
        if (!count) return 0;
        const unsigned table = Allocate(count * 4);
        if (!table) return 8;
        for (unsigned n = 0; n < count; ++n) Write32(table + n * 4, Read32(source + n * 4));
        Write32(i + 0x3c, table);
        return 0;
    }
    case 0x103:
    {
        const unsigned i = instance();
        if (!i) return 87;
        const unsigned flags = Read32(i);
        if (!(flags & 2))
        {
            Write32(i, flags | 2);
            ++(Read32(i + 0x2c) == 2 ? dinput_vxd.keyboard_acquired : dinput_vxd.acquired);
            if (flags & 1) ++(Read32(i + 0x2c) == 2 ? dinput_vxd.keyboard_exclusive : dinput_vxd.exclusive);
        }
        return 0;
    }
    case 0x104:
    {
        const unsigned i = instance();
        if (!i) return 87;
        const unsigned flags = Read32(i);
        if (flags & 2)
        {
            Write32(i, flags & ~2u);
            auto &acquired = Read32(i + 0x2c) == 2 ? dinput_vxd.keyboard_acquired : dinput_vxd.acquired;
            auto &exclusive = Read32(i + 0x2c) == 2 ? dinput_vxd.keyboard_exclusive : dinput_vxd.exclusive;
            if (acquired) --acquired;
            if ((flags & 1) && exclusive) --exclusive;
        }
        return 0;
    }
    case 0x105:
    {
        const unsigned i = instance();
        if (!i) return 87;
        const unsigned old = Read32(i + 0x30);
        Write32(i + 0x30, Read32(in + 4));
        if (old) DinputCloseEvent(old);
        return 0;
    }
    case 0x106:
    {
        const unsigned i = instance();
        if (!i) return 87;
        if (Read32(i) & 2) return 0xaa;
        const unsigned count = Read32(in + 4);
        if (count > 0x10000) return 8;
        unsigned ring = 0, bytes = 0;
        if (count)
        {
            bytes = (count + 1) * 16;
            ring = Allocate(bytes);
            if (!ring) return 8;
            for (unsigned n = 0; n < bytes; n += 4) Write32(ring + n, 0);
        }
        Write32(i + 0x10, ring);
        Write32(i + 0x14, ring);
        Write32(i + 0x18, 0);
        Write32(i + 0x0c, ring + bytes);
        const unsigned old = Read32(i + 8);
        Write32(i + 8, ring);
        if (old) Free(old);
        return 0;
    }
    case 0x107:
    case 0x109:
    {
        const bool keyboard = code == 0x109;
        const unsigned state_size = keyboard ? 256 : StateSize;
        const unsigned bytes = Read32(in), count = Read32(in + 4), objects = Read32(in + 8);
        if ((keyboard ? bytes != 256 : bytes != 16 && bytes != StateSize) || count > state_size ||
            (count && !virtual_memory.Access(objects, count * 16, false))) return 0x1f;
        // 10020150: the format of the device, once, from the first caller.
        if (!keyboard && !dinput_vxd.formatted)
        {
            for (unsigned n = 0; n < count; ++n)
            {
                const unsigned offset = Read32(objects + n * 16 + 4);
                if (offset < StateSize) dinput_vxd.format[offset] = Read32(objects + n * 16 + 8);
            }
            dinput_vxd.formatted = true;
        }
        auto &device_state = keyboard ? dinput_vxd.keyboard_state : dinput_vxd.state;
        if (!device_state)
        {
            device_state = Allocate(state_size);
            if (!device_state) return 8;
            for (unsigned n = 0; n < state_size; n += 4) Write32(device_state + n, 0);
        }
        if (dinput_vxd.count == sizeof dinput_vxd.instances / sizeof dinput_vxd.instances[0]) return 8;
        const unsigned i = Allocate(InstanceSize);
        if (!i) return 8;
        for (unsigned n = 0; n < InstanceSize; n += 4) Write32(i + n, 0);
        Write32(i + 4, device_state);
        Write32(i + 0x28, a[0]);
        Write32(i + 0x2c, keyboard ? 2 : 1);   // native device identity
        Write32(i + 0x34, 1);
        dinput_vxd.instances[dinput_vxd.count++] = i;
        Write32(out, i);
        return 0;
    }
    case 0x108:
        // 10020441: the buttons DINPUT read with GetAsyncKeyState, as long as
        // no instance holds the mouse.
        if (!dinput_vxd.acquired && dinput_vxd.state) Write32(dinput_vxd.state + 12, Read32(in));
        return 0;
    case 0x10a:
        // Original DINPUT.VXD object : clear the 256 keys and
        // retain Kana/Caps Lock toggle state until the first acquisition.
        if (!dinput_vxd.keyboard_acquired && dinput_vxd.keyboard_state)
        {
            unsigned char keys[256] = {};
            const unsigned toggles = Read32(in + 4);
            if (toggles & 1) keys[0x70] = 0x80;
            if (toggles & 2) keys[0x3a] = 0x80;
            Store(dinput_vxd.keyboard_state, keys, sizeof keys);
        }
        return 0;
    case 0x10f:
        if (!dinput_vxd.sequence)
        {
            dinput_vxd.sequence = Allocate(4);
            if (!dinput_vxd.sequence) return 8;
            Write32(dinput_vxd.sequence, 0);
        }
        Write32(out, dinput_vxd.sequence);
        return 0;
    case 0x111:
        Write32(out, 3);   // SM_CMOUSEBUTTONS of this machine (src/user_settings.h)
        return 0;
    case 0x110:
    {
        // The axes VJOYD reports for one joystick identifier, four masks:
        // position, velocity, acceleration and force. DINPUT.DLL 4.08 builds
        // the objects of the device from them; 10000h in the first
        // says that position axes exist at all, the low bits name X, Y, Z,
        // R, U and V, and 40h the point-of-view hat. Without an answer the DLL
        // takes a mask out of the registry that lacks 10000h, and
        // the joystick has no axes. The host's controllers come from SDL.
        AH_Status status{};
        const unsigned id = Read32(in);
        if (!HostDevices::current || HostDevices::current->Status(status) || id >= status.joysticks) return 50;
        const auto &joy = status.joystick[id];
        unsigned mask = 0x10000;
        for (unsigned axis = 0; axis < joy.axes && axis < 6; ++axis) mask |= 1u << axis;
        if (joy.pov != ~0u) mask |= 0x40;
        const unsigned answer[4] = {mask, 0, 0, 0};
        Store(out, answer, sizeof answer);
        return 0;
    }
    default:
        // Joystick and the VJOYD services: not provided, the DLL
        // falls back to its emulation.
        return 50;
    }
}

void Application::DinputCloseEvent(unsigned ring0)
{
    if ((ring0 & 0xff000000u) != 0x8e000000u) return;
    const unsigned object = (ring0 & 0xffffffu) - 1;
    if (object < KernelObjectSlots && events[object].references) --events[object].references;
}

// 10000576 and 100004eb: the packet of VMD, added to the state, then every
// object whose value changed goes to each instance that holds the mouse.
void Application::DinputMouse(int dx, int dy, int dz, unsigned buttons)
{
    if (!dinput_vxd.state || !dinput_vxd.formatted || state == Failed || state == Exited) return;
    unsigned char staging[StateSize];
    Read(dinput_vxd.state, staging, StateSize);
    int axes[3];
    memcpy(axes, staging, sizeof axes);
    axes[0] += dx; axes[1] += dy; axes[2] += dz;
    memcpy(staging, axes, sizeof axes);
    staging[12] = (buttons & 1) ? 0x80 : 0;
    staging[13] = (buttons & 2) ? 0x80 : 0;
    staging[14] = (buttons & 4) ? 0x80 : 0;
    for (unsigned n = 15; n < StateSize; ++n) staging[n] = 0;
    unsigned sequence = 0;
    if (dinput_vxd.sequence) { sequence = Read32(dinput_vxd.sequence) + 1; Write32(dinput_vxd.sequence, sequence); }
    const unsigned time = (unsigned)(Now() / 1000);
    for (unsigned offset = 0; offset < StateSize;)
    {
        const unsigned type = dinput_vxd.format[offset];
        unsigned value = 0, change = 0;
        unsigned step = 1;
        bool changed = false;
        if (type & 0x13)
        {
            step = 4;
            unsigned now = 0;
            memcpy(&now, staging + offset, 4);
            const unsigned before = Read32(dinput_vxd.state + offset);
            if (now != before)
            {
                Write32(dinput_vxd.state + offset, now);
                value = now;
                change = (type & 0x10) ? now : now - before;
                changed = true;
            }
        }
        else if (type & 0x0c)
        {
            unsigned char before = 0;
            Read(dinput_vxd.state + offset, &before, 1);
            const unsigned char now = staging[offset] & 0x80;
            if (now != before)
            {
                Store(dinput_vxd.state + offset, &now, 1);
                value = change = now;
                changed = true;
            }
        }
        if (changed)
            for (unsigned n = 0; n < dinput_vxd.count; ++n)
            {
                const unsigned i = dinput_vxd.instances[n];
                const unsigned flags = Read32(i);
                if (!(flags & 2)) continue;
                if (Read32(i + 0x2c) != 1) continue;
                DinputEvent(i, offset, (flags & 4) ? change : value, sequence, time);
            }
        offset += step;
    }
}

// The ring protocol is shared by VMD mouse packets and VKD keyboard packets.
void Application::DinputEvent(unsigned i, unsigned offset, unsigned value, unsigned sequence, unsigned time)
{
    if (const unsigned event = Read32(i + 0x30); (event & 0xff000000u) == 0x8e000000u)
    {
        const unsigned object = (event & 0xffffffu) - 1;
        if (object < KernelObjectSlots) events[object].signaled = true;
    }
    const unsigned head = Read32(i + 0x10), table = Read32(i + 0x3c);
    if (!head || !table || offset >= Read32(i + 0x38)) return;
    const unsigned mapped = Read32(table + offset * 4);
    if (mapped == ~0u) return;
    Write32(head, mapped);
    Write32(head + 4, value);
    Write32(head + 8, time);
    Write32(head + 12, sequence);
    unsigned next = head + 16;
    if (next >= Read32(i + 0x0c)) next = Read32(i + 8);
    if (next == Read32(i + 0x14)) Write32(i + 0x18, Read32(i + 0x18) | 1);
    else Write32(i + 0x10, next);
}

void Application::DinputKey(unsigned scan, bool down)
{
    if (!dinput_vxd.keyboard_state || scan >= 256 || state == Failed || state == Exited) return;
    unsigned char before = 0, value = down ? 0x80 : 0;
    Read(dinput_vxd.keyboard_state + scan, &before, 1);
    if (before == value) return; // VKD state transitions, not typematic repeats.
    Store(dinput_vxd.keyboard_state + scan, &value, 1);
    unsigned sequence = 0;
    if (dinput_vxd.sequence) { sequence = Read32(dinput_vxd.sequence) + 1; Write32(dinput_vxd.sequence, sequence); }
    const unsigned time = (unsigned)(Now() / 1000);
    for (unsigned n = 0; n < dinput_vxd.count; ++n)
    {
        const unsigned i = dinput_vxd.instances[n];
        if (Read32(i + 0x2c) == 2 && (Read32(i) & 2)) DinputEvent(i, scan, value, sequence, time);
    }
}

bool Application::DinputKeyboard(unsigned scan, bool down)
{
    bool exclusive = false;
    for (auto *process : session_processes)
        if (process)
        {
            process->DinputKey(scan, down);
            exclusive |= process->dinput_vxd.keyboard_exclusive != 0;
        }
    return exclusive;
}

// The host packet, for every process of the session: VMD reports the
// movement to the one DINPUT.VXD of the machine.
bool Application::DinputMouseExclusive()
{
    // DIOC Acquire/Unacquire already maintains this count from the original
    // VxD instance flags. The host must not infer this hold from cursor warps:
    // exclusive input deliberately leaves the Windows pointer stationary.
    for (auto *process : session_processes)
        if (process && (process->state == Running || process->state == Idle) && process->dinput_vxd.exclusive)
            return true;
    return false;
}

bool Application::DinputPointer(int x, int y, unsigned buttons, const Gdi::POINT *motion)
{
    // VMD receives mouse mickeys before display scaling and ClipCursor.
    // Absolute-only producers retain their previous-position fallback.
    const int dx = motion ? motion->x : have_last ? x - last_x : 0;
    const int dy = motion ? motion->y : have_last ? y - last_y : 0;
    last_x = x; last_y = y; have_last = true;
    bool exclusive = false;
    for (auto *process : session_processes)
        if (process)
        {
            process->DinputMouse(dx, dy, 0, buttons);
            exclusive |= process->dinput_vxd.exclusive != 0;
        }
    return exclusive;
}

// `units` of the Z axis, 120 a notch.
bool Application::DinputWheel(int units)
{
    bool exclusive = false;
    for (auto *process : session_processes)
        if (process)
        {
            process->DinputMouse(0, 0, units, User::PhysicalMouseButtons());
            exclusive |= process->dinput_vxd.exclusive != 0;
        }
    return exclusive;
}

// The next packet of the host is a place, not a movement (the end of a pause
// of the machine).
void Application::DinputForget() { have_last = false; }

void Application::DinputWarp(int x, int y)
{
    last_x = x; last_y = y; have_last = true;
}
} // namespace X86
