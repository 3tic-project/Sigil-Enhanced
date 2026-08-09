/************************************************************************
**
**  Copyright (C) 2026  Sigil-Enhanced contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include "Misc/SearchBatchRunner.h"

class MainWindow;
class TextResource;

class SearchBatchCoordinator final
{
public:
    struct Snapshot {
        QStringList resourcePaths;
        QHash<QString, QString> originalTexts;
        QHash<QString, QString> mediaTypes;
    };

    // GUI-thread boundary: flush open tabs and copy all worker input.
    static bool CaptureSnapshot(MainWindow* main_window,
                                const QStringList& ordered_paths,
                                const QHash<QString, TextResource*>& resources,
                                Snapshot& snapshot,
                                QString* error = nullptr);

    // GUI-thread boundary: conflict check, one checkpoint, then one undoable
    // write per changed resource. The staged result is never recomputed here.
    static SearchBatch::Result CommitStagedResult(
        MainWindow* main_window,
        const QHash<QString, TextResource*>& resources,
        const Snapshot& snapshot,
        const SearchBatch::Result& staged_result);

    // Compatibility wrapper for saved-search batches.
    static SearchBatch::Result Run(MainWindow* main_window,
                                   const QList<SearchBatch::Rule>& rules,
                                   const QHash<QString, TextResource*>& resources,
                                   const SearchBatch::ApplyFunction& apply);

private:
    static bool ResourcesMatchSnapshot(const QHash<QString, TextResource*>& resources,
                                       const Snapshot& snapshot,
                                       QString* conflict_path);
};
