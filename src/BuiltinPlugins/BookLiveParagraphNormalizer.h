/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#pragma once
#ifndef BOOKLIVEPARAGRAPHNORMALIZER_H
#define BOOKLIVEPARAGRAPHNORMALIZER_H

#include <QString>
#include <QStringList>

namespace BuiltinPlugins
{

class BookLiveParagraphNormalizer
{
public:
    enum class PageKind {
        NormalBodyFlow,
        AlreadyNormalized,
        TocLike,
        NoticeOrImprint,
        ShortFlow,
        BlockLayout,
        ImageOrTitlePage,
        NoCandidate,
        NoBody,
        ParseError
    };

    struct Analysis {
        bool ok = false;
        bool candidate = false;
        bool safeToNormalize = false;
        PageKind pageKind = PageKind::ParseError;
        QString reason;
        QString message;
        int paragraphLeaves = 0;
        int spacerBrLeaves = 0;
        int sceneBreaks = 0;
        int imageLeaves = 0;
        int wrappedBlockLeaves = 0;
        int headingBlocks = 0;
        int anchorOnly = 0;
        int existingParagraphs = 0;
        int nestedComplexLeaves = 0;
        int otherLeaves = 0;
        int linkCount = 0;
        int imageCount = 0;
        int bodyTextLength = 0;
        int contentParentChildCount = 0;
        int wrapperDepth = 0;
        int convertibleLeaves = 0;
        bool usedShortParentPass = false;
    };

    struct NormalizeResult {
        bool ok = false;
        bool changed = false;
        QString text;
        QStringList messages;
        Analysis before;
        Analysis after;
    };

    static Analysis analyzeXhtmlText(const QString& source);
    static NormalizeResult normalizeXhtmlText(const QString& source,
                                              bool allowManualReview = false);
    static QString pageKindName(PageKind pageKind);
};

}

#endif
