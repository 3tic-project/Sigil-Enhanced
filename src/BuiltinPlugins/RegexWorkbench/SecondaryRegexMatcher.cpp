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

#include "BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.h"

#include "Misc/PreSearchMatcher.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

namespace
{

SecondaryMatchResult ValidationFailure(const QString& message)
{
    SecondaryMatchResult result;
    result.stage = MatchStage::Validation;
    result.errorMessage = message;
    return result;
}

SecondaryMatchResult RegexFailure(MatchStage stage, const RegexSearch::MatchResult& matchResult)
{
    SecondaryMatchResult result;
    result.stage = stage;
    result.regexError = matchResult.error;
    result.nativeErrorCode = matchResult.nativeErrorCode;
    result.errorOffset = matchResult.errorOffset;
    result.errorMessage = matchResult.errorMessage;
    return result;
}

SecondaryMatchResult PreSearchFailure(const RegexSearch::PreSearchRangeResult& rangeResult)
{
    SecondaryMatchResult result;
    result.stage = MatchStage::Secondary;
    result.regexError = rangeResult.error;
    result.nativeErrorCode = rangeResult.nativeErrorCode;
    result.errorOffset = rangeResult.errorOffset;
    result.errorMessage = rangeResult.errorMessage;
    return result;
}

bool ValidateSecondaryConfiguration(const RegexWorkbenchRule& rule, QString& error)
{
    if (rule.secondaryMode == SecondaryMode::None) {
        if (!rule.secondaryPattern.isEmpty()) {
            error = QStringLiteral("secondaryPattern must be empty when secondaryMode is None");
            return false;
        }
        return true;
    }

    if (rule.secondaryPattern.isEmpty()) {
        error = QStringLiteral("secondaryPattern must not be empty when a secondary mode is active");
        return false;
    }
    if (rule.secondaryMode != SecondaryMode::PreSearch &&
        rule.secondaryMode != SecondaryMode::FilterAccept &&
        rule.secondaryMode != SecondaryMode::FilterReject) {
        error = QStringLiteral("Unknown secondary regex mode");
        return false;
    }
    return true;
}

RegexSearch::MatchOptions PrimaryOptions(const RegexWorkbenchRule& rule,
                                         RegexSearch::MatchOptions options)
{
    options.allowEmpty = rule.recursive && rule.allowEmpty;
    return options;
}

void AppendPrimaryCandidates(const RegexSearch::MatchResult& matches,
                             QList<CandidateMatch>& candidates)
{
    candidates.reserve(candidates.size() + matches.matches.size());
    for (const RegexSearch::Match& match : matches.matches) {
        CandidateMatch candidate;
        candidate.primary = match;
        candidates.append(candidate);
    }
}

}

SecondaryMatchResult SecondaryRegexMatcher::Enumerate(const RegexWorkbenchRule& rule,
                                                      const QString& text,
                                                      RegexSearch::MatchOptions options)
{
    QString validationError;
    if (!ValidateSecondaryConfiguration(rule, validationError)) {
        return ValidationFailure(validationError);
    }

    RegexSearch::RegexMatchEnumerator primaryEnumerator(rule.find);
    if (!primaryEnumerator.isValid()) {
        return RegexFailure(MatchStage::Primary, primaryEnumerator.enumerate(QString()));
    }
    const RegexSearch::MatchOptions primaryOptions = PrimaryOptions(rule, options);

    if (rule.secondaryMode == SecondaryMode::PreSearch) {
        const RegexSearch::PreSearchRangeResult ranges =
            RegexSearch::EnumeratePreSearchRanges(rule.secondaryPattern, text, options);
        if (!ranges.success) {
            return PreSearchFailure(ranges);
        }

        SecondaryMatchResult result;
        for (const std::pair<int, int>& range : ranges.ranges) {
            RegexSearch::MatchOptions rangeOptions = primaryOptions;
            rangeOptions.from = range.first;
            rangeOptions.to = range.second;
            const RegexSearch::MatchResult primaryMatches = primaryEnumerator.enumerate(text, rangeOptions);
            if (!primaryMatches.success) {
                return RegexFailure(MatchStage::Primary, primaryMatches);
            }
            AppendPrimaryCandidates(primaryMatches, result.candidates);
        }
        result.success = true;
        return result;
    }

    const RegexSearch::MatchResult primaryMatches = primaryEnumerator.enumerate(text, primaryOptions);
    if (!primaryMatches.success) {
        return RegexFailure(MatchStage::Primary, primaryMatches);
    }

    SecondaryMatchResult result;
    if (rule.secondaryMode == SecondaryMode::None) {
        AppendPrimaryCandidates(primaryMatches, result.candidates);
        result.success = true;
        return result;
    }

    RegexSearch::RegexMatchEnumerator filterEnumerator(rule.secondaryPattern);
    if (!filterEnumerator.isValid()) {
        return RegexFailure(MatchStage::Secondary, filterEnumerator.enumerate(QString()));
    }

    RegexSearch::MatchOptions filterOptions = options;
    filterOptions.allowEmpty = true;
    filterOptions.from = 0;
    filterOptions.to = -1;
    filterOptions.maxMatches = 1;
    result.candidates.reserve(primaryMatches.matches.size());
    for (const RegexSearch::Match& primary : primaryMatches.matches) {
        const QString matchText = text.mid(primary.start, primary.end - primary.start);
        const RegexSearch::MatchResult filterMatches = filterEnumerator.enumerate(matchText, filterOptions);
        if (!filterMatches.success) {
            return RegexFailure(MatchStage::Secondary, filterMatches);
        }

        const bool filterMatched = !filterMatches.matches.isEmpty();
        const bool keep = rule.secondaryMode == SecondaryMode::FilterAccept
                              ? filterMatched
                              : !filterMatched;
        if (!keep) {
            continue;
        }

        CandidateMatch candidate;
        candidate.primary = primary;
        if (filterMatched) {
            candidate.hasFilterMatch = true;
            candidate.filter = filterMatches.matches.first();
        }
        result.candidates.append(candidate);
    }

    result.success = true;
    return result;
}

}
}
