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

#include <QSet>

#include "BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.h"
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

void AddChangedFrameNames(const SearchVariableStore::Frame& before,
                          const SearchVariableStore::Frame& after,
                          QSet<QString>& changed)
{
    QSet<QString> names(before.keyBegin(), before.keyEnd());
    names.unite(QSet<QString>(after.keyBegin(), after.keyEnd()));
    for (const QString& name : names) {
        if (before.value(name) != after.value(name)) {
            changed.insert(name);
        }
    }
}

QStringList ChangedVariableNames(const SearchVariableStore::Snapshot& before,
                                 const SearchVariableStore::Snapshot& after)
{
    QSet<QString> changed;
    AddChangedFrameNames(before.batchFrame, after.batchFrame, changed);
    AddChangedFrameNames(before.sessionFrame, after.sessionFrame, changed);
    QSet<QString> resources(before.resourceFrames.keyBegin(),
                            before.resourceFrames.keyEnd());
    resources.unite(QSet<QString>(after.resourceFrames.keyBegin(),
                                  after.resourceFrames.keyEnd()));
    for (const QString& resource : resources) {
        AddChangedFrameNames(before.resourceFrames.value(resource),
                             after.resourceFrames.value(resource), changed);
    }
    QStringList names(changed.cbegin(), changed.cend());
    names.sort();
    return names;
}

}

struct PreparedRegexWorkbenchVariableExecutor::Impl
{
    explicit Impl(const RegexWorkbenchRule& sourceRule) :
        rule(sourceRule)
    {
        if (!rule.enabled) {
            valid = true;
            return;
        }
        if (IsWholeFunctionReplacement(rule.replace)) {
            error = QStringLiteral("Whole Python function replacements are not supported in Regex Workbench");
            return;
        }
        matcher = std::make_unique<SecondaryRegexMatcher>(rule);
        if (!matcher->isValid()) {
            error = matcher->initializationFailure().errorMessage;
            return;
        }
        primaryRegex = std::make_unique<SPCRE>(rule.find);
        if (rule.secondaryMode == SecondaryMode::FilterAccept ||
            rule.secondaryMode == SecondaryMode::FilterReject) {
            filterRegex = std::make_unique<SPCRE>(rule.secondaryPattern);
        }
        if (!CapturesAreAvailable(rule, *primaryRegex, filterRegex.get(), error)) {
            return;
        }
        valid = true;
    }

    RegexWorkbenchRule rule;
    std::unique_ptr<SecondaryRegexMatcher> matcher;
    std::unique_ptr<SPCRE> primaryRegex;
    std::unique_ptr<SPCRE> filterRegex;
    bool valid = false;
    QString error;
};

PreparedRegexWorkbenchVariableExecutor::PreparedRegexWorkbenchVariableExecutor(
    const RegexWorkbenchRule& rule) :
    m_impl(std::make_unique<Impl>(rule))
{
}

PreparedRegexWorkbenchVariableExecutor::~PreparedRegexWorkbenchVariableExecutor() = default;

bool PreparedRegexWorkbenchVariableExecutor::isValid() const
{
    return m_impl->valid;
}

QString PreparedRegexWorkbenchVariableExecutor::errorMessage() const
{
    return m_impl->error;
}

RegexWorkbenchEngineResult PreparedRegexWorkbenchVariableExecutor::Apply(
    const QString& text,
    SearchVariableStore& store,
    RegexWorkbenchEngineOptions options)
{
    const RegexWorkbenchRule& rule = m_impl->rule;
    if (!m_impl->valid) {
        return InvalidResult(text, m_impl->error.isEmpty()
                                      ? QStringLiteral("Prepared regex rule is invalid")
                                      : m_impl->error);
    }
    if (!rule.enabled) {
        RegexWorkbenchEngineResult result;
        result.success = true;
        result.text = text;
        result.termination = EngineTermination::Disabled;
        return result;
    }
    if (options.beforeExpand || options.afterExpand ||
        options.stateSnapshot || options.restoreState) {
        return InvalidResult(text,
                             QStringLiteral("Variable executor owns replacement callbacks and store transactions"));
    }
    const SearchVariableStore::Snapshot initialStore = store.snapshot();
    options.stateSnapshot = [&store]() { return store.stateData(); };
    options.restoreState = [&store, initialStore](const QByteArray&) {
        store.restore(initialStore);
    };

    const ReplacementPassCallback traceObserver = options.replacementPassApplied;
    QHash<int, SearchVariableStore::Snapshot> beforeCandidateStores;
    QHash<int, QStringList> changedCandidateVariables;

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
        options.beforeExpand = [&store, &rule, &beforeCandidateStores, this](
            int candidateIndex,
            const CandidateMatch& candidate,
            const QString& primaryText,
            QString& error) {
            beforeCandidateStores.insert(candidateIndex, store.snapshot());
            if (!candidate.hasFilterMatch || m_impl->filterRegex == nullptr) {
                return true;
            }
            const RegexSearch::Match& filter = candidate.filter;
            const QString filterText = primaryText.mid(filter.start,
                                                       filter.end - filter.start);
            return store.ingestNamedCaptures(*m_impl->filterRegex, filterText,
                                             RelativeCaptures(filter),
                                             rule.captureToVar, &error);
        };
        options.afterExpand = [&store, &rule, &beforeCandidateStores,
                               &changedCandidateVariables, this](
            int candidateIndex,
            const CandidateMatch& candidate,
            const QString& primaryText,
            QString& error) {
            if (!store.ingestNamedCaptures(*m_impl->primaryRegex, primaryText,
                                           RelativeCaptures(candidate.primary),
                                           rule.captureToVar, &error)) {
                return false;
            }
            changedCandidateVariables.insert(
                candidateIndex,
                ChangedVariableNames(beforeCandidateStores.value(candidateIndex),
                                     store.snapshot()));
            return true;
        };
    }

    if (traceObserver && ShouldIngest(rule)) {
        options.replacementPassApplied =
            [traceObserver, &beforeCandidateStores,
             &changedCandidateVariables](QList<RegexWorkbenchReplacementTrace> traces) {
                for (RegexWorkbenchReplacementTrace& trace : traces) {
                    trace.variableNames = changedCandidateVariables.value(
                        trace.candidateIndex);
                }
                beforeCandidateStores.clear();
                changedCandidateVariables.clear();
                traceObserver(std::move(traces));
            };
    }

    const SearchOperations::ReplacementExpander expander =
        [this, &resolver](const QString& matchedText,
                         const QList<std::pair<int, int>>& captures,
                         const QString& replacement,
                         QString& expanded) {
            return m_impl->primaryRegex->replaceText(matchedText, captures,
                                                     replacement, expanded, resolver);
        };

    RegexWorkbenchEngineResult result = RegexWorkbenchEngine::ApplyPreparedRule(
        rule, *m_impl->matcher, text, expander, options);
    if (!result.success && result.termination == EngineTermination::ExpansionFailure &&
        !variableError.isEmpty()) {
        result.termination = EngineTermination::UndefinedVariable;
        result.errorMessage = variableError;
    }
    return result;
}

RegexWorkbenchEngineResult RegexWorkbenchVariableExecutor::ApplyRule(
    const RegexWorkbenchRule& rule,
    const QString& text,
    SearchVariableStore& store,
    RegexWorkbenchEngineOptions options)
{
    PreparedRegexWorkbenchVariableExecutor prepared(rule);
    return prepared.Apply(text, store, options);
}

}
}
