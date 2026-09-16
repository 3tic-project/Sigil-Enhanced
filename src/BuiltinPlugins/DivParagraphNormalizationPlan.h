/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#pragma once
#ifndef DIVPARAGRAPHNORMALIZATIONPLAN_H
#define DIVPARAGRAPHNORMALIZATIONPLAN_H

#include <functional>

#include <QString>
#include <QStringList>
#include <QVector>

#include "BuiltinPlugins/BookLiveParagraphNormalizer.h"

namespace BuiltinPlugins
{

class DivParagraphNormalizationPlan
{
public:
    enum class Status {
        Apply,
        Review,
        Skip,
        Error
    };

    struct Input {
        QString resourceId;
        QString text;
        QString baseRevision;
        QVector<DivParagraphCssAnalyzer::Source> stylesheets;
    };

    struct Entry {
        QString resourceId;
        QString baseRevision;
        QString source;
        QString beforeHash;
        QString cssHash;
        QString afterHash;
        Status status = Status::Skip;
        BookLiveParagraphNormalizer::Analysis analysis;
        QString output;
        QStringList messages;
    };

    struct Result {
        bool ok = false;
        bool cancelled = false;
        QString planId;
        QString ruleVersion;
        QString presetId;
        QVector<Entry> entries;
        QStringList errors;
        int applyFiles = 0;
        int reviewFiles = 0;
        int skippedFiles = 0;
        int errorFiles = 0;
        int conversionCount = 0;
        int protectedCount = 0;
    };

    using ProgressFunction = std::function<bool(int completed, int total)>;

    static Result build(const QVector<Input>& inputs,
                        const BookLiveParagraphNormalizer::Options& options,
                        const ProgressFunction& progress = ProgressFunction());
    static Result buildCmoa(const QVector<Input>& inputs,
                            const BookLiveParagraphNormalizer::Options& options,
                            const ProgressFunction& progress = ProgressFunction());
    static QStringList revisionConflicts(const Result& plan,
                                         const QVector<Input>& currentInputs);
    static void refreshIdentity(Result& plan);
    static QString hashText(const QString& text);
    static QString hashStylesheets(
        const QVector<DivParagraphCssAnalyzer::Source>& stylesheets);
};

}

#endif
