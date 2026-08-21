/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "MainUI/RegexWorkbenchBatchCommitter.h"

#include <QCoreApplication>

SearchBatch::Result RegexWorkbenchBatchCommitter::Commit(
    MainWindow* main_window,
    const QHash<QString, TextResource*>& resources,
    const SearchBatchCoordinator::Snapshot& snapshot,
    const BuiltinPlugins::RegexWorkbench::RegexWorkbenchBatchResult& batch_result,
    BuiltinPlugins::RegexWorkbench::SearchVariableStore& store,
    bool create_recovery_checkpoint)
{
    SearchBatch::Result result = batch_result.staged;
    if (!result.success) {
        return result;
    }
    if (!batch_result.validation.success) {
        result.success = false;
        result.error = batch_result.validation.error.isEmpty()
                           ? QCoreApplication::translate(
                                 "RegexWorkbenchCore",
                                 "Regex workbench staged validation did not succeed")
                           : batch_result.validation.error;
        return result;
    }

    BuiltinPlugins::RegexWorkbench::SearchVariableStore pendingStore = store;
    QString storeError;
    if (!pendingStore.restore(batch_result.finalStore, &storeError)) {
        result.success = false;
        result.error = QCoreApplication::translate(
                           "RegexWorkbenchCore",
                           "Regex workbench variable state is invalid: %1")
                           .arg(storeError);
        return result;
    }

    if (result.changedTexts.isEmpty()) {
        QString conflictPath;
        if (!SearchBatchCoordinator::ResourcesMatchSnapshot(
                resources, snapshot, &conflictPath)) {
            result.success = false;
            result.error = QCoreApplication::translate(
                               "RegexWorkbenchCore",
                               "Search batch target changed during staging: %1")
                               .arg(conflictPath);
            return result;
        }
    }

    result = SearchBatchCoordinator::CommitStagedResult(
        main_window, resources, snapshot, result, create_recovery_checkpoint);
    if (result.success) {
        store = pendingStore;
    }
    return result;
}
