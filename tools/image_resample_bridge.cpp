// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_image_resample.h"
extern "C" unsigned image_resample(const unsigned char *source,unsigned sw,unsigned sh,unsigned depth,
                                  const unsigned *palette,unsigned char *target,unsigned w,unsigned h)
{
    return Win98ImageResample::Scale(source,sw,sh,depth,palette,target,w,h);
}
