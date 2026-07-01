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
#ifndef EPUBSTRUCTURENORMALIZER_H
#define EPUBSTRUCTURENORMALIZER_H

#include <QList>

#include "Misc/ValidationResult.h"

class Book;

namespace BuiltinPlugins
{

class EpubStructureNormalizer
{
public:
    struct Options {
        bool repairOpfManifest = true;
        bool repairLinkCase = true;
        bool dryRun = false;
    };

    struct Result {
        QList<ValidationResult> validationResults;
        bool bookBrowserRefreshRequired = false;
        bool modified = false;
    };

    explicit EpubStructureNormalizer(Book* book);

    Result normalize();
    Result normalize(const Options& options);
    Result normalizeOpfManifest();
    Result normalizeOpfManifest(const Options& options);
    Result normalizeLinkCase();
    Result normalizeLinkCase(const Options& options);

private:
    void appendResult(Result& target, const Result& source) const;
    void addResult(Result& result,
                   ValidationResult::ResType type,
                   const QString& bookpath,
                   const QString& message) const;

    Book* m_Book;
};

}

#endif
