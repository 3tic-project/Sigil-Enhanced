/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#pragma once

#include <QDialog>

#include "ChineseConversion/ChineseConversionTypes.h"

class QCheckBox;
class QComboBox;
class QLabel;

class ChineseConversionDialog final : public QDialog
{
    Q_OBJECT

public:
    enum class RequestedAction {
        Preview,
        Convert
    };

    ChineseConversionDialog(const ChineseConversionOptions& options,
                            bool selectionAvailable,
                            const QString& resourcePath,
                            QWidget *parent = nullptr);

    ChineseConversionOptions Options() const;
    RequestedAction Action() const;

private slots:
    void UpdateDescription();
    void RequestPreview();
    void RequestConvert();

private:
    QComboBox *m_Mode = nullptr;
    QComboBox *m_Scope = nullptr;
    QLabel *m_Description = nullptr;
    QCheckBox *m_IncludeAltText = nullptr;
    QCheckBox *m_IncludeTitleAttributes = nullptr;
    QCheckBox *m_IncludeAriaLabels = nullptr;
    QCheckBox *m_PreserveJapanese = nullptr;
    QCheckBox *m_SkipCode = nullptr;
    QCheckBox *m_SkipPre = nullptr;
    QCheckBox *m_PreviewBeforeApply = nullptr;
    ChineseConversionOptions m_InitialOptions;
    RequestedAction m_Action = RequestedAction::Preview;
};
