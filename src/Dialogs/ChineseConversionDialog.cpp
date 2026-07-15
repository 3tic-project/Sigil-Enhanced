/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#include "Dialogs/ChineseConversionDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ChineseConversion/ChineseConversionProfile.h"

namespace
{

int FindData(QComboBox *combo, int value)
{
    const int index = combo->findData(value);
    return index < 0 ? 0 : index;
}

}

ChineseConversionDialog::ChineseConversionDialog(
    const ChineseConversionOptions& options,
    bool selectionAvailable,
    const QString& resourcePath,
    QWidget *parent)
    : QDialog(parent),
      m_InitialOptions(options)
{
    setWindowTitle(tr("Chinese Conversion"));
    setMinimumWidth(560);

    auto *root = new QVBoxLayout(this);
    auto *target = new QLabel(resourcePath, this);
    target->setTextInteractionFlags(Qt::TextSelectableByMouse);
    target->setWordWrap(true);
    root->addWidget(target);

    auto *conversion = new QGroupBox(tr("Conversion"), this);
    auto *conversionLayout = new QFormLayout(conversion);
    m_Mode = new QComboBox(conversion);
    for (const ChineseConversionProfile& profile : ChineseConversionProfile::All()) {
        m_Mode->addItem(profile.DisplayName(), static_cast<int>(profile.Mode()));
    }
    m_Scope = new QComboBox(conversion);
    if (selectionAvailable) {
        m_Scope->addItem(tr("Current selection"),
                         static_cast<int>(ChineseConversionScope::CurrentSelection));
    }
    m_Scope->addItem(tr("Current file"), static_cast<int>(ChineseConversionScope::CurrentFile));
    m_Description = new QLabel(conversion);
    m_Description->setWordWrap(true);
    conversionLayout->addRow(tr("Mode:"), m_Mode);
    conversionLayout->addRow(tr("Scope:"), m_Scope);
    conversionLayout->addRow(QString(), m_Description);

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

    m_PreviewBeforeApply = new QCheckBox(tr("Preview changes before applying"), this);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton *preview = buttons->addButton(tr("Preview"), QDialogButtonBox::ActionRole);
    QPushButton *convert = buttons->addButton(tr("Convert"), QDialogButtonBox::AcceptRole);
    convert->setDefault(true);

    root->addWidget(conversion);
    root->addWidget(attributes);
    root->addWidget(protection);
    root->addWidget(m_PreviewBeforeApply);
    root->addWidget(buttons);

    m_Mode->setCurrentIndex(FindData(m_Mode, static_cast<int>(options.mode)));
    m_Scope->setCurrentIndex(FindData(m_Scope, static_cast<int>(options.scope)));
    m_IncludeAltText->setChecked(options.includeAltText);
    m_IncludeTitleAttributes->setChecked(options.includeTitleAttributes);
    m_IncludeAriaLabels->setChecked(options.includeAriaLabels);
    m_PreserveJapanese->setChecked(options.preserveJapaneseText);
    m_SkipCode->setChecked(options.skipCodeElements);
    m_SkipPre->setChecked(options.skipPreElements);
    m_PreviewBeforeApply->setChecked(options.previewBeforeApply);
    UpdateDescription();

    connect(m_Mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChineseConversionDialog::UpdateDescription);
    connect(preview, &QPushButton::clicked, this, &ChineseConversionDialog::RequestPreview);
    connect(convert, &QPushButton::clicked, this, &ChineseConversionDialog::RequestConvert);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ChineseConversionOptions ChineseConversionDialog::Options() const
{
    ChineseConversionOptions options = m_InitialOptions;
    options.mode = static_cast<ChineseConversionMode>(m_Mode->currentData().toInt());
    options.scope = static_cast<ChineseConversionScope>(m_Scope->currentData().toInt());
    options.includeAltText = m_IncludeAltText->isChecked();
    options.includeTitleAttributes = m_IncludeTitleAttributes->isChecked();
    options.includeAriaLabels = m_IncludeAriaLabels->isChecked();
    options.preserveJapaneseText = m_PreserveJapanese->isChecked();
    options.skipCodeElements = m_SkipCode->isChecked();
    options.skipPreElements = m_SkipPre->isChecked();
    options.previewBeforeApply = m_PreviewBeforeApply->isChecked();
    return options;
}

ChineseConversionDialog::RequestedAction ChineseConversionDialog::Action() const
{
    return m_Action;
}

void ChineseConversionDialog::UpdateDescription()
{
    const auto mode = static_cast<ChineseConversionMode>(m_Mode->currentData().toInt());
    m_Description->setText(ChineseConversionProfile::ForMode(mode).Description());
}

void ChineseConversionDialog::RequestPreview()
{
    m_Action = RequestedAction::Preview;
    accept();
}

void ChineseConversionDialog::RequestConvert()
{
    m_Action = RequestedAction::Convert;
    accept();
}
