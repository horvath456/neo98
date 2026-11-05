// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/glide_clip.h"
#include <stdio.h>
int main()
{
    int clip[4];
    if (scanf("%d%d%d%d", clip, clip+1, clip+2, clip+3) != 4) return 1;
    Glide::ClipVertex input[3];
    while (scanf("%f", &input[0][0]) == 1)
    {
        for (unsigned i = 1; i < 45; ++i)
            if (scanf("%f", &input[i / 15][i % 15]) != 1) return 2;
        auto result = Glide::ClipTriangle(input, clip);
        printf("%zu", result.size());
        for (const auto &v : result) for (float f : v) printf(" %.9g", f);
        puts("");
    }
}
