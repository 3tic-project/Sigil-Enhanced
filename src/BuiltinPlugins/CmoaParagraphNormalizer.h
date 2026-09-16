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
#ifndef CMOAPARAGRAPHNORMALIZER_H
#define CMOAPARAGRAPHNORMALIZER_H

#include "BuiltinPlugins/BookLiveParagraphNormalizer.h"

namespace BuiltinPlugins
{

// Cmoa/EBPAJ books and BookLive books share a DIV-leaf representation, but
// their safety profiles are intentionally separate.  This adapter recognizes
// the paired Cmoa reset rules before allowing the source-preserving engine to
// apply a plan.  The legacy BookLive preset is not changed.
class CmoaParagraphNormalizer
{
public:
    using Options = BookLiveParagraphNormalizer::Options;
    using Analysis = BookLiveParagraphNormalizer::Analysis;
    using NormalizeResult = BookLiveParagraphNormalizer::NormalizeResult;

    static Options defaultOptions();
    static QString presetId(const Options& options);

    static Analysis analyzeXhtmlText(
        const QString& source,
        const Options& options,
        const QVector<DivParagraphCssAnalyzer::Source>& stylesheets);
    static NormalizeResult normalizeXhtmlText(
        const QString& source,
        const Options& options,
        const QVector<DivParagraphCssAnalyzer::Source>& stylesheets);
};

}

#endif
