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
#ifndef BRPARAGRAPHNORMALIZER_H
#define BRPARAGRAPHNORMALIZER_H

#include <QString>
#include <QStringList>

namespace BuiltinPlugins
{

class BrParagraphNormalizer
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
        int brCount = 0;
        int pCount = 0;
        int directTextSegments = 0;
        int directBlockChildren = 0;
        int imageCount = 0;
        int bodyTextLength = 0;
        int candidateParagraphs = 0;
        int titleParagraphs = 0;
        int sceneBreaks = 0;
        int linkRuns = 0;
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
    static NormalizeResult normalizeXhtmlText(const QString& source, bool allowManualReview = false);
    static QString pageKindName(PageKind pageKind);
};

}

#endif
