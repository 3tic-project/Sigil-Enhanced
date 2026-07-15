/************************************************************************
**
**  Copyright (C) 2026 Sigil Enhanced contributors
**
*************************************************************************/

#pragma once

#include "Dialogs/PreferenceWidgets/PreferencesWidget.h"

class QCheckBox;
class QComboBox;

class ChineseConversionSettingsWidget final : public PreferencesWidget
{
    Q_OBJECT

public:
    explicit ChineseConversionSettingsWidget(QWidget *parent = nullptr);
    ResultActions saveSettings() override;

private:
    QComboBox *m_Mode = nullptr;
    QComboBox *m_Scope = nullptr;
    QCheckBox *m_IncludeAltText = nullptr;
    QCheckBox *m_IncludeTitleAttributes = nullptr;
    QCheckBox *m_IncludeAriaLabels = nullptr;
    QCheckBox *m_PreserveJapanese = nullptr;
    QCheckBox *m_SkipCode = nullptr;
    QCheckBox *m_SkipPre = nullptr;
    QCheckBox *m_PreviewBeforeApply = nullptr;
};
