#include <cstdlib>
#include <iostream>

#include <QString>

#include "Misc/SearchOperations.h"

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

SearchOperations::ReplacementMatch Match(int start,
                                         int end,
                                         QList<std::pair<int, int>> captures = {})
{
    SearchOperations::ReplacementMatch match;
    match.offset = std::make_pair(start, end);
    match.captureGroups = captures;
    return match;
}

SearchOperations::ReplacementExpander TestExpander()
{
    return [](const QString& matchedText,
              const QList<std::pair<int, int>>& captures,
              const QString& replacement,
              QString& expanded) {
        if (replacement == QStringLiteral("FAIL")) {
            return false;
        }
        if (replacement == QStringLiteral("UPPER")) {
            expanded = matchedText.toUpper();
            return true;
        }
        if (replacement == QStringLiteral("CAP1")) {
            if (captures.size() < 2) {
                return false;
            }
            expanded = matchedText.mid(captures.at(1).first,
                                       captures.at(1).second - captures.at(1).first);
            return true;
        }
        expanded = replacement;
        return true;
    };
}

void TestLeftToRightSpliceGoldens()
{
    const QList<SearchOperations::ReplacementMatch> matches {
        Match(1, 4, {{0, 3}}),
        Match(6, 9, {{0, 3}})
    };
    QString output;
    int count = 0;
    std::tie(output, count) = SearchOperations::ApplyReplacements(
        QStringLiteral("xone ytwo z"), matches, QStringLiteral("UPPER"), TestExpander());
    Require(output == QStringLiteral("xONE yTWO z") && count == 2,
            "shared apply left-to-right splice golden mismatch");

    std::tie(output, count) = SearchOperations::ApplyReplacements(
        QString::fromUtf8("甲ab乙cd丙"),
        {Match(1, 3, {{0, 2}}), Match(4, 6, {{0, 2}})},
        QStringLiteral("X"), TestExpander());
    Require(output == QString::fromUtf8("甲X乙X丙") && count == 2,
            "shared apply must use QString UTF-16 offsets");
}

void TestCaptureOffsetsRemainMatchRelative()
{
    QString output;
    int count = 0;
    std::tie(output, count) = SearchOperations::ApplyReplacements(
        QStringLiteral("pre[abc]post"),
        {Match(3, 8, {{0, 5}, {1, 4}})},
        QStringLiteral("CAP1"), TestExpander());
    Require(output == QStringLiteral("preabcpost") && count == 1,
            "capture groups must be passed to the expander relative to matched text");
}

void TestFailedExpansionPreservesOriginalSegment()
{
    QString output;
    int count = 0;
    std::tie(output, count) = SearchOperations::ApplyReplacements(
        QStringLiteral("a b c"),
        {Match(0, 1, {{0, 1}}), Match(4, 5, {{0, 1}})},
        QStringLiteral("FAIL"), TestExpander());
    Require(output == QStringLiteral("a b c") && count == 0,
            "failed expansions must preserve original text and remain uncounted");
}

void TestCallbacksFollowPerCandidateExpansionOrder()
{
    QStringList events;
    SearchOperations::ApplyReplacementsOptions options;
    options.beforeExpand = [&events](int index, const SearchOperations::ReplacementMatch&) {
        events.append(QStringLiteral("before-%1").arg(index));
    };
    options.afterExpand = [&events](int index, const SearchOperations::ReplacementMatch&) {
        events.append(QStringLiteral("after-%1").arg(index));
    };

    QString output;
    int count = 0;
    std::tie(output, count) = SearchOperations::ApplyReplacements(
        QStringLiteral("a b"),
        {Match(0, 1, {{0, 1}}), Match(2, 3, {{0, 1}})},
        QStringLiteral("X"), TestExpander(), options);
    Require(output == QStringLiteral("X X") && count == 2,
            "callback golden replacement mismatch");
    Require(events == QStringList({QStringLiteral("before-0"), QStringLiteral("after-0"),
                                   QStringLiteral("before-1"), QStringLiteral("after-1")}),
            "callbacks must run before-expand then after-expand for each candidate");

    events.clear();
    std::tie(output, count) = SearchOperations::ApplyReplacements(
        QStringLiteral("a"), {Match(0, 1, {{0, 1}})},
        QStringLiteral("FAIL"), TestExpander(), options);
    Require(events == QStringList({QStringLiteral("before-0")}),
            "afterExpand must not run when expansion fails");
}

void TestNoMatchesIsIdentity()
{
    QString output;
    int count = -1;
    std::tie(output, count) = SearchOperations::ApplyReplacements(
        QStringLiteral("unchanged"), {}, QStringLiteral("X"), TestExpander());
    Require(output == QStringLiteral("unchanged") && count == 0,
            "empty match list must be an identity transformation");
}

}

int main()
{
    TestLeftToRightSpliceGoldens();
    TestCaptureOffsetsRemainMatchRelative();
    TestFailedExpansionPreservesOriginalSegment();
    TestCallbacksFollowPerCandidateExpansionOrder();
    TestNoMatchesIsIdentity();
    std::cout << "shared regex apply tests passed\n";
    return 0;
}
