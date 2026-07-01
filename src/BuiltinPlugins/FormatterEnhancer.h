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
#ifndef FORMATTERENHANCER_H
#define FORMATTERENHANCER_H

#include <QList>
#include <QStringList>

#include "Misc/ValidationResult.h"

class Book;

namespace BuiltinPlugins
{

class FormatterEnhancer
{
public:
    struct Options {
        bool formatXhtml = true;
        bool formatCss = true;
        bool cssMultipleLineFormat = true;
        bool dryRun = false;
    };

    struct FormatResult {
        QString text;
        bool ok = false;
        bool changed = false;
        QStringList messages;
    };

    struct Result {
        QList<ValidationResult> validationResults;
        bool modified = false;
        int xhtmlResourcesChecked = 0;
        int cssResourcesChecked = 0;
        int resourcesChanged = 0;
        int resourcesSkipped = 0;
        int errorCount = 0;
        qint64 elapsedMs = 0;
    };

    explicit FormatterEnhancer(Book* book);

    Result format();
    Result format(const Options& options);

    static FormatResult formatXhtmlText(const QString& source,
                                        const QString& epubVersion,
                                        const QString& xhtmlFormatConfig);
    static FormatResult formatCssText(const QString& source,
                                      bool multipleLineFormat);

private:
    void addResult(Result& result,
                   ValidationResult::ResType type,
                   const QString& bookpath,
                   const QString& message) const;

    Book* m_Book;
};

}

#endif
