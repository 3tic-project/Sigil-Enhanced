/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef AGENTSETTINGSWIDGET_H
#define AGENTSETTINGSWIDGET_H

#include "PreferencesWidget.h"

class QCheckBox;
class QComboBox;
class QLineEdit;

class AgentSettingsWidget : public PreferencesWidget
{
    Q_OBJECT

public:
    AgentSettingsWidget();
    PreferencesWidget::ResultActions saveSettings() override;

private:
    void readSettings();

    QLineEdit *m_baseUrl = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QLineEdit *m_model = nullptr;
    QCheckBox *m_thinking = nullptr;
    QComboBox *m_effort = nullptr;
};

#endif
