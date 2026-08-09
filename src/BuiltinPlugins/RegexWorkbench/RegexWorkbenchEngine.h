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
#ifndef REGEX_WORKBENCH_ENGINE_H
#define REGEX_WORKBENCH_ENGINE_H

#include <functional>

#include <QByteArray>
#include <QString>

#include "BuiltinPlugins/RegexWorkbench/RecursiveReplaceGuard.h"
#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchTypes.h"
#include "Misc/SearchOperations.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

enum class EngineTermination {
    NotRun,
    Disabled,
    SinglePassComplete,
    NoMatches,
    InvalidConfiguration,
    MatchFailure,
    Cancelled,
    ExpansionFailure,
    UndefinedVariable,
    VariableFailure,
    ReplacementLimit,
    TextGrowthLimit,
    TextSizeLimit,
    StalledWithMatches,
    StateCycle,
    IterationLimit
};

using CandidateCallback = std::function<bool(int candidateIndex,
                                             const CandidateMatch& candidate,
                                             const QString& matchedText,
                                             QString& error)>;

struct RegexWorkbenchEngineOptions
{
    RegexSearch::MatchOptions matchOptions;
    RecursiveGuardOptions guardOptions;
    CandidateCallback beforeExpand;
    CandidateCallback afterExpand;
    std::function<QByteArray()> stateSnapshot;
    std::function<void(const QByteArray&)> restoreState;
};

struct RegexWorkbenchEngineResult
{
    bool success = false;
    QString text;
    qint64 replacementCount = 0;
    int appliedIterations = 0;
    EngineTermination termination = EngineTermination::NotRun;
    SecondaryMatchResult matchFailure;
    QString errorMessage;
};

class RegexWorkbenchEngine final
{
public:
    static RegexWorkbenchEngineResult ApplyRule(
        const RegexWorkbenchRule& rule,
        const QString& text,
        const SearchOperations::ReplacementExpander& expander,
        const RegexWorkbenchEngineOptions& options = RegexWorkbenchEngineOptions());
};

}
}

#endif // REGEX_WORKBENCH_ENGINE_H
