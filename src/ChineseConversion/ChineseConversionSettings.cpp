/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "ChineseConversion/ChineseConversionSettings.h"

#include "ChineseConversion/ChineseConversionProfile.h"

namespace
{

const char *SettingsGroup = "chinese_conversion";

}

ChineseConversionOptions ChineseConversionSettings::LoadFrom(QSettings& settings)
{
    ChineseConversionOptions options;
    settings.beginGroup(QLatin1String(SettingsGroup));

    const auto mode = ChineseConversionProfile::ModeFromKey(
        settings.value(QStringLiteral("mode"), QStringLiteral("s2t")).toString());
    options.mode = mode.value_or(ChineseConversionMode::S2T);
    options.scope = ScopeFromKey(
        settings.value(QStringLiteral("scope"), QStringLiteral("current_file")).toString());
    options.includeNav = settings.value(QStringLiteral("include_nav"), true).toBool();
    options.includeNcx = settings.value(QStringLiteral("include_ncx"), true).toBool();
    options.includeMetadata = settings.value(QStringLiteral("include_metadata"), false).toBool();
    options.includeAltText = settings.value(QStringLiteral("include_alt_text"), true).toBool();
    options.includeTitleAttributes = settings.value(QStringLiteral("include_title_attributes"), true).toBool();
    options.includeAriaLabels = settings.value(QStringLiteral("include_aria_labels"), true).toBool();
    options.skipCodeElements = settings.value(QStringLiteral("skip_code_elements"), true).toBool();
    options.skipPreElements = settings.value(QStringLiteral("skip_pre_elements"), false).toBool();
    options.preserveJapaneseText = settings.value(QStringLiteral("preserve_japanese_text"), true).toBool();
    options.previewBeforeApply = settings.value(QStringLiteral("preview_before_apply"), true).toBool();
    options.updateLanguageMetadata = settings.value(QStringLiteral("update_language_metadata"), false).toBool();

    settings.endGroup();
    return options;
}

void ChineseConversionSettings::SaveTo(QSettings& settings,
                                         const ChineseConversionOptions& options)
{
    settings.beginGroup(QLatin1String(SettingsGroup));
    settings.setValue(QStringLiteral("mode"),
                      ChineseConversionProfile::ForMode(options.mode).Key());
    settings.setValue(QStringLiteral("scope"), ScopeKey(options.scope));
    settings.setValue(QStringLiteral("include_nav"), options.includeNav);
    settings.setValue(QStringLiteral("include_ncx"), options.includeNcx);
    settings.setValue(QStringLiteral("include_metadata"), options.includeMetadata);
    settings.setValue(QStringLiteral("include_alt_text"), options.includeAltText);
    settings.setValue(QStringLiteral("include_title_attributes"), options.includeTitleAttributes);
    settings.setValue(QStringLiteral("include_aria_labels"), options.includeAriaLabels);
    settings.setValue(QStringLiteral("skip_code_elements"), options.skipCodeElements);
    settings.setValue(QStringLiteral("skip_pre_elements"), options.skipPreElements);
    settings.setValue(QStringLiteral("preserve_japanese_text"), options.preserveJapaneseText);
    settings.setValue(QStringLiteral("preview_before_apply"), options.previewBeforeApply);
    settings.setValue(QStringLiteral("update_language_metadata"), options.updateLanguageMetadata);
    settings.endGroup();
}

QString ChineseConversionSettings::ScopeKey(ChineseConversionScope scope)
{
    switch (scope) {
    case ChineseConversionScope::CurrentSelection:      return QStringLiteral("current_selection");
    case ChineseConversionScope::CurrentFile:           return QStringLiteral("current_file");
    case ChineseConversionScope::SelectedResources:     return QStringLiteral("selected_resources");
    case ChineseConversionScope::AllTextResources:      return QStringLiteral("all_text_resources");
    case ChineseConversionScope::WholeBookWithMetadata: return QStringLiteral("whole_book_with_metadata");
    }
    return QStringLiteral("current_file");
}

ChineseConversionScope ChineseConversionSettings::ScopeFromKey(const QString& key)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("current_selection")) {
        return ChineseConversionScope::CurrentSelection;
    }
    if (normalized == QStringLiteral("selected_resources")) {
        return ChineseConversionScope::SelectedResources;
    }
    if (normalized == QStringLiteral("all_text_resources")) {
        return ChineseConversionScope::AllTextResources;
    }
    if (normalized == QStringLiteral("whole_book_with_metadata")) {
        return ChineseConversionScope::WholeBookWithMetadata;
    }
    return ChineseConversionScope::CurrentFile;
}
