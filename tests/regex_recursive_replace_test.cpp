#include <cstdlib>
#include <iostream>

#include <QString>

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchEngine.h"

namespace
{

using BuiltinPlugins::RegexWorkbench::EngineTermination;
using BuiltinPlugins::RegexWorkbench::GuardError;
using BuiltinPlugins::RegexWorkbench::RecursiveGuardOptions;
using BuiltinPlugins::RegexWorkbench::RecursiveReplaceGuard;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchEngine;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchEngineOptions;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchEngineResult;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchRule;
using BuiltinPlugins::RegexWorkbench::SecondaryMode;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

SearchOperations::ReplacementExpander TestExpander()
{
    return [](const QString& matchedText,
              const QList<std::pair<int, int>>&,
              const QString& replacement,
              QString& expanded) {
        if (replacement == QStringLiteral("DECREMENT")) {
            expanded = QString::number(matchedText.toInt() - 1);
        } else if (replacement == QStringLiteral("SWAP_AB")) {
            expanded = matchedText == QStringLiteral("a")
                           ? QStringLiteral("b")
                           : QStringLiteral("a");
        } else if (replacement == QStringLiteral("DROP_LAST")) {
            expanded = matchedText.left(matchedText.size() - 1);
        } else if (replacement == QStringLiteral("FAIL")) {
            return false;
        } else {
            expanded = replacement;
        }
        return true;
    };
}

RegexWorkbenchRule RecursiveRule(const QString& find, const QString& replace)
{
    RegexWorkbenchRule rule;
    rule.find = find;
    rule.replace = replace;
    rule.recursive = true;
    return rule;
}

void TestSinglePassUsesSharedApplySemantics()
{
    RegexWorkbenchRule rule;
    rule.find = QStringLiteral("a");
    rule.replace = QStringLiteral("x");
    const RegexWorkbenchEngineResult result =
        RegexWorkbenchEngine::ApplyRule(rule, QStringLiteral("a a"), TestExpander());
    Require(result.success && result.text == QStringLiteral("x x") &&
                result.replacementCount == 2 &&
                result.termination == EngineTermination::SinglePassComplete,
            "non-recursive workbench apply must use single-pass shared splice semantics");

    rule.enabled = false;
    const RegexWorkbenchEngineResult disabled = RegexWorkbenchEngine::ApplyRule(
        rule, QStringLiteral("a"), SearchOperations::ReplacementExpander());
    Require(disabled.success && disabled.text == QStringLiteral("a") &&
                disabled.termination == EngineTermination::Disabled,
            "disabled rules must be unconditional no-ops");
}

void TestRecursiveConvergenceAndFinalProbe()
{
    RegexWorkbenchRule decrement = RecursiveRule(QStringLiteral("[1-9]\\d*"),
                                                 QStringLiteral("DECREMENT"));
    const RegexWorkbenchEngineResult converged =
        RegexWorkbenchEngine::ApplyRule(decrement, QStringLiteral("3"), TestExpander());
    Require(converged.success && converged.text == QStringLiteral("0") &&
                converged.replacementCount == 3 && converged.appliedIterations == 3 &&
                converged.termination == EngineTermination::NoMatches,
            "recursive replacement must re-enumerate until no matches remain");

    RegexWorkbenchRule onePass = RecursiveRule(QStringLiteral("x"), QString());
    onePass.maxIterations = 1;
    const RegexWorkbenchEngineResult finalProbe =
        RegexWorkbenchEngine::ApplyRule(onePass, QStringLiteral("x"), TestExpander());
    Require(finalProbe.success && finalProbe.text.isEmpty() &&
                finalProbe.appliedIterations == 1 &&
                finalProbe.termination == EngineTermination::NoMatches,
            "maxIterations=1 must permit a read-only no-match probe after its mutation pass");
}

void TestSecondaryFilterIsReevaluatedEachIteration()
{
    RegexWorkbenchRule rule = RecursiveRule(QStringLiteral("\\d+"),
                                            QStringLiteral("DROP_LAST"));
    rule.secondaryMode = SecondaryMode::FilterAccept;
    rule.secondaryPattern = QStringLiteral("^\\d{2,}$");
    const RegexWorkbenchEngineResult result =
        RegexWorkbenchEngine::ApplyRule(rule, QStringLiteral("123"), TestExpander());
    Require(result.success && result.text == QStringLiteral("1") &&
                result.appliedIterations == 2 && result.replacementCount == 2,
            "recursive mode must re-evaluate FilterAccept against current iteration text");
}

void TestStallCycleAndIterationLimitAreFatal()
{
    const RegexWorkbenchEngineResult stalled = RegexWorkbenchEngine::ApplyRule(
        RecursiveRule(QStringLiteral("a"), QStringLiteral("a")),
        QStringLiteral("a"), TestExpander());
    Require(!stalled.success && stalled.text == QStringLiteral("a") &&
                stalled.termination == EngineTermination::StalledWithMatches,
            "unchanged recursive state with matches must fail closed");

    const RegexWorkbenchEngineResult cycle = RegexWorkbenchEngine::ApplyRule(
        RecursiveRule(QStringLiteral("a|b"), QStringLiteral("SWAP_AB")),
        QStringLiteral("a"), TestExpander());
    Require(!cycle.success && cycle.text == QStringLiteral("a") &&
                cycle.termination == EngineTermination::StateCycle,
            "recursive state cycles must fail closed to original text");

    RegexWorkbenchRule growing = RecursiveRule(QStringLiteral("a"), QStringLiteral("aa"));
    growing.maxIterations = 2;
    const RegexWorkbenchEngineResult limited =
        RegexWorkbenchEngine::ApplyRule(growing, QStringLiteral("a"), TestExpander());
    Require(!limited.success && limited.text == QStringLiteral("a") &&
                limited.termination == EngineTermination::IterationLimit,
            "remaining matches after the final mutation pass must trigger IterationLimit");
}

void TestReplacementGrowthAndAbsoluteGuards()
{
    RecursiveReplaceGuard small(100);
    Require(small.isValid() && small.check(500, 1).success,
            "the absolute growth floor must allow 100 to 500 UTF-16 units");

    RecursiveReplaceGuard large(2 * 1024 * 1024);
    const auto growth = large.check(9 * 1024 * 1024, 1);
    Require(!growth.success && growth.error == GuardError::TextGrowthLimit,
            "2 Mi to 9 Mi UTF-16 units must exceed the original-relative growth limit");

    RecursiveGuardOptions hardOptions;
    hardOptions.maxTextCodeUnits = 128 * 1024 * 1024;
    RecursiveReplaceGuard hard(1, hardOptions);
    Require(hard.effectiveMaxTextCodeUnits() == RecursiveReplaceGuard::HardMaxTextCodeUnits,
            "callers must not raise the absolute hard text limit");
    const auto absolute = hard.check(RecursiveReplaceGuard::HardMaxTextCodeUnits + 1, 1);
    Require(!absolute.success && absolute.error == GuardError::TextSizeLimit,
            "the absolute 64 Mi UTF-16 unit limit must fail closed");

    RecursiveReplaceGuard doubling(300 * 1024);
    Require(doubling.check(600 * 1024, 1).success &&
                doubling.check(1200 * 1024, 2).success &&
                doubling.check(2400 * 1024, 3).error == GuardError::TextGrowthLimit,
            "continuous per-pass doubling must remain bounded by the original text baseline");

    RegexWorkbenchRule many = RecursiveRule(QStringLiteral("a"), QString());
    RegexWorkbenchEngineOptions options;
    options.guardOptions.maxTotalReplacements = 4;
    const RegexWorkbenchEngineResult replacementLimit = RegexWorkbenchEngine::ApplyRule(
        many, QStringLiteral("aaaaa"), TestExpander(), options);
    Require(!replacementLimit.success && replacementLimit.text == QStringLiteral("aaaaa") &&
                replacementLimit.termination == EngineTermination::ReplacementLimit,
            "replacement-count guard failures must discard the staged pass");
}

void TestExpansionCancellationAndExternalStateRollback()
{
    RegexWorkbenchRule rule;
    rule.find = QStringLiteral("a");
    rule.replace = QStringLiteral("FAIL");
    QByteArray state("before");
    RegexWorkbenchEngineOptions options;
    options.stateSnapshot = [&state]() { return state; };
    options.restoreState = [&state](const QByteArray& snapshot) { state = snapshot; };
    options.beforeExpand = [&state](int, const auto&, const QString&, QString&) {
        state = QByteArray("mutated");
        return true;
    };
    const RegexWorkbenchEngineResult failed = RegexWorkbenchEngine::ApplyRule(
        rule, QStringLiteral("a"), TestExpander(), options);
    Require(!failed.success && failed.termination == EngineTermination::ExpansionFailure &&
                failed.text == QStringLiteral("a") && state == QByteArray("before"),
            "expansion failure must restore text publication and external state snapshot");

    RegexWorkbenchEngineOptions unsafeCallback;
    unsafeCallback.beforeExpand = [](int, const auto&, const QString&, QString&) {
        return true;
    };
    const RegexWorkbenchEngineResult unsafe = RegexWorkbenchEngine::ApplyRule(
        rule, QStringLiteral("a"), TestExpander(), unsafeCallback);
    Require(!unsafe.success && unsafe.termination == EngineTermination::InvalidConfiguration,
            "stateful replacement callbacks must provide rollback handlers");

    state = QByteArray("before");
    options.beforeExpand = [&state](int, const auto&, const QString&, QString& error) {
        state = QByteArray("mutated");
        error = QStringLiteral("intentional variable failure");
        return false;
    };
    rule.replace = QStringLiteral("x");
    const RegexWorkbenchEngineResult callbackFailure = RegexWorkbenchEngine::ApplyRule(
        rule, QStringLiteral("a"), TestExpander(), options);
    Require(!callbackFailure.success &&
                callbackFailure.termination == EngineTermination::VariableFailure &&
                callbackFailure.errorMessage == QStringLiteral("intentional variable failure") &&
                state == QByteArray("before"),
            "callback failure must preserve its diagnostic and restore external state");

    RegexWorkbenchEngineOptions cancelled;
    cancelled.matchOptions.isCancelled = []() { return true; };
    const RegexWorkbenchEngineResult cancelledResult = RegexWorkbenchEngine::ApplyRule(
        rule, QStringLiteral("a"), TestExpander(), cancelled);
    Require(!cancelledResult.success && cancelledResult.text == QStringLiteral("a") &&
                cancelledResult.termination == EngineTermination::Cancelled,
            "cancellation must discard all staged recursive work");
}

void TestReplacementPassTraceUsesIterationCoordinates()
{
    RegexWorkbenchRule rule = RecursiveRule(QStringLiteral("[1-9]\\d*"),
                                            QStringLiteral("DECREMENT"));
    QList<BuiltinPlugins::RegexWorkbench::RegexWorkbenchReplacementTrace> traces;
    RegexWorkbenchEngineOptions options;
    options.replacementPassApplied = [&traces](auto pass) {
        traces.append(pass);
    };
    const auto result = RegexWorkbenchEngine::ApplyRule(
        rule, QStringLiteral("3"), TestExpander(), options);
    Require(result.success && traces.size() == 3 &&
                traces.at(0).iterationNumber == 1 &&
                traces.at(0).inputStart == 0 && traces.at(0).inputEnd == 1 &&
                traces.at(0).outputStart == 0 && traces.at(0).outputEnd == 1 &&
                traces.at(0).beforeText == QStringLiteral("3") &&
                traces.at(0).afterText == QStringLiteral("2") &&
                traces.at(2).iterationNumber == 3 &&
                traces.at(2).afterText == QStringLiteral("0"),
            "replacement traces must expose bounded per-pass input and output coordinates");
}

}

int main()
{
    TestSinglePassUsesSharedApplySemantics();
    TestRecursiveConvergenceAndFinalProbe();
    TestSecondaryFilterIsReevaluatedEachIteration();
    TestStallCycleAndIterationLimitAreFatal();
    TestReplacementGrowthAndAbsoluteGuards();
    TestExpansionCancellationAndExternalStateRollback();
    TestReplacementPassTraceUsesIterationCoordinates();
    std::cout << "recursive regex replacement tests passed\n";
    return 0;
}
