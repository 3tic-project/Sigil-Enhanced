/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "ChineseConversion/ChineseConversionProfile.h"

#include <stdexcept>

ChineseConversionProfile::ChineseConversionProfile(ChineseConversionMode mode,
                                                     const QString& key,
                                                     const QString& configFile,
                                                     const QString& sourceLocale,
                                                     const QString& targetLocale)
    : m_Mode(mode),
      m_Key(key),
      m_ConfigFile(configFile),
      m_SourceLocale(sourceLocale),
      m_TargetLocale(targetLocale)
{
}

ChineseConversionMode ChineseConversionProfile::Mode() const
{
    return m_Mode;
}

QString ChineseConversionProfile::Key() const
{
    return m_Key;
}

QString ChineseConversionProfile::ConfigFile() const
{
    return m_ConfigFile;
}

QString ChineseConversionProfile::SourceLocale() const
{
    return m_SourceLocale;
}

QString ChineseConversionProfile::TargetLocale() const
{
    return m_TargetLocale;
}

QList<ChineseConversionProfile> ChineseConversionProfile::All()
{
    return {
        { ChineseConversionMode::S2T,   QStringLiteral("s2t"),   QStringLiteral("s2t.json"),   QStringLiteral("zh-CN"), QStringLiteral("zh-Hant") },
        { ChineseConversionMode::T2S,   QStringLiteral("t2s"),   QStringLiteral("t2s.json"),   QStringLiteral("zh-Hant"), QStringLiteral("zh-CN") },
        { ChineseConversionMode::S2TW,  QStringLiteral("s2tw"),  QStringLiteral("s2tw.json"),  QStringLiteral("zh-CN"), QStringLiteral("zh-TW") },
        { ChineseConversionMode::TW2S,  QStringLiteral("tw2s"),  QStringLiteral("tw2s.json"),  QStringLiteral("zh-TW"), QStringLiteral("zh-CN") },
        { ChineseConversionMode::S2HK,  QStringLiteral("s2hk"),  QStringLiteral("s2hk.json"),  QStringLiteral("zh-CN"), QStringLiteral("zh-HK") },
        { ChineseConversionMode::HK2S,  QStringLiteral("hk2s"),  QStringLiteral("hk2s.json"),  QStringLiteral("zh-HK"), QStringLiteral("zh-CN") },
        { ChineseConversionMode::S2TWP, QStringLiteral("s2twp"), QStringLiteral("s2twp.json"), QStringLiteral("zh-CN"), QStringLiteral("zh-TW") },
        { ChineseConversionMode::TW2SP, QStringLiteral("tw2sp"), QStringLiteral("tw2sp.json"), QStringLiteral("zh-TW"), QStringLiteral("zh-CN") },
        { ChineseConversionMode::T2TW,  QStringLiteral("t2tw"),  QStringLiteral("t2tw.json"),  QStringLiteral("zh-Hant"), QStringLiteral("zh-TW") },
        { ChineseConversionMode::TW2T,  QStringLiteral("tw2t"),  QStringLiteral("tw2t.json"),  QStringLiteral("zh-TW"), QStringLiteral("zh-Hant") },
        { ChineseConversionMode::T2HK,  QStringLiteral("t2hk"),  QStringLiteral("t2hk.json"),  QStringLiteral("zh-Hant"), QStringLiteral("zh-HK") },
        { ChineseConversionMode::HK2T,  QStringLiteral("hk2t"),  QStringLiteral("hk2t.json"),  QStringLiteral("zh-HK"), QStringLiteral("zh-Hant") }
    };
}

ChineseConversionProfile ChineseConversionProfile::ForMode(ChineseConversionMode mode)
{
    const auto profiles = All();
    for (const auto& profile : profiles) {
        if (profile.Mode() == mode) {
            return profile;
        }
    }
    throw std::invalid_argument("Unknown Chinese conversion mode");
}

std::optional<ChineseConversionMode> ChineseConversionProfile::ModeFromKey(const QString& key)
{
    const QString normalized = key.trimmed().toLower();
    const auto profiles = All();
    for (const auto& profile : profiles) {
        if (profile.Key() == normalized) {
            return profile.Mode();
        }
    }
    return std::nullopt;
}
