// SPDX-License-Identifier: GPL-2.0-or-later
// Native serial provider: two in-memory ports (COM1, COM2) joined by a
// null-modem cable. The KERNEL32 ring-3 contract comes from win98_comm.h; this
// file replaces VCOMM and SERIAL.VXD, which Win98 reaches through VxDCall.
//
#include "x86_comm_state.h"
#include "win98_serialui.h"
#include "win98_serial_driver.h"
namespace X86
{
Application::CommOwner::~CommOwner() { delete state; }
namespace
{
bool DcbAcceptable(const unsigned char *dcb)
{
    return Win98SerialDriver::Accept(dcb);
}
} // namespace
// COM1..COM9 with or without a trailing colon, optionally behind \\.\.
unsigned Application::CommPort(unsigned pointer)
{
    if (!pointer) return 0;
    const char *s = ReadString(pointer);
    if (s[0] == '\\' && s[1] == '\\' && s[2] == '.' && s[3] == '\\') s += 4;
    if ((s[0] | 32) != 'c' || (s[1] | 32) != 'o' || (s[2] | 32) != 'm' || s[3] < '1' || s[3] > '9') return 0;
    if (s[4] && (s[4] != ':' || s[5])) return 0;
    return s[3] - '0';
}
// Fetches what has arrived at the host endpoint of an attached port into its
// receive queue, with the events a byte and a changed input line raise. What
// does not fit stays with the host: back-pressure, not CE_RXOVER.
void Application::CommPump()
{
    const CommHost::Provider *host = CommHost::Current();
    if (!comm.state || !host) return;
    auto &s = *comm.state;
    if (state == Exited || state == Failed)
    {
        // A process that ended without CloseHandle: its end frees the port.
        for (unsigned i = 0; i < 2; ++i)
            if (s.host[i]) { host->Close(host->owner, i, s.holder); s.host[i] = false; }
        return;
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        auto &p = s.ports[i];
        if (!s.host[i] || !p.open) continue;
        unsigned raised = 0;
        while (p.rx_count < p.rx_capacity)
        {
            unsigned char chunk[256];
            const unsigned room = p.rx_capacity - p.rx_count;
            const unsigned got = host->Read(host->owner, i, chunk, room < sizeof chunk ? room : sizeof chunk);
            if (!got) break;
            for (unsigned k = 0; k < got; ++k)
            {
                p.rx[(p.rx_first + p.rx_count) % p.rx_capacity] = chunk[k];
                ++p.rx_count;
                if (chunk[k] == p.dcb[24]) raised |= 2;
            }
            raised |= 1;
        }
        const unsigned lines = host->Lines(host->owner, i), changed = lines ^ p.modem;
        p.modem = lines;
        if (changed & 0x10) raised |= 8;
        if (changed & 0x20) raised |= 0x10;
        if (changed & 0x80) raised |= 0x20;
        p.events |= raised & p.mask;
        const unsigned pending = p.events & p.mask;
        if (p.pending_pointer && pending)
        {
            Write32(p.pending_pointer, pending);
            p.events &= ~pending;
            auto *h = FindKernelHandle(p.pending_event);
            if (h && h->kind == 1) events[h->object].signaled = true;
            p.pending_pointer = p.pending_event = 0;
        }
    }
}
bool Application::CommReady(unsigned thread) const
{
    const auto &t = threads[thread];
    if (!comm.state) return true;
    const_cast<Application *>(this)->CommPump();
    const unsigned i = comm.state->Index(t.wait_handle);
    if (i == ~0u) return true;
    const auto &p = comm.state->ports[i];
    return t.block == 14 ? p.rx_count || p.abort_read : (p.events & p.mask) || p.abort_wait;
}
unsigned Application::CommApi(ApiName name, const unsigned *a)
{
    const bool dialog_gate = API_IS(name, "CommConfigDialogA") && TakeFresh();
    // A handle of the parallel port goes to its own provider.
    // The configuration calls take a port name, not a handle.
    if (!API_IS(name, "CreateFileA") && !API_IS(name, "GetDefaultCommConfigA") &&
        !API_IS(name, "SetDefaultCommConfigA") && !API_IS(name, "CommConfigDialogA") && LptHandle(a[0]))
        return LptApi(name, a);
    using namespace Win98Comm;
    if (!comm.state) comm.state = new CommState;
    auto &s = *comm.state;
    const auto bad = [&](unsigned error) { last_error = error; return 0u; };
    const auto signal = [&](unsigned handle, bool on) {
        auto *h = FindKernelHandle(handle);
        if (h && h->kind == 1) events[h->object].signaled = on;
    };
    const CommHost::Provider *host = CommHost::Current();
    CommPump();
    const auto modem_status = [&](unsigned i) {
        if (s.host[i]) return host->Lines(host->owner, i);
        if (s.host[i ^ 1]) return 0u; // the peer went to the host: no cable here
        const auto &peer = s.ports[i ^ 1];
        return peer.open ? (peer.rts ? 0x10u : 0) | (peer.dtr ? 0xa0u : 0) : 0u;
    };
    const auto deliver = [&](CommState::Port &p) {
        if (!p.pending_pointer) return;
        const unsigned pending = p.events & p.mask;
        if (!pending) return;
        Write32(p.pending_pointer, pending);
        p.events &= ~pending;
        signal(p.pending_event, true);
        p.pending_pointer = p.pending_event = 0;
    };
    const auto raise = [&](unsigned i, unsigned flag) {
        auto &p = s.ports[i];
        if (!p.open || !(p.mask & flag)) return;
        p.events |= flag;
        deliver(p);
    };
    const auto update_modem = [&](unsigned i) {
        // Called for the far end whenever the near end's output lines moved.
        if (s.host[i ^ 1])
        {
            const auto &near = s.ports[i ^ 1];
            host->Control(host->owner, i ^ 1, near.dtr, near.rts, near.brk);
        }
        auto &p = s.ports[i];
        const unsigned now = modem_status(i), changed = now ^ p.modem;
        p.modem = now;
        if (changed & 0x10) raise(i, 8);
        if (changed & 0x20) raise(i, 0x10);
        if (changed & 0x80) raise(i, 0x20);
    };
    const auto apply_lines = [&](unsigned i) {
        auto &p = s.ports[i];
        const unsigned flags = Win98CommParse::Get(p.dcb + 8), dtr = (flags >> 4) & 3, rts = (flags >> 12) & 3;
        p.dtr = dtr == 1 || dtr == 2;
        p.rts = rts == 1 || rts == 2;
        update_modem(i ^ 1);
    };
    const auto transmit = [&](unsigned i, unsigned char byte) {
        if (s.host[i]) { host->Write(host->owner, i, &byte, 1); return; }
        if (s.host[i ^ 1]) return;
        auto &peer = s.ports[i ^ 1];
        if (!peer.open) return;
        if (peer.rx_count < peer.rx_capacity)
        {
            peer.rx[(peer.rx_first + peer.rx_count) % peer.rx_capacity] = byte;
            ++peer.rx_count;
            raise(i ^ 1, 1);
            if (byte == peer.dcb[24]) raise(i ^ 1, 2);
        }
        else peer.errors |= 1; // CE_RXOVER
    };
    if (API_IS(name,"CreateFileA"))
    {
        const unsigned number = CommPort(a[0]);
        const auto invalid = [&](unsigned error) { last_error = error; return Files::Invalid; };
        if (a[6] || (a[5] & ~0x5a0000a7u)) return invalid(120);
        if (number > 2) return invalid(2); // only COM1 and COM2 exist in the installed hive and here
        auto &p = s.ports[number - 1];
        if (p.open) return invalid(5);
        s.host[number - 1] = false;
        if (CommHost::Attached(number - 1))
        {
            // One UART, one driver: another process or a DOS machine may hold it.
            s.holder = CommHost::ProcessHolder(process_id);
            if (!host->Open(host->owner, number - 1, s.holder)) return invalid(5);
            s.host[number - 1] = true;
        }
        if (!p.rx) p.rx = new unsigned char[p.rx_capacity];
        p.open = true;
        p.generation = p.generation % 65535 + 1;
        p.overlapped = (a[5] & 0x40000000u) != 0;
        p.mask = p.events = p.errors = p.rx_first = p.rx_count = 0;
        p.brk = p.abort_read = p.abort_wait = false;
        p.wait_pointer = p.pending_pointer = p.pending_event = 0;
        memset(p.timeouts, 0, sizeof p.timeouts);
        // Initial DCB: the port driver's "DCB" value from the installed hive
        // (Services\Class\Ports\000n), otherwise 9600 8N1 binary.
        static const unsigned char fallback[28] = {28, 0, 0, 0, 0x80, 0x25, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0x11, 0x13, 0, 0, 0, 0, 0};
        memcpy(p.dcb, fallback, 28);
        struct Reg
        {
            Application &app;
            bool OpenKey(unsigned parent, const char *path, unsigned &key)
            {
                unsigned handle = 0; bool created = false;
                if (app.registry.Open(parent, path, false, handle, created, "")) return false;
                key = handle; return true;
            }
            bool EnumKey(unsigned key, unsigned index, char *out, unsigned capacity)
            {
                unsigned node = 0, child = 0;
                if (app.registry.Key(key, node) || app.registry.store.EnumKey(node, index, child)) return false;
                const char *n = app.registry.store.nodes[child].name;
                if (Length(n) + 1 > capacity) return false;
                Copy(out, n, capacity); return true;
            }
            bool QueryString(unsigned key, const char *value, char *out, unsigned capacity)
            {
                unsigned node = 0; const Registry::Value *v = nullptr;
                if (app.registry.Key(key, node) || app.registry.store.Query(node, value, v) || v->size + 1 > capacity) return false;
                memcpy(out, v->data, v->size); out[v->size] = 0; return true;
            }
            // The installed hive stores the port DCB without its trailing
            // wReserved1 word (26 bytes); shorter tails are zero-filled.
            bool QueryBinary(unsigned key, const char *value, unsigned char *out, unsigned size)
            {
                unsigned node = 0; const Registry::Value *v = nullptr;
                if (app.registry.Key(key, node) || app.registry.store.Query(node, value, v) || v->size < size - 2 || v->size > size) return false;
                memset(out, 0, size); memcpy(out, v->data, v->size); return true;
            }
            void CloseKey(unsigned key) { app.registry.Close(key); }
        } reg{*this};
        char port_name[8] = {'C', 'O', 'M', char('0' + number), 0};
        unsigned char stored[28];
        struct DcbResolver
        {
            Reg &reg; unsigned char *out;
            bool operator()(const char *driver, unsigned) const
            {
                char path[NameCapacity + 40] = "System\\CurrentControlSet\\Services\\Class\\";
                Copy(path + 40, driver, NameCapacity);
                unsigned key = 0;
                if (!reg.OpenKey(0x80000002u, path, key)) return false;
                const bool found = reg.QueryBinary(key, "DCB", out, 28);
                reg.CloseKey(key); return found;
            }
        } resolver{reg, stored};
        if (Walk(reg, port_name, resolver) && Win98CommParse::Get(stored) == 28 && DcbAcceptable(stored)) Win98SerialDriver::SetDcb(p.dcb, stored);
        Win98SerialDriver::Normalize(p.dcb);
        const unsigned i = number - 1;
        p.modem = modem_status(i);
        apply_lines(i);
        update_modem(i);
        return s.Handle(i);
    }
    const unsigned index = s.Index(a[0]);
    if (API_IS(name,"CloseHandle"))
    {
        if (index == ~0u) return bad(6);
        auto &p = s.ports[index];
        p.open = false;
        p.generation = p.generation % 65535 + 1;
        if (s.host[index])
        {
            host->Close(host->owner, index, s.holder); // sends what is queued, then hangs up
            s.host[index] = false;
        }
        update_modem(index ^ 1);
        return 1;
    }
    if (API_IS(name,"GetFileType")) return index == ~0u ? bad(6) : 2;
    if (API_IS(name,"FlushFileBuffers"))
    {
        // Returns when everything written is on the wire; an endpoint that
        // takes nothing holds it for a second at most.
        if (index == ~0u) return bad(6);
        if (s.host[index]) host->Drain(host->owner, index, 1000);
        return 1;
    }
    if (API_IS(name,"GetOverlappedResult"))
    {
        if (index == ~0u) return bad(6);
        if (!virtual_memory.Access(a[1], 20, false) || !virtual_memory.Access(a[2], 4, true)) return bad(87);
        unsigned status = Read32(a[1]);
        if (status == 997)
        {
            const unsigned event = a[3] ? Read32(a[1] + 16) : 0;
            if (!event) return bad(996);
            const unsigned wait[2] = {event, ~0u};
            if (ThreadApi("WaitForSingleObject", wait) == ~0u) return 0;
            status = Read32(a[1]);
        }
        Write32(a[2], Read32(a[1] + 4));
        return status ? bad(status) : 1;
    }
    const bool reading = API_IS(name,"ReadFile"), writing = API_IS(name,"WriteFile");
    if (reading || writing)
    {
        if (index == ~0u) return bad(6);
        auto *p = &s.ports[index];
        if (a[3]) { if (!virtual_memory.Access(a[3], 4, true)) return bad(87); Write32(a[3], 0); }
        if (!a[3] && !a[4]) return bad(87);
        if ((a[4] != 0) != p->overlapped) return bad(87); // ERROR_INVALID_PARAMETER for the mismatched OVERLAPPED use
        if (a[4] && !virtual_memory.Access(a[4], 20, true)) return bad(87);
        if (a[2] > 8u * 1024 * 1024) return bad(8);
        if (a[2] && !virtual_memory.Access(a[1], a[2], reading)) return bad(998);
        unsigned done = 0;
        if (writing)
        {
            for (; done < a[2]; ++done) { unsigned char byte; Read(a[1] + done, &byte, 1); transmit(index, byte); }
            raise(index, 4);
        }
        else if (a[2])
        {
            const unsigned interval = p->timeouts[0], multiplier = p->timeouts[1], constant = p->timeouts[2];
            const bool immediate = interval == ~0u && !multiplier && !constant;
            const bool partial = interval == ~0u && multiplier == ~0u && constant && constant != ~0u;
            unsigned long long total = multiplier || constant ? Now() + ((unsigned long long)multiplier * a[2] + constant) * 1000 : 0, last = 0;
            if (partial) total = 0;
            for (;;)
            {
                while (done < a[2] && p->rx_count)
                {
                    Store(a[1] + done++, p->rx + p->rx_first, 1);
                    p->rx_first = (p->rx_first + 1) % p->rx_capacity;
                    --p->rx_count;
                    last = Now();
                }
                if (done == a[2] || immediate || (partial && done)) break;
                unsigned long long deadline = total;
                if (done && interval && interval != ~0u)
                {
                    const unsigned long long gap = last + (unsigned long long)interval * 1000; // measured from the last byte
                    deadline = deadline && deadline < gap ? deadline : gap;
                }
                if (deadline && Now() >= deadline) break;
                auto &t = threads[current_thread];
                t.block = 14; t.wait_handle = a[0]; t.deadline = deadline;
                Suspend(Idle);
                t.block = 0; t.deadline = 0;
                if (state == Exited || state == Failed) return bad(995);
                // The slot can change while waiting if another thread closes
                // and reopens the port. Use the validated index returned by
                // the check.
                const unsigned live = s.Index(a[0]);
                if (live == ~0u) return bad(6);
                p = &s.ports[live];

                if (p->abort_read) { p->abort_read = false; break; }
            }
        }
        if (a[3]) Write32(a[3], done);
        if (a[4]) { Write32(a[4], 0); Write32(a[4] + 4, done); signal(Read32(a[4] + 16), true); }
        return 1;
    }
    // Ring-3 environment of the original exports over this provider.
    struct Environment
    {
        Application &app; CommState &s;
        decltype(signal) &signal; decltype(raise) &raise; decltype(update_modem) &update_modem;
        decltype(apply_lines) &apply_lines; decltype(transmit) &transmit; decltype(deliver) &deliver;
        decltype(modem_status) &modem_status;
        bool Access(unsigned p, unsigned n, bool write) { return app.virtual_memory.Access(p, n, write); }
        unsigned Object(unsigned handle) { const unsigned i = s.Index(handle); return i == ~0u ? 0 : i + 1; }
        void SetError(unsigned e) { app.last_error = e; }
        unsigned Read32(unsigned p) { return app.Read32(p); }
        void Write32(unsigned p, unsigned v) { app.Write32(p, v); }
        bool Lookup(unsigned handle, unsigned &object, bool &overlapped, unsigned &event)
        {
            const unsigned i = s.Index(handle);
            if (i == ~0u) return false;
            object = i + 1; overlapped = s.ports[i].overlapped; event = CommState::OwnEvent | i; return true;
        }
        unsigned EventObject(unsigned handle) { auto *h = app.FindKernelHandle(handle); return h && h->kind == 1 ? handle : 0; }
        void Wait(unsigned event)
        {
            // VCOMM completes the port's own event when a masked event arrives.
            if ((event & 0xfff00000u) != CommState::OwnEvent) return;
            unsigned i = event & 0xf;
            if (i >= 2 || !s.ports[i].open) return;
            const unsigned handle = s.Handle(i);
            for (;;)
            {
                auto &p = s.ports[i];
                if (p.abort_wait) { p.abort_wait = false; app.Write32(p.wait_pointer, 0); break; }
                const unsigned pending = p.events & p.mask;
                if (pending) { app.Write32(p.wait_pointer, pending); p.events &= ~pending; break; }
                auto &t = app.threads[app.current_thread];
                t.block = 15; t.wait_handle = handle; t.deadline = 0;
                app.Suspend(Application::Idle);
                t.block = 0;
                if (app.state == Application::Exited || app.state == Application::Failed) break;
                i = s.Index(handle);
                if (i == ~0u) break;
            }
        }
        bool Service(unsigned service, const unsigned *a, unsigned count)
        {
            if (service == ServiceResetEvent || service == ServiceSetEvent)
            {
                if ((a[0] & 0xfff00000u) != CommState::OwnEvent) signal(a[0], service == ServiceSetEvent);
                return true;
            }
            const unsigned i = a[0] - 1;
            auto &p = s.ports[i];
            switch (service)
            {
            case ServiceSetupComm:
                if (a[1] > (1u << 20) || a[2] > (1u << 20)) return false;
                if (a[1] && a[1] != p.rx_capacity)
                {
                    auto *fresh = new unsigned char[a[1]];
                    const unsigned keep = p.rx_count < a[1] ? p.rx_count : a[1];
                    for (unsigned n = 0; n < keep; ++n) fresh[n] = p.rx[(p.rx_first + n) % p.rx_capacity];
                    delete[] p.rx; p.rx = fresh; p.rx_capacity = a[1]; p.rx_first = 0; p.rx_count = keep;
                }
                if (a[2]) p.tx_capacity = a[2];
                return true;
            case ServiceEscape:
                switch (a[1])
                {
                case 1: case 2: case 7: return true;
                case 3: p.rts = true; break;
                case 4: p.rts = false; break;
                case 5: p.dtr = true; break;
                case 6: p.dtr = false; break;
                case 12:
                    if (!p.brk) { p.brk = true; auto &peer = s.ports[i ^ 1]; if (peer.open) { peer.errors |= 0x10; raise(i ^ 1, 0x40); } }
                    return true;
                case 13: p.brk = false; return true;
                default: return false;
                }
                update_modem(i ^ 1);
                return true;
            case ServiceGetMask: app.Write32(a[1], p.mask); return true;
            case ServiceGetProperties:
            {
                unsigned char prop[64];
                Win98SerialDriver::Properties(prop, p.rx_capacity, p.tx_capacity);
                p.errors = 0;
                return app.Store(a[1], prop, sizeof prop);
            }
            case ServiceGetState: p.errors = 0; return app.Store(a[1], p.dcb, 26);
            case ServiceGetTimeouts: return app.Store(a[1], p.timeouts, 20);
            case ServicePurge:
                if (a[1] & ~15u) return false;
                if (a[1] & 8)
                {
                    p.rx_first = p.rx_count = 0;
                    // What has arrived at the host and was not fetched yet goes too.
                    if (s.host[i])
                    {
                        const CommHost::Provider *host = CommHost::Current();
                        unsigned char waste[256];
                        while (host->Read(host->owner, i, waste, sizeof waste)) {}
                    }
                }
                if (a[1] & 2) p.abort_read = true;
                return true;
            case ServiceSetMask:
                if (a[1] & ~0x1ffu) return false;
                p.mask = a[1];
                p.events &= a[1];
                if (p.pending_pointer) { app.Write32(p.pending_pointer, 0); signal(p.pending_event, true); p.pending_pointer = p.pending_event = 0; }
                p.abort_wait = true;
                return true;
            case ServiceSetState:
            {
                unsigned char dcb[28];
                if (!app.Read(a[1], dcb, 28) || !DcbAcceptable(dcb)) return false;
                Win98SerialDriver::SetDcb(p.dcb, dcb);
                apply_lines(i);
                if (s.host[i])
                {
                    const CommHost::Provider *host = CommHost::Current();
                    host->Configure(host->owner, i, Win98CommParse::Get(p.dcb + 4), p.dcb[18], p.dcb[19], p.dcb[20]);
                }
                return true;
            }
            case ServiceSetTimeouts: return app.Read(a[1], p.timeouts, 20);
            case ServiceTransmitChar: transmit(i, (unsigned char)a[1]); raise(i, 4); return true;
            case ServiceWaitEvent:
            {
                if (!p.mask) return false;
                const unsigned pending = p.events & p.mask;
                if (pending) { app.Write32(a[1], pending); p.events &= ~pending; return true; }
                if (a[2] && (a[3] & 0xfff00000u) != CommState::OwnEvent) { p.pending_pointer = a[1]; p.pending_event = a[3]; }
                else p.wait_pointer = a[1];
                p.abort_wait = false;
                return true;
            }
            case ServiceModemStatus: app.Write32(a[1], modem_status(i)); return true;
            case ServiceClearError:
            {
                app.Write32(a[2], p.errors); p.errors = 0;
                if (a[1])
                {
                    const CommHost::Provider *host = CommHost::Current();
                    const unsigned stat[3] = {0, p.rx_count, s.host[i] ? host->Pending(host->owner, i) : 0};
                    app.Store(a[1], stat, 12);
                }
                return true;
            }
            case ServiceLastError: return true;
            case ServiceGetConfig:
            {
                if (!Access(a[2], 4, true)) return false;
                if (app.Read32(a[2]) < 52) { app.Write32(a[2], 52); return false; }
                if (!Access(a[1], 52, true)) return false;
                unsigned char cc[52] = {52, 0, 0, 0, 1, 0, 0, 0};
                memcpy(cc + 8, p.dcb, 28);
                cc[36] = 1; // PST_RS232
                app.Write32(a[2], 52);
                return app.Store(a[1], cc, 52);
            }
            case ServiceSetConfig:
            {
                unsigned char cc[52];
                if (a[2] < 52 || !app.Read(a[1], cc, 52) || Win98CommParse::Get(cc) < 52 || !DcbAcceptable(cc + 8)) return false;
                Win98SerialDriver::SetDcb(p.dcb, cc + 8);
                apply_lines(i);
                return true;
            }
            }
            return false;
        }
    } environment{*this, s, signal, raise, update_modem, apply_lines, transmit, deliver, modem_status};
    static const char *const names[] = {"SetupComm", "ClearCommBreak", "EscapeCommFunction", "PurgeComm", "SetCommBreak", "SetCommMask", "TransmitCommChar",
                                        "ClearCommError", "GetCommMask", "GetCommProperties", "GetCommModemStatus", "GetCommState", "GetCommTimeouts",
                                        "SetCommState", "SetCommTimeouts", "WaitCommEvent", "GetCommConfig", "SetCommConfig",
                                        "GetDefaultCommConfigA", "SetDefaultCommConfigA", "CommConfigDialogA"};
    unsigned id = 0;
    while (id < Count && strcmp(names[id], name)) ++id;
    if(id==Count){
        const char*direct[]={"SerialUiDialog16","SerialUiGet16","SerialUiSet16","SerialUiFifo16","SerialUiEnum16"};
        for(unsigned i=0;i<5;++i)if(!strcmp(name,direct[i])){id=Count+1+i;break;}
        if(id==Count){Fail(name);return 0;}
    }
    if (id < WaitCommEvent) return Win98Comm::Thin(environment, id, a);
    if (id == WaitCommEvent) return Win98Comm::Wait(environment, a);
    if (id == GetCommConfig || id == SetCommConfig) return Win98Comm::Config(environment, id, a);
    // GetDefaultCommConfigA / SetDefaultCommConfigA / CommConfigDialogA read
    // lpCC->dwProviderSubType unguarded; an unreadable structure
    // would fault in Win98 and reports 87 here.
    const unsigned config = id == CommConfigDialogA ? a[2] : a[1];
    if (id<Count && (!a[0] || !virtual_memory.Access(config, 0x28, false))) return bad(87);
    struct Driver
    {
        Application &app;
        unsigned load_error = 0;
        bool dialog_gate = false;
        unsigned loaded_module = 0;
        bool OpenKey(unsigned parent, const char *path, unsigned &key)
        {
            unsigned handle = 0; bool created = false;
            if (app.registry.Open(parent, path, false, handle, created, "")) return false;
            key = handle; return true;
        }
        bool EnumKey(unsigned key, unsigned index, char *out, unsigned capacity)
        {
            unsigned node = 0, child = 0;
            if (app.registry.Key(key, node) || app.registry.store.EnumKey(node, index, child)) return false;
            const char *n = app.registry.store.nodes[child].name;
            if (Length(n) + 1 > capacity) return false;
            Copy(out, n, capacity); return true;
        }
        bool QueryString(unsigned key, const char *value, char *out, unsigned capacity)
        {
            unsigned node = 0; const Registry::Value *v = nullptr;
            if (app.registry.Key(key, node) || app.registry.store.Query(node, value, v) || v->size + 1 > capacity) return false;
            memcpy(out, v->data, v->size); out[v->size] = 0; return true;
        }
        void CloseKey(unsigned key) { app.registry.Close(key); }
        static bool SerialUi(const char *path)
        {
            return Same(path, "SERIALUI.DLL") || Same(path, "C:\\WINDOWS\\SYSTEM\\SERIALUI.DLL");
        }
        unsigned ModuleType(const char *path) { return SerialUi(path) ? 0 : 8; }
        unsigned Read32(unsigned p) { return app.Read32(p); }
        void Write32(unsigned p, unsigned v) { app.Write32(p, v); }
        void Write16(unsigned p, unsigned v) { unsigned short w = v; app.Store(p, &w, 2); }
        // SERIALUI searches in three separate passes and checks Class.
        bool Resolve(unsigned name, unsigned &instance, unsigned &driver,const char*field=nullptr)
        {
            char requested[256]; Copy(requested, app.ReadString(name), sizeof requested);
            for (unsigned pass = 0; pass < (field?1u:3u); ++pass)
            {
                unsigned root = 0;
                if (!OpenKey(0x80000002u, "Enum", root)) return false;
                char part[256], value[256];
                bool found = false;
                for (unsigned i = 0; !found && EnumKey(root, i, part, sizeof part); ++i)
                {
                    unsigned bus = 0;
                    if (!OpenKey(root, part, bus)) continue;
                    for (unsigned j = 0; !found && EnumKey(bus, j, part, sizeof part); ++j)
                    {
                        unsigned device = 0;
                        if (!OpenKey(bus, part, device)) continue;
                        for (unsigned k = 0; !found && EnumKey(device, k, part, sizeof part); ++k)
                        {
                            unsigned key = 0;
                            if (!OpenKey(device, part, key)) continue;
                            if (QueryString(key, "Class", value, sizeof value) && Same(value, pass == 2 ? "Modem" : "ports") &&
                                QueryString(key, pass ? "PortName" : field?field:"FriendlyName", value, sizeof value) && Same(value, requested) &&
                                QueryString(key, "Driver", value, sizeof value))
                            {
                                char path[300] = "System\\CurrentControlSet\\Services\\Class\\";
                                Copy(path + 40, value, sizeof path - 40);
                                unsigned handle = 0;
                                if (OpenKey(0x80000002u, path, handle))
                                {
                                    app.registry.Key(key, instance);
                                    app.registry.Key(handle, driver);
                                    CloseKey(handle);
                                    found = true;
                                }
                            }
                            CloseKey(key);
                        }
                        CloseKey(device);
                    }
                    CloseKey(bus);
                }
                CloseKey(root);
                if (found) return true;
            }
            return false;
        }
        unsigned QueryDcb(unsigned driver, unsigned destination, unsigned &bytes)
        {
            const Registry::Value *value = nullptr;
            unsigned error = app.registry.store.Query(driver, "DCB", value);
            if (error) return error;
            const unsigned capacity = bytes;
            bytes = value->size;
            if (!destination) return 0;
            if (capacity < bytes) return 234;
            return app.Store(destination, value->data, bytes) ? 0 : 87;
        }
        unsigned SetDcb(unsigned driver, unsigned source, unsigned bytes)
        {
            unsigned char dcb[28];
            if (bytes != sizeof dcb || !app.Read(source, dcb, bytes)) return 87;
            return app.registry.store.Set(driver, "DCB", 3, dcb, bytes);
        }
        void Profile(unsigned instance, unsigned config)
        {
            const Registry::Value *value = nullptr;
            if (app.registry.store.Query(instance, "PortName", value) || value->size > 8) return;
            char port[12] = {};
            memcpy(port, value->data, value->size);
            unsigned n = Length(port); port[n++] = ':'; port[n] = 0;
            unsigned char dcb[28];
            if (!app.Read(config + 8, dcb, sizeof dcb)) return;
            char text[64];
            snprintf(text, sizeof text, "%d,%c,%u,%s,%c", int(Win98CommParse::Get(dcb + 4)),
                     Win98SerialUi::Parity(dcb[19]), unsigned(dcb[18]), Win98SerialUi::StopBits(dcb[20]),
                     Win98SerialUi::Flow(Win98CommParse::Get(dcb + 8)));
            unsigned section = Guest("ports"), key = Guest(port), data = Guest(text);
            if (section && key && data)
            {
                unsigned args[3] = {section, key, 0};
                app.ProfileApi("WriteProfileStringA", args);
                args[2] = data;
                app.ProfileApi("WriteProfileStringA", args);
                // USER.604 BroadcastSystemMessage, BSF_NOHANG, BSM_APPLICATIONS.
                // The existing native broadcast provider delivers synchronously.
                app.BroadcastMessage(1, 0x1a, 0, section);
            }
            if (data) app.Free(data);
            if (key) app.Free(key);
            if (section) app.Free(section);
        }
        unsigned Guest(const char *text)
        {
            const unsigned p = app.Allocate(Length(text) + 1);
            if (p) app.Store(p, text, Length(text) + 1);
            return p;
        }
        unsigned LoadLibrary(const char *path)
        {
            const unsigned p = Guest(path);
            if (!p) { load_error = 8; return 0; }
            const unsigned saved = app.last_error;
            app.last_error = 0;
            const unsigned args[1] = {p};
            const unsigned module = app.ModuleApi("LoadLibraryA", args);
            load_error = module ? 0 : app.last_error ? app.last_error : 126;
            app.last_error = saved;
            app.Free(p);
            loaded_module = module;
            return module;
        }
        unsigned ProcAddress(unsigned module, const char *entry)
        {
            const unsigned p = Guest(entry);
            if (!p) return 0;
            const unsigned args[2] = {module, p};
            const unsigned result = app.ModuleApi("GetProcAddress", args);
            app.Free(p);
            return result;
        }
        unsigned CallDriver(unsigned procedure, unsigned x, unsigned y, unsigned z)
        {
            const unsigned args[3] = {x, y, z};
            const bool kept = dialog_gate && app.PushHostFrame(Application::HostCommConfig, loaded_module);
            if (kept) app.GiveTail(Application::TailRecord);
            const unsigned result = app.Call(procedure, args, 3);
            if (kept) app.PopHostFrame();
            return result;
        }
        unsigned Thunk16(unsigned mode, const char *path, const char *, unsigned x, unsigned y, unsigned z, unsigned w)
        {
            if (!SerialUi(path)) return load_error ? load_error : 126;
            if (mode == 1) return Win98SerialUi::Get(*this, x, y, z);
            if (mode == 2) return Win98SerialUi::Set(*this, x, y, w);
            if (!x || !y) return 87;
            if (app.Read32(y) < 52) return 122;
            unsigned instance = 0, key = 0;
            if (!Resolve(x, instance, key)) return 1010;
            const bool kept = dialog_gate && app.PushHostFrame(Application::HostCommConfig, loaded_module);
            if (kept) app.GiveTail(Application::TailFresh);
            const unsigned result = app.SerialUiDialog(x, w, y, instance);
            if (kept) app.PopHostFrame();
            return result;
        }
        void FreeLibrary(unsigned module) { const unsigned args[1] = {module}; app.ModuleApi("FreeLibrary", args); }
        void SetError(unsigned e) { app.last_error = e; }
    } driver{*this};
    driver.dialog_gate = dialog_gate;
    if(id==Count+1)return driver.Thunk16(0,"SERIALUI.DLL","",a[0],a[2],0,a[1]);
    if(id==Count+2)return Win98SerialUi::Get(driver,a[0],a[1],a[2]);
    if(id==Count+3)return Win98SerialUi::Set(driver,a[0],a[1],a[2]);
    if(id==Count+4){unsigned instance=0,key=0;if(!a[0]||!driver.Resolve(a[0],instance,key))return ~0u;return SerialUiAdvanced(a[1],a[0],instance);}
    if(id==Count+5){
        // SERIALUI resolves DEVICE_INFO.DeviceDesc at byte 6.
        unsigned instance=0,key=0;if(!a[0]||!driver.Resolve(a[0]+6,instance,key,"DeviceDesc"))return 0;
        const Registry::Value*value=nullptr;
        if(registry.store.Query(instance,"PortName",value)||!value->size||value->size>256)return 0;
        unsigned memory=Allocate(320);if(!memory)return 0;
        Store(memory,value->data,value->size);unsigned char zero=0;Store(memory+value->size,&zero,1);
        Write32(memory+312,52);
        unsigned result=0;
        if(!Win98SerialUi::Get(driver,memory,memory+260,memory+312))
            result=SerialUiEnum(memory,instance,memory+260,a[1],a[2]);
        Free(memory);return result;
    }
    return Win98Comm::Dispatch(driver, id, ReadString(a[0]), a);
}
unsigned Application::FinishCommConfigDialog(unsigned result, unsigned module)
{
    if (result) last_error = result;
    const unsigned answer = result ? 0 : 1;
    if (module) ModuleApi("FreeLibrary", &module);
    return answer;
}
} // namespace X86
