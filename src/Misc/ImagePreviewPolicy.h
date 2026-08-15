#pragma once
#ifndef IMAGEPREVIEWPOLICY_H
#define IMAGEPREVIEWPOLICY_H

#include <algorithm>

namespace ImagePreviewPolicy
{

constexpr int FALLBACK_HOVER_DELAY_MS = 400;
constexpr int MAC_ARM64_MINIMUM_HOVER_DELAY_MS = 300;

inline int hoverDelayMs(int platformDelayMs, bool isMacArm64)
{
    const int platformDelay = platformDelayMs > 0 ?
                              platformDelayMs : FALLBACK_HOVER_DELAY_MS;
    return isMacArm64 ?
           std::max(platformDelay, MAC_ARM64_MINIMUM_HOVER_DELAY_MS) :
           platformDelay;
}

} // namespace ImagePreviewPolicy

#endif // IMAGEPREVIEWPOLICY_H
