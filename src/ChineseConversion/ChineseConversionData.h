/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#pragma once

#include <QString>
#include <QStringList>

class ChineseConversionData final
{
public:
    static QString FindDataDirectory();
    static QStringList CandidateDirectories();
};
