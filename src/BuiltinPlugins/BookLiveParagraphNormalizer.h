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
#include <QVector>

namespace BuiltinPlugins
{

class BookLiveParagraphNormalizer
{
public:
    enum class CandidateKind {
        Paragraph,
        SpacerBr,
        SceneBreak,
        ImageWrapper,
        SingleBlockWrapper
    };

    struct Options {
        bool convertParagraphs = true;
        bool convertSpacerBr = false;
        bool convertSceneBreaks = false;
        bool convertImageWrappers = false;
        bool convertSingleBlockWrappers = false;
        bool addLegacyStyleCompensation = false;

        static Options conservative();
        static Options bookLiveCompatibility();
        QString presetId() const;
    };

    struct SourceRange {
        int start = -1;
        int end = -1;
        CandidateKind kind = CandidateKind::Paragraph;
    };

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
        int protectedHeadingBlocks = 0;
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
        QString presetId;
        QString ruleVersion;
        QString beforeHash;
        QVector<SourceRange> candidateRanges;
        QVector<SourceRange> protectedRanges;
        QStringList warnings;
    };

    struct NormalizeResult {
        bool ok = false;
        bool changed = false;
        QString text;
        QStringList messages;
        Analysis before;
        Analysis after;
        QString afterHash;
    };

    // Compatibility overloads retain the behavior of the original BookLive action.
    static Analysis analyzeXhtmlText(const QString& source);
    static Analysis analyzeXhtmlText(const QString& source, const Options& options);
    static NormalizeResult normalizeXhtmlText(const QString& source,
                                              bool allowManualReview = false);
    static NormalizeResult normalizeXhtmlText(const QString& source,
                                              const Options& options,
                                              bool allowManualReview = false);
    static QString pageKindName(PageKind pageKind);
};

}

#endif
