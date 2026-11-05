// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 interlocked operations. Provider reads and writes must not yield
// between the two operations.
#pragma once
namespace Win98Interlocked
{
using U = unsigned;
// Replace the destination and return its previous value.
template <class E> U Exchange(E &e, U address, U value)
{
    const U previous = e.Read32(address);
    e.Write32(address, value);
    return previous;
}
// Compare and exchange, returning the previous value. Write the destination
// even when the comparison fails.
template <class E> U CompareExchange(E &e, U address, U exchange, U compare)
{
    const U previous = e.Read32(address);
    e.Write32(address, previous == compare ? exchange : previous);
    return previous;
}
// Increment the destination and return its new value.
template <class E> U Increment(E &e, U address)
{
    const U value = e.Read32(address) + 1u;
    e.Write32(address, value);
    return value;
}
// Decrement the destination and return its new value.
template <class E> U Decrement(E &e, U address)
{
    const U value = e.Read32(address) - 1u;
    e.Write32(address, value);
    return value;
}
// Add to the destination and return its previous value.
template <class E> U ExchangeAdd(E &e, U address, U delta)
{
    const U previous = e.Read32(address);
    e.Write32(address, previous + delta);
    return previous;
}
} // namespace Win98Interlocked
