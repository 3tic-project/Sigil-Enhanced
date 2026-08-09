/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef REGEX_WORKBENCH_BATCH_RUNNER_H
#define REGEX_WORKBENCH_BATCH_RUNNER_H

#include <functional>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "BuiltinPlugins/RegexWorkbench/RegexRecipeStore.h"
#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchEngine.h"
#include "Misc/SearchBatchRunner.h"
#include "Misc/StagedTextValidator.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

enum class CoordinateSpace {
    Snapshot,
    Intermediate,
    Final
};

struct RegexWorkbenchReportRow
{
    QString ruleId;
    QString ruleName;
    QString bookpath;
    int iterationCount = 0;
    qint64 replacementCount = 0;
    CoordinateSpace coordinateSpace = CoordinateSpace::Intermediate;
    bool exactNavigationAvailable = false;
    QString beforeSnippet;
    QString afterSnippet;
};

struct RegexWorkbenchDryRunReport
{
    QList<RegexWorkbenchReportRow> rows;
    qint64 totalMatches = 0;
    qint64 totalReplacements = 0;
    int changedResourceCount = 0;
    bool fatal = false;
    QString fatalMessage;
    bool rowsTruncated = false;
    qint64 omittedRowCount = 0;
};

struct RegexWorkbenchBatchOptions
{
    RegexWorkbenchEngineOptions engineOptions;
    SearchBatch::StagedValidationOptions validationOptions;
    std::function<bool()> isCancelled;
    int maxReportRows = 10000;
    int maxSnippetCodeUnits = 240;
    qint64 maxRunReplacements = 1000000;
    bool validateStagedTexts = true;
};

struct RegexWorkbenchBatchResult
{
    SearchBatch::Result staged;
    SearchBatch::StagedValidationResult validation;
    RegexWorkbenchDryRunReport report;
    SearchVariableStore::Snapshot finalStore;
};

class RegexWorkbenchBatchRunner final
{
public:
    static RegexWorkbenchBatchResult Run(
        const RegexRecipe& recipe,
        const QStringList& orderedResourcePaths,
        const QHash<QString, QString>& originalTexts,
        const QHash<QString, QString>& mediaTypes,
        const SearchVariableStore& initialStore,
        RegexWorkbenchBatchOptions options = RegexWorkbenchBatchOptions());
};

}
}

#endif // REGEX_WORKBENCH_BATCH_RUNNER_H
