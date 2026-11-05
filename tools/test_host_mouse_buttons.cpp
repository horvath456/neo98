// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_mouse_buttons.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <SDL.h>

int main()
{
    unsigned held = 0;
    assert(SDL_Init(SDL_INIT_EVENTS) == 0);
    for (bool down : {true, false, true, false})
    {
        SDL_Event event{};
        event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
        assert(SDL_PushEvent(&event) == 1);
    }
    unsigned transitions = 0;
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (event.type != SDL_MOUSEBUTTONDOWN && event.type != SDL_MOUSEBUTTONUP) continue;
        // Pushed events do not alter the physical snapshot. This also models
        // polling after a complete real click has already been pumped.
        assert(!(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK));
        const unsigned next = HostMouseButtons::Apply(held, event.button.button, event.type == SDL_MOUSEBUTTONDOWN);
        if (next != held) ++transitions;
        held = next;
    }
    assert(transitions == 4 && held == 0);
    SDL_Quit();
    // A click can be queued in its entirety while the guest loads a game.
    // The live SDL snapshot is already released when either event is read.
    held = HostMouseButtons::Apply(held, 1, true);
    assert(held == 1);
    held = HostMouseButtons::Apply(held, 1, false);
    assert(held == 0);
    // An injected left press survives a pointer warp and an unrelated right
    // click. A real left release still releases it, even without a host press.
    held = HostMouseButtons::Motion(1, 0, 0);
    assert(held == 1);
    held = HostMouseButtons::Apply(held, SDL_BUTTON_RIGHT, true);
    assert(held == 3);
    held = HostMouseButtons::Motion(held, 2, 2);
    assert(held == 3);
    held = HostMouseButtons::Apply(held, SDL_BUTTON_RIGHT, false);
    assert(held == 1);
    held = HostMouseButtons::Apply(held, SDL_BUTTON_LEFT, false);
    assert(held == 0);
    assert(HostMouseButtons::Motion(7, 3, 2) == 6);
    assert(HostMouseButtons::Motion(4, 0, 1) == 5);
    // Do not collapse the release between two presses in the same batch.
    for (bool down : {true, false, true, false})
    {
        held = HostMouseButtons::Apply(held, 1, down);
        assert(held == unsigned(down));
    }
    // Releasing one button preserves the other buttons held by the guest.
    held = HostMouseButtons::Apply(held, 1, true);
    held = HostMouseButtons::Apply(held, 3, true);
    held = HostMouseButtons::Apply(held, 2, true);
    assert(held == 7);
    held = HostMouseButtons::Apply(held, 1, false);
    assert(held == 6);
    held = HostMouseButtons::Apply(held, 5, false);
    assert(held == 6);
    held = HostMouseButtons::Apply(held, 3, false);
    held = HostMouseButtons::Apply(held, 2, false);
    assert(held == 0);
    puts("HOST MOUSE BUTTONS PASS: queued clicks, repeated presses, multiple buttons");
}
