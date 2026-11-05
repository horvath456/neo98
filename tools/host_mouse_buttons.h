// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace HostMouseButtons
{
// SDL button events describe one transition. SDL_GetMouseState describes
// the latest state, which can already include events still in the queue.
inline unsigned Apply(unsigned held, unsigned sdl_button, bool down)
{
    const unsigned bit = sdl_button == 1 ? 1u : sdl_button == 3 ? 2u : sdl_button == 2 ? 4u : 0u;
    return down ? held | bit : held & ~bit;
}
// A motion packet repeats the host button snapshot. Guest mouse_event may
// have changed the logical buttons since that snapshot; only host edges
// change them. In particular, SDL's SetCursorPos warp must not release an
// injected press just because the host's physical button remains up.
inline unsigned Motion(unsigned guest, unsigned previous_host, unsigned reported_host)
{
    const unsigned changed = previous_host ^ reported_host;
    return (guest & ~changed) | (reported_host & changed);
}
}
