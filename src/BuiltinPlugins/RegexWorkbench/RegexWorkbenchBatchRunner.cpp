/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchBatchRunner.h"

#include <limits>
#include <memory>
#include <vector>

#include <QSet>

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchVariableExecutor.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

namespace
{

QString Snippet(const QString& text, int maximum)
{
    if (text.size() <= maximum) {
        return text;
    }
    const int leftSize = maximum / 2;
    const int rightSize = maximum - leftSize - 1;
    return text.left(leftSize) + QChar(0x2026) + text.right(rightSize);
}

void Fail(RegexWorkbenchBatchResult& result, const QString& error)
{
    result.staged.success = false;
    result.staged.changedTexts.clear();
    result.staged.error = error;
    result.report.rows.clear();
    result.report.fatal = true;
    result.report.fatalMessage = error;
}

}

RegexWorkbenchBatchResult RegexWorkbenchBatchRunner::Run(
    const RegexRecipe& recipe,
    const QStringList& orderedResourcePaths,
    const QHash<QString, QString>& originalTexts,
    const QHash<QString, QString>& mediaTypes,
    const SearchVariableStore& initialStore,
    RegexWorkbenchBatchOptions options)
{
    RegexWorkbenchBatchResult result;
    result.finalStore = initialStore.snapshot();
    if (options.maxReportRows <= 0 || options.maxSnippetCodeUnits <= 0 ||
        options.maxRunReplacements <= 0) {
        Fail(result, QStringLiteral("Invalid regex workbench batch limits"));
        return result;
    }
    QString recipeError;
    if (!RegexRecipeStore::Validate(recipe, &recipeError)) {
        Fail(result, recipeError);
        return result;
    }

    QSet<QString> seenPaths;
    for (const QString& path : orderedResourcePaths) {
        if (path.isEmpty() || seenPaths.contains(path) || !originalTexts.contains(path)) {
            Fail(result, path.isEmpty()
                             ? QStringLiteral("Regex workbench batch contains an empty resource path")
                             : seenPaths.contains(path)
                                   ? QStringLiteral("Regex workbench batch contains duplicate resource path: %1")
                                         .arg(path)
                                   : QStringLiteral("Regex workbench batch target is missing: %1")
                                         .arg(path));
            return result;
        }
        seenPaths.insert(path);
    }

    std::vector<std::unique_ptr<PreparedRegexWorkbenchVariableExecutor>> prepared;
    prepared.reserve(static_cast<size_t>(recipe.rules.size()));
    QHash<QString, int> ruleIndexes;
    QList<SearchBatch::Rule> batchRules;
    batchRules.reserve(recipe.rules.size());
    for (int index = 0; index < recipe.rules.size(); ++index) {
        const RegexWorkbenchRule& rule = recipe.rules.at(index);
        auto executor = std::make_unique<PreparedRegexWorkbenchVariableExecutor>(rule);
        if (!executor->isValid()) {
            Fail(result, QStringLiteral("Regex workbench rule %1 failed to compile: %2")
                             .arg(rule.name, executor->errorMessage()));
            return result;
        }
        prepared.push_back(std::move(executor));
        ruleIndexes.insert(rule.id, index);

        SearchBatch::Rule batchRule;
        batchRule.id = rule.id;
        batchRule.name = rule.name;
        batchRule.searchRegex = rule.find;
        batchRule.replacement = rule.replace;
        batchRule.resourcePaths = orderedResourcePaths;
        batchRules.append(batchRule);
    }

    SearchVariableStore workingStore = initialStore;
    workingStore.clearRunLocals();
    workingStore.setScope(recipe.variableScope);
    workingStore.setWritePolicy(recipe.writePolicy);

    const auto callerMatchCancellation = options.engineOptions.matchOptions.isCancelled;
    options.engineOptions.matchOptions.isCancelled =
        [cancel = options.isCancelled, callerMatchCancellation]() {
            return (cancel && cancel()) ||
                   (callerMatchCancellation && callerMatchCancellation());
        };

    qint64 observedReplacements = 0;
    result.staged = SearchBatch::Runner::Run(
        batchRules, originalTexts,
        [&](const SearchBatch::Rule& batchRule,
            const QString& resourcePath,
            const QString& currentText) {
            SearchBatch::ApplyResult applied;
            const int ruleIndex = ruleIndexes.value(batchRule.id, -1);
            if (ruleIndex < 0 || ruleIndex >= static_cast<int>(prepared.size())) {
                applied.ok = false;
                applied.error = QStringLiteral("Prepared regex workbench rule is missing: %1")
                                    .arg(batchRule.id);
                return applied;
            }

            workingStore.setActiveResource(resourcePath);
            const RegexWorkbenchEngineResult engineResult =
                prepared.at(static_cast<size_t>(ruleIndex))->Apply(
                    currentText, workingStore, options.engineOptions);
            if (!engineResult.success) {
                applied.ok = false;
                applied.text = currentText;
                applied.error = QStringLiteral("Regex workbench rule %1 failed for %2: %3")
                                    .arg(batchRule.name, resourcePath,
                                         engineResult.errorMessage);
                return applied;
            }
            if (engineResult.replacementCount >
                std::numeric_limits<qint64>::max() - observedReplacements) {
                applied.ok = false;
                applied.text = currentText;
                applied.error = QStringLiteral("Regex workbench replacement count overflow");
                return applied;
            }
            observedReplacements += engineResult.replacementCount;
            if (observedReplacements > options.maxRunReplacements) {
                applied.ok = false;
                applied.text = currentText;
                applied.error = QStringLiteral("Regex workbench run exceeded replacement limit %1")
                                    .arg(options.maxRunReplacements);
                return applied;
            }

            applied.text = engineResult.text;
            applied.replacementCount = engineResult.replacementCount;
            if (engineResult.replacementCount > 0) {
                RegexWorkbenchReportRow row;
                row.ruleId = batchRule.id;
                row.ruleName = batchRule.name;
                row.bookpath = resourcePath;
                row.iterationCount = engineResult.appliedIterations;
                row.replacementCount = engineResult.replacementCount;
                row.beforeSnippet = Snippet(currentText, options.maxSnippetCodeUnits);
                row.afterSnippet = Snippet(engineResult.text,
                                           options.maxSnippetCodeUnits);
                if (result.report.rows.size() < options.maxReportRows) {
                    result.report.rows.append(row);
                } else {
                    result.report.rowsTruncated = true;
                    ++result.report.omittedRowCount;
                }
            }
            return applied;
        },
        options.isCancelled);

    result.report.totalMatches = result.staged.replacementCount;
    result.report.totalReplacements = result.staged.replacementCount;
    if (!result.staged.success) {
        result.report.fatal = true;
        result.report.fatalMessage = result.staged.error;
        result.report.rows.clear();
        return result;
    }

    if (options.validateStagedTexts) {
        const auto validationCancellation = options.validationOptions.isCancelled;
        options.validationOptions.isCancelled =
            [cancel = options.isCancelled, validationCancellation]() {
                return (cancel && cancel()) ||
                       (validationCancellation && validationCancellation());
            };
        result.validation = SearchBatch::StagedTextValidator::Validate(
            result.staged.changedTexts, mediaTypes, options.validationOptions);
        if (!result.validation.success) {
            result.staged.cancelled = result.validation.cancelled;
            Fail(result, result.validation.error);
            return result;
        }
    } else {
        result.validation.success = true;
    }

    result.report.changedResourceCount = result.staged.changedTexts.size();
    result.finalStore = workingStore.snapshot();
    return result;
}

}
}
