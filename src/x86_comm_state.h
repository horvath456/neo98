// SPDX-License-Identifier: GPL-2.0-or-later
// State of the native COM1/COM2 null-modem provider.
#pragma once
#include "x86_app.h"
#include "comm_host.h"
namespace X86
{
struct Application::CommState
{
    struct Port
    {
        bool open = false, overlapped = false, rts = false, dtr = false, brk = false, abort_read = false, abort_wait = false;
        unsigned generation = 1, mask = 0, events = 0, errors = 0, modem = 0;
        unsigned char dcb[28] = {};
        unsigned timeouts[5] = {};
        unsigned char *rx = nullptr;
        unsigned rx_capacity = 4096, rx_first = 0, rx_count = 0, tx_capacity = 4096;
        unsigned wait_pointer = 0, pending_pointer = 0, pending_event = 0; // WaitCommEvent bookkeeping
        ~Port() { delete[] rx; }
    } ports[2];
    // A port whose bytes go to the host endpoint (src/comm_host.h) instead of
    // its in-memory peer, and who holds it there for this process.
    bool host[2] = {};
    unsigned long long holder = 0;
    ~CommState()
    {
        const CommHost::Provider *provider = CommHost::Current();
        for (unsigned i = 0; i < 2; ++i)
            if (host[i] && provider) provider->Close(provider->owner, i, holder);
    }
    static constexpr unsigned Prefix = 0x67000000u, OwnEvent = 0x67e00000u;
    unsigned Index(unsigned handle) const
    {
        const unsigned i = (handle & 255) - 1;
        return (handle & 0xff000000u) == Prefix && i < 2 && ports[i].open && ports[i].generation == ((handle >> 8) & 65535) ? i : ~0u;
    }
    unsigned Handle(unsigned i) const { return Prefix | (ports[i].generation << 8) | (i + 1); }
};
} // namespace X86
