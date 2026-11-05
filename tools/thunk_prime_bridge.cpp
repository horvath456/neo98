// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_thunk_prime.h"
#include <cstring>
extern "C" void prime(unsigned quick,unsigned table,unsigned gate,unsigned char *out){
 unsigned char code[32];Win98ThunkPrime::Template(quick,table,gate,code);memcpy(out,code,32);
}
