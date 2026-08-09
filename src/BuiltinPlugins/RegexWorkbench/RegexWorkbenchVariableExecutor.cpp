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

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchVariableExecutor.h"

#include <memory>

#include "PCRE2/SPCRE.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

namespace
{

RegexWorkbenchEngineResult InvalidResult(const QString& text, const QString& error)
{
    RegexWorkbenchEngineResult result;
    result.text = text;
    result.termination = EngineTermination::InvalidConfiguration;
    result.errorMessage = error;
    return result;
}

bool IsWholeFunctionReplacement(const QString& replacement)
{
    const QString trimmed = replacement.trimmed();
    return trimmed.startsWith(QStringLiteral("\\F<")) &&
           trimmed.endsWith(QLatin1Char('>'));
}

QList<std::pair<int, int>> RelativeCaptures(const RegexSearch::Match& match)
{
    QList<std::pair<int, int>> captures;
    captures.reserve(match.captureGroups.size() + 1);
    captures.append(std::make_pair(0, match.end - match.start));
    for (const RegexSearch::Capture& capture : match.captureGroups) {
        captures.append(capture.participated
                            ? std::make_pair(capture.start - match.start,
                                             capture.end - match.start)
                            : std::make_pair(-1, -1));
    }
    return captures;
}

bool ShouldIngest(const RegexWorkbenchRule& rule)
{
    return rule.autoIngestNamedCaptures || !rule.captureToVar.isEmpty();
}

bool CapturesAreAvailable(const RegexWorkbenchRule& rule,
                          SPCRE& primary,
                          SPCRE* filter,
                          QString& error)
{
    if (rule.captureToVar.isEmpty() || !primary.isValid() ||
        (filter != nullptr && !filter->isValid())) {
        return true;
    }
    QStringList available = primary.getCaptureNames();
    if (filter != nullptr) {
        available.append(filter->getCaptureNames());
    }
    for (const QString& name : rule.captureToVar) {
        if (!available.contains(name)) {
            error = QStringLiteral("Configured named capture does not exist: %1").arg(name);
            return false;
        }
    }
    return true;
}

}

RegexWorkbenchEngineResult RegexWorkbenchVariableExecutor::ApplyRule(
    const RegexWorkbenchRule& rule,
    const QString& text,
    SearchVariableStore& store,
    RegexWorkbenchEngineOptions options)
{
    if (!rule.enabled) {
        return RegexWorkbenchEngine::ApplyRule(
            rule, text, SearchOperations::ReplacementExpander(), options);
    }
    if (options.beforeExpand || options.afterExpand ||
        options.stateSnapshot || options.restoreState) {
        return InvalidResult(text,
                             QStringLiteral("Variable executor owns replacement callbacks and store transactions"));
    }
    if (IsWholeFunctionReplacement(rule.replace)) {
        return InvalidResult(text,
                             QStringLiteral("Whole Python function replacements are not supported in Regex Workbench"));
    }

    SPCRE primaryRegex(rule.find);
    std::unique_ptr<SPCRE> filterRegex;
    if (rule.secondaryMode == SecondaryMode::FilterAccept ||
        rule.secondaryMode == SecondaryMode::FilterReject) {
        filterRegex = std::make_unique<SPCRE>(rule.secondaryPattern);
    }
    QString captureError;
    if (!CapturesAreAvailable(rule, primaryRegex, filterRegex.get(), captureError)) {
        return InvalidResult(text, captureError);
    }

    const SearchVariableStore::Snapshot initialStore = store.snapshot();
    options.stateSnapshot = [&store]() { return store.stateData(); };
    options.restoreState = [&store, initialStore](const QByteArray&) {
        store.restore(initialStore);
    };

    QString variableError;
    ReplacementVariableResolver resolver;
    if (rule.variableExpansionEnabled) {
        resolver = [&store, &variableError](const QString& name, QString& value) {
            bool found = false;
            value = store.get(name, &found);
            if (!found) {
                variableError = QStringLiteral("Undefined variable: %1").arg(name);
            }
            return found;
        };
    }

    if (ShouldIngest(rule)) {
        options.beforeExpand = [&store, &rule, &filterRegex](
            int,
            const CandidateMatch& candidate,
            const QString& primaryText,
            QString& error) {
            if (!candidate.hasFilterMatch || filterRegex == nullptr) {
                return true;
            }
            const RegexSearch::Match& filter = candidate.filter;
            const QString filterText = primaryText.mid(filter.start,
                                                       filter.end - filter.start);
            return store.ingestNamedCaptures(*filterRegex, filterText,
                                             RelativeCaptures(filter),
                                             rule.captureToVar, &error);
        };
        options.afterExpand = [&store, &rule, &primaryRegex](
            int,
            const CandidateMatch& candidate,
            const QString& primaryText,
            QString& error) {
            return store.ingestNamedCaptures(primaryRegex, primaryText,
                                             RelativeCaptures(candidate.primary),
                                             rule.captureToVar, &error);
        };
    }

    const SearchOperations::ReplacementExpander expander =
        [&primaryRegex, &resolver](const QString& matchedText,
                                  const QList<std::pair<int, int>>& captures,
                                  const QString& replacement,
                                  QString& expanded) {
            return primaryRegex.replaceText(matchedText, captures, replacement,
                                            expanded, resolver);
        };

    RegexWorkbenchEngineResult result = RegexWorkbenchEngine::ApplyRule(
        rule, text, expander, options);
    if (!result.success && result.termination == EngineTermination::ExpansionFailure &&
        !variableError.isEmpty()) {
        result.termination = EngineTermination::UndefinedVariable;
        result.errorMessage = variableError;
    }
    return result;
}

}
}
