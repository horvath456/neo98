// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_keys.h"

// New input producer. Win98Keys supplies the query/copy routines.
// The runtime currently has one USER queue, so there is one delivered state.
struct InputState : Win98Keys::Environment
{
    unsigned char physical[256] = {}, delivered[256] = {}, asynchronous[96] = {};
    bool Down(unsigned key) const { return physical[key & 255] & 128; }
    bool Transition(unsigned key, bool down)
    {
        key &= 255;
        const bool was = Down(key);
        auto &value = physical[key];
        value = (value & 127) | (down ? 128 : 0);
        if (down && !was && (key == 0x14 || key == 0x90 || key == 0x91))
            value ^= 1;
        auto &packed = asynchronous[key / 4];
        const unsigned bit = 1u << ((key & 3) * 2);
        packed = (packed & ~bit) | (down ? bit : 0);
        if (down && !was)
            asynchronous[64 + key / 8] |= 1u << (key & 7);
        return was;
    }
    void Deliver(const unsigned char *snapshot)
    {
        for (unsigned i = 0; i < 256; ++i)
            delivered[i] = snapshot[i];
    }
    unsigned TaskToken() const override { return 0; }
    // No Win16 keyboard driver is loaded. Character translation is a separate
    // native input adapter; SetKeyboardState does not change USB LEDs or latches.
    unsigned ToAsciiZero(const unsigned char *) override { return 0; }
    unsigned Key(unsigned key) const { return Win98Keys::Signed(Win98Keys::Key(delivered, key)); }
    unsigned Async(unsigned key) { return Win98Keys::Signed(Win98Keys::Async(asynchronous, key)); }
    unsigned Copy(unsigned char *buffer, bool set) { return Win98Keys::Copy(*this, delivered, buffer, set); }
};
