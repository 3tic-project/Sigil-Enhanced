/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#pragma once
#ifndef AGENTSETTINGSWIDGET_H
#define AGENTSETTINGSWIDGET_H

#include <atomic>
#include <memory>

#include <QHash>
#include <QList>

#include "Agent/Model/AgentModelCatalog.h"
#include "PreferencesWidget.h"

class QCheckBox;
class QComboBox;
template<typename T> class QFutureWatcher;
class QLabel;
class QLineEdit;
class QPushButton;

namespace SigilAgent
{
struct AgentConnectionProbeResult;
}

class AgentSettingsWidget : public PreferencesWidget
{
    Q_OBJECT

public:
    AgentSettingsWidget();
    ~AgentSettingsWidget() override;
    PreferencesWidget::ResultActions saveSettings() override;

private:
    void readSettings();
    void onProviderChanged();
    void refreshModels();
    void finishModelRefresh();
    void testConnection();
    void finishConnectionTest();
    void invalidateConnectionTest(bool update_status);
    void showRememberedConnectionTest();
    void setConnectionControlsEnabled(bool enabled);
    void fillModelCombo();
    void updateModelInfo();
    void rememberCurrentProvider();
    void applyStoredProvider(const QString &kind);
    QString selectedModelId() const;
    SigilAgent::CatalogModel selectedCatalogModel() const;
    SigilAgent::AgentProviderKind currentKind() const;
    QString currentConnectionFingerprint() const;

    QComboBox *m_provider = nullptr;
    QLineEdit *m_baseUrl = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QComboBox *m_model = nullptr;
    QFutureWatcher<SigilAgent::CatalogResult> *m_catalogWatcher = nullptr;
    QFutureWatcher<SigilAgent::AgentConnectionProbeResult> *m_connectionWatcher = nullptr;
    QPushButton *m_refreshModels = nullptr;
    QPushButton *m_testConnection = nullptr;
    QLabel *m_modelInfo = nullptr;
    QLabel *m_status = nullptr;
    QCheckBox *m_thinking = nullptr;
    QCheckBox *m_tokenUsage = nullptr;
    QComboBox *m_effort = nullptr;

    QString m_activeProvider;
    QHash<QString, QString> m_providerKeys;
    QHash<QString, QString> m_providerModels;
    QHash<QString, QString> m_providerUrls;
    QHash<QString, QString> m_providerCatalogs;
    QList<SigilAgent::CatalogModel> m_models;
    QString m_catalogJson;
    SigilAgent::AgentProviderKind m_catalogRequestKind = SigilAgent::AgentProviderKind::Custom;
    QString m_catalogRequestProvider;
    std::shared_ptr<std::atomic_bool> m_catalogCancelled;
    std::shared_ptr<std::atomic_bool> m_connectionCancelled;
    QString m_successfulConnectionFingerprint;
    qint64 m_successfulConnectionAtMs = 0;
    bool m_loading = false;
};

#endif
