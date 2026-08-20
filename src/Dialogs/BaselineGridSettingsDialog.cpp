/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#include "Dialogs/BaselineGridSettingsDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtMath>

BaselineGridSettingsDialog::BaselineGridSettingsDialog(
    const BaselineGridSettings &settings,
    qreal currentElementFontPx,
    bool darkTheme,
    QWidget *parent)
    : QDialog(parent),
      m_currentElementFontPx(currentElementFontPx),
      m_darkTheme(darkTheme),
      m_showGrid(new QCheckBox(tr("Show baseline grid"), this)),
      m_showMetrics(new QCheckBox(tr("Show layout metrics"), this)),
      m_unit(new QComboBox(this)),
      m_step(new QDoubleSpinBox(this)),
      m_referenceFont(new QDoubleSpinBox(this)),
      m_useCurrent(new QPushButton(tr("Use Current Element"), this)),
      m_resolvedStep(new QLabel(this)),
      m_origin(new QComboBox(this)),
      m_offset(new QDoubleSpinBox(this)),
      m_majorEvery(new QSpinBox(this)),
      m_minorColorButton(new QPushButton(this)),
      m_minorOpacity(new QSpinBox(this)),
      m_majorColorButton(new QPushButton(this)),
      m_majorOpacity(new QSpinBox(this)),
      m_minimumZoom(new QSpinBox(this))
{
    setWindowTitle(tr("Baseline / Rhythm Grid"));
    setMinimumSize(560, 520);

    m_unit->addItem(tr("Pixels (px)"), static_cast<int>(BaselineGridUnit::Pixels));
    m_unit->addItem(tr("Fixed reference em"), static_cast<int>(BaselineGridUnit::Em));
    m_origin->addItem(tr("Document Top"), static_cast<int>(BaselineGridOrigin::DocumentTop));
    m_origin->addItem(tr("Body Content Top"), static_cast<int>(BaselineGridOrigin::BodyContentTop));

    m_step->setDecimals(3);
    m_step->setRange(0.001, 1000.0);
    m_step->setSingleStep(0.25);
    m_referenceFont->setDecimals(2);
    m_referenceFont->setRange(0.25, 1000.0);
    m_referenceFont->setSuffix(tr(" px"));
    m_offset->setDecimals(2);
    m_offset->setRange(-9999.0, 9999.0);
    m_offset->setSuffix(tr(" px"));
    m_majorEvery->setRange(1, 100);
    m_minorOpacity->setRange(0, 100);
    m_minorOpacity->setSuffix(tr("%"));
    m_majorOpacity->setRange(0, 100);
    m_majorOpacity->setSuffix(tr("%"));
    m_minimumZoom->setRange(10, 400);
    m_minimumZoom->setSuffix(tr("%"));
    m_useCurrent->setEnabled(qIsFinite(m_currentElementFontPx) && m_currentElementFontPx > 0.0);

    QHBoxLayout *referenceLayout = new QHBoxLayout;
    referenceLayout->setContentsMargins(0, 0, 0, 0);
    referenceLayout->addWidget(m_referenceFont, 1);
    referenceLayout->addWidget(m_useCurrent);

    QFormLayout *geometryLayout = new QFormLayout;
    geometryLayout->addRow(tr("Unit:"), m_unit);
    geometryLayout->addRow(tr("Grid step:"), m_step);
    geometryLayout->addRow(tr("Reference font size:"), referenceLayout);
    geometryLayout->addRow(tr("Resolved step:"), m_resolvedStep);
    geometryLayout->addRow(tr("Grid origin:"), m_origin);
    geometryLayout->addRow(tr("Grid offset:"), m_offset);
    geometryLayout->addRow(tr("Major line every:"), m_majorEvery);
    QGroupBox *geometryGroup = new QGroupBox(tr("Geometry"), this);
    geometryGroup->setLayout(geometryLayout);

    QFormLayout *appearanceLayout = new QFormLayout;
    appearanceLayout->addRow(tr("Minor line color:"), m_minorColorButton);
    appearanceLayout->addRow(tr("Minor line opacity:"), m_minorOpacity);
    appearanceLayout->addRow(tr("Major line color:"), m_majorColorButton);
    appearanceLayout->addRow(tr("Major line opacity:"), m_majorOpacity);
    appearanceLayout->addRow(tr("Minimum zoom for minor lines:"), m_minimumZoom);
    QGroupBox *appearanceGroup = new QGroupBox(tr("Appearance"), this);
    appearanceGroup->setLayout(appearanceLayout);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults,
        this);
    connect(buttons, &QDialogButtonBox::accepted, this, &BaselineGridSettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &BaselineGridSettingsDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
            this, &BaselineGridSettingsDialog::resetDefaults);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_showGrid);
    layout->addWidget(m_showMetrics);
    layout->addWidget(geometryGroup);
    layout->addWidget(appearanceGroup);
    layout->addStretch();
    layout->addWidget(buttons);

    connect(m_unit, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &BaselineGridSettingsDialog::updateResolvedStep);
    connect(m_step, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &BaselineGridSettingsDialog::updateResolvedStep);
    connect(m_referenceFont, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &BaselineGridSettingsDialog::updateResolvedStep);
    connect(m_useCurrent, &QPushButton::clicked, this, &BaselineGridSettingsDialog::useCurrentElement);
    connect(m_minorColorButton, &QPushButton::clicked, this, &BaselineGridSettingsDialog::chooseMinorColor);
    connect(m_majorColorButton, &QPushButton::clicked, this, &BaselineGridSettingsDialog::chooseMajorColor);

    populate(settings);
}

void BaselineGridSettingsDialog::populate(const BaselineGridSettings &settings)
{
    m_showGrid->setChecked(settings.enabled);
    m_showMetrics->setChecked(settings.metricsEnabled);
    m_unit->setCurrentIndex(m_unit->findData(static_cast<int>(settings.unit)));
    m_step->setValue(settings.step);
    m_referenceFont->setValue(settings.referenceFontPx);
    m_origin->setCurrentIndex(m_origin->findData(static_cast<int>(settings.origin)));
    m_offset->setValue(settings.offsetCssPx);
    m_majorEvery->setValue(settings.majorEvery);
    m_minorColor = settings.minorColor;
    m_minorOpacity->setValue(qRound(settings.minorOpacity * 100.0));
    m_majorColor = settings.majorColor;
    m_colorsCustomized = settings.colorsCustomized;
    m_majorOpacity->setValue(qRound(settings.majorOpacity * 100.0));
    m_minimumZoom->setValue(settings.minimumZoomPercent);
    updateColorButton(m_minorColorButton, m_minorColor);
    updateColorButton(m_majorColorButton, m_majorColor);
    updateResolvedStep();
}

BaselineGridSettings BaselineGridSettingsDialog::gridSettings() const
{
    BaselineGridSettings settings;
    settings.enabled = m_showGrid->isChecked();
    settings.metricsEnabled = m_showMetrics->isChecked();
    settings.unit = static_cast<BaselineGridUnit>(m_unit->currentData().toInt());
    settings.step = m_step->value();
    settings.referenceFontPx = m_referenceFont->value();
    settings.origin = static_cast<BaselineGridOrigin>(m_origin->currentData().toInt());
    settings.offsetCssPx = m_offset->value();
    settings.majorEvery = m_majorEvery->value();
    settings.minorColor = m_minorColor;
    settings.minorOpacity = m_minorOpacity->value() / 100.0;
    settings.majorColor = m_majorColor;
    settings.majorOpacity = m_majorOpacity->value() / 100.0;
    settings.colorsCustomized = m_colorsCustomized;
    settings.minimumZoomPercent = m_minimumZoom->value();
    return settings;
}

void BaselineGridSettingsDialog::accept()
{
    if (!gridSettings().isValid()) {
        QMessageBox::warning(this, tr("Invalid Grid Settings"),
                             tr("The resolved grid step must be between 0.25 px and 1000 px."));
        return;
    }
    QDialog::accept();
}

void BaselineGridSettingsDialog::chooseMinorColor()
{
    const QColor color = QColorDialog::getColor(m_minorColor, this, tr("Choose Minor Grid Color"));
    if (color.isValid()) {
        m_minorColor = color;
        m_colorsCustomized = true;
        updateColorButton(m_minorColorButton, color);
    }
}

void BaselineGridSettingsDialog::chooseMajorColor()
{
    const QColor color = QColorDialog::getColor(m_majorColor, this, tr("Choose Major Grid Color"));
    if (color.isValid()) {
        m_majorColor = color;
        m_colorsCustomized = true;
        updateColorButton(m_majorColorButton, color);
    }
}

void BaselineGridSettingsDialog::useCurrentElement()
{
    if (qIsFinite(m_currentElementFontPx) && m_currentElementFontPx > 0.0) {
        m_referenceFont->setValue(m_currentElementFontPx);
    }
}

void BaselineGridSettingsDialog::resetDefaults()
{
    populate(BaselineGridSettings::defaults(m_darkTheme));
}

void BaselineGridSettingsDialog::updateResolvedStep()
{
    const BaselineGridUnit unit = static_cast<BaselineGridUnit>(m_unit->currentData().toInt());
    const bool usesReference = unit == BaselineGridUnit::Em;
    m_referenceFont->setEnabled(usesReference);
    m_useCurrent->setEnabled(usesReference && qIsFinite(m_currentElementFontPx)
                             && m_currentElementFontPx > 0.0);
    const qreal resolved = usesReference ? m_step->value() * m_referenceFont->value() : m_step->value();
    m_resolvedStep->setText(tr("%1 CSS px").arg(resolved, 0, 'f', 2));
}

void BaselineGridSettingsDialog::updateColorButton(QPushButton *button, const QColor &color)
{
    button->setText(color.name(QColor::HexRgb));
    button->setStyleSheet(QStringLiteral("QPushButton { border-left: 24px solid %1; }").arg(color.name()));
    button->setAccessibleName(tr("Grid color %1").arg(color.name(QColor::HexRgb)));
}
