// SPDX-License-Identifier: GPL-2.0-or-later
// Keyboard, mouse and touch for the KMS host (tools/neo98_kms.cpp), read
// directly through libinput on seat0. The user needs access to
// /dev/input/event* (the group input).
#pragma once
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <libinput.h>
#include <libudev.h>
#include <linux/input-event-codes.h>
#include <string>
#include <unistd.h>
namespace HostKms
{
// evdev key codes as USB HID usages (the scan codes UsbKeys::Map takes).
inline unsigned Usage(unsigned code)
{
    static const unsigned char table[128] = {
        0,    0x29, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e, 0x2a, 0x2b,  // 0-15
        0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c, 0x12, 0x13, 0x2f, 0x30, 0x28, 0xe0, 0x04, 0x16,  // 16-31
        0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33, 0x34, 0x35, 0xe1, 0x31, 0x1d, 0x1b, 0x06, 0x19,  // 32-47
        0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xe5, 0x55, 0xe2, 0x2c, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,  // 48-63
        0x3f, 0x40, 0x41, 0x42, 0x43, 0x53, 0x47, 0x5f, 0x60, 0x61, 0x56, 0x5c, 0x5d, 0x5e, 0x57, 0x59,  // 64-79
        0x5a, 0x5b, 0x62, 0x63, 0,    0,    0x64, 0x44, 0x45, 0,    0,    0,    0,    0,    0,    0,     // 80-95
        0x58, 0xe4, 0x54, 0x46, 0xe6, 0,    0x4a, 0x52, 0x4b, 0x50, 0x4f, 0x4d, 0x51, 0x4e, 0x49, 0x4c,  // 96-111
        0,    0,    0,    0,    0,    0,    0,    0x48, 0,    0,    0,    0,    0,    0xe3, 0xe7, 0x65}; // 112-127
    return code < 128 ? table[code] : 0;
}

// What the host does with the input: a key (USB usage, down), the pointer
// moved by or to a point of the screen, the buttons (1 left, 2 right,
// 4 middle) and wheel notches (positive away from the user).
struct Sink
{
    std::function<void(unsigned usage, bool down)> key;
    std::function<void(double dx, double dy)> move;
    std::function<void(double x, double y)> place;
    std::function<void(unsigned buttons)> buttons;
    std::function<void(int notches)> wheel;
};

class Input
{
    udev *devices = nullptr;
    libinput *context = nullptr;
    unsigned held = 0;
    int touch_slot = -1;
    double wheel_rest = 0;

    static int OpenRestricted(const char *path, int flags, void *)
    {
        const int fd = open(path, flags | O_CLOEXEC);
        return fd < 0 ? -errno : fd;
    }
    static void CloseRestricted(int fd, void *) { close(fd); }
    static constexpr libinput_interface interface{OpenRestricted, CloseRestricted};

  public:
    unsigned width = 0, height = 0;  // the screen, for absolute devices
    ~Input()
    {
        if (context) libinput_unref(context);
        if (devices) udev_unref(devices);
    }
    bool Open(std::string &error)
    {
        devices = udev_new();
        if (!devices) { error = "udev is not available"; return false; }
        context = libinput_udev_create_context(&interface, nullptr, devices);
        if (!context || libinput_udev_assign_seat(context, "seat0")) { error = "libinput cannot open seat0"; return false; }
        libinput_dispatch(context);
        return true;
    }
    int Fd() const { return context ? libinput_get_fd(context) : -1; }
    // Counts the devices libinput opened, for the message when there is none.
    unsigned keyboards = 0, pointers = 0;
    void Dispatch(const Sink &sink)
    {
        if (!context) return;
        libinput_dispatch(context);
        while (libinput_event *event = libinput_get_event(context))
        {
            switch (libinput_event_get_type(event))
            {
            case LIBINPUT_EVENT_DEVICE_ADDED:
            {
                auto *device = libinput_event_get_device(event);
                if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_KEYBOARD)) ++keyboards;
                if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_POINTER) ||
                    libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_TOUCH)) ++pointers;
                break;
            }
            case LIBINPUT_EVENT_KEYBOARD_KEY:
            {
                auto *k = libinput_event_get_keyboard_event(event);
                const unsigned usage = Usage(libinput_event_keyboard_get_key(k));
                if (usage && sink.key) sink.key(usage, libinput_event_keyboard_get_key_state(k) == LIBINPUT_KEY_STATE_PRESSED);
                break;
            }
            case LIBINPUT_EVENT_POINTER_MOTION:
            {
                auto *p = libinput_event_get_pointer_event(event);
                if (sink.move) sink.move(libinput_event_pointer_get_dx(p), libinput_event_pointer_get_dy(p));
                break;
            }
            case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
            {
                auto *p = libinput_event_get_pointer_event(event);
                if (sink.place) sink.place(libinput_event_pointer_get_absolute_x_transformed(p, width), libinput_event_pointer_get_absolute_y_transformed(p, height));
                break;
            }
            case LIBINPUT_EVENT_POINTER_BUTTON:
            {
                auto *p = libinput_event_get_pointer_event(event);
                const unsigned code = libinput_event_pointer_get_button(p);
                const unsigned bit = code == BTN_LEFT ? 1 : code == BTN_RIGHT ? 2 : code == BTN_MIDDLE ? 4 : 0;
                if (libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED) held |= bit;
                else held &= ~bit;
                if (bit && sink.buttons) sink.buttons(held);
                break;
            }
            case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
            {
                auto *p = libinput_event_get_pointer_event(event);
                if (!libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) break;
                // 120 per notch; down is positive in libinput, away from the user in Windows.
                wheel_rest -= libinput_event_pointer_get_scroll_value_v120(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL) / 120.0;
                const int notches = int(wheel_rest);
                wheel_rest -= notches;
                if (notches && sink.wheel) sink.wheel(notches);
                break;
            }
            // The first finger is the left button; the tablet has no other.
            case LIBINPUT_EVENT_TOUCH_DOWN:
            case LIBINPUT_EVENT_TOUCH_MOTION:
            {
                auto *t = libinput_event_get_touch_event(event);
                const int slot = libinput_event_touch_get_seat_slot(t);
                if (touch_slot < 0 && libinput_event_get_type(event) == LIBINPUT_EVENT_TOUCH_DOWN) touch_slot = slot;
                if (slot != touch_slot) break;
                if (sink.place) sink.place(libinput_event_touch_get_x_transformed(t, width), libinput_event_touch_get_y_transformed(t, height));
                if (libinput_event_get_type(event) == LIBINPUT_EVENT_TOUCH_DOWN) { held |= 1; if (sink.buttons) sink.buttons(held); }
                break;
            }
            case LIBINPUT_EVENT_TOUCH_UP:
            case LIBINPUT_EVENT_TOUCH_CANCEL:
            {
                auto *t = libinput_event_get_touch_event(event);
                if (libinput_event_get_type(event) == LIBINPUT_EVENT_TOUCH_UP && libinput_event_touch_get_seat_slot(t) != touch_slot) break;
                touch_slot = -1;
                held &= ~1u;
                if (sink.buttons) sink.buttons(held);
                break;
            }
            default:
                break;
            }
            libinput_event_destroy(event);
        }
    }
};
}
