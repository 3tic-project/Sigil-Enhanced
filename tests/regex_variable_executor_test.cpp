#include <cstdlib>
#include <iostream>

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchVariableExecutor.h"
#include "PCRE2/SPCRE.h"

namespace
{
int gSpcreConstructions = 0;
}

// Keep this orchestration test independent from embedded Python. The production
// SPCRE replacement builder has its own resolver tests; this stub supplies the
// capture metadata and literal variable expansion needed to exercise the real
// workbench executor end to end.
SPCRE::SPCRE(const QString& pattern) :
    m_valid(!pattern.contains(QStringLiteral("INVALID"))),
    m_errpos(-1),
    m_pattern(pattern),
    m_re(nullptr),
    m_matchdata(nullptr),
    m_captureSubpatternCount(0),
    m_mcontext(nullptr)
{
    ++gSpcreConstructions;
#ifndef PCRE_NO_JIT
    m_jitstack = nullptr;
#endif
}

SPCRE::~SPCRE() = default;

bool SPCRE::isValid()
{
    return m_valid;
}

QStringList SPCRE::getCaptureNames() const
{
    QStringList names;
    if (m_pattern.contains(QStringLiteral("?<primary>"))) {
        names.append(QStringLiteral("primary"));
    }
    if (m_pattern.contains(QStringLiteral("?<filter>"))) {
        names.append(QStringLiteral("filter"));
    }
    if (m_pattern.contains(QStringLiteral("?<seed>"))) {
        names.append(QStringLiteral("seed"));
    }
    return names;
}

int SPCRE::getCaptureStringNumber(const QString& name)
{
    return getCaptureNames().contains(name) ? 1 : -1;
}

bool SPCRE::replaceText(const QString&,
                        const QList<std::pair<int, int>>&,
                        const QString& replacement,
                        QString& out,
                        const ReplacementVariableResolver& resolver)
{
    out.clear();
    int cursor = 0;
    while (cursor < replacement.size()) {
        const int opening = replacement.indexOf(QStringLiteral("${var:"), cursor);
        if (opening < 0 || !resolver) {
            out += replacement.mid(cursor);
            return true;
        }
        out += replacement.mid(cursor, opening - cursor);
        const int closing = replacement.indexOf(QLatin1Char('}'), opening + 6);
        if (closing < 0) {
            out += replacement.mid(opening);
            return true;
        }
        QString value;
        if (!resolver(replacement.mid(opening + 6, closing - opening - 6), value)) {
            return false;
        }
        out += value;
        cursor = closing + 1;
    }
    return true;
}

namespace
{

using BuiltinPlugins::RegexWorkbench::EngineTermination;
using BuiltinPlugins::RegexWorkbench::PreparedRegexWorkbenchVariableExecutor;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchRule;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchVariableExecutor;
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

SearchVariableStore BatchStore()
{
    SearchVariableStore store;
    store.setScope(VariableScope::Batch);
    return store;
}

void TestPrimaryCaptureFeedsLaterRule()
{
    SearchVariableStore store = BatchStore();
    RegexWorkbenchRule capture;
    capture.find = QStringLiteral("(?<seed>\\d+)");
    capture.replace = QStringLiteral("kept");
    capture.autoIngestNamedCaptures = true;

    const auto first = RegexWorkbenchVariableExecutor::ApplyRule(
        capture, QStringLiteral("42"), store);
    Require(first.success && first.text == QStringLiteral("kept") &&
                store.get(QStringLiteral("seed")) == QStringLiteral("42"),
            "primary named captures must be committed after expansion");

    RegexWorkbenchRule consume;
    consume.find = QStringLiteral("x");
    consume.replace = QStringLiteral("value=${var:seed}");
    consume.variableExpansionEnabled = true;
    const auto second = RegexWorkbenchVariableExecutor::ApplyRule(
        consume, QStringLiteral("x"), store);
    Require(second.success && second.text == QStringLiteral("value=42"),
            "later rules must resolve variables captured by earlier rules");
}

void TestFilterCaptureIsVisibleToSameCandidate()
{
    SearchVariableStore store = BatchStore();
    RegexWorkbenchRule rule;
    rule.find = QStringLiteral("\\[(?<primary>\\d+)\\]");
    rule.secondaryMode = SecondaryMode::FilterAccept;
    rule.secondaryPattern = QStringLiteral("(?<filter>\\d+)");
    rule.replace = QStringLiteral("${var:filter}");
    rule.variableExpansionEnabled = true;
    rule.autoIngestNamedCaptures = true;

    const auto result = RegexWorkbenchVariableExecutor::ApplyRule(
        rule, QStringLiteral("[1] [22]"), store);
    Require(result.success && result.text == QStringLiteral("1 22") &&
                store.get(QStringLiteral("primary")) == QStringLiteral("22") &&
                store.get(QStringLiteral("filter")) == QStringLiteral("22"),
            "filter captures must be ingested before same-candidate expansion");
}

void TestUndefinedVariableRollsBackTextAndStore()
{
    SearchVariableStore store = BatchStore();
    Require(store.set(QStringLiteral("stable"), QStringLiteral("before")),
            "test setup must seed the variable store");
    const auto before = store.stateData();

    RegexWorkbenchRule rule;
    rule.find = QStringLiteral("(?<seed>x)");
    rule.replace = QStringLiteral("${var:missing}");
    rule.variableExpansionEnabled = true;
    rule.autoIngestNamedCaptures = true;
    const auto result = RegexWorkbenchVariableExecutor::ApplyRule(
        rule, QStringLiteral("x"), store);
    Require(!result.success && result.text == QStringLiteral("x") &&
                result.termination == EngineTermination::UndefinedVariable &&
                store.stateData() == before,
            "undefined variables must fail closed and restore the complete store");
}

void TestDisabledExpansionAndUnsupportedFunction()
{
    SearchVariableStore store = BatchStore();
    RegexWorkbenchRule literal;
    literal.find = QStringLiteral("x");
    literal.replace = QStringLiteral("${var:missing}");
    const auto unchangedSyntax = RegexWorkbenchVariableExecutor::ApplyRule(
        literal, QStringLiteral("x"), store);
    Require(unchangedSyntax.success &&
                unchangedSyntax.text == QStringLiteral("${var:missing}"),
            "disabled variable expansion must preserve dollar-brace syntax literally");

    RegexWorkbenchRule function = literal;
    function.replace = QStringLiteral("\\F<replace_all>");
    const auto rejected = RegexWorkbenchVariableExecutor::ApplyRule(
        function, QStringLiteral("x"), store);
    Require(!rejected.success &&
                rejected.termination == EngineTermination::InvalidConfiguration,
            "whole Python function replacements must be rejected before execution");
}

void TestMissingConfiguredCaptureIsRejected()
{
    SearchVariableStore store = BatchStore();
    RegexWorkbenchRule rule;
    rule.find = QStringLiteral("(?<seed>x)");
    rule.replace = QStringLiteral("y");
    rule.captureToVar = {QStringLiteral("missing")};
    const auto result = RegexWorkbenchVariableExecutor::ApplyRule(
        rule, QStringLiteral("x"), store);
    Require(!result.success &&
                result.termination == EngineTermination::InvalidConfiguration,
            "configured capture names must exist in the primary or filter pattern");
}

void TestPreparedExecutorReusesReplacementPatterns()
{
    SearchVariableStore store = BatchStore();
    RegexWorkbenchRule rule;
    rule.find = QStringLiteral("\\[(?<primary>\\d+)\\]");
    rule.secondaryMode = SecondaryMode::FilterAccept;
    rule.secondaryPattern = QStringLiteral("(?<filter>\\d+)");
    rule.replace = QStringLiteral("${var:filter}");
    rule.variableExpansionEnabled = true;
    rule.autoIngestNamedCaptures = true;

    const int before = gSpcreConstructions;
    PreparedRegexWorkbenchVariableExecutor prepared(rule);
    Require(prepared.isValid() && gSpcreConstructions == before + 2,
            "prepared Filter rules must compile one primary and one filter replacement pattern");
    const auto first = prepared.Apply(QStringLiteral("[1]"), store);
    const auto second = prepared.Apply(QStringLiteral("[22]"), store);
    Require(first.success && first.text == QStringLiteral("1") &&
                second.success && second.text == QStringLiteral("22") &&
                gSpcreConstructions == before + 2,
            "prepared rules must reuse compiled replacement patterns across resources");
}

}

int main()
{
    TestPrimaryCaptureFeedsLaterRule();
    TestFilterCaptureIsVisibleToSameCandidate();
    TestUndefinedVariableRollsBackTextAndStore();
    TestDisabledExpansionAndUnsupportedFunction();
    TestMissingConfiguredCaptureIsRejected();
    TestPreparedExecutorReusesReplacementPatterns();
    std::cout << "regex variable executor tests passed\n";
    return 0;
}
