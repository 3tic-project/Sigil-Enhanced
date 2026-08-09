#include <cstdlib>
#include <iostream>

#include <QString>

#include "BuiltinPlugins/RegexWorkbench/SecondaryRegexMatcher.h"

namespace
{

using BuiltinPlugins::RegexWorkbench::CandidateMatch;
using BuiltinPlugins::RegexWorkbench::MatchStage;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchRule;
using BuiltinPlugins::RegexWorkbench::SecondaryMatchResult;
using BuiltinPlugins::RegexWorkbench::SecondaryMode;
using BuiltinPlugins::RegexWorkbench::SecondaryRegexMatcher;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

RegexWorkbenchRule BaseRule()
{
    RegexWorkbenchRule rule;
    rule.find = QStringLiteral("\\b\\w+\\b");
    return rule;
}

void TestSecondaryConfigurationValidation()
{
    RegexWorkbenchRule none = BaseRule();
    none.secondaryPattern = QStringLiteral("unexpected");
    const SecondaryMatchResult noneResult = SecondaryRegexMatcher::Enumerate(none, QStringLiteral("text"));
    Require(!noneResult.success && noneResult.stage == MatchStage::Validation,
            "None mode must reject a non-empty secondary pattern");

    RegexWorkbenchRule accept = BaseRule();
    accept.secondaryMode = SecondaryMode::FilterAccept;
    const SecondaryMatchResult acceptResult = SecondaryRegexMatcher::Enumerate(accept, QStringLiteral("text"));
    Require(!acceptResult.success && acceptResult.stage == MatchStage::Validation,
            "FilterAccept mode must require a secondary pattern");

    RegexWorkbenchRule presearch = BaseRule();
    presearch.secondaryMode = SecondaryMode::PreSearch;
    const SecondaryMatchResult presearchResult =
        SecondaryRegexMatcher::Enumerate(presearch, QStringLiteral("text"));
    Require(!presearchResult.success && presearchResult.stage == MatchStage::Validation,
            "PreSearch mode must require a secondary pattern");
}

void TestNoneAndPreSearchModes()
{
    RegexWorkbenchRule none = BaseRule();
    const SecondaryMatchResult noneResult =
        SecondaryRegexMatcher::Enumerate(none, QStringLiteral("one two"));
    Require(noneResult.success && noneResult.candidates.size() == 2,
            "None mode must enumerate all primary matches");

    SecondaryRegexMatcher prepared(none);
    const SecondaryMatchResult preparedFirst = prepared.enumerate(QStringLiteral("one"));
    const SecondaryMatchResult preparedSecond = prepared.enumerate(QStringLiteral("two three"));
    Require(preparedFirst.success && preparedFirst.candidates.size() == 1 &&
                preparedSecond.success && preparedSecond.candidates.size() == 2,
            "a prepared matcher must safely reuse compiled patterns across current-text enumerations");

    RegexWorkbenchRule presearch = BaseRule();
    presearch.secondaryMode = SecondaryMode::PreSearch;
    presearch.secondaryPattern = QStringLiteral("<keep>(.*?)</keep>");
    const SecondaryMatchResult presearchResult = SecondaryRegexMatcher::Enumerate(
        presearch, QStringLiteral("skip <keep>one two</keep> tail"));
    Require(presearchResult.success && presearchResult.candidates.size() == 2,
            "PreSearch must restrict primary matching to group 1 ranges");
    Require(presearchResult.candidates.at(0).primary.start == 11 &&
                presearchResult.candidates.at(0).primary.end == 14 &&
                presearchResult.candidates.at(1).primary.start == 15 &&
                presearchResult.candidates.at(1).primary.end == 18,
            "PreSearch primary offsets must remain resource-absolute");
}

void TestFilterAcceptCarriesCandidateLocalCapture()
{
    RegexWorkbenchRule rule;
    rule.find = QStringLiteral("\\[[^]]+\\]");
    rule.secondaryMode = SecondaryMode::FilterAccept;
    rule.secondaryPattern = QStringLiteral("id=(\\d+)");
    const SecondaryMatchResult result =
        SecondaryRegexMatcher::Enumerate(rule, QStringLiteral("[name=x] [id=12] [id=345]"));

    Require(result.success && result.candidates.size() == 2,
            "FilterAccept must keep only matching primary candidates");
    const CandidateMatch first = result.candidates.at(0);
    const CandidateMatch second = result.candidates.at(1);
    Require(first.hasFilterMatch && second.hasFilterMatch,
            "accepted candidates must carry their own filter match");
    Require(first.filter.start == 1 && first.filter.end == 6 &&
                first.filter.captureGroups.at(0).start == 4 &&
                first.filter.captureGroups.at(0).end == 6,
            "first filter offsets must be local to its primary candidate");
    Require(second.filter.captureGroups.at(0).start == 4 &&
                second.filter.captureGroups.at(0).end == 7,
            "candidate-local filter captures must not leak across candidates");
}

void TestFilterRejectAndZeroWidthPredicate()
{
    RegexWorkbenchRule reject = BaseRule();
    reject.secondaryMode = SecondaryMode::FilterReject;
    reject.secondaryPattern = QStringLiteral("\\d");
    const SecondaryMatchResult rejectResult =
        SecondaryRegexMatcher::Enumerate(reject, QStringLiteral("cat dog42 bird"));
    Require(rejectResult.success && rejectResult.candidates.size() == 2,
            "FilterReject must drop candidates with a secondary match");
    Require(rejectResult.candidates.at(0).primary.start == 0 &&
                rejectResult.candidates.at(1).primary.start == 10 &&
                !rejectResult.candidates.at(0).hasFilterMatch &&
                !rejectResult.candidates.at(1).hasFilterMatch,
            "FilterReject survivors must not carry a filter match");

    RegexWorkbenchRule accept = BaseRule();
    accept.secondaryMode = SecondaryMode::FilterAccept;
    accept.secondaryPattern = QStringLiteral("(?=dog)");
    const SecondaryMatchResult acceptResult =
        SecondaryRegexMatcher::Enumerate(accept, QStringLiteral("cat dog"));
    Require(acceptResult.success && acceptResult.candidates.size() == 1 &&
                acceptResult.candidates.first().primary.start == 4 &&
                acceptResult.candidates.first().hasFilterMatch &&
                acceptResult.candidates.first().filter.start == 0 &&
                acceptResult.candidates.first().filter.end == 0,
            "zero-width filter predicates must count as a secondary match");
}

void TestJapaneseQuoteTemplateDoesNotMatchMarkupWhitespace()
{
    RegexWorkbenchRule rule;
    rule.secondaryMode = SecondaryMode::PreSearch;
    rule.secondaryPattern = QStringLiteral("(?sU)<p.*>(.*)</p>");
    rule.find = QStringLiteral(
        "(?|^([^「」]*)」$(?#末尾孤引号匹配)|[「」]([^「」]*)(?:[「」]|$)(?#强制成对匹配))");
    const QString inlineMarkup = QStringLiteral(
        "<p><span class=\"gfont\">カバー・口絵　本文イラスト</span></p>\n"
        "<p><span class=\"gfont bold\">ただのゆきこ</span></p>");
    const SecondaryMatchResult markupResult =
        SecondaryRegexMatcher::Enumerate(rule, inlineMarkup);
    Require(markupResult.success && markupResult.candidates.isEmpty(),
            "Japanese quote correction must not treat tag attribute spaces as quotes");

    const SecondaryMatchResult quoteResult = SecondaryRegexMatcher::Enumerate(
        rule, QStringLiteral("<p><span class=\"gfont\">「台詞」</span></p>"));
    Require(quoteResult.success && quoteResult.candidates.size() == 1,
            "Japanese quote correction must still enumerate quoted dialogue");
}

void TestEmptyPrimaryRequiresRecursiveOptIn()
{
    RegexWorkbenchRule rule;
    rule.find = QStringLiteral("(?=a)");
    rule.allowEmpty = true;
    const SecondaryMatchResult nonRecursive =
        SecondaryRegexMatcher::Enumerate(rule, QStringLiteral("a"));
    Require(nonRecursive.success && nonRecursive.candidates.isEmpty(),
            "non-recursive matching must force allowEmpty off");

    rule.recursive = true;
    const SecondaryMatchResult recursive =
        SecondaryRegexMatcher::Enumerate(rule, QStringLiteral("a"));
    Require(recursive.success && recursive.candidates.size() == 1 &&
                recursive.candidates.first().primary.start == 0 &&
                recursive.candidates.first().primary.end == 0,
            "recursive matching must honor an explicit allowEmpty opt-in");
}

void TestPatternErrorsAndCancellationAreFatal()
{
    RegexWorkbenchRule badPrimary = BaseRule();
    badPrimary.find = QStringLiteral("(");
    const SecondaryMatchResult primaryResult =
        SecondaryRegexMatcher::Enumerate(badPrimary, QStringLiteral("text"));
    Require(!primaryResult.success && primaryResult.stage == MatchStage::Primary &&
                primaryResult.regexError == RegexSearch::MatchError::InvalidPattern,
            "primary compile errors must be classified");

    badPrimary.secondaryMode = SecondaryMode::PreSearch;
    badPrimary.secondaryPattern = QStringLiteral("never-matches");
    const SecondaryMatchResult primaryWithoutRanges =
        SecondaryRegexMatcher::Enumerate(badPrimary, QStringLiteral("text"));
    Require(!primaryWithoutRanges.success && primaryWithoutRanges.stage == MatchStage::Primary &&
                primaryWithoutRanges.regexError == RegexSearch::MatchError::InvalidPattern,
            "invalid primary patterns must fail even when PreSearch yields no ranges");

    RegexWorkbenchRule badSecondary = BaseRule();
    badSecondary.secondaryMode = SecondaryMode::FilterAccept;
    badSecondary.secondaryPattern = QStringLiteral("(");
    const SecondaryMatchResult secondaryResult =
        SecondaryRegexMatcher::Enumerate(badSecondary, QStringLiteral("text"));
    Require(!secondaryResult.success && secondaryResult.stage == MatchStage::Secondary &&
                secondaryResult.regexError == RegexSearch::MatchError::InvalidPattern,
            "secondary compile errors must be classified even when candidates exist");

    RegexSearch::MatchOptions cancelled;
    cancelled.isCancelled = []() { return true; };
    const SecondaryMatchResult cancelledResult =
        SecondaryRegexMatcher::Enumerate(BaseRule(), QStringLiteral("text"), cancelled);
    Require(!cancelledResult.success && cancelledResult.stage == MatchStage::Primary &&
                cancelledResult.regexError == RegexSearch::MatchError::Cancelled &&
                cancelledResult.candidates.isEmpty(),
            "cancellation must fail without publishing partial candidates");
}

}

int main()
{
    TestSecondaryConfigurationValidation();
    TestNoneAndPreSearchModes();
    TestFilterAcceptCarriesCandidateLocalCapture();
    TestFilterRejectAndZeroWidthPredicate();
    TestJapaneseQuoteTemplateDoesNotMatchMarkupWhitespace();
    TestEmptyPrimaryRequiresRecursiveOptIn();
    TestPatternErrorsAndCancellationAreFatal();
    std::cout << "regex secondary match tests passed\n";
    return 0;
}
