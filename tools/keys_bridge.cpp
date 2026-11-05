// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_keys.h"
#include <cstring>
extern "C" unsigned keys_key(const unsigned char *state, unsigned key) { return Win98Keys::Key(state, key); }
extern "C" unsigned keys_async(unsigned char *state, unsigned key) { return Win98Keys::Async(state, key); }
extern "C" unsigned keys_copy(unsigned char *queue, unsigned char *buffer, unsigned set, unsigned token,
                              unsigned driver_result, unsigned *driver_calls, unsigned char *seen)
{
    struct E final : Win98Keys::Environment
    {
        unsigned token, result, *calls;
        unsigned char *seen;
        unsigned TaskToken() const override { return token; }
        unsigned ToAsciiZero(const unsigned char *state) override
        {
            ++*calls;
            std::memcpy(seen, state, 256);
            return result;
        }
    } env;
    env.token = token;
    env.result = driver_result;
    env.calls = driver_calls;
    env.seen = seen;
    return Win98Keys::Copy(env, queue, buffer, set != 0);
}
