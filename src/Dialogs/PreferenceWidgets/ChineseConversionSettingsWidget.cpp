/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#include "Dialogs/PreferenceWidgets/ChineseConversionSettingsWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

#include "ChineseConversion/ChineseConversionProfile.h"
#include "ChineseConversion/ChineseConversionSettings.h"

namespace
{

int FindData(QComboBox *combo, int value)
{
    const int index = combo->findData(value);
    return index < 0 ? 0 : index;
}

}

ChineseConversionSettingsWidget::ChineseConversionSettingsWidget(QWidget *parent)
    : PreferencesWidget()
{
    setParent(parent);
    setWindowTitle(tr("Chinese Conversion"));

    auto *root = new QVBoxLayout(this);
    auto *defaults = new QGroupBox(tr("Defaults"), this);
    auto *defaultsLayout = new QFormLayout(defaults);
    m_Mode = new QComboBox(defaults);
    for (const ChineseConversionProfile& profile : ChineseConversionProfile::All()) {
        m_Mode->addItem(profile.DisplayName(), static_cast<int>(profile.Mode()));
    }
    m_Scope = new QComboBox(defaults);
    m_Scope->addItem(tr("Current file"), static_cast<int>(ChineseConversionScope::CurrentFile));
    m_Scope->addItem(tr("Current selection"), static_cast<int>(ChineseConversionScope::CurrentSelection));
    defaultsLayout->addRow(tr("Conversion mode:"), m_Mode);
    defaultsLayout->addRow(tr("Scope:"), m_Scope);

    auto *attributes = new QGroupBox(tr("Text attributes"), this);
    auto *attributesLayout = new QVBoxLayout(attributes);
    m_IncludeAltText = new QCheckBox(tr("Alternative text (alt)"), attributes);
    m_IncludeTitleAttributes = new QCheckBox(tr("Title attributes"), attributes);
    m_IncludeAriaLabels = new QCheckBox(tr("Accessible labels (ARIA)"), attributes);
    attributesLayout->addWidget(m_IncludeAltText);
    attributesLayout->addWidget(m_IncludeTitleAttributes);
    attributesLayout->addWidget(m_IncludeAriaLabels);

    auto *protection = new QGroupBox(tr("Protected content"), this);
    auto *protectionLayout = new QVBoxLayout(protection);
    m_PreserveJapanese = new QCheckBox(tr("Skip Japanese language content"), protection);
    m_SkipCode = new QCheckBox(tr("Skip code, kbd, samp, and var elements"), protection);
    m_SkipPre = new QCheckBox(tr("Skip pre elements"), protection);
    protectionLayout->addWidget(m_PreserveJapanese);
    protectionLayout->addWidget(m_SkipCode);
    protectionLayout->addWidget(m_SkipPre);

    auto *workflow = new QGroupBox(tr("Workflow"), this);
    auto *workflowLayout = new QVBoxLayout(workflow);
    m_PreviewBeforeApply = new QCheckBox(tr("Preview changes before applying"), workflow);
    workflowLayout->addWidget(m_PreviewBeforeApply);

    root->addWidget(defaults);
    root->addWidget(attributes);
    root->addWidget(protection);
    root->addWidget(workflow);
    root->addStretch();

    const ChineseConversionOptions options = ChineseConversionSettings::Load();
    m_Mode->setCurrentIndex(FindData(m_Mode, static_cast<int>(options.mode)));
    m_Scope->setCurrentIndex(FindData(m_Scope, static_cast<int>(options.scope)));
    m_IncludeAltText->setChecked(options.includeAltText);
    m_IncludeTitleAttributes->setChecked(options.includeTitleAttributes);
    m_IncludeAriaLabels->setChecked(options.includeAriaLabels);
    m_PreserveJapanese->setChecked(options.preserveJapaneseText);
    m_SkipCode->setChecked(options.skipCodeElements);
    m_SkipPre->setChecked(options.skipPreElements);
    m_PreviewBeforeApply->setChecked(options.previewBeforeApply);
}

PreferencesWidget::ResultActions ChineseConversionSettingsWidget::saveSettings()
{
    ChineseConversionOptions options = ChineseConversionSettings::Load();
    options.mode = static_cast<ChineseConversionMode>(m_Mode->currentData().toInt());
    options.scope = static_cast<ChineseConversionScope>(m_Scope->currentData().toInt());
    options.includeAltText = m_IncludeAltText->isChecked();
    options.includeTitleAttributes = m_IncludeTitleAttributes->isChecked();
    options.includeAriaLabels = m_IncludeAriaLabels->isChecked();
    options.preserveJapaneseText = m_PreserveJapanese->isChecked();
    options.skipCodeElements = m_SkipCode->isChecked();
    options.skipPreElements = m_SkipPre->isChecked();
    options.previewBeforeApply = m_PreviewBeforeApply->isChecked();
    ChineseConversionSettings::Save(options);
    return ResultAction_None;
}
