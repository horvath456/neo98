// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Original Win98 PostThreadMessage. USER32!PostThreadMessageA and
// the null-window branch of USER32!PostMessageA push a zero flag
// and fall into the shared worker, which selects entry 14h of the
// MsgThkConnectionDataLS block. USER.EXE handles the 16-bit side.
// Message translation, the KERNEL queue lookup, the queue signature check, the
// queue posting and the posted-message flush are explicit providers.
namespace Win98PostThreadMessage {
using U = unsigned;
inline U W(U x) { return x & 65535u; }
// USER's message-class/weight tables reject these system messages
// before posting, even with a null lParam. WM_WININICHANGE must not
// enqueue a pointer into another process. The original leaves LastError alone.
inline bool MessageAllowed(U message)
{
    switch (W(message)) {
    case 0x001: case 0x00c: case 0x00d: case 0x01a: case 0x01b:
    case 0x024: case 0x02b: case 0x02c: case 0x02d: case 0x038: case 0x039:
    case 0x046: case 0x047: case 0x04a: case 0x04e: case 0x053:
    case 0x07c: case 0x07d: case 0x081: case 0x083: case 0x087:
    case 0x0b0: case 0x0b2: case 0x0b3: case 0x0b4: case 0x0c2:
    case 0x0c4: case 0x0cb: case 0x0e3: case 0x0e9: case 0x0ea:
    case 0x110: case 0x124: case 0x140: case 0x143: case 0x145:
    case 0x148: case 0x14a: case 0x14c: case 0x14d: case 0x152: case 0x158:
    case 0x180: case 0x181: case 0x189: case 0x18c: case 0x18d: case 0x18f:
    case 0x191: case 0x192: case 0x196: case 0x198: case 0x1a2:
    case 0x1aa: case 0x1ab: case 0x1ac: case 0x1ad:
    case 0x213: case 0x214: case 0x216: case 0x219: case 0x220:
    case 0x229: case 0x22a: case 0x22b: case 0x22d: case 0x22e: case 0x22f:
    case 0x309: case 0x30b: case 0x30c:
        return false;
    default: return true;
    }
}
// CWDE : the 16-bit routine answers in AX and the thunk sign-extends.
inline U Cwde(U x) { return U(int(short(x))); }
// The thunk template indexes its target table with a byte; entry
// 14h is the eleventh word, and the return gate pops 14h argument
// bytes, the five DWORDs the 32-bit worker pushed.
constexpr U ThunkIndex = 0x14, ArgumentBytes = 0x14, ConversionKind = 1;
// USER's message queue carries the signature 'HQ' at offset 56h.
constexpr U QueueSignature = 0x5148, SignatureOffset = 0x56;

// The four words Windows 98 leaves in the caller's frame at bp-1Ah, bp-18h,
// bp-16h and the DWORD at bp-14h.
struct Thunked { U message = 0, wparam = 0, extra = 0, lparam = 0; };

// USER, reached through the parameter frame. The thread handle
// comes from KERNEL ordinal 464; Windows 98 answers the queue as a far pointer
// whose selector is zero unless the object carries the 'HQ' signature.
template <class E> U Body(E &e, U thread, const Thunked &m)
{
    const U handle = W(e.ThreadQueue(thread));
    const U queue = W(e.ValidateQueue(handle));
    if (!queue) return 0;  // FALSE, and the original sets no error code.
    e.FlushPostedMessages();
    // receives the queue as a far pointer with offset zero, so the
    // message is posted with window handle 0.
    if (!W(e.PostEvent(queue, 0, m.message, m.wparam, m.lparam, m.extra))) return 0;
    // relinks the queue's pending record: the word at queue+10h selects
    // a record in USER's data segment, and when that record still names this
    // queue at +0Eh it is advanced to the queue's word at +6.
    const U record = W(e.QueueWord(queue, 0x10));
    if (W(e.DataWord(record + 0x0e)) == queue)
        e.SetDataWord(record + 0x10, W(e.QueueWord(queue, 6)));
    return 1;
}

// USER.EXE, the 16-bit side of MsgThkConnectionDataLS entry 14h.
// `broadcast` is the flag the 32-bit worker pushed: zero for a thread message
// from PostThreadMessageA or PostMessageA(NULL,...), one for the broadcast
// that PostMessageA(-1,...) and PostMessageA(FFFFh,...) reach.
template <class E> U Thunk(E &e, U broadcast, U target, U message, U wparam, U lparam)
{
    // Windows 98 validates the message number first. A rejected message
    // returns zero without touching any queue or converting arguments.
    if (!e.MessageAllowed(W(message))) return 0;
    Thunked m{};
    e.ThunkMessage(ConversionKind, W(target), W(message), wparam >> 16, W(wparam), lparam, m);
    // the thread branch passes the whole 32-bit identifier, the
    // broadcast branch only its low word.
    if (!W(broadcast)) return Cwde(W(Body(e, target, m)));
    return Cwde(W(e.Broadcast(W(target), m.message, m.wparam, m.lparam, m.extra)));
}

// USER32!PostThreadMessageA and USER32!PostMessageA.
// The A entry pushes flag 0 and jumps to the worker. PostMessageA sends a null
// window to the same entry, -1 and FFFFh to 5734h with flag 1, and every other
// window to its own path.
enum Route { Thread = 0, Broadcast = 1, Window = 2 };
inline Route PostMessageRoute(U hwnd)
{
    if (!hwnd) return Thread;
    if (hwnd == 0xffffffffu || hwnd == 0xffffu) return Broadcast;
    return Window;
}
// Windows 98 PostThreadMessageW and PostMessageW are unsupported: return
// zero, set error 120, and consume four argument DWORDs.
constexpr U WideStubIndex = 4, WideError = 120, WideResult = 0;
inline U WideArgumentBytes() { return (WideStubIndex & 0xf) * 4; }
}
