#include <cstdlib>
#include <iostream>

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchBatchRunner.h"
#include "Misc/Utility.h"
#include "PCRE2/SPCRE.h"

namespace
{
int gSpcreConstructions = 0;
}

QString Utility::DefinePrefsDir()
{
    return QString();
}

SPCRE::SPCRE(const QString& pattern) :
    m_valid(true),
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
    for (const QString& name : {QStringLiteral("seed"),
                                QStringLiteral("primary"),
                                QStringLiteral("filter")}) {
        if (m_pattern.contains(QStringLiteral("?<%1>").arg(name))) {
            names.append(name);
        }
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

using BuiltinPlugins::RegexWorkbench::RegexRecipe;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchBatchOptions;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchBatchRunner;
using BuiltinPlugins::RegexWorkbench::RegexWorkbenchRule;
using BuiltinPlugins::RegexWorkbench::SearchVariableStore;
using BuiltinPlugins::RegexWorkbench::VariableScope;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

QStringList Paths()
{
    return {QStringLiteral("Text/a.xhtml"), QStringLiteral("Text/b.xhtml")};
}

QHash<QString, QString> OriginalTexts()
{
    return {
        {QStringLiteral("Text/a.xhtml"), QStringLiteral("<p>A1 x</p>")},
        {QStringLiteral("Text/b.xhtml"), QStringLiteral("<p>A2 x</p>")}
    };
}

QHash<QString, QString> MediaTypes()
{
    return {
        {QStringLiteral("Text/a.xhtml"), QStringLiteral("application/xhtml+xml")},
        {QStringLiteral("Text/b.xhtml"), QStringLiteral("application/xhtml+xml")}
    };
}

RegexRecipe VariableRecipe(VariableScope scope)
{
    RegexRecipe recipe;
    recipe.name = QStringLiteral("variable pipeline");
    recipe.variableScope = scope;

    RegexWorkbenchRule capture;
    capture.id = QStringLiteral("capture");
    capture.name = QStringLiteral("capture seed");
    capture.find = QStringLiteral("(?<seed>A\\d)");
    capture.replace = QStringLiteral("mark");
    capture.autoIngestNamedCaptures = true;
    recipe.rules.append(capture);

    RegexWorkbenchRule consume;
    consume.id = QStringLiteral("consume");
    consume.name = QStringLiteral("consume seed");
    consume.find = QStringLiteral("x");
    consume.replace = QStringLiteral("${var:seed}");
    consume.variableExpansionEnabled = true;
    recipe.rules.append(consume);
    return recipe;
}

void TestBatchScopeOrderAndPreparedReuse()
{
    SearchVariableStore initial;
    initial.setScope(VariableScope::Session);
    Require(initial.set(QStringLiteral("persistent"), QStringLiteral("keep")),
            "test store setup failed");
    const auto initialState = initial.stateData();
    const int constructions = gSpcreConstructions;

    RegexWorkbenchBatchOptions options;
    options.maxReportRows = 2;
    options.maxSnippetCodeUnits = 12;
    QList<std::pair<int, int>> progress;
    options.progressCallback = [&progress](int completed, int total) {
        progress.append(std::make_pair(completed, total));
    };
    const auto result = RegexWorkbenchBatchRunner::Run(
        VariableRecipe(VariableScope::Batch), Paths(), OriginalTexts(),
        MediaTypes(), initial, options);
    Require(result.staged.success && result.validation.success &&
                result.staged.replacementCount == 4 &&
                result.staged.changedTexts.size() == 2 &&
                result.staged.changedTexts.value(QStringLiteral("Text/a.xhtml")) ==
                    QStringLiteral("<p>mark A2</p>") &&
                result.staged.changedTexts.value(QStringLiteral("Text/b.xhtml")) ==
                    QStringLiteral("<p>mark A2</p>"),
            "Batch-scoped variables must follow rule-outer/resource-inner order");
    Require(gSpcreConstructions == constructions + 2,
            "each rule must compile its replacement pattern once for the whole batch");
    Require(result.report.rows.size() == 2 && result.report.rowsTruncated &&
                result.report.omittedRowCount == 2 &&
                result.report.totalReplacements == 4 &&
                result.report.changedResourceCount == 2 &&
                result.report.rows.first().replacementCount == 1 &&
                result.report.rows.first().exactNavigationAvailable &&
                result.report.rows.first().coordinateSpace ==
                    BuiltinPlugins::RegexWorkbench::CoordinateSpace::Final &&
                result.report.rows.first().lineHint == 1 &&
                result.report.rows.first().exactSnapshotNavigationAvailable &&
                result.report.rows.first().snapshotMatchStart == 3 &&
                result.report.rows.first().snapshotMatchEnd == 5 &&
                result.report.rows.first().snapshotLineHint == 1 &&
                result.report.rows.first().beforeSnippet == QStringLiteral("A1") &&
                result.report.rows.first().afterSnippet == QStringLiteral("mark") &&
                result.report.rows.first().variableNames ==
                    QStringList{QStringLiteral("seed")} &&
                progress.first() == std::make_pair(0, 4) &&
                progress.last() == std::make_pair(4, 4),
            "batch report details must be bounded without losing exact totals");
    Require(result.finalStore.batchFrame.value(QStringLiteral("seed")).last() ==
                QStringLiteral("A2") && initial.stateData() == initialState,
            "batch execution must return a staged store without mutating its caller");
}

void TestResourceScopeIsolationAndRepeatability()
{
    SearchVariableStore initial;
    const auto first = RegexWorkbenchBatchRunner::Run(
        VariableRecipe(VariableScope::Resource), Paths(), OriginalTexts(),
        MediaTypes(), initial);
    const auto second = RegexWorkbenchBatchRunner::Run(
        VariableRecipe(VariableScope::Resource), Paths(), OriginalTexts(),
        MediaTypes(), initial);
    Require(first.staged.success && second.staged.success &&
                first.staged.changedTexts == second.staged.changedTexts &&
                first.staged.changedTexts.value(QStringLiteral("Text/a.xhtml")) ==
                    QStringLiteral("<p>mark A1</p>") &&
                first.staged.changedTexts.value(QStringLiteral("Text/b.xhtml")) ==
                    QStringLiteral("<p>mark A2</p>"),
            "resource variables must remain isolated and independent runs must restage");
}

void TestValidationLimitAndCancellationDiscardPublication()
{
    RegexRecipe invalid;
    invalid.name = QStringLiteral("invalid XML");
    RegexWorkbenchRule breakXml;
    breakXml.id = QStringLiteral("break");
    breakXml.name = QStringLiteral("break XML");
    breakXml.find = QStringLiteral("x");
    breakXml.replace = QStringLiteral("</p>");
    invalid.rules.append(breakXml);

    SearchVariableStore initial;
    const auto validationFailure = RegexWorkbenchBatchRunner::Run(
        invalid, Paths(), OriginalTexts(), MediaTypes(), initial);
    Require(!validationFailure.staged.success &&
                validationFailure.staged.changedTexts.isEmpty() &&
                !validationFailure.validation.success &&
                validationFailure.validation.issueCount == 2 &&
                validationFailure.report.fatal,
            "staged XML validation failure must discard all publishable texts");

    RegexWorkbenchBatchOptions limited;
    limited.maxRunReplacements = 1;
    const auto limitFailure = RegexWorkbenchBatchRunner::Run(
        VariableRecipe(VariableScope::Batch), Paths(), OriginalTexts(),
        MediaTypes(), initial, limited);
    Require(!limitFailure.staged.success &&
                limitFailure.staged.changedTexts.isEmpty() &&
                limitFailure.staged.error.contains(QStringLiteral("replacement limit")),
            "run-wide replacement limits must fail before publishing staged texts");

    RegexWorkbenchBatchOptions cancelled;
    cancelled.isCancelled = []() { return true; };
    const auto cancelResult = RegexWorkbenchBatchRunner::Run(
        VariableRecipe(VariableScope::Batch), Paths(), OriginalTexts(),
        MediaTypes(), initial, cancelled);
    Require(!cancelResult.staged.success && cancelResult.staged.cancelled &&
                cancelResult.staged.changedTexts.isEmpty(),
            "cancelled workbench batches must publish neither text nor store state");
}

void TestTargetAndMediaMetadataAreClosedWorld()
{
    SearchVariableStore initial;
    QStringList duplicate = Paths();
    duplicate.append(duplicate.first());
    const auto duplicateResult = RegexWorkbenchBatchRunner::Run(
        VariableRecipe(VariableScope::Batch), duplicate, OriginalTexts(),
        MediaTypes(), initial);
    Require(!duplicateResult.staged.success &&
                duplicateResult.staged.error.contains(QStringLiteral("duplicate")),
            "duplicate ordered resource paths must be rejected");

    QHash<QString, QString> missingMedia = MediaTypes();
    missingMedia.remove(QStringLiteral("Text/b.xhtml"));
    const auto missingMediaResult = RegexWorkbenchBatchRunner::Run(
        VariableRecipe(VariableScope::Batch), Paths(), OriginalTexts(),
        missingMedia, initial);
    Require(!missingMediaResult.staged.success &&
                missingMediaResult.staged.changedTexts.isEmpty() &&
                missingMediaResult.validation.issueCount == 1,
            "changed resources without snapshotted media metadata must fail validation");
}

void TestFinalNavigationTracksLaterRulesAndInvalidatesOverlap()
{
    RegexRecipe recipe;
    recipe.name = QStringLiteral("navigation");
    RegexWorkbenchRule first;
    first.id = QStringLiteral("first");
    first.name = QStringLiteral("expand a");
    first.find = QStringLiteral("a");
    first.replace = QStringLiteral("XX");
    recipe.rules.append(first);

    RegexWorkbenchRule second;
    second.id = QStringLiteral("second");
    second.name = QStringLiteral("replace b");
    second.find = QStringLiteral("b");
    second.replace = QStringLiteral("Y");
    recipe.rules.append(second);

    RegexWorkbenchRule overlap;
    overlap.id = QStringLiteral("overlap");
    overlap.name = QStringLiteral("replace prior result");
    overlap.find = QStringLiteral("XX");
    overlap.replace = QStringLiteral("Z");
    recipe.rules.append(overlap);

    const QString path = QStringLiteral("Text/plain.txt");
    const auto result = RegexWorkbenchBatchRunner::Run(
        recipe, QStringList{path}, {{path, QStringLiteral("abc")}},
        {{path, QStringLiteral("text/plain")}}, SearchVariableStore());
    Require(result.staged.success &&
                result.staged.changedTexts.value(path) == QStringLiteral("ZYc") &&
                result.report.rows.size() == 3 &&
                !result.report.rows.at(0).exactNavigationAvailable &&
                result.report.rows.at(0).matchStart == 0 &&
                result.report.rows.at(0).lineHint == 1 &&
                result.report.rows.at(0).exactSnapshotNavigationAvailable &&
                result.report.rows.at(0).snapshotMatchStart == 0 &&
                result.report.rows.at(0).snapshotMatchEnd == 1 &&
                result.report.rows.at(1).exactNavigationAvailable &&
                result.report.rows.at(1).matchStart == 1 &&
                result.report.rows.at(1).matchEnd == 2 &&
                result.report.rows.at(1).exactSnapshotNavigationAvailable &&
                result.report.rows.at(1).snapshotMatchStart == 1 &&
                result.report.rows.at(1).snapshotMatchEnd == 2 &&
                result.report.rows.at(2).exactNavigationAvailable &&
                result.report.rows.at(2).matchStart == 0 &&
                result.report.rows.at(2).matchEnd == 1 &&
                !result.report.rows.at(2).exactSnapshotNavigationAvailable &&
                result.report.rows.at(2).snapshotMatchStart == 0 &&
                result.report.rows.at(2).snapshotMatchEnd == -1 &&
                result.report.rows.at(2).snapshotLineHint == 1,
            "navigation must map final and snapshot coordinates across later edits");
}

void TestSnapshotNavigationPreservesOriginalLineAcrossInsertedNewlines()
{
    RegexRecipe recipe;
    recipe.name = QStringLiteral("line navigation");
    RegexWorkbenchRule insertLine;
    insertLine.id = QStringLiteral("insert-line");
    insertLine.name = QStringLiteral("insert staged line");
    insertLine.find = QStringLiteral("head");
    insertLine.replace = QStringLiteral("HEAD\nEXTRA");
    recipe.rules.append(insertLine);

    RegexWorkbenchRule replaceBody;
    replaceBody.id = QStringLiteral("replace-body");
    replaceBody.name = QStringLiteral("replace body");
    replaceBody.find = QStringLiteral("b");
    replaceBody.replace = QStringLiteral("B");
    recipe.rules.append(replaceBody);

    const QString path = QStringLiteral("Text/lines.txt");
    const auto result = RegexWorkbenchBatchRunner::Run(
        recipe, QStringList{path}, {{path, QStringLiteral("head\na b\n")}},
        {{path, QStringLiteral("text/plain")}}, SearchVariableStore());
    Require(result.staged.success && result.report.rows.size() == 2 &&
                result.report.rows.at(1).lineHint == 3 &&
                result.report.rows.at(1).exactSnapshotNavigationAvailable &&
                result.report.rows.at(1).snapshotMatchStart == 7 &&
                result.report.rows.at(1).snapshotMatchEnd == 8 &&
                result.report.rows.at(1).snapshotLineHint == 2,
            "Dry-Run navigation must display and select the original snapshot line");
}

}

int main()
{
    TestBatchScopeOrderAndPreparedReuse();
    TestResourceScopeIsolationAndRepeatability();
    TestValidationLimitAndCancellationDiscardPublication();
    TestTargetAndMediaMetadataAreClosedWorld();
    TestFinalNavigationTracksLaterRulesAndInvalidatesOverlap();
    TestSnapshotNavigationPreservesOriginalLineAcrossInsertedNewlines();
    std::cout << "regex workbench batch tests passed\n";
    return 0;
}
