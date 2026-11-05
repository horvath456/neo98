// SPDX-License-Identifier: GPL-2.0-or-later
// The host side of the serial ports: what stands behind COM1 to COM4 of the
// simulated machine.
// Under Windows 98 a COM port is a 16550 that SERIAL.VXD drives for VCOMM, and
// VCD hands the same chip to a DOS box that touches its registers. This port
// keeps the Win32 provider (src/x86_kernel_comm.inc) and the UART of the DOS
// machine (tools/dosbox_pure_serial.inc) and replaces the cable: a host
// installs an object of this interface, and the bytes of an attached port go
// to its endpoint, a TCP connection or a serial device of the Mac
// (tools/host_comm.h has the POSIX one). Without a provider, and for a port
// the provider has no endpoint for, nothing changes: COM1 and COM2 stay the
// in-memory null-modem pair and the DOS UARTs stay unconnected.
// One port has one holder at a time, as one UART has one driver: a Win32
// process or a DOS machine. Open refuses a port somebody else holds. The
// functions may be called from the thread of a DOS machine as well as from
// the main thread; a provider serialises them itself.
#pragma once
namespace CommHost
{
constexpr unsigned PortCapacity = 4;
// Who holds a port: a Win32 process of the session or a DOS machine.
inline unsigned long long ProcessHolder(unsigned process) { return 1ull << 32 | process; }
inline unsigned long long DosHolder(unsigned machine) { return 2ull << 32 | machine; }
// The input lines as GetCommModemStatus and the 16550 name them.
enum : unsigned
{
    Cts = 0x10,
    Dsr = 0x20,
    Ring = 0x40,
    Carrier = 0x80,
};
struct Provider
{
    void *owner = nullptr;
    // Whether the host has an endpoint for the port (0 is COM1).
    bool (*Attached)(void *owner, unsigned port) = nullptr;
    // Takes the port for `holder` and connects its endpoint. False only when
    // somebody else holds the port. An endpoint that does not answer is a
    // port with no cable: the open succeeds, what is sent is lost, nothing
    // arrives, and the provider keeps looking for the endpoint.
    bool (*Open)(void *owner, unsigned port, unsigned long long holder) = nullptr;
    // Sends what is still queued, closes the connection and frees the port.
    // Ignored when `holder` does not hold it.
    void (*Close)(void *owner, unsigned port, unsigned long long holder) = nullptr;
    // What has arrived, at most `capacity` bytes; never waits.
    unsigned (*Read)(void *owner, unsigned port, unsigned char *out, unsigned capacity) = nullptr;
    // Takes all bytes: what the endpoint does not accept at once is queued.
    void (*Write)(void *owner, unsigned port, const unsigned char *data, unsigned count) = nullptr;
    // The bytes queued and not yet handed to the endpoint.
    unsigned (*Pending)(void *owner, unsigned port) = nullptr;
    // Waits until the queue is empty, at most `milliseconds`.
    void (*Drain)(void *owner, unsigned port, unsigned milliseconds) = nullptr;
    // The input lines of the port, Cts to Carrier.
    unsigned (*Lines)(void *owner, unsigned port) = nullptr;
    // The output lines and the break state.
    void (*Control)(void *owner, unsigned port, bool dtr, bool rts, bool line_break) = nullptr;
    // The line format: parity 0 none, 1 odd, 2 even, 3 mark, 4 space; stop
    // bits 0 one, 1 one and a half, 2 two (the DCB's numbers).
    void (*Configure)(void *owner, unsigned port, unsigned baud, unsigned data_bits, unsigned parity,
                      unsigned stop_bits) = nullptr;
};
inline const Provider *&Slot()
{
    static const Provider *provider = nullptr;
    return provider;
}
inline void SetProvider(const Provider *provider) { Slot() = provider; }
inline const Provider *Current() { return Slot(); }
inline bool Attached(unsigned port)
{
    const Provider *p = Current();
    return p && port < PortCapacity && p->Attached && p->Attached(p->owner, port);
}
} // namespace CommHost
