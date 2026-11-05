// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_parameters.h"
static unsigned power_states;
struct Provider : Win98Parameters::Environment
{
    unsigned PowerStates() const override { return power_states; }
    const unsigned char *state, *fonts;
    Provider(const unsigned char *s, const unsigned char *f) : state(s), fonts(f) {}
    unsigned short Word(unsigned offset) const override
    {
        return offset < 0x11ff ? state[offset] | unsigned(state[offset + 1]) << 8 : 0;
    }
    bool Font(unsigned handle, unsigned char (&out)[50]) const override
    {
        if (handle < 0x501 || handle >= 0x507)
            return false;
        for (unsigned i = 0; i < 50; ++i)
            out[i] = fonts[(handle - 0x501) * 50 + i];
        return true;
    }
};
extern "C" int parameters_query(const unsigned char *state, const unsigned char *fonts, unsigned action,
                                unsigned parameter, unsigned char *out, unsigned capacity, unsigned *written)
{
    Provider provider(state, fonts);
    return Win98Parameters::Query(provider, action, parameter, out, capacity, *written);
}
extern "C" void parameters_power(unsigned states) { power_states = states; }
