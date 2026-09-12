/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef AGENTSETTINGSWIDGET_H
#define AGENTSETTINGSWIDGET_H

#include <QHash>
#include <QList>

#include "Agent/Model/AgentModelCatalog.h"
#include "PreferencesWidget.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class AgentSettingsWidget : public PreferencesWidget
{
    Q_OBJECT

public:
    AgentSettingsWidget();
    PreferencesWidget::ResultActions saveSettings() override;

private:
    void readSettings();
    void onProviderChanged();
    void refreshModels();
    void testConnection();
    void invalidateConnectionTest(bool update_status);
    void setConnectionControlsEnabled(bool enabled);
    void fillModelCombo();
    void updateModelInfo();
    void rememberCurrentProvider();
    void applyStoredProvider(const QString &kind);
    QString selectedModelId() const;
    SigilAgent::CatalogModel selectedCatalogModel() const;
    SigilAgent::AgentProviderKind currentKind() const;

    QComboBox *m_provider = nullptr;
    QLineEdit *m_baseUrl = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QComboBox *m_model = nullptr;
    QPushButton *m_refreshModels = nullptr;
    QPushButton *m_testConnection = nullptr;
    QLabel *m_modelInfo = nullptr;
    QLabel *m_status = nullptr;
    QCheckBox *m_thinking = nullptr;
    QComboBox *m_effort = nullptr;

    QString m_activeProvider;
    QHash<QString, QString> m_providerKeys;
    QHash<QString, QString> m_providerModels;
    QHash<QString, QString> m_providerUrls;
    QHash<QString, QString> m_providerCatalogs;
    QList<SigilAgent::CatalogModel> m_models;
    QString m_catalogJson;
    bool m_loading = false;
};

#endif
