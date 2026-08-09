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

#pragma once
#ifndef SEARCH_TEMPLATE_COMPATIBILITY_H
#define SEARCH_TEMPLATE_COMPATIBILITY_H

#include <QString>

namespace SearchTemplateCompatibility
{

inline bool CorrectLegacyJapaneseQuoteSpacing(const QString& name,
                                              const QString& fullName,
                                              QString& find,
                                              QString& replacement)
{
    static const QString legacyFind = QStringLiteral(
        "(?|^([^「 」]*)」$(?#末尾孤引号匹配)|[「 」]([^「 」]*)(?:[「 」]|$)(?#强制成对匹配))");
    if ((name != QStringLiteral("日文引号纠正") &&
         !name.endsWith(QStringLiteral("/日文引号纠正")) &&
         !fullName.endsWith(QStringLiteral("/日文引号纠正"))) ||
        find != legacyFind || replacement != QStringLiteral("「 \\1」")) {
        return false;
    }
    find = QStringLiteral(
        "(?|^([^「」]*)」$(?#末尾孤引号匹配)|[「」]([^「」]*)(?:[「」]|$)(?#强制成对匹配))");
    replacement = QStringLiteral("「\\1」");
    return true;
}

}

#endif // SEARCH_TEMPLATE_COMPATIBILITY_H
