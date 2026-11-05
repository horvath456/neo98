// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Pending-send cancellation when a target disappears. Queue exit
// distinguishes sender from receiver; window destruction targets records for
// that window.
// State bits match win98_send_message.h: 1 replied, 2 detached by receiver, 4
// sender gone, 8 abandoned, 0x10 executing, 0x100 callback record and 0x400
// early reply.
namespace Win98SendCancel {

// When a target disappears, return one for messages 0x0011 and 0x003B and
// zero otherwise.
inline unsigned VanishedResult(unsigned message) { return message == 0x11 || message == 0x3b ? 1u : 0u; }

// On sender-queue exit, clear the sender and set the sender-gone flag. Retain
// executing records. Free a detached record or a replied, non-abandoned
// record; otherwise mark it abandoned for receiver cleanup.
enum Action { Keep, Free };
inline Action SenderGone(unsigned &flags)
{
    flags |= 4;                        //
    if (flags & 0x10) return Keep;     //
    if (flags & 2) return Free;        //
    if ((flags & 9) == 1) return Free; //
    flags |= 8;                        //
    return Keep;
}

// On receiver-queue exit, clear the receiver, mark the record detached and
// remove it from the queue's send list. Clear QS_SENDMESSAGE when the list
// empties. Wake a waiting sender with VanishedResult. Otherwise invoke
// callback records with zero and free records that are not executing.
struct Receiver
{
    bool wake = false;      // Wake the waiting sender.
    bool store = false;     // Store the result in the send record.
    bool callback = false;  // Invoke the cancellation callback with a zero result.
    bool free_record = false;
    unsigned result = 0;
};
inline Receiver ReceiverGone(unsigned &flags, unsigned message, bool sender_waiting)
{
    Receiver decision;
    flags |= 2; //
    if (sender_waiting)
    {                                 //
        decision.wake = true;
        if (!(flags & 1))
        {                             //
            flags |= 1;
            decision.store = true;
            decision.result = VanishedResult(message);
        }
        else
            flags &= ~8u;             //
        return decision;
    }
    if (!(flags & 4) && (flags & 0x101) == 0x100)
    {                                 //
        flags |= 1;
        decision.callback = true;
    }
    decision.free_record = !(flags & 0x10); //
    return decision;
}

// On target-window destruction, check for a vanished sender first and leave
// executing records untouched.
enum Window { Abandon, Skip, Callback, ReplyOnly, ReplyWake };
inline Window WindowDestroyed(unsigned &flags, unsigned message, bool sender_waiting, unsigned &result)
{
    if (flags & 4)
    {                    //
        flags |= 9;      //
        return Abandon;
    }
    if (flags & 1) return Skip;      //
    if (flags & 0x100)
    {                                //
        flags |= 1;                  //
        result = 0;                  // schieben Nullen
        return Callback;             // ruft USER
    }
    if (flags & 0x10) return Skip;   //
    if (!sender_waiting)
    {                                //
        flags |= 1;                  //
        return ReplyOnly;
    }
    flags |= 1;                      //
    result = VanishedResult(message); //
    return ReplyWake;                 // ruft USER
}

}
