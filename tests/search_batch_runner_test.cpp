#include <cstdlib>
#include <iostream>

#include <QHash>
#include <QString>

#include "Misc/SearchBatchRunner.h"

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

SearchBatch::ApplyResult LiteralApply(const SearchBatch::Rule& rule,
                                      const QString&,
                                      const QString& text)
{
    SearchBatch::ApplyResult result;
    result.text = text;
    result.replacementCount = text.count(rule.searchRegex);
    result.text.replace(rule.searchRegex, rule.replacement);
    return result;
}

void TestRuleOrderAndSingleFinalText()
{
    QHash<QString, QString> originals;
    originals.insert(QStringLiteral("a.xhtml"), QStringLiteral("A A"));
    originals.insert(QStringLiteral("b.xhtml"), QStringLiteral("A"));

    SearchBatch::Rule first;
    first.id = QStringLiteral("1");
    first.name = QStringLiteral("A to B");
    first.searchRegex = QStringLiteral("A");
    first.replacement = QStringLiteral("B");
    first.resourcePaths = QStringList{QStringLiteral("a.xhtml"), QStringLiteral("b.xhtml")};

    SearchBatch::Rule second;
    second.id = QStringLiteral("2");
    second.name = QStringLiteral("B to C");
    second.searchRegex = QStringLiteral("B");
    second.replacement = QStringLiteral("C");
    second.resourcePaths = first.resourcePaths;

    const SearchBatch::Result result = SearchBatch::Runner::Run(
        QList<SearchBatch::Rule>{first, second}, originals, LiteralApply);

    Require(result.success, "ordered batch should succeed");
    Require(result.replacementCount == 6, "replacement count must include both ordered rules");
    Require(result.changedTexts.size() == 2, "each changed resource must have one final text");
    Require(result.changedTexts.value(QStringLiteral("a.xhtml")) == QStringLiteral("C C"),
            "later rule must see earlier staged text");
    Require(result.changedTexts.value(QStringLiteral("b.xhtml")) == QStringLiteral("C"),
            "resource order result mismatch");
    Require(result.rules.size() == 2 && result.rules.at(0).changedResourceCount == 2 &&
                result.rules.at(1).changedResourceCount == 2,
            "per-rule changed resource counters are incorrect");
}

void TestCountWithoutTextChange()
{
    QHash<QString, QString> originals;
    originals.insert(QStringLiteral("same.xhtml"), QStringLiteral("x x"));

    SearchBatch::Rule rule;
    rule.name = QStringLiteral("same text");
    rule.searchRegex = QStringLiteral("x");
    rule.replacement = QStringLiteral("x");
    rule.resourcePaths = QStringList{QStringLiteral("same.xhtml")};

    const SearchBatch::Result result = SearchBatch::Runner::Run(
        QList<SearchBatch::Rule>{rule}, originals, LiteralApply);

    Require(result.success, "same-text replacement should succeed");
    Require(result.replacementCount == 2, "same-text replacement must preserve match count");
    Require(result.changedTexts.isEmpty(), "same-text replacement must not schedule a write");
    Require(result.rules.first().matchedResourceCount == 1 &&
                result.rules.first().changedResourceCount == 0,
            "match and change counters must remain distinct");
}

void TestFailureAndCancellationDoNotPublishTexts()
{
    QHash<QString, QString> originals;
    originals.insert(QStringLiteral("a.xhtml"), QStringLiteral("A"));

    SearchBatch::Rule rule;
    rule.name = QStringLiteral("failure");
    rule.resourcePaths = QStringList{QStringLiteral("a.xhtml")};

    const SearchBatch::Result failed = SearchBatch::Runner::Run(
        QList<SearchBatch::Rule>{rule}, originals,
        [](const SearchBatch::Rule&, const QString&, const QString&) {
            SearchBatch::ApplyResult result;
            result.ok = false;
            result.error = QStringLiteral("intentional failure");
            return result;
        });
    Require(!failed.success && failed.changedTexts.isEmpty(),
            "failed batch must not publish staged texts");

    int cancellationChecks = 0;
    const SearchBatch::Result cancelled = SearchBatch::Runner::Run(
        QList<SearchBatch::Rule>{rule}, originals, LiteralApply,
        [&cancellationChecks]() { return ++cancellationChecks == 1; });
    Require(!cancelled.success && cancelled.cancelled && cancelled.changedTexts.isEmpty(),
            "cancelled batch must not publish staged texts");
}

void TestMissingTargetFailsClosed()
{
    SearchBatch::Rule rule;
    rule.name = QStringLiteral("missing");
    rule.resourcePaths = QStringList{QStringLiteral("missing.xhtml")};

    const SearchBatch::Result result = SearchBatch::Runner::Run(
        QList<SearchBatch::Rule>{rule}, QHash<QString, QString>(), LiteralApply);
    Require(!result.success && result.error.contains(QStringLiteral("missing.xhtml")),
            "missing target must fail closed with its path");
}

}

int main()
{
    TestRuleOrderAndSingleFinalText();
    TestCountWithoutTextChange();
    TestFailureAndCancellationDoNotPublishTexts();
    TestMissingTargetFailsClosed();
    std::cout << "search batch runner tests passed\n";
    return 0;
}
