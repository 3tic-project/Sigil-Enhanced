/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef REGEX_WORKBENCH_BATCH_COMMITTER_H
#define REGEX_WORKBENCH_BATCH_COMMITTER_H

#include <QHash>

#include "BuiltinPlugins/RegexWorkbench/RegexWorkbenchBatchRunner.h"
#include "MainUI/SearchBatchCoordinator.h"

class MainWindow;
class TextResource;

class RegexWorkbenchBatchCommitter final
{
public:
    static SearchBatch::Result Commit(
        MainWindow* main_window,
        const QHash<QString, TextResource*>& resources,
        const SearchBatchCoordinator::Snapshot& snapshot,
        const BuiltinPlugins::RegexWorkbench::RegexWorkbenchBatchResult& batch_result,
        BuiltinPlugins::RegexWorkbench::SearchVariableStore& store,
        bool create_recovery_checkpoint = true);
};

#endif // REGEX_WORKBENCH_BATCH_COMMITTER_H
