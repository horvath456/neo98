// SPDX-License-Identifier: GPL-2.0-or-later
// The far end of the parallel port. Under Win98 the spooler writes a job to
// LPT1 through VCOMM and the port driver LPT.VXD, and the bytes leave the
// machine on the cable. In this port the cable ends in a host provider: the
// SDL host and the test runners install a sink that collects the bytes of a
// job and turns them into a file on the Mac (tools/printer_port_host.h).
// src/x86_kernel_lpt.cpp is the VCOMM side. Without a sink the port behaves
// like a port without a printer: writes do not complete and ClearCommError
// reports CE_DNS, device not selected.
#pragma once
namespace PrinterPort
{
struct Sink
{
    virtual ~Sink() = default;
    // A handle to LPTn was opened; answers false when the port has no device.
    virtual bool Open(unsigned port) = 0;
    virtual bool Write(unsigned port, const unsigned char *bytes, unsigned count) = 0;
    // The handle was closed: one job, or one run of a program that wrote to
    // the port itself, is complete.
    virtual void Close(unsigned port) = 0;
};
inline Sink *&Slot()
{
    static Sink *sink = nullptr;
    return sink;
}
inline void Install(Sink *sink) { Slot() = sink; }
inline Sink *Installed() { return Slot(); }
} // namespace PrinterPort
