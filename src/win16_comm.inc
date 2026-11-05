// SPDX-License-Identifier: GPL-2.0-or-later
// The communication functions of USER.EXE for 16-bit code, ordinals 200 to
// 215. Included by src/win16_user.cpp.
//
// In Win98 three layers answer them: USER.EXE segment 22 checks the
// arguments and keeps its own list of open ports, COMM.DRV (a thin 16-bit
// driver) keeps the DCB of every port and forwards to VCOMM through the
// protected-mode entry of VxD 2Bh (int 2Fh 1684h), and VCOMM drives the
// port. USER and COMM.DRV are 16-bit modules and VCOMM a VxD, so all three
// are native here; the port itself is the null-modem provider of
// src/x86_kernel_comm.inc, reached through its KERNEL32 contract. The places
// in the tree:
//
// USER OpenComm parses the name, Windows 98 builds the
// DCB, then COMM.3 SETQUE and COMM.1 INICOM
// USER SetCommState COMM.2 SETCOM
// USER GetCommState copies 25 bytes from COMM.15 GETDCB
// USER ReadComm the ungot character, then COMM.20
// READCOMMSTRING
// USER WriteComm COMM.19 COMMWRITESTRING; a short write
// answers the negative count
// USER CloseComm COMM.7 TRMCOM
// USER GetCommError COMM.8 STACOM; the ungot character counts
// in cbInQue
// USER UngetCommChar one character per port, in USER's entry
// USER BuildCommDCB the Windows 3.1 parser, all in USER
// USER.. TransmitCommChar, SetCommEventMask, GetCommEventMask,
// SetCommBreak, ClearCommBreak, FlushComm and
// EscapeCommFunction go straight to COMM.6 and COMM.9 to 14
//
// COMM.DRV answers 8000h for a port it does not have open; USER turns that into -1 where it checks (CloseComm) and passes it
// through elsewhere. What is not carried: the ports are per process (USER's
// list is per system), LPT1 to LPT3 through OpenComm, the CN_RECEIVE reason
// of EnableCommNotification (USER.245) and the event word is brought up to date by the calls of this
// file instead of by VCOMM at the moment the event happens.
namespace
{
constexpr int IeBadId = -1, IeOpen = -2, IeNotOpen = -3, IeMemory = -4, IeDefault = -5, IeHardware = -10,
              IeByteSize = -11, IeBaudRate = -12;
constexpr unsigned CommNotOpen = 0x8000; // COMM.DRV for a closed port

// USER: "COMn", "AUX[n]", "LPTn", "PRN[n]", an optional colon, the
// end of the string. COMn is n - 1, LPTn is 80h + n - 1; -1 for anything else.
int Win16CommId(const char *s)
{
    const char *model = nullptr;
    int base = 0;
    bool required = true;
    const char first = (char)(s[0] >= 'a' ? s[0] - 0x20 : s[0]);
    switch (first)
    {
    case 'A': model = "AUX1"; required = false; break;
    case 'C': model = "COM1"; break;
    case 'L': model = "LPT1"; base = 0x80; break;
    case 'P': model = "PRN1"; base = 0x80; required = false; break;
    default: return -1;
    }
    for (; *model != '1'; ++model, ++s)
    {
        const char c = (char)(*s >= 'a' ? *s - 0x20 : *s);
        if (c != *model) return -1;
    }
    int number = 0;
    bool digits = false;
    for (; *s >= '0'; ++s)
    {
        if (*s > '9' || number < 0) break;
        number = number * 10 + (*s - '0');
        digits = true;
    }
    if (required && !digits) return -1;
    if (number > 0x80) return -1;
    if (digits) --number;
    if (*s == ':') ++s;
    if (number < 0 || *s) return -1;
    return number + base;
}

// USER: the next token of a device string into `out`, upper case,
// ended by blank, colon or comma, with the blanks behind it skipped. The
// answer is where the rest starts: the end of the string when the token ran
// to it, null when only blanks follow the delimiter. A null or empty source
// answers null and leaves `out` as it was.
const char *Win16CommToken(const char *s, char (&out)[80])
{
    if (!s || !*s) return nullptr;
    unsigned n = 0;
    for (;;)
    {
        const char c = *s;
        if (!c)
        {
            out[n] = 0;
            return s;
        }
        ++s;
        if (c == ' ' || c == ':' || c == ',') break;
        if (n + 1 < sizeof out) out[n++] = (char)(c >= 'a' ? c - 0x20 : c);
    }
    out[n] = 0;
    while (*s == ' ') ++s;
    return *s ? s : nullptr;
}

unsigned Word16(const unsigned char *p) { return p[0] | p[1] << 8; }
void PutWord16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }

// USER, BuildCommDCB. 0 or -1; the DCB is cleared first.
int Win16BuildCommDcb(const char *text, unsigned char (&dcb)[25])
{
    memset(dcb, 0, sizeof dcb);
    char token[80] = {};
    const char *rest = Win16CommToken(text, token);
    const int id = Win16CommId(token);
    if (id == -1) return -1;
    dcb[0] = (unsigned char)id;
    if (id & 0x80) return 0;
    rest = Win16CommToken(rest, token);
    if (strlen(token) < 2) return -1;
    static const struct { char a, b; unsigned rate; } rates[] = {
        {'1', '1', 110},  {'1', '2', 1200}, {'1', '5', 150},  {'1', '9', 19200}, {'2', '4', 2400},
        {'3', '0', 300},  {'4', '8', 4800}, {'6', '0', 600},  {'9', '6', 9600},
    };
    unsigned rate = 0;
    for (const auto &r : rates)
        if (token[0] == r.a && token[1] == r.b) rate = r.rate;
    if (!rate) return -1;
    PutWord16(dcb + 1, rate);
    PutWord16(dcb + 16, 10); // XonLim
    PutWord16(dcb + 18, 10); // XoffLim
    dcb[12] |= 1;            // fBinary
    dcb[14] = 0x11;          // XonChar
    dcb[15] = 0x13;          // XoffChar
    // Every further token is read only when the call that cut it found
    // something : a trailing delimiter ends the string before it.
    // Parity: E (also empty) 2, M 3, N 0, O 1, S 4.
    if (!(rest = Win16CommToken(rest, token))) return 0;
    switch (token[0])
    {
    case 0: case 'E': dcb[4] = 2; break;
    case 'M': dcb[4] = 3; break;
    case 'N': dcb[4] = 0; break;
    case 'O': dcb[4] = 1; break;
    case 'S': dcb[4] = 4; break;
    default: return -1;
    }
    // Data bits: 7 (also empty) or 8.
    if (!(rest = Win16CommToken(rest, token))) return 0;
    if (token[0] == '8') dcb[3] = 8;
    else if (!token[0] || token[0] == '7') dcb[3] = 7;
    else return -1;
    // Stop bits: 1 is ONESTOPBIT, 2 TWOSTOPBITS; empty is two at 110 baud.
    if (!(rest = Win16CommToken(rest, token))) return 0;
    if (token[0] == '2' || (!token[0] && rate == 110)) dcb[5] = 2;
    else if (!token[0] || token[0] == '1') dcb[5] = 0;
    else return -1;
    // "P": no timeouts on the modem lines.
    if (!Win16CommToken(rest, token)) return 0;
    if (token[0] != 'P') return -1;
    PutWord16(dcb + 6, 0xffff);
    PutWord16(dcb + 8, 0xffff);
    PutWord16(dcb + 10, 0xffff);
    return 0;
}

// The Win16 CBR_ codes (FF10h and up) as the rate they stand for, or the
// number itself below FF00h; zero for a code Windows does not define.
unsigned Win16CommRate(unsigned value)
{
    if (value < 0xff00) return value;
    switch (value)
    {
    case 0xff10: return 110;   case 0xff11: return 300;   case 0xff12: return 600;
    case 0xff13: return 1200;  case 0xff14: return 2400;  case 0xff15: return 4800;
    case 0xff16: return 9600;  case 0xff17: return 14400; case 0xff18: return 19200;
    case 0xff1b: return 38400; case 0xff1f: return 56000; case 0xff23: return 128000;
    case 0xff27: return 256000;
    default: return 0;
    }
}

// The 16-bit DCB as the 28 bytes of the 32-bit one, the conversion VCOMM
// makes for its protected-mode callers. The line timeouts and TxDelay have no
// place there. 0 or the IE_ code.
int Win16CommDcb32(const unsigned char (&d)[25], unsigned char (&out)[28])
{
    const unsigned rate = Win16CommRate(Word16(d + 1));
    if (!rate) return IeBaudRate;
    if (d[3] < 4 || d[3] > 8) return IeByteSize;
    if (d[4] > 4 || d[5] > 2) return IeDefault;
    memset(out, 0, sizeof out);
    const auto put32 = [&](unsigned at, unsigned v) {
        out[at] = (unsigned char)v; out[at + 1] = (unsigned char)(v >> 8);
        out[at + 2] = (unsigned char)(v >> 16); out[at + 3] = (unsigned char)(v >> 24);
    };
    put32(0, 28);
    put32(4, rate);
    const unsigned a = d[12], b = d[13];
    unsigned bits = 0;
    if (a & 0x01) bits |= 1u << 0;  // fBinary
    if (a & 0x04) bits |= 1u << 1;  // fParity
    if (a & 0x08) bits |= 1u << 2;  // fOutxCtsFlow
    if (a & 0x10) bits |= 1u << 3;  // fOutxDsrFlow
    const unsigned dtr = (a & 0x80) ? 0 : (b & 0x20) ? 2 : 1; // fDtrDisable, fDtrflow
    bits |= dtr << 4;
    if (b & 0x01) bits |= 1u << 8;  // fOutX
    if (b & 0x02) bits |= 1u << 9;  // fInX
    if (b & 0x04) bits |= 1u << 10; // fPeChar: fErrorChar
    if (b & 0x08) bits |= 1u << 11; // fNull
    const unsigned rts = (a & 0x02) ? 0 : (b & 0x40) ? 2 : 1; // fRtsDisable, fRtsflow
    bits |= rts << 12;
    put32(8, bits);
    out[14] = d[16]; out[15] = d[17]; // XonLim
    out[16] = d[18]; out[17] = d[19]; // XoffLim
    out[18] = d[3];                   // ByteSize
    out[19] = d[4];                   // Parity
    out[20] = d[5];                   // StopBits
    out[21] = d[14];                  // XonChar
    out[22] = d[15];                  // XoffChar
    out[23] = d[20];                  // ErrorChar (PeChar)
    out[24] = d[21];                  // EofChar
    out[25] = d[22];                  // EvtChar
    return 0;
}
} // namespace

// The event word of COMM.DRV for port `cid` ( hands out its far
// address): the masked events the provider holds, which GetCommEventMask
// takes back out. Brought up to date here, on every call of this family.
unsigned Application::Win16CommEventWord(unsigned cid, bool far_pointer)
{
    auto &machine = *win16.state;
    if (!machine.comm_event_block)
    {
        machine.comm_event_block = Win16GlobalAlloc(Win98NeTask::ZeroInit, 2 * Win16Machine::CommPorts);
        if (!machine.comm_event_block) return 0;
    }
    Win16Block *block = Win16GlobalBlock(machine.comm_event_block);
    if (!block || cid >= Win16Machine::CommPorts) return 0;
    const auto &port = machine.comm_ports[cid];
    unsigned events = 0;
    if (port.open && comm.state)
    {
        const unsigned i = comm.state->Index(port.handle);
        if (i != ~0u) events = comm.state->ports[i].events & 0xffff;
    }
    const unsigned char word[2] = {(unsigned char)events, (unsigned char)(events >> 8)};
    Store(block->memory + cid * 2, word, 2);
    return far_pointer ? (block->selector << 16) | (cid * 2) : events;
}

bool Application::Win16CommCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words)
{
    auto &machine = *win16.state;
    const auto port_of = [&](unsigned cid) -> Win16Machine::CommPort * {
        cid &= 0xff;
        return cid < Win16Machine::CommPorts && machine.comm_ports[cid].open ? &machine.comm_ports[cid] : nullptr;
    };
    const auto word = [](int v) { return (unsigned)v & 0xffff; };
    // The 32-bit contract of the provider with a guest block for the
    // structures it fills.
    const auto call = [&](const char *name, unsigned a0, unsigned a1 = 0, unsigned a2 = 0, unsigned a3 = 0,
                          unsigned a4 = 0, unsigned a5 = 0, unsigned a6 = 0) {
        const unsigned a[] = {a0, a1, a2, a3, a4, a5, a6};
        return Api(name, a);
    };
    const auto set_state = [&](Win16Machine::CommPort &port, const unsigned char (&dcb)[25]) -> int {
        unsigned char wide[28];
        if (const int error = Win16CommDcb32(dcb, wide)) return error;
        const unsigned block = Allocate(28);
        if (!block) return IeMemory;
        Store(block, wide, 28);
        const unsigned done = call("SetCommState", port.handle, block);
        Free(block);
        if (!done) return IeDefault;
        memcpy(port.dcb, dcb, sizeof port.dcb);
        return 0;
    };
    // WM_COMMNOTIFY (44h) for a port with EnableCommNotification: the port
    // in wParam, the reasons in lParam. The provider writes at once, so the
    // transmit queue is below any threshold as soon as a write is done
    // (CN_TRANSMIT, 2); CN_EVENT (4) goes out when the event word holds an
    // event. CN_RECEIVE would need a watcher on the line and is not sent: a
    // program that waits for it alone sees no input (the note at the top).
    const auto notify = [&](unsigned cid, unsigned reasons) {
        const unsigned events = Win16CommEventWord(cid, false);
        if (cid >= Win16Machine::CommPorts) return;
        const auto &port = machine.comm_ports[cid];
        if (!port.open || !port.notify_window) return;
        if (events) reasons |= 4;
        if (!reasons) return;
        const unsigned a[] = {port.notify_window, 0x44, cid, reasons};
        Api("PostMessageA", a);
    };
    result = 0;
    switch (ordinal)
    {
    case 245: // EnableCommNotification(idComDev, hwnd, cbWriteNotify, cbOutQueue)
    {
        words = 4;
        auto *port = port_of(frame.Word(3));
        if (!port) return true; // FALSE for a port that is not open
        port->notify_window = frame.Word(2);
        port->notify_receive = (short)frame.Word(1);
        port->notify_transmit = (short)frame.Word(0);
        result = 1;
        return true;
    }
    case 200: // OpenComm(lpszDevControl, cbInQueue, cbOutQueue)
    {
        words = 4;
        const unsigned name = frame.Pointer(2), in_queue = frame.Word(1), out_queue = frame.Word(0);
        char text[80] = {};
        if (name) snprintf(text, sizeof text, "%s", ReadString(name));
        const int cid = Win16CommId(text);
        if (cid < 0) { result = word(IeBadId); return true; }
        if (cid < (int)Win16Machine::CommPorts && machine.comm_ports[cid].open) { result = word(IeOpen); return true; }
        // a serial port needs both queues.
        if (!(cid & 0x80) && (!in_queue || !out_queue)) { result = word(IeMemory); return true; }
        // LPT through OpenComm is not carried (see the head of this file).
        if (cid >= (int)Win16Machine::CommPorts) { result = word(IeBadId); return true; }
        // the DCB of the name, else the one of "COM1:9600,E,7,1";
        // the identifier is the port's either way.
        unsigned char dcb[25];
        if ((strlen(text) < 4 || Win16BuildCommDcb(text, dcb) == -1) &&
            Win16BuildCommDcb("COM1:9600,E,7,1", dcb) == -1)
        {
            result = word(IeDefault);
            return true;
        }
        dcb[0] = (unsigned char)cid;
        char device[8] = {'C', 'O', 'M', char('1' + cid), 0};
        if (cid == 9) memcpy(device, "COM10", 6);
        const unsigned path = String(device);
        last_error = 0;
        const unsigned handle = path ? call("CreateFileA", path, 0xc0000000u, 0, 0, 3, 0, 0) : 0xffffffffu;
        if (path) Free(path);
        if (handle == 0xffffffffu || !handle)
        {
            result = word(last_error == 5 ? IeOpen : last_error == 2 ? IeBadId : IeHardware);
            return true;
        }
        auto &port = machine.comm_ports[cid];
        port = {};
        port.handle = handle;
        port.open = true;
        call("SetupComm", handle, in_queue, out_queue);
        // ReadComm and GetCommError never wait: every read answers at once.
        const unsigned timeouts = Allocate(20);
        if (timeouts)
        {
            const unsigned values[5] = {0xffffffffu, 0, 0, 0, 0};
            Store(timeouts, values, sizeof values);
            call("SetCommTimeouts", handle, timeouts);
            Free(timeouts);
        }
        if (const int error = set_state(port, dcb))
        {
            call("CloseHandle", handle);
            port = {};
            result = word(error);
            return true;
        }
        result = (unsigned)cid;
        return true;
    }
    case 201: // SetCommState(lpDCB)
    {
        words = 2;
        const unsigned at = frame.Pointer(0);
        unsigned char dcb[25] = {};
        if (!at || !Read(at, dcb, sizeof dcb)) { result = 0xffff; return true; }
        auto *port = port_of(dcb[0]);
        if (!port) { result = word(IeNotOpen); return true; }
        result = word(set_state(*port, dcb));
        return true;
    }
    case 202: // GetCommState(idComDev, lpDCB)
    {
        words = 3;
        auto *port = port_of(frame.Word(2));
        const unsigned at = frame.Pointer(0);
        if (!port) { result = word(IeNotOpen); return true; }
        if (!at) { result = 0xffff; return true; }
        Store(at, port->dcb, sizeof port->dcb);
        return true;
    }
    case 203: // GetCommError(idComDev, lpStat)
    {
        words = 3;
        const unsigned cid = frame.Word(2), at = frame.Pointer(0);
        auto *port = port_of(cid);
        if (!port) { result = CommNotOpen; return true; }
        const unsigned block = Allocate(16);
        if (!block) { result = CommNotOpen; return true; }
        Win16CommEventWord(cid & 0xff, false);
        call("ClearCommError", port->handle, block, block + 4);
        unsigned stat[4] = {};
        Read(block, stat, sizeof stat);
        Free(block);
        result = stat[0] & 0xffff;
        if (at)
        {
            unsigned char out[5] = {(unsigned char)(stat[1] & 0x7f)};
            PutWord16(out + 1, stat[2] + (port->ungot ? 1 : 0));
            PutWord16(out + 3, stat[3]);
            Store(at, out, sizeof out);
        }
        return true;
    }
    case 204: // ReadComm(idComDev, lpvBuf, cbRead)
    {
        words = 4;
        unsigned at = frame.Pointer(1);
        const unsigned size = frame.Word(0);
        auto *port = port_of(frame.Word(3));
        if (!size || !port) return true; // Windows 98 answers zero
        // the character UngetCommChar left comes first. COMM.20 then
        // reads up to the full count behind it, and its count is the answer.
        if (port->ungot)
        {
            port->ungot = false;
            Store(at, &port->ungot_char, 1);
            ++at;
        }
        const unsigned count = Allocate(4);
        if (!count) return true;
        Write32(count, 0);
        if (at) call("ReadFile", port->handle, at, size, count, 0);
        result = Read32(count) & 0xffff;
        Free(count);
        notify(frame.Word(3) & 0xff, 0);
        return true;
    }
    case 205: // WriteComm(idComDev, lpvBuf, cbWrite)
    {
        words = 4;
        const unsigned at = frame.Pointer(1), size = frame.Word(0);
        auto *port = port_of(frame.Word(3));
        if (!port) { result = 0xffff; return true; }
        unsigned done = 0;
        if (size && at)
        {
            const unsigned count = Allocate(4);
            if (count)
            {
                Write32(count, 0);
                call("WriteFile", port->handle, at, size, count, 0);
                done = Read32(count);
                Free(count);
            }
        }
        result = word(done < size ? -(int)done : (int)done);
        notify(frame.Word(3) & 0xff, done && port->notify_transmit != -1 ? 2u : 0u);
        return true;
    }
    case 206: // TransmitCommChar(idComDev, chTransmit)
    {
        words = 2;
        auto *port = port_of(frame.Word(1));
        if (!port) { result = CommNotOpen; return true; }
        result = call("TransmitCommChar", port->handle, frame.Word(0) & 0xff) ? 0 : 0xffff;
        return true;
    }
    case 207: // CloseComm(idComDev)
    {
        words = 1;
        auto *port = port_of(frame.Word(0));
        if (!port) { result = 0xffff; return true; }
        call("CloseHandle", port->handle);
        *port = {};
        return true;
    }
    case 208: // SetCommEventMask(idComDev, fuEvtMask): the far address of the event word
    {
        words = 2;
        const unsigned cid = frame.Word(1) & 0xff;
        auto *port = port_of(cid);
        if (!port) return true;
        call("SetCommMask", port->handle, frame.Word(0) & 0x3ff);
        result = Win16CommEventWord(cid, true);
        return true;
    }
    case 209: // GetCommEventMask(idComDev, fnEvtClear): the events, the named ones cleared
    {
        words = 2;
        const unsigned cid = frame.Word(1) & 0xff;
        auto *port = port_of(cid);
        if (!port) return true;
        result = Win16CommEventWord(cid, false);
        if (comm.state)
        {
            const unsigned i = comm.state->Index(port->handle);
            if (i != ~0u) comm.state->ports[i].events &= ~(unsigned)frame.Word(0);
        }
        Win16CommEventWord(cid, false);
        return true;
    }
    case 210: // SetCommBreak(idComDev): COMM, escape 0Ch
    case 211: // ClearCommBreak(idComDev): COMM, escape 0Dh
    {
        words = 1;
        auto *port = port_of(frame.Word(0));
        if (!port) { result = CommNotOpen; return true; }
        result = call(ordinal == 210 ? "SetCommBreak" : "ClearCommBreak", port->handle) ? 0 : 0xffff;
        return true;
    }
    case 212: // UngetCommChar(idComDev, chUnget)
    {
        words = 2;
        auto *port = port_of(frame.Word(1));
        if (!port || port->ungot) { result = 0xffff; return true; }
        port->ungot = true;
        port->ungot_char = (unsigned char)frame.Word(0);
        return true;
    }
    case 213: // BuildCommDCB(lpszDef, lpdcb)
    {
        words = 4;
        const unsigned text = frame.Pointer(2), at = frame.Pointer(0);
        char definition[128] = {};
        if (text) snprintf(definition, sizeof definition, "%s", ReadString(text));
        unsigned char dcb[25];
        const int answer = Win16BuildCommDcb(definition, dcb);
        if (at) Store(at, dcb, sizeof dcb);
        result = word(answer);
        return true;
    }
    case 214: // EscapeCommFunction(idComDev, nFunction), a LONG in DX:AX
    {
        words = 2;
        const unsigned cid = frame.Word(1) & 0xff, function = frame.Word(0);
        // COMM : the counts need no open port.
        if (function == 8) { result = 0x82; return true; } // GETMAXLPT
        if (function == 9) { result = 3; return true; }    // GETMAXCOM
        auto *port = port_of(cid);
        if (function == 10 || function == 11) // GETBASEIRQ: COM1 3F8h IRQ 4, COM2 2F8h IRQ 3
        {
            result = cid == 0 ? 0x000403f8u : cid == 1 ? 0x000302f8u : 0xffffffffu;
            return true;
        }
        if (!port) { result = CommNotOpen; return true; }
        if (function == 14) { result = port->handle; return true; } // the VCOMM handle
        if (function == 7) return true;                              // RESETDEV: printers only
        if (function >= 1 && function <= 6)
        {
            result = call("EscapeCommFunction", port->handle, function) ? 0 : 0xffffffffu;
            return true;
        }
        result = 0xffffffffu;
        return true;
    }
    case 215: // FlushComm(idComDev, fnQueue): 0 the transmit queue, 1 the receive queue
    {
        words = 2;
        auto *port = port_of(frame.Word(1));
        if (!port) { result = CommNotOpen; return true; }
        const unsigned queue = frame.Word(0);
        if (queue > 1) { result = 0xffff; return true; }
        result = call("PurgeComm", port->handle, queue ? 8u : 4u) ? 0 : 0xffff;
        return true;
    }
    default:
        break;
    }
    return false;
}
