// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <vector>

namespace Glide
{
using ClipVertex = std::array<float, 15>;
// Glide 2.4, 10003300..100039a0: right, bottom, left, top. Clip before
// converting coordinates to the card's 12.4 format, or large offscreen
// coordinates wrap into the visible area. z is not a Glide interpolant.
inline std::vector<ClipVertex> ClipTriangle(const ClipVertex (&input)[3], const int (&clip)[4])
{
    bool inside = true;
    for (const auto &v : input)
        inside &= v[0] >= clip[0] && v[0] < clip[2] && v[1] >= clip[1] && v[1] < clip[3];
    std::vector<ClipVertex> polygon(input, input + 3);
    if (inside) return polygon;
    for (unsigned side : {2u, 3u, 0u, 1u})
    {
        if (polygon.empty()) break;
        const unsigned axis = side & 1;
        const float edge = float(clip[side]);
        auto within = [&](const ClipVertex &v) { return side >= 2 ? v[axis] < edge : v[axis] > edge; };
        std::vector<ClipVertex> result;
        ClipVertex previous = polygon.back();
        bool previous_inside = within(previous);
        for (const auto &current : polygon)
        {
            const bool current_inside = within(current);
            if (current_inside != previous_inside)
            {
                // x87 keeps the coordinate fraction at extended precision;
                // attribute interpolation receives it rounded to float.
                const double fraction = (double(edge) - previous[axis]) / (double(current[axis]) - previous[axis]);
                const float t = float(fraction);
                ClipVertex intersection{};
                intersection[axis] = edge;
                intersection[axis ^ 1] = float(previous[axis ^ 1] + fraction * (double(current[axis ^ 1]) - previous[axis ^ 1]));
                for (unsigned j = 3; j < intersection.size(); ++j)
                    intersection[j] = float(previous[j] + double(t) * (double(current[j]) - previous[j]));
                result.push_back(intersection);
            }
            if (current_inside) result.push_back(current);
            previous = current;
            previous_inside = current_inside;
        }
        polygon.swap(result);
    }
    // The original stores and reloads y after adding 2^19; x stays on
    // the x87 stack across subtraction. Preserve that rounding boundary.
    for (auto &v : polygon)
    {
        volatile float rounded_y = v[1] + 524288.0f;
        v[1] = rounded_y - 524288.0f;
    }
    return polygon;
}
}
