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
#include <QGridLayout>
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
      m_showGrid(new QCheckBox(tr("Show grid"), this)),
      m_showMetrics(new QCheckBox(tr("Show layout metrics"), this)),
      m_horizontalGrid(new QCheckBox(tr("Enabled"), this)),
      m_verticalGrid(new QCheckBox(tr("Enabled"), this)),
      m_unit(new QComboBox(this)),
      m_step(new QDoubleSpinBox(this)),
      m_verticalStep(new QDoubleSpinBox(this)),
      m_referenceFont(new QDoubleSpinBox(this)),
      m_useCurrent(new QPushButton(tr("Use Current Element"), this)),
      m_resolvedStep(new QLabel(this)),
      m_verticalResolvedStep(new QLabel(this)),
      m_origin(new QComboBox(this)),
      m_offset(new QDoubleSpinBox(this)),
      m_majorEvery(new QSpinBox(this)),
      m_minorColorButton(new QPushButton(this)),
      m_minorOpacity(new QSpinBox(this)),
      m_majorColorButton(new QPushButton(this)),
      m_majorOpacity(new QSpinBox(this)),
      m_minimumZoom(new QSpinBox(this))
{
    setWindowTitle(tr("Grid Settings"));
    setMinimumSize(760, 360);
    resize(800, 390);

    m_showGrid->setObjectName(QStringLiteral("showGrid"));
    m_horizontalGrid->setObjectName(QStringLiteral("horizontalGrid"));
    m_verticalGrid->setObjectName(QStringLiteral("verticalGrid"));
    m_horizontalGrid->setAccessibleName(tr("Horizontal lines"));
    m_verticalGrid->setAccessibleName(tr("Vertical lines"));
    m_step->setObjectName(QStringLiteral("horizontalSpacing"));
    m_verticalStep->setObjectName(QStringLiteral("verticalSpacing"));

    m_unit->addItem(tr("Pixels (px)"), static_cast<int>(BaselineGridUnit::Pixels));
    m_unit->addItem(tr("Fixed reference em"), static_cast<int>(BaselineGridUnit::Em));
    m_origin->addItem(tr("Document Top"), static_cast<int>(BaselineGridOrigin::DocumentTop));
    m_origin->addItem(tr("Body Content Top"), static_cast<int>(BaselineGridOrigin::BodyContentTop));

    m_step->setDecimals(3);
    m_step->setRange(0.001, 1000.0);
    m_step->setSingleStep(0.25);
    m_verticalStep->setDecimals(3);
    m_verticalStep->setRange(0.001, 1000.0);
    m_verticalStep->setSingleStep(0.25);
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
    m_useCurrent->setEnabled(false);
    m_showMetrics->setChecked(false);
    m_showMetrics->setVisible(false);
    m_useCurrent->setVisible(false);

    QGridLayout *geometryLayout = new QGridLayout;
    geometryLayout->setAlignment(Qt::AlignTop);
    geometryLayout->setHorizontalSpacing(12);
    geometryLayout->setVerticalSpacing(8);
    geometryLayout->addWidget(new QLabel(tr("Unit:"), this), 0, 0);
    geometryLayout->addWidget(m_unit, 0, 1);
    geometryLayout->addWidget(new QLabel(tr("Reference font size:"), this), 0, 2);
    geometryLayout->addWidget(m_referenceFont, 0, 3);

    QLabel *horizontalHeading = new QLabel(tr("Horizontal lines"), this);
    QLabel *verticalHeading = new QLabel(tr("Vertical lines"), this);
    horizontalHeading->setAlignment(Qt::AlignCenter);
    verticalHeading->setAlignment(Qt::AlignCenter);
    geometryLayout->addWidget(horizontalHeading, 1, 1);
    geometryLayout->addWidget(verticalHeading, 1, 3);
    geometryLayout->addWidget(m_horizontalGrid, 2, 1, Qt::AlignCenter);
    geometryLayout->addWidget(m_verticalGrid, 2, 3, Qt::AlignCenter);
    geometryLayout->addWidget(new QLabel(tr("Spacing:"), this), 3, 0);
    geometryLayout->addWidget(m_step, 3, 1);
    geometryLayout->addWidget(m_verticalStep, 3, 3);
    geometryLayout->addWidget(new QLabel(tr("Resolved spacing:"), this), 4, 0);
    geometryLayout->addWidget(m_resolvedStep, 4, 1);
    geometryLayout->addWidget(m_verticalResolvedStep, 4, 3);

    geometryLayout->addWidget(new QLabel(tr("Grid origin:"), this), 5, 0);
    geometryLayout->addWidget(m_origin, 5, 1);
    geometryLayout->addWidget(new QLabel(tr("Grid offset:"), this), 5, 2);
    geometryLayout->addWidget(m_offset, 5, 3);
    geometryLayout->addWidget(new QLabel(tr("Major line every:"), this), 6, 0);
    geometryLayout->addWidget(m_majorEvery, 6, 1);
    geometryLayout->setColumnStretch(1, 1);
    geometryLayout->setColumnStretch(3, 1);
    QGroupBox *geometryGroup = new QGroupBox(tr("Geometry"), this);
    geometryGroup->setObjectName(QStringLiteral("gridGeometryGroup"));
    geometryGroup->setLayout(geometryLayout);

    QGridLayout *appearanceLayout = new QGridLayout;
    appearanceLayout->setAlignment(Qt::AlignTop);
    appearanceLayout->setHorizontalSpacing(12);
    appearanceLayout->setVerticalSpacing(8);
    QLabel *minorHeading = new QLabel(tr("Minor lines"), this);
    QLabel *majorHeading = new QLabel(tr("Major lines"), this);
    minorHeading->setAlignment(Qt::AlignCenter);
    majorHeading->setAlignment(Qt::AlignCenter);
    appearanceLayout->addWidget(minorHeading, 0, 1);
    appearanceLayout->addWidget(majorHeading, 0, 2);
    appearanceLayout->addWidget(new QLabel(tr("Color:"), this), 1, 0);
    appearanceLayout->addWidget(m_minorColorButton, 1, 1);
    appearanceLayout->addWidget(m_majorColorButton, 1, 2);
    appearanceLayout->addWidget(new QLabel(tr("Opacity:"), this), 2, 0);
    appearanceLayout->addWidget(m_minorOpacity, 2, 1);
    appearanceLayout->addWidget(m_majorOpacity, 2, 2);
    appearanceLayout->addWidget(
        new QLabel(tr("Minimum zoom for minor lines:"), this), 3, 0, 1, 2);
    appearanceLayout->addWidget(m_minimumZoom, 3, 2);
    appearanceLayout->setColumnStretch(1, 1);
    appearanceLayout->setColumnStretch(2, 1);
    QGroupBox *appearanceGroup = new QGroupBox(tr("Appearance"), this);
    appearanceGroup->setObjectName(QStringLiteral("gridAppearanceGroup"));
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
    QHBoxLayout *settingsColumns = new QHBoxLayout;
    settingsColumns->setObjectName(QStringLiteral("gridSettingsColumns"));
    settingsColumns->addWidget(geometryGroup, 3);
    settingsColumns->addWidget(appearanceGroup, 2);
    layout->addLayout(settingsColumns);
    layout->addStretch(1);
    layout->addWidget(buttons);

    connect(m_unit, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &BaselineGridSettingsDialog::updateResolvedStep);
    connect(m_showGrid, &QCheckBox::toggled,
            this, &BaselineGridSettingsDialog::emitPreviewSettings);
    connect(m_step, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &BaselineGridSettingsDialog::updateResolvedStep);
    connect(m_verticalStep, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &BaselineGridSettingsDialog::updateResolvedStep);
    connect(m_horizontalGrid, &QCheckBox::toggled,
            this, &BaselineGridSettingsDialog::updateResolvedStep);
    connect(m_verticalGrid, &QCheckBox::toggled,
            this, &BaselineGridSettingsDialog::updateResolvedStep);
    connect(m_referenceFont, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &BaselineGridSettingsDialog::updateResolvedStep);
    connect(m_origin, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &BaselineGridSettingsDialog::emitPreviewSettings);
    connect(m_offset, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &BaselineGridSettingsDialog::emitPreviewSettings);
    connect(m_majorEvery, qOverload<int>(&QSpinBox::valueChanged),
            this, &BaselineGridSettingsDialog::emitPreviewSettings);
    connect(m_minorOpacity, qOverload<int>(&QSpinBox::valueChanged),
            this, &BaselineGridSettingsDialog::emitPreviewSettings);
    connect(m_majorOpacity, qOverload<int>(&QSpinBox::valueChanged),
            this, &BaselineGridSettingsDialog::emitPreviewSettings);
    connect(m_minimumZoom, qOverload<int>(&QSpinBox::valueChanged),
            this, &BaselineGridSettingsDialog::emitPreviewSettings);
    connect(m_useCurrent, &QPushButton::clicked, this, &BaselineGridSettingsDialog::useCurrentElement);
    connect(m_minorColorButton, &QPushButton::clicked, this, &BaselineGridSettingsDialog::chooseMinorColor);
    connect(m_majorColorButton, &QPushButton::clicked, this, &BaselineGridSettingsDialog::chooseMajorColor);

    populate(settings);
    setCurrentElementFontPx(m_currentElementFontPx);
}

void BaselineGridSettingsDialog::populate(const BaselineGridSettings &settings)
{
    m_populating = true;
    m_showGrid->setChecked(settings.enabled);
    m_showMetrics->setChecked(false);
    m_horizontalGrid->setChecked(settings.horizontalEnabled);
    m_verticalGrid->setChecked(settings.verticalEnabled);
    m_unit->setCurrentIndex(m_unit->findData(static_cast<int>(settings.unit)));
    m_step->setValue(settings.step);
    m_verticalStep->setValue(settings.verticalStep);
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
    m_populating = false;
    updateResolvedStep();
}

BaselineGridSettings BaselineGridSettingsDialog::gridSettings() const
{
    BaselineGridSettings settings;
    settings.enabled = m_showGrid->isChecked();
    settings.metricsEnabled = false;
    settings.horizontalEnabled = m_horizontalGrid->isChecked();
    settings.verticalEnabled = m_verticalGrid->isChecked();
    settings.unit = static_cast<BaselineGridUnit>(m_unit->currentData().toInt());
    settings.step = m_step->value();
    settings.verticalStep = m_verticalStep->value();
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

void BaselineGridSettingsDialog::setCurrentElementFontPx(qreal currentElementFontPx)
{
    m_currentElementFontPx = currentElementFontPx;
    updateResolvedStep();
    if (qIsFinite(currentElementFontPx) && currentElementFontPx >= 0.25
            && currentElementFontPx <= 1000.0) {
        m_useCurrent->setToolTip(
            tr("Use the measured current element font size (%1 px).")
                .arg(currentElementFontPx, 0, 'f', 2));
    } else {
        m_useCurrent->setToolTip(tr("The current element font size is unavailable."));
    }
}

void BaselineGridSettingsDialog::accept()
{
    if (!gridSettings().isValid()) {
        QMessageBox::warning(this, tr("Invalid Grid Settings"),
                             tr("The resolved horizontal and vertical spacing must each be between 0.25 px and 1000 px."));
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
        emitPreviewSettings();
    }
}

void BaselineGridSettingsDialog::chooseMajorColor()
{
    const QColor color = QColorDialog::getColor(m_majorColor, this, tr("Choose Major Grid Color"));
    if (color.isValid()) {
        m_majorColor = color;
        m_colorsCustomized = true;
        updateColorButton(m_majorColorButton, color);
        emitPreviewSettings();
    }
}

void BaselineGridSettingsDialog::useCurrentElement()
{
    if (qIsFinite(m_currentElementFontPx) && m_currentElementFontPx >= 0.25
            && m_currentElementFontPx <= 1000.0) {
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
    m_step->setEnabled(m_horizontalGrid->isChecked());
    m_verticalStep->setEnabled(m_verticalGrid->isChecked());
    m_referenceFont->setEnabled(usesReference);
    m_useCurrent->setEnabled(usesReference && qIsFinite(m_currentElementFontPx)
                             && m_currentElementFontPx >= 0.25
                             && m_currentElementFontPx <= 1000.0);
    const qreal resolved = usesReference ? m_step->value() * m_referenceFont->value() : m_step->value();
    m_resolvedStep->setText(tr("%1 CSS px").arg(resolved, 0, 'f', 2));
    const qreal verticalResolved = usesReference
        ? m_verticalStep->value() * m_referenceFont->value() : m_verticalStep->value();
    m_verticalResolvedStep->setText(tr("%1 CSS px").arg(verticalResolved, 0, 'f', 2));
    emitPreviewSettings();
}

void BaselineGridSettingsDialog::emitPreviewSettings()
{
    if (m_populating) {
        return;
    }
    const BaselineGridSettings settings = gridSettings();
    if (settings.isValid()) {
        emit previewSettingsChanged(settings);
    }
}

void BaselineGridSettingsDialog::updateColorButton(QPushButton *button, const QColor &color)
{
    button->setText(color.name(QColor::HexRgb));
    button->setStyleSheet(QStringLiteral("QPushButton { border-left: 24px solid %1; }").arg(color.name()));
    button->setAccessibleName(tr("Grid color %1").arg(color.name(QColor::HexRgb)));
}
