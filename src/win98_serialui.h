// SPDX-License-Identifier: GPL-2.0-or-later
// Serial-port property parsing, validation, and configuration behavior.
#pragma once
namespace Win98SerialUi {
// E supplies guest memory, registry lookup/query/write and profile notification.
// Resolve follows : ports/FriendlyName, ports/PortName, Modem/PortName.
template<class E> unsigned Get(E &e, unsigned name, unsigned config, unsigned size) {
    if (!name || !config || !size) return 87;
    if (e.Read32(size) < 52) { e.Write32(size, 52); return 122; }
    unsigned instance = 0, driver = 0;
    if (!e.Resolve(name, instance, driver)) return 1010;
    e.Write32(size, 52);
    e.Write32(config, 52);
    e.Write16(config + 4, 1);
    e.Write32(config + 36, 1);
    e.Write32(config + 40, 0);
    e.Write32(config + 44, 0);
    unsigned bytes = 0;
    if (e.QueryDcb(driver, 0, bytes)) return 1010;
    if (bytes > 28) return 1009;
    if (e.QueryDcb(driver, config + 8, bytes)) return 1010;
    return e.Read32(config + 8) == 28 ? 0 : 1009;
}
template<class E> unsigned Set(E &e, unsigned name, unsigned config, unsigned /*size*/) {
    if (!name || !config) return 87;
    // The original tests COMMCONFIG.dwSize, not the dwSize argument.
    if (e.Read32(config) < 52) return 122;
    unsigned instance = 0, driver = 0;
    if (!e.Resolve(name, instance, driver)) return 1010;
    const unsigned result = e.SetDcb(driver, config + 8, 28);
    // Original continues the profile update even when RegSetValueEx fails.
    e.Profile(instance, config);
    return result;
}
// NE : WIN.INI [ports] text. Invalid parity/stop bits use index zero.
inline char Parity(unsigned value) { return "noems"[value < 5 ? value : 0]; }
inline const char *StopBits(unsigned value) {
    return value == 1 ? "1.5" : value == 2 ? "2" : "1";
}
inline char Flow(unsigned flags) {
    return (flags & 0x104) == 0x100 ? 'x' : (flags & 0x104) == 4 ? 'p' : ' ';
}
}
