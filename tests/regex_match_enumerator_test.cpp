#include <cstdlib>
#include <iostream>

#include <QString>

#include "Misc/RegexMatchEnumerator.h"

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void TestBasicMatchesAndAbsoluteCaptures()
{
    RegexSearch::RegexMatchEnumerator enumerator(QStringLiteral("(a)(b)?"));
    const RegexSearch::MatchResult result = enumerator.enumerate(QStringLiteral("zab za"));

    Require(result.success, "basic enumeration should succeed");
    Require(result.matches.size() == 2, "basic enumeration match count mismatch");
    const RegexSearch::Match first = result.matches.at(0);
    Require(first.start == 1 && first.end == 3, "first match offsets must be absolute");
    Require(first.captureGroups.size() == 2, "capture group count mismatch");
    Require(first.captureGroups.at(0).participated && first.captureGroups.at(0).start == 1 &&
                first.captureGroups.at(0).end == 2,
            "participating group offsets mismatch");
    const RegexSearch::Match second = result.matches.at(1);
    Require(!second.captureGroups.at(1).participated && second.captureGroups.at(1).start == -1,
            "unmatched group must remain distinguishable from an empty group");
}

void TestEmptyAlternativeRetriesNonEmptyAtSameOffset()
{
    RegexSearch::RegexMatchEnumerator enumerator(QStringLiteral("(?:|a)"));
    RegexSearch::MatchOptions withoutEmpty;
    const RegexSearch::MatchResult nonEmptyResult = enumerator.enumerate(QStringLiteral("a"), withoutEmpty);
    Require(nonEmptyResult.success && nonEmptyResult.matches.size() == 1,
            "allowEmpty=false must retain the same-offset non-empty alternative");
    Require(nonEmptyResult.matches.first().start == 0 && nonEmptyResult.matches.first().end == 1,
            "same-offset non-empty alternative offsets mismatch");

    RegexSearch::MatchOptions withEmpty;
    withEmpty.allowEmpty = true;
    const RegexSearch::MatchResult allResult = enumerator.enumerate(QStringLiteral("a"), withEmpty);
    Require(allResult.success && allResult.matches.size() == 3,
            "allowEmpty=true must emit empty, non-empty, and range-end empty matches");
    Require(allResult.matches.at(0).start == 0 && allResult.matches.at(0).end == 0 &&
                allResult.matches.at(1).start == 0 && allResult.matches.at(1).end == 1 &&
                allResult.matches.at(2).start == 1 && allResult.matches.at(2).end == 1,
            "standard PCRE2 empty-match sequence mismatch");
}

void TestUnicodeAndCrLfAdvance()
{
    RegexSearch::RegexMatchEnumerator enumerator(QStringLiteral("(?=)"));
    RegexSearch::MatchOptions options;
    options.allowEmpty = true;

    const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
    const RegexSearch::MatchResult emojiResult = enumerator.enumerate(emoji, options);
    Require(emojiResult.success && emojiResult.matches.size() == 2,
            "empty matching must advance over a surrogate pair as one code point");
    Require(emojiResult.matches.at(0).start == 0 && emojiResult.matches.at(1).start == 2,
            "surrogate-pair offsets mismatch");

    const RegexSearch::MatchResult crlfResult = enumerator.enumerate(QStringLiteral("\r\n"), options);
    Require(crlfResult.success && crlfResult.matches.size() == 2,
            "empty matching must advance over CRLF as one newline sequence");
    Require(crlfResult.matches.at(0).start == 0 && crlfResult.matches.at(1).start == 2,
            "CRLF offsets mismatch");
}

void TestRangesAndEmptySubject()
{
    RegexSearch::RegexMatchEnumerator anchored(QStringLiteral("^a$"));
    RegexSearch::MatchOptions range;
    range.from = 1;
    range.to = 2;
    const RegexSearch::MatchResult rangeResult = anchored.enumerate(QStringLiteral("xay"), range);
    Require(rangeResult.success && rangeResult.matches.size() == 1,
            "range must be matched as an independent subject");
    Require(rangeResult.matches.first().start == 1 && rangeResult.matches.first().end == 2,
            "range match must map offsets back to the original text");

    RegexSearch::RegexMatchEnumerator empty(QStringLiteral("$"));
    RegexSearch::MatchOptions allowEmpty;
    allowEmpty.allowEmpty = true;
    const RegexSearch::MatchResult emptyResult = empty.enumerate(QString(), allowEmpty);
    Require(emptyResult.success && emptyResult.matches.size() == 1 &&
                emptyResult.matches.first().start == 0 && emptyResult.matches.first().end == 0,
            "empty subject must support a terminal empty match");

    range.from = 3;
    range.to = 2;
    const RegexSearch::MatchResult invalidRange = anchored.enumerate(QStringLiteral("xay"), range);
    Require(!invalidRange.success && invalidRange.error == RegexSearch::MatchError::InvalidRange,
            "invalid ranges must fail explicitly");

    range.from = 1;
    range.to = 2;
    const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
    const RegexSearch::MatchResult splitSurrogate = anchored.enumerate(emoji, range);
    Require(!splitSurrogate.success && splitSurrogate.error == RegexSearch::MatchError::InvalidRange,
            "ranges must not split UTF-16 surrogate pairs");
}

void TestMatchCountBound()
{
    RegexSearch::RegexMatchEnumerator enumerator(QStringLiteral("."));
    RegexSearch::MatchOptions options;
    options.maxMatches = 1;
    const RegexSearch::MatchResult result = enumerator.enumerate(QStringLiteral("abcdef"), options);
    Require(result.success && result.matches.size() == 1 && result.matches.first().start == 0,
            "maxMatches must stop enumeration after the requested number of matches");
}

void TestCompileCancellationAndMatchLimitErrors()
{
    RegexSearch::RegexMatchEnumerator invalid(QStringLiteral("("));
    const RegexSearch::MatchResult invalidResult = invalid.enumerate(QStringLiteral("x"));
    Require(!invalidResult.success && invalidResult.error == RegexSearch::MatchError::InvalidPattern &&
                invalidResult.errorOffset >= 0 && !invalidResult.errorMessage.isEmpty(),
            "invalid patterns must preserve compile diagnostics");

    RegexSearch::RegexMatchEnumerator literal(QStringLiteral("x"));
    RegexSearch::MatchOptions cancelled;
    cancelled.isCancelled = []() { return true; };
    const RegexSearch::MatchResult cancelledResult = literal.enumerate(QStringLiteral("x"), cancelled);
    Require(!cancelledResult.success && cancelledResult.error == RegexSearch::MatchError::Cancelled &&
                cancelledResult.matches.isEmpty(),
            "cancellation must fail without publishing partial matches");

    RegexSearch::RegexMatchEnumerator expensive(QStringLiteral("^(a+)+$"));
    RegexSearch::MatchOptions limited;
    limited.matchLimit = 10;
    const RegexSearch::MatchResult limitedResult = expensive.enumerate(
        QString(80, QLatin1Char('a')) + QLatin1Char('b'), limited);
    Require(!limitedResult.success && limitedResult.error == RegexSearch::MatchError::MatchLimit &&
                limitedResult.matches.isEmpty(),
            "PCRE2 match-limit failures must be fatal and classified");

    RegexSearch::RegexMatchEnumerator recursive(QStringLiteral("^(a(?1)?a)$"));
    RegexSearch::MatchOptions shallow;
    shallow.depthLimit = 1;
    const RegexSearch::MatchResult depthResult = recursive.enumerate(QStringLiteral("aaaa"), shallow);
    Require(!depthResult.success && depthResult.error == RegexSearch::MatchError::DepthLimit &&
                depthResult.matches.isEmpty(),
            "PCRE2 depth-limit failures must be fatal and classified");
}

}

int main()
{
    TestBasicMatchesAndAbsoluteCaptures();
    TestEmptyAlternativeRetriesNonEmptyAtSameOffset();
    TestUnicodeAndCrLfAdvance();
    TestRangesAndEmptySubject();
    TestMatchCountBound();
    TestCompileCancellationAndMatchLimitErrors();
    std::cout << "regex match enumerator tests passed\n";
    return 0;
}
