// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Keys
{
// USER returns FF80h for the Down bit, plus the Toggle bit.
inline unsigned Key(const unsigned char *queue, unsigned key)
{
    const unsigned value = queue[key & 255];
    return ((value & 128) ? 0xff80 : 0) | (value & 1);
}
// USER: global packed Down bits and a separate read-and-clear latch.
inline unsigned Async(unsigned char *state, unsigned key)
{
    key &= 255;
    unsigned result = state[key / 4] & (1u << ((key & 3) * 2)) ? 0x8000 : 0;
    auto &pressed = state[64 + key / 8];
    const unsigned bit = 1u << (key & 7);
    if (pressed & bit)
    {
        result |= 1;
        pressed &= ~bit;
    }
    return result;
}
struct Environment
{
    virtual unsigned TaskToken() const = 0;
    virtual unsigned ToAsciiZero(const unsigned char *state) = 0;
};
// USER. The keyboard driver is called after Set's copy.
inline unsigned Copy(Environment &e, unsigned char *queue, unsigned char *buffer, bool set)
{
    unsigned result = e.TaskToken();
    unsigned char *to = set ? queue : buffer;
    const unsigned char *from = set ? buffer : queue;
    for (unsigned i = 0; i < 256; ++i)
        to[i] = from[i];
    if (set)
        result = e.ToAsciiZero(queue);
    return (result | 1) & 65535;
}
inline unsigned Signed(unsigned value) { return value & 32768 ? value | 0xffff0000 : value; }
} // namespace Win98Keys
