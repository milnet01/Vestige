// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file rtl.cpp
/// @brief Lightweight RTL reordering implementation (Phase 10 Localization L3).
#include "utils/rtl.h"
#include "utils/utf8.h"

#include <algorithm>

namespace Vestige::rtl
{
namespace
{
// Spaces/tabs are neutral: they join an RTL run only when flanked by RTL on
// both sides. The run scan walks across them, but the reversal extends only to
// the LAST RTL codepoint, so a trailing space before an LTR char stays LTR.
bool isNeutral(uint32_t cp)
{
    return cp == 0x20 || cp == 0x09;
}
} // namespace

void toVisualOrderInPlace(std::vector<uint32_t>& cps)
{
    const size_t n = cps.size();
    size_t i = 0;
    while (i < n)
    {
        if (!utf8::isHebrew(cps[i]))
        {
            ++i;
            continue;
        }

        // Start of an RTL run. Extend across Hebrew + interior neutrals, but
        // remember the last RTL position so trailing neutrals stay LTR.
        const size_t runStart = i;
        size_t lastRtl = i;
        size_t j = i;
        while (j < n && (utf8::isHebrew(cps[j]) || isNeutral(cps[j])))
        {
            if (utf8::isHebrew(cps[j]))
            {
                lastRtl = j;
            }
            ++j;
        }
        std::reverse(cps.begin() + static_cast<std::ptrdiff_t>(runStart),
                     cps.begin() + static_cast<std::ptrdiff_t>(lastRtl) + 1);
        i = lastRtl + 1;
    }
}

std::vector<uint32_t> toVisualOrder(const std::vector<uint32_t>& logical)
{
    std::vector<uint32_t> out = logical;
    toVisualOrderInPlace(out);
    return out;
}

bool containsRTL(const std::vector<uint32_t>& cps)
{
    for (uint32_t cp : cps)
    {
        if (utf8::isHebrew(cp))
        {
            return true;
        }
    }
    return false;
}

} // namespace Vestige::rtl
