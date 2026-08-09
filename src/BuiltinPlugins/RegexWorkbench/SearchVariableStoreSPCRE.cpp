/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "BuiltinPlugins/RegexWorkbench/SearchVariableStore.h"

#include "PCRE2/SPCRE.h"

namespace BuiltinPlugins
{
namespace RegexWorkbench
{

bool SearchVariableStore::ingestNamedCaptures(
    SPCRE& regex,
    const QString& matchText,
    const QList<std::pair<int, int>>& captures,
    const QStringList& onlyNames,
    QString* error)
{
    const QStringList names = regex.getCaptureNames();
    QHash<QString, int> captureNumbers;
    for (const QString& name : names) {
        captureNumbers.insert(name, regex.getCaptureStringNumber(name));
    }
    return ingestNamedCaptures(captureNumbers, matchText, captures, onlyNames, error);
}

}
}
