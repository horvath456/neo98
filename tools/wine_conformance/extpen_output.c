/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

START_TEST(extpen_output)
{
    HPEN pens[4];
    LOGBRUSH brush = {BS_SOLID, RGB(1, 2, 3), 0};
    const int sizes[] = {0, 1, 15, 16, 28, 29, 64, -1};
    unsigned i, j, k;
    pens[0] = CreatePen(PS_SOLID, 1, brush.lbColor);
    pens[1] = ExtCreatePen(PS_COSMETIC, 1, &brush, 0, NULL);
    pens[2] = ExtCreatePen(PS_GEOMETRIC, 3, &brush, 0, NULL);
    pens[3] = GetStockObject(NULL_PEN);
    for (i = 0; i < 4; ++i) {
        ok(pens[i] != NULL, "pen %u creation failed\n", i);
        if (!pens[i]) continue;
        ok(GetObjectA(pens[i], 0, NULL) == sizeof(LOGPEN),
           "pen %u query size\n", i);
        for (j = 0; j < sizeof(sizes)/sizeof(sizes[0]); ++j) {
            unsigned char output[64];
            unsigned written, expected = (unsigned)sizes[j];
            if (expected > sizeof(LOGPEN)) expected = sizeof(LOGPEN);
            memset(output, 0xa5, sizeof(output));
            written = GetObjectA(pens[i], sizes[j], output);
            ok(written == expected, "pen %u size %d returned %u expected %u\n",
               i, sizes[j], written, expected);
            for (k = expected; k < sizeof(output); ++k)
                if (output[k] != 0xa5) break;
            ok(k == sizeof(output), "pen %u size %d changed byte %u after result\n",
               i, sizes[j], k);
            trace("PEN_OUTPUT pen=%u size=%d returned=%u hatch=%02x%02x%02x%02x\n",
                  i, sizes[j], written, output[16], output[17], output[18], output[19]);
        }
        if (i != 3) DeleteObject(pens[i]);
    }
}
