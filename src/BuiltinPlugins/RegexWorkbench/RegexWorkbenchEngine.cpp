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

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchEngine.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QSet>

#include "BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

namespace
{

QByteArray CurrentExternalState(const RegexWorkbenchEngineOptions& options)
{
    return options.stateSnapshot ? options.stateSnapshot() : QByteArray();
}

QByteArray StateDigest(const QString& text, const QByteArray& externalState)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(text.size()));
    hash.addData(QByteArrayView(":", 1));
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(text.constData()),
                                text.size() * static_cast<int>(sizeof(QChar))));
    hash.addData(externalState);
    return hash.result();
}

QList<SearchOperations::ReplacementMatch> ReplacementMatches(
    const QList<CandidateMatch>& candidates)
{
    QList<SearchOperations::ReplacementMatch> matches;
    matches.reserve(candidates.size());
    for (const CandidateMatch& candidate : candidates) {
        SearchOperations::ReplacementMatch match;
        match.offset = std::make_pair(candidate.primary.start, candidate.primary.end);
        match.captureGroups.reserve(candidate.primary.captureGroups.size() + 1);
        match.captureGroups.append(std::make_pair(
            0, candidate.primary.end - candidate.primary.start));
        for (const RegexSearch::Capture& capture : candidate.primary.captureGroups) {
            if (capture.participated) {
                match.captureGroups.append(std::make_pair(
                    capture.start - candidate.primary.start,
                    capture.end - candidate.primary.start));
            } else {
                match.captureGroups.append(std::make_pair(-1, -1));
            }
        }
        matches.append(match);
    }
    return matches;
}

EngineTermination GuardTermination(GuardError error)
{
    switch (error) {
        case GuardError::ReplacementLimit:
            return EngineTermination::ReplacementLimit;
        case GuardError::TextGrowthLimit:
            return EngineTermination::TextGrowthLimit;
        case GuardError::TextSizeLimit:
            return EngineTermination::TextSizeLimit;
        case GuardError::InvalidConfiguration:
            return EngineTermination::InvalidConfiguration;
        case GuardError::None:
            break;
    }
    return EngineTermination::InvalidConfiguration;
}

RegexWorkbenchEngineResult Failure(const QString& originalText,
                                   EngineTermination termination,
                                   const QString& message,
                                   const RegexWorkbenchEngineOptions& options,
                                   const QByteArray& initialExternalState)
{
    if (options.restoreState) {
        options.restoreState(initialExternalState);
    }
    RegexWorkbenchEngineResult result;
    result.text = originalText;
    result.termination = termination;
    result.errorMessage = message;
    return result;
}

RegexWorkbenchEngineResult MatchFailure(const QString& originalText,
                                        const SecondaryMatchResult& matchResult,
                                        const RegexWorkbenchEngineOptions& options,
                                        const QByteArray& initialExternalState)
{
    const EngineTermination termination =
        matchResult.regexError == RegexSearch::MatchError::Cancelled
            ? EngineTermination::Cancelled
            : EngineTermination::MatchFailure;
    RegexWorkbenchEngineResult result = Failure(originalText, termination,
                                                matchResult.errorMessage,
                                                options, initialExternalState);
    result.matchFailure = matchResult;
    return result;
}

QList<RegexWorkbenchReplacementTrace> BuildReplacementTrace(
    const QList<CandidateMatch>& candidates,
    const QList<QString>& expandedTexts,
    int iterationNumber)
{
    QList<RegexWorkbenchReplacementTrace> traces;
    traces.reserve(candidates.size());
    int outputDelta = 0;
    for (int index = 0; index < candidates.size(); ++index) {
        const CandidateMatch& candidate = candidates.at(index);
        const QString& expanded = expandedTexts.at(index);
        RegexWorkbenchReplacementTrace trace;
        trace.iterationNumber = iterationNumber;
        trace.candidateIndex = index;
        trace.inputStart = candidate.primary.start;
        trace.inputEnd = candidate.primary.end;
        trace.outputStart = candidate.primary.start + outputDelta;
        trace.outputEnd = trace.outputStart + expanded.size();
        trace.afterText = expanded;
        outputDelta += expanded.size() -
                       (candidate.primary.end - candidate.primary.start);
        traces.append(trace);
    }
    return traces;
}

}

RegexWorkbenchEngineResult RegexWorkbenchEngine::ApplyRule(
    const RegexWorkbenchRule& rule,
    const QString& text,
    const SearchOperations::ReplacementExpander& expander,
    const RegexWorkbenchEngineOptions& options)
{
    if (!rule.enabled) {
        RegexWorkbenchEngineResult result;
        result.success = true;
        result.text = text;
        result.termination = EngineTermination::Disabled;
        return result;
    }
    SecondaryRegexMatcher matcher(rule);
    return ApplyPreparedRule(rule, matcher, text, expander, options);
}

RegexWorkbenchEngineResult RegexWorkbenchEngine::ApplyPreparedRule(
    const RegexWorkbenchRule& rule,
    SecondaryRegexMatcher& matcher,
    const QString& text,
    const SearchOperations::ReplacementExpander& expander,
    const RegexWorkbenchEngineOptions& options)
{
    if (!rule.enabled) {
        RegexWorkbenchEngineResult result;
        result.success = true;
        result.text = text;
        result.termination = EngineTermination::Disabled;
        return result;
    }

    const QByteArray initialExternalState = CurrentExternalState(options);
    if (static_cast<bool>(options.stateSnapshot) != static_cast<bool>(options.restoreState)) {
        return Failure(text, EngineTermination::InvalidConfiguration,
                       QStringLiteral("stateSnapshot and restoreState must be configured together"),
                       options, initialExternalState);
    }
    if ((options.beforeExpand || options.afterExpand) && !options.stateSnapshot) {
        return Failure(text, EngineTermination::InvalidConfiguration,
                       QStringLiteral("Replacement callbacks require state snapshot and restore handlers"),
                       options, initialExternalState);
    }
    if (!expander) {
        return Failure(text, EngineTermination::InvalidConfiguration,
                       QStringLiteral("A replacement expander is required"),
                       options, initialExternalState);
    }
    if (rule.recursive && rule.maxIterations <= 0) {
        return Failure(text, EngineTermination::InvalidConfiguration,
                       QStringLiteral("Recursive maxIterations must be greater than zero"),
                       options, initialExternalState);
    }

    RecursiveReplaceGuard guard(text.size(), options.guardOptions);
    if (!guard.isValid()) {
        const GuardResult guardResult = guard.check(text.size(), 0);
        return Failure(text, EngineTermination::InvalidConfiguration,
                       guardResult.errorMessage, options, initialExternalState);
    }

    QString currentText = text;
    qint64 totalReplacements = 0;
    int appliedIterations = 0;
    QSet<QByteArray> seenStates;
    seenStates.insert(StateDigest(currentText, initialExternalState));

    while (true) {
        const SecondaryMatchResult candidates = matcher.enumerate(currentText,
                                                                  options.matchOptions);
        if (!candidates.success) {
            return MatchFailure(text, candidates, options, initialExternalState);
        }
        if (candidates.candidates.isEmpty()) {
            RegexWorkbenchEngineResult result;
            result.success = true;
            result.text = currentText;
            result.replacementCount = totalReplacements;
            result.appliedIterations = appliedIterations;
            result.termination = rule.recursive
                                     ? EngineTermination::NoMatches
                                     : EngineTermination::SinglePassComplete;
            return result;
        }
        if (rule.recursive && appliedIterations >= rule.maxIterations) {
            return Failure(text, EngineTermination::IterationLimit,
                           QStringLiteral("Recursive replacement reached iteration limit %1 with matches remaining")
                               .arg(rule.maxIterations),
                           options, initialExternalState);
        }

        const QByteArray beforeState = StateDigest(currentText,
                                                   CurrentExternalState(options));
        bool expansionFailed = false;
        bool cancelledDuringApply = false;
        bool callbackFailed = false;
        QString callbackError;
        QList<QString> expandedTexts;
        expandedTexts.reserve(candidates.candidates.size());
        const SearchOperations::ReplacementExpander guardedExpander =
            [&](const QString& matchedText,
                const QList<std::pair<int, int>>& captureGroups,
                const QString& replacement,
                QString& expanded) {
                if (expansionFailed || cancelledDuringApply || callbackFailed) {
                    return false;
                }
                if (options.matchOptions.isCancelled &&
                    options.matchOptions.isCancelled()) {
                    cancelledDuringApply = true;
                    return false;
                }
                const bool expandedOk = expander(matchedText, captureGroups,
                                                 replacement, expanded);
                expansionFailed = expansionFailed || !expandedOk;
                if (expandedOk) {
                    expandedTexts.append(expanded);
                }
                return expandedOk;
            };

        SearchOperations::ApplyReplacementsOptions applyOptions;
        if (options.beforeExpand) {
            applyOptions.beforeExpand = [&](int index,
                                            const SearchOperations::ReplacementMatch&) {
                if (!expansionFailed && !cancelledDuringApply && !callbackFailed) {
                    const CandidateMatch& candidate = candidates.candidates.at(index);
                    callbackFailed = !options.beforeExpand(
                        index, candidate,
                        currentText.mid(candidate.primary.start,
                                        candidate.primary.end - candidate.primary.start),
                        callbackError);
                }
            };
        }
        if (options.afterExpand) {
            applyOptions.afterExpand = [&](int index,
                                           const SearchOperations::ReplacementMatch&) {
                if (!expansionFailed && !cancelledDuringApply && !callbackFailed) {
                    const CandidateMatch& candidate = candidates.candidates.at(index);
                    callbackFailed = !options.afterExpand(
                        index, candidate,
                        currentText.mid(candidate.primary.start,
                                        candidate.primary.end - candidate.primary.start),
                        callbackError);
                }
            };
        }

        QString nextText;
        int passReplacements = 0;
        std::tie(nextText, passReplacements) = SearchOperations::ApplyReplacements(
            currentText, ReplacementMatches(candidates.candidates), rule.replace,
            guardedExpander, applyOptions);
        if (cancelledDuringApply) {
            return Failure(text, EngineTermination::Cancelled,
                           QStringLiteral("Regex replacement cancelled"),
                           options, initialExternalState);
        }
        if (callbackFailed) {
            return Failure(text, EngineTermination::VariableFailure,
                           callbackError.isEmpty()
                               ? QStringLiteral("Replacement variable callback failed")
                               : callbackError,
                           options, initialExternalState);
        }
        if (expansionFailed) {
            return Failure(text, EngineTermination::ExpansionFailure,
                           QStringLiteral("Replacement expansion failed"),
                           options, initialExternalState);
        }

        const qint64 nextTotalReplacements = totalReplacements + passReplacements;
        const GuardResult guardResult = guard.check(nextText.size(), nextTotalReplacements);
        if (!guardResult.success) {
            return Failure(text, GuardTermination(guardResult.error),
                           guardResult.errorMessage, options, initialExternalState);
        }

        if (expandedTexts.size() != candidates.candidates.size()) {
            return Failure(text, EngineTermination::ExpansionFailure,
                           QStringLiteral("Replacement trace did not cover every candidate"),
                           options, initialExternalState);
        }

        QList<RegexWorkbenchReplacementTrace> passTrace = BuildReplacementTrace(
            candidates.candidates, expandedTexts, appliedIterations + 1);
        for (int index = 0; index < passTrace.size(); ++index) {
            const CandidateMatch& candidate = candidates.candidates.at(index);
            passTrace[index].beforeText = currentText.mid(
                candidate.primary.start,
                candidate.primary.end - candidate.primary.start);
        }

        if (!rule.recursive) {
            if (options.replacementPassApplied) {
                options.replacementPassApplied(passTrace);
            }
            RegexWorkbenchEngineResult result;
            result.success = true;
            result.text = nextText;
            result.replacementCount = nextTotalReplacements;
            result.appliedIterations = passReplacements > 0 ? 1 : 0;
            result.termination = EngineTermination::SinglePassComplete;
            return result;
        }

        const QByteArray afterState = StateDigest(nextText, CurrentExternalState(options));
        if (afterState == beforeState) {
            return Failure(text, EngineTermination::StalledWithMatches,
                           QStringLiteral("Recursive replacement made no state progress while matches remain"),
                           options, initialExternalState);
        }
        if (seenStates.contains(afterState)) {
            return Failure(text, EngineTermination::StateCycle,
                           QStringLiteral("Recursive replacement entered a previously seen state"),
                           options, initialExternalState);
        }

        if (options.replacementPassApplied) {
            options.replacementPassApplied(passTrace);
        }

        seenStates.insert(afterState);
        currentText = nextText;
        totalReplacements = nextTotalReplacements;
        ++appliedIterations;
    }
}

}
}
