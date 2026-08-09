/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
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
#ifndef REGEX_WORKBENCH_TYPES_H
#define REGEX_WORKBENCH_TYPES_H

#include <QList>
#include <QString>
#include <QStringList>

#include "Misc/RegexMatchEnumerator.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

enum class SecondaryMode {
    None,
    PreSearch,
    FilterAccept,
    FilterReject
};

struct RegexWorkbenchRule
{
    QString id;
    QString name;
    QString find;
    QString secondaryPattern;
    QString replace;
    SecondaryMode secondaryMode = SecondaryMode::None;
    bool recursive = false;
    int maxIterations = 32;
    bool allowEmpty = false;
    bool variableExpansionEnabled = false;
    bool autoIngestNamedCaptures = false;
    QStringList captureToVar;
    bool enabled = true;
};

struct CandidateMatch
{
    RegexSearch::Match primary;
    bool hasFilterMatch = false;
    // Filter offsets are local to the primary match text, not the full resource.
    RegexSearch::Match filter;
};

enum class MatchStage {
    None,
    Validation,
    Primary,
    Secondary
};

struct SecondaryMatchResult
{
    bool success = false;
    QList<CandidateMatch> candidates;
    MatchStage stage = MatchStage::None;
    RegexSearch::MatchError regexError = RegexSearch::MatchError::None;
    int nativeErrorCode = 0;
    int errorOffset = -1;
    QString errorMessage;
};

}
}

#endif // REGEX_WORKBENCH_TYPES_H
