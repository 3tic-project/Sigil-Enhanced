#include <cstdlib>
#include <iostream>

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchEngine.h"
#include "BuiltinPlugins/RegexWorkbench/SearchVariableStore.h"

namespace
{

using BuiltinPlugins::RegexWorkbench::CandidateMatch;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchEngine;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchEngineOptions;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchRule;
using BuiltinPlugins::RegexWorkbench::SearchVariableStore;
using BuiltinPlugins::RegexWorkbench::SecondaryMode;
using BuiltinPlugins::RegexWorkbench::VariableScope;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

QList<std::pair<int, int>> RelativeCaptures(const RegexSearch::Match& match)
{
    QList<std::pair<int, int>> captures;
    captures.append({0, match.end - match.start});
    for (const RegexSearch::Capture& capture : match.captureGroups) {
        captures.append(capture.participated
                            ? std::make_pair(capture.start - match.start,
                                             capture.end - match.start)
                            : std::make_pair(-1, -1));
    }
    return captures;
}

void TestFilterThenExpandThenPrimaryOrder()
{
    RegexWorkbenchRule rule;
    rule.find = QStringLiteral("\\[(?<primary>\\d+)\\]");
    rule.replace = QStringLiteral("PIPELINE");
    rule.secondaryMode = SecondaryMode::FilterAccept;
    rule.secondaryPattern = QStringLiteral("(?<filter>\\d+)");

    SearchVariableStore store;
    store.setScope(VariableScope::Batch);
    const auto initial = store.snapshot();
    RegexWorkbenchEngineOptions options;
    options.stateSnapshot = [&store]() { return store.stateData(); };
    options.restoreState = [&store, initial](const QByteArray&) { store.restore(initial); };
    options.beforeExpand = [&store](int, const CandidateMatch& candidate,
                                    const QString& primaryText, QString& error) {
        const auto filterNumbers = QHash<QString, int>{{QStringLiteral("filter"), 1}};
        const RegexSearch::Match& filter = candidate.filter;
        const QString filterText = primaryText.mid(filter.start, filter.end - filter.start);
        return store.ingestNamedCaptures(filterNumbers, filterText,
                                         RelativeCaptures(filter), {}, &error);
    };
    options.afterExpand = [&store](int, const CandidateMatch& candidate,
                                   const QString& primaryText, QString& error) {
        const auto primaryNumbers = QHash<QString, int>{{QStringLiteral("primary"), 1}};
        return store.ingestNamedCaptures(primaryNumbers, primaryText,
                                         RelativeCaptures(candidate.primary), {}, &error);
    };

    const SearchOperations::ReplacementExpander expander =
        [&store](const QString&, const auto&, const QString&, QString& expanded) {
            bool hadPreviousPrimary = false;
            const QString previousPrimary = store.get(QStringLiteral("primary"),
                                                      &hadPreviousPrimary);
            expanded = store.get(QStringLiteral("filter")) + QLatin1Char(':') +
                       (hadPreviousPrimary ? previousPrimary : QStringLiteral("-"));
            return true;
        };
    const auto result = RegexWorkbenchEngine::ApplyRule(
        rule, QStringLiteral("[1] [22]"), expander, options);
    Require(result.success && result.text == QStringLiteral("1:- 22:1"),
            "each candidate must ingest its filter, expand with prior primary state, then ingest primary");
    Require(store.get(QStringLiteral("filter")) == QStringLiteral("22") &&
                store.get(QStringLiteral("primary")) == QStringLiteral("22"),
            "final store values must come from the final accepted candidate");
}

void TestRejectedCandidatesDoNotIngest()
{
    RegexWorkbenchRule rule;
    rule.find = QStringLiteral("\\w+");
    rule.replace = QStringLiteral("x");
    rule.secondaryMode = SecondaryMode::FilterReject;
    rule.secondaryPattern = QStringLiteral("(?<digits>\\d+)");

    SearchVariableStore store;
    store.setScope(VariableScope::Batch);
    const auto initial = store.snapshot();
    int filterIngests = 0;
    RegexWorkbenchEngineOptions options;
    options.stateSnapshot = [&store]() { return store.stateData(); };
    options.restoreState = [&store, initial](const QByteArray&) { store.restore(initial); };
    options.beforeExpand = [&filterIngests](int, const CandidateMatch& candidate,
                                            const QString&, QString&) {
        if (candidate.hasFilterMatch) {
            ++filterIngests;
        }
        return true;
    };
    const SearchOperations::ReplacementExpander literal =
        [](const QString&, const auto&, const QString& replacement, QString& expanded) {
            expanded = replacement;
            return true;
        };
    const auto result = RegexWorkbenchEngine::ApplyRule(
        rule, QStringLiteral("cat dog42 bird"), literal, options);
    Require(result.success && result.text == QStringLiteral("x dog42 x") &&
                filterIngests == 0,
            "FilterReject survivors and rejected candidates must not ingest filter captures");
}

}

int main()
{
    TestFilterThenExpandThenPrimaryOrder();
    TestRejectedCandidatesDoNotIngest();
    std::cout << "regex filter ingest order tests passed\n";
    return 0;
}
