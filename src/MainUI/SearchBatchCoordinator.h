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

#include "Misc/SearchBatchRunner.h"

class MainWindow;
class TextResource;

class SearchBatchCoordinator final
{
public:
    static SearchBatch::Result Run(MainWindow* main_window,
                                   const QList<SearchBatch::Rule>& rules,
                                   const QHash<QString, TextResource*>& resources,
                                   const SearchBatch::ApplyFunction& apply);

private:
    static bool ResourcesMatchSnapshot(const QHash<QString, TextResource*>& resources,
                                       const QHash<QString, QString>& original_texts,
                                       QString* conflict_path);
};
