#include <cstdlib>
#include <iostream>

#include <QString>

#include "Misc/PreSearchMatcher.h"

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void TestFullMatchRangesWithoutCapture()
{
    const RegexSearch::PreSearchRangeResult result = RegexSearch::EnumeratePreSearchRanges(
        QStringLiteral("<p>.*?</p>"), QStringLiteral("x<p>one</p>y<p>two</p>"));
    Require(result.success && result.ranges.size() == 2,
            "presearch without group 1 must return full match ranges");
    Require(result.ranges.at(0) == std::make_pair(1, 11) &&
                result.ranges.at(1) == std::make_pair(12, 22),
            "full presearch range offsets mismatch");
}

void TestGroupOneSelectsRange()
{
    const RegexSearch::PreSearchRangeResult result = RegexSearch::EnumeratePreSearchRanges(
        QStringLiteral("<p>(.*?)</p>"), QStringLiteral("x<p>one</p>y<p>two</p>"));
    Require(result.success && result.ranges.size() == 2,
            "presearch group 1 must select each inner range");
    Require(result.ranges.at(0) == std::make_pair(4, 7) &&
                result.ranges.at(1) == std::make_pair(15, 18),
            "captured presearch range offsets mismatch");
}

void TestOuterFullMatchControlsProgress()
{
    const RegexSearch::PreSearchRangeResult result = RegexSearch::EnumeratePreSearchRanges(
        QStringLiteral("(a).*?X"), QStringLiteral("aaXX"));
    Require(result.success && result.ranges.size() == 1,
            "presearch must not rescan from the end of an early group 1");
    Require(result.ranges.first() == std::make_pair(0, 1),
            "outer-match progress selected the wrong group 1 range");
}

void TestEmptyAndUnmatchedGroupOneAreSkippedSafely()
{
    const RegexSearch::PreSearchRangeResult empty = RegexSearch::EnumeratePreSearchRanges(
        QStringLiteral("()a"), QStringLiteral("aa"));
    Require(empty.success && empty.ranges.isEmpty(),
            "empty group 1 ranges must be skipped without stalling");

    const RegexSearch::PreSearchRangeResult optional = RegexSearch::EnumeratePreSearchRanges(
        QStringLiteral("(x)?a"), QStringLiteral("xaxa"));
    Require(optional.success && optional.ranges.size() == 2 &&
                optional.ranges.at(0) == std::make_pair(0, 1) &&
                optional.ranges.at(1) == std::make_pair(2, 3),
            "participating optional group 1 ranges mismatch");

    const RegexSearch::PreSearchRangeResult unmatched = RegexSearch::EnumeratePreSearchRanges(
        QStringLiteral("(x)?a"), QStringLiteral("aa"));
    Require(unmatched.success && unmatched.ranges.isEmpty(),
            "unmatched group 1 ranges must be skipped without bogus offsets");
}

void TestErrorsRemainAvailableToWorkbenchCallers()
{
    const RegexSearch::PreSearchRangeResult invalid =
        RegexSearch::EnumeratePreSearchRanges(QStringLiteral("("), QStringLiteral("text"));
    Require(!invalid.success && invalid.error == RegexSearch::MatchError::InvalidPattern &&
                invalid.errorOffset >= 0 && !invalid.errorMessage.isEmpty(),
            "presearch helper must preserve compile diagnostics");

    RegexSearch::MatchOptions cancelled;
    cancelled.isCancelled = []() { return true; };
    const RegexSearch::PreSearchRangeResult cancelledResult = RegexSearch::EnumeratePreSearchRanges(
        QStringLiteral("text"), QStringLiteral("text"), cancelled);
    Require(!cancelledResult.success && cancelledResult.error == RegexSearch::MatchError::Cancelled &&
                cancelledResult.ranges.isEmpty(),
            "presearch cancellation must not publish partial ranges");
}

}

int main()
{
    TestFullMatchRangesWithoutCapture();
    TestGroupOneSelectsRange();
    TestOuterFullMatchControlsProgress();
    TestEmptyAndUnmatchedGroupOneAreSkippedSafely();
    TestErrorsRemainAvailableToWorkbenchCallers();
    std::cout << "presearch progress tests passed\n";
    return 0;
}
