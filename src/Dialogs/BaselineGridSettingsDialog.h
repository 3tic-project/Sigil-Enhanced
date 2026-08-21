/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook Contributors
**
**  This file is part of Sigil.
**
*************************************************************************/

#pragma once
#ifndef BASELINEGRIDSETTINGSDIALOG_H
#define BASELINEGRIDSETTINGSDIALOG_H

#include "ViewEditors/BaselineGridModel.h"

#include <QDialog>

class QCheckBox;
class QColor;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

class BaselineGridSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    BaselineGridSettingsDialog(const BaselineGridSettings &settings,
                               qreal currentElementFontPx,
                               bool darkTheme,
                               QWidget *parent = nullptr);

    BaselineGridSettings gridSettings() const;
    void setCurrentElementFontPx(qreal currentElementFontPx);

protected:
    void accept() override;

private slots:
    void chooseMinorColor();
    void chooseMajorColor();
    void useCurrentElement();
    void resetDefaults();
    void updateResolvedStep();

private:
    void populate(const BaselineGridSettings &settings);
    void updateColorButton(QPushButton *button, const QColor &color);

    qreal m_currentElementFontPx;
    bool m_darkTheme;
    QColor m_minorColor;
    QColor m_majorColor;
    bool m_colorsCustomized = false;
    QCheckBox *m_showGrid;
    QCheckBox *m_showMetrics;
    QCheckBox *m_horizontalGrid;
    QCheckBox *m_verticalGrid;
    QComboBox *m_unit;
    QDoubleSpinBox *m_step;
    QDoubleSpinBox *m_verticalStep;
    QDoubleSpinBox *m_referenceFont;
    QPushButton *m_useCurrent;
    QLabel *m_resolvedStep;
    QLabel *m_verticalResolvedStep;
    QComboBox *m_origin;
    QDoubleSpinBox *m_offset;
    QSpinBox *m_majorEvery;
    QPushButton *m_minorColorButton;
    QSpinBox *m_minorOpacity;
    QPushButton *m_majorColorButton;
    QSpinBox *m_majorOpacity;
    QSpinBox *m_minimumZoom;
};

#endif // BASELINEGRIDSETTINGSDIALOG_H
