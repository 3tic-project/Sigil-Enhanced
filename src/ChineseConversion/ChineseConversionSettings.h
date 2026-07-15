/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once

#include <QSettings>

#include "ChineseConversion/ChineseConversionTypes.h"

class ChineseConversionSettings final
{
public:
    static ChineseConversionOptions Load();
    static void Save(const ChineseConversionOptions& options);

    static ChineseConversionOptions LoadFrom(QSettings& settings);
    static void SaveTo(QSettings& settings, const ChineseConversionOptions& options);

    static QString ScopeKey(ChineseConversionScope scope);
    static ChineseConversionScope ScopeFromKey(const QString& key);
};
