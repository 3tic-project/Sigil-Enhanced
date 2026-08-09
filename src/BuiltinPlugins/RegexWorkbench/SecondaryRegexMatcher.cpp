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

#include <memory>

#include <QCoreApplication>

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
            error = QCoreApplication::translate(
                "RegexWorkbenchCore",
                "secondaryPattern must be empty when secondaryMode is None");
            return false;
        }
        return true;
    }

    if (rule.secondaryPattern.isEmpty()) {
        error = QCoreApplication::translate(
            "RegexWorkbenchCore",
            "secondaryPattern must not be empty when a secondary mode is active");
        return false;
    }
    if (rule.secondaryMode != SecondaryMode::PreSearch &&
        rule.secondaryMode != SecondaryMode::FilterAccept &&
        rule.secondaryMode != SecondaryMode::FilterReject) {
        error = QCoreApplication::translate(
            "RegexWorkbenchCore", "Unknown secondary regex mode");
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

struct SecondaryRegexMatcher::Impl
{
    explicit Impl(const RegexWorkbenchRule& sourceRule) : rule(sourceRule)
    {
        QString validationError;
        if (!ValidateSecondaryConfiguration(rule, validationError)) {
            initializationFailure = ValidationFailure(validationError);
            return;
        }

        primary = std::make_unique<RegexSearch::RegexMatchEnumerator>(rule.find);
        if (!primary->isValid()) {
            initializationFailure = RegexFailure(MatchStage::Primary, primary->enumerate(QString()));
            return;
        }

        if (rule.secondaryMode != SecondaryMode::None) {
            secondary = std::make_unique<RegexSearch::RegexMatchEnumerator>(rule.secondaryPattern);
            if (!secondary->isValid()) {
                initializationFailure = RegexFailure(MatchStage::Secondary,
                                                     secondary->enumerate(QString()));
                return;
            }
        }
        valid = true;
    }

    RegexWorkbenchRule rule;
    std::unique_ptr<RegexSearch::RegexMatchEnumerator> primary;
    std::unique_ptr<RegexSearch::RegexMatchEnumerator> secondary;
    bool valid = false;
    SecondaryMatchResult initializationFailure;
};

SecondaryRegexMatcher::SecondaryRegexMatcher(const RegexWorkbenchRule& rule) :
    m_impl(std::make_unique<Impl>(rule))
{
}

SecondaryRegexMatcher::~SecondaryRegexMatcher() = default;

bool SecondaryRegexMatcher::isValid() const
{
    return m_impl->valid;
}

SecondaryMatchResult SecondaryRegexMatcher::initializationFailure() const
{
    return m_impl->initializationFailure;
}

SecondaryMatchResult SecondaryRegexMatcher::enumerate(const QString& text,
                                                      RegexSearch::MatchOptions options)
{
    if (!m_impl->valid) {
        return m_impl->initializationFailure;
    }

    const RegexSearch::MatchOptions primaryOptions = PrimaryOptions(m_impl->rule, options);

    if (m_impl->rule.secondaryMode == SecondaryMode::PreSearch) {
        const RegexSearch::PreSearchRangeResult ranges =
            RegexSearch::EnumeratePreSearchRanges(*m_impl->secondary, text, options);
        if (!ranges.success) {
            return PreSearchFailure(ranges);
        }

        SecondaryMatchResult result;
        for (const std::pair<int, int>& range : ranges.ranges) {
            RegexSearch::MatchOptions rangeOptions = primaryOptions;
            rangeOptions.from = range.first;
            rangeOptions.to = range.second;
            const RegexSearch::MatchResult primaryMatches = m_impl->primary->enumerate(text, rangeOptions);
            if (!primaryMatches.success) {
                return RegexFailure(MatchStage::Primary, primaryMatches);
            }
            AppendPrimaryCandidates(primaryMatches, result.candidates);
        }
        result.success = true;
        return result;
    }

    const RegexSearch::MatchResult primaryMatches = m_impl->primary->enumerate(text, primaryOptions);
    if (!primaryMatches.success) {
        return RegexFailure(MatchStage::Primary, primaryMatches);
    }

    SecondaryMatchResult result;
    if (m_impl->rule.secondaryMode == SecondaryMode::None) {
        AppendPrimaryCandidates(primaryMatches, result.candidates);
        result.success = true;
        return result;
    }

    RegexSearch::MatchOptions filterOptions = options;
    filterOptions.allowEmpty = true;
    filterOptions.from = 0;
    filterOptions.to = -1;
    filterOptions.maxMatches = 1;
    result.candidates.reserve(primaryMatches.matches.size());
    for (const RegexSearch::Match& primary : primaryMatches.matches) {
        const QString matchText = text.mid(primary.start, primary.end - primary.start);
        const RegexSearch::MatchResult filterMatches = m_impl->secondary->enumerate(matchText, filterOptions);
        if (!filterMatches.success) {
            return RegexFailure(MatchStage::Secondary, filterMatches);
        }

        const bool filterMatched = !filterMatches.matches.isEmpty();
        const bool keep = m_impl->rule.secondaryMode == SecondaryMode::FilterAccept
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

SecondaryMatchResult SecondaryRegexMatcher::Enumerate(const RegexWorkbenchRule& rule,
                                                      const QString& text,
                                                      RegexSearch::MatchOptions options)
{
    SecondaryRegexMatcher matcher(rule);
    return matcher.enumerate(text, options);
}

}
}
