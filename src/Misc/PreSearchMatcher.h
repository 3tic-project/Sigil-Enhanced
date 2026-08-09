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

#pragma once
#ifndef PRE_SEARCH_MATCHER_H
#define PRE_SEARCH_MATCHER_H

#include <utility>

#include <QList>
#include <QString>

#include "Misc/RegexMatchEnumerator.h"

namespace RegexSearch
{

struct PreSearchRangeResult
{
    bool success = false;
    QList<std::pair<int, int>> ranges;
    MatchError error = MatchError::None;
    int nativeErrorCode = 0;
    int errorOffset = -1;
    QString errorMessage;
};

PreSearchRangeResult EnumeratePreSearchRanges(const QString& pattern,
                                              const QString& text,
                                              MatchOptions options = MatchOptions());
PreSearchRangeResult EnumeratePreSearchRanges(RegexMatchEnumerator& enumerator,
                                              const QString& text,
                                              MatchOptions options = MatchOptions());

}

#endif // PRE_SEARCH_MATCHER_H
