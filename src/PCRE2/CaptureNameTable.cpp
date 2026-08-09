/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "PCRE2/CaptureNameTable.h"

#include <QSet>

namespace PCRE2Helpers
{

QStringList CaptureNames(const pcre2_code_16* code)
{
    QStringList names;
    if (code == nullptr) {
        return names;
    }

    uint32_t nameCount = 0;
    uint32_t entrySize = 0;
    PCRE2_SPTR16 table = nullptr;
    if (pcre2_pattern_info_16(code, PCRE2_INFO_NAMECOUNT, &nameCount) != 0 ||
        nameCount == 0 ||
        pcre2_pattern_info_16(code, PCRE2_INFO_NAMEENTRYSIZE, &entrySize) != 0 ||
        pcre2_pattern_info_16(code, PCRE2_INFO_NAMETABLE, &table) != 0 ||
        table == nullptr || entrySize < 2) {
        return names;
    }

    QSet<QString> seen;
    for (uint32_t index = 0; index < nameCount; ++index) {
        const PCRE2_SPTR16 nameStart = table + 1;
        int length = 0;
        while (length < static_cast<int>(entrySize - 1) && nameStart[length] != 0) {
            ++length;
        }
        const QString name = QString::fromUtf16(
            reinterpret_cast<const char16_t*>(nameStart), length);
        if (!name.isEmpty() && !seen.contains(name)) {
            names.append(name);
            seen.insert(name);
        }
        table += entrySize;
    }
    return names;
}

}
