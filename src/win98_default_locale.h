// SPDX-License-Identifier: GPL-2.0-or-later
// GetUserDefaultLCID and its cached locale initialization.
#pragma once
namespace Win98DefaultLocale
{
using U = unsigned;
struct State { U user = 0; };
// Registry text, hexadecimal conversion, optional heap
// ownership, and the machine's configured default remain explicit providers.
template <class E> void Initialize(E &e, State &state)
{
    char buffer[128];
    char *text = buffer;
    U allocated = 0;
    if (e.ReadUserLocale(text, sizeof buffer, allocated))
    {
        state.user = e.SystemLocale();
        return;
    }
    const U parsed = e.ParseHex(text);
    // The original zero-result branch also skips freeing an allocated string.
    // A bounded native provider may use the supplied buffer with allocated=0.
    if (!parsed)
        return;
    if (allocated && text)
        e.FreeLocale(text);
    if (parsed != state.user)
        state.user = parsed;
}
// Unlike GetThreadLocale, this accessor fills an empty cache.
template <class E> U GetUser(E &e, State &state)
{
    if (!state.user)
        Initialize(e, state);
    return state.user;
}
} // namespace Win98DefaultLocale
