#pragma once
#ifndef IMAGEPREVIEWPOLICY_H
#define IMAGEPREVIEWPOLICY_H

#include <algorithm>

namespace ImagePreviewPolicy
{

constexpr int FALLBACK_HOVER_DELAY_MS = 400;
constexpr int MAC_ARM64_MINIMUM_HOVER_DELAY_MS = 300;
constexpr int DEFAULT_MAXIMUM_SIDE = 300;
constexpr int MINIMUM_MAXIMUM_SIDE = 50;
constexpr int MAXIMUM_MAXIMUM_SIDE = 600;

inline int hoverDelayMs(int platformDelayMs, bool isMacArm64)
{
    const int platformDelay = platformDelayMs > 0 ?
                              platformDelayMs : FALLBACK_HOVER_DELAY_MS;
    return isMacArm64 ?
           std::max(platformDelay, MAC_ARM64_MINIMUM_HOVER_DELAY_MS) :
           platformDelay;
}

inline int normalizedMaximumSide(int maximumSide)
{
    return std::max(MINIMUM_MAXIMUM_SIDE,
                    std::min(maximumSide, MAXIMUM_MAXIMUM_SIDE));
}

} // namespace ImagePreviewPolicy

#endif // IMAGEPREVIEWPOLICY_H
