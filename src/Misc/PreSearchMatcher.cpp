/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "Misc/PreSearchMatcher.h"

namespace RegexSearch
{

PreSearchRangeResult EnumeratePreSearchRanges(const QString& pattern,
                                              const QString& text,
                                              MatchOptions options)
{
    PreSearchRangeResult result;
    if (pattern.isEmpty()) {
        result.success = true;
        return result;
    }

    RegexMatchEnumerator enumerator(pattern);
    return EnumeratePreSearchRanges(enumerator, text, options);
}

PreSearchRangeResult EnumeratePreSearchRanges(RegexMatchEnumerator& enumerator,
                                              const QString& text,
                                              MatchOptions options)
{
    PreSearchRangeResult result;
    options.allowEmpty = false;
    const MatchResult matchResult = enumerator.enumerate(text, options);
    if (!matchResult.success) {
        result.error = matchResult.error;
        result.nativeErrorCode = matchResult.nativeErrorCode;
        result.errorOffset = matchResult.errorOffset;
        result.errorMessage = matchResult.errorMessage;
        return result;
    }

    const bool usesCaptureRange = enumerator.captureGroupCount() > 0;
    for (const Match& match : matchResult.matches) {
        if (!usesCaptureRange) {
            result.ranges.append(std::make_pair(match.start, match.end));
            continue;
        }

        const Capture& rangeCapture = match.captureGroups.first();
        if (rangeCapture.participated && rangeCapture.start < rangeCapture.end) {
            result.ranges.append(std::make_pair(rangeCapture.start, rangeCapture.end));
        }
    }

    result.success = true;
    return result;
}

}
