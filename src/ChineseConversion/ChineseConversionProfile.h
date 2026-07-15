/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once

#include <optional>

#include <QList>
#include <QString>

#include "ChineseConversion/ChineseConversionTypes.h"

class ChineseConversionProfile
{
public:
    ChineseConversionProfile() = default;
    ChineseConversionProfile(ChineseConversionMode mode,
                             const QString& key,
                             const QString& configFile,
                             const QString& sourceLocale,
                             const QString& targetLocale);

    ChineseConversionMode Mode() const;
    QString Key() const;
    QString ConfigFile() const;
    QString SourceLocale() const;
    QString TargetLocale() const;
    QString DisplayName() const;
    QString Description() const;

    static QList<ChineseConversionProfile> All();
    static ChineseConversionProfile ForMode(ChineseConversionMode mode);
    static std::optional<ChineseConversionMode> ModeFromKey(const QString& key);

private:
    ChineseConversionMode m_Mode = ChineseConversionMode::S2T;
    QString m_Key;
    QString m_ConfigFile;
    QString m_SourceLocale;
    QString m_TargetLocale;
};
