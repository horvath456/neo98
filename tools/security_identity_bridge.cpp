/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_kernel.h"
extern "C" unsigned security_identity_result(unsigned encoding){return Win98::UnsupportedResult(encoding);}
#ifdef SECURITY_IDENTITY_SANITIZER
#include <cstdio>
#include <cassert>
int main(){unsigned e,v,n=0;while(fread(&e,4,1,stdin)==1){assert(fread(&v,4,1,stdin)==1&&security_identity_result(e)==v);++n;}printf("PASS %u original gate cases ASan/UBSan\n",n);}
#endif
