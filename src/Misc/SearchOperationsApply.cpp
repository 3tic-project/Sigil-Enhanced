/************************************************************************
**
**  Copyright (C) 2015-2026 Kevin B. Hendricks, Stratford Ontario Canada
**  Copyright (C) 2009-2011 Strahinja Markovic <strahinja.markovic@gmail.com>
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "Misc/SearchOperations.h"

std::tuple<QString, int> SearchOperations::ApplyReplacements(
    const QString& text,
    const QList<ReplacementMatch>& matches,
    const QString& replacement,
    const ReplacementExpander& expander,
    const ApplyReplacementsOptions& options)
{
    QString newText;
    newText.reserve(text.size());
    int replacementCount = 0;
    int headStart = 0;

    for (int matchIndex = 0; matchIndex < matches.size(); ++matchIndex) {
        const ReplacementMatch& match = matches.at(matchIndex);
        const int matchStart = match.offset.first;
        const int matchEnd = match.offset.second;
        newText += text.mid(headStart, matchStart - headStart);

        const QString matchedText = text.mid(matchStart, matchEnd - matchStart);
        if (options.beforeExpand) {
            options.beforeExpand(matchIndex, match);
        }

        QString expanded;
        if (expander && expander(matchedText, match.captureGroups, replacement, expanded)) {
            newText += expanded;
            ++replacementCount;
            if (options.afterExpand) {
                options.afterExpand(matchIndex, match);
            }
        } else {
            newText += matchedText;
        }
        headStart = matchEnd;
    }

    newText += text.mid(headStart);
    return std::make_tuple(newText, replacementCount);
}
