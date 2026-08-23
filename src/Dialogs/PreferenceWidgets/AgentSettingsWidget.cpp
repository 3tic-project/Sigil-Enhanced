/************************************************************************
**
**  This file is part of Sigil-Enhanced.
**
*************************************************************************/

#include "AgentSettingsWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "Agent/Model/AgentProviderPreset.h"
#include "Agent/Persistence/AgentSettings.h"

using SigilAgent::AgentProviderKind;
using SigilAgent::CatalogModel;
using SigilAgent::CatalogResult;

AgentSettingsWidget::AgentSettingsWidget()
{
    setWindowTitle(tr("Native Agent"));
    auto *layout = new QFormLayout(this);

    m_provider = new QComboBox(this);
    m_provider->setObjectName(QStringLiteral("agentProviderCombo"));
    m_provider->addItem(tr("DeepSeek"), QStringLiteral("deepseek"));
    m_provider->addItem(tr("OpenCode Go"), QStringLiteral("opencode_go"));
    m_provider->addItem(tr("OpenRouter"), QStringLiteral("openrouter"));
    m_provider->addItem(tr("Custom (OpenAI-compatible)"), QStringLiteral("custom"));

    m_baseUrl = new QLineEdit(this);
    m_baseUrl->setObjectName(QStringLiteral("agentBaseUrl"));
    m_baseUrl->setPlaceholderText(QStringLiteral("https://api.deepseek.com/chat/completions"));

    m_apiKey = new QLineEdit(this);
    m_apiKey->setObjectName(QStringLiteral("agentApiKey"));
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(tr("Stored only in local Sigil settings, never in the EPUB or transcript"));

    auto *model_row = new QWidget(this);
    auto *model_layout = new QHBoxLayout(model_row);
    model_layout->setContentsMargins(0, 0, 0, 0);
    m_model = new QComboBox(model_row);
    m_model->setObjectName(QStringLiteral("agentModelCombo"));
    m_model->setEditable(true);
    m_model->setInsertPolicy(QComboBox::NoInsert);
    m_model->setMaxVisibleItems(20);
    m_model->setMinimumWidth(240);
    if (QCompleter *completer = m_model->completer()) {
        completer->setFilterMode(Qt::MatchContains);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
    }
    m_refreshModels = new QPushButton(tr("Refresh models"), model_row);
    m_refreshModels->setObjectName(QStringLiteral("agentRefreshModelsButton"));
    model_layout->addWidget(m_model, 1);
    model_layout->addWidget(m_refreshModels);

    m_modelInfo = new QLabel(this);
    m_modelInfo->setObjectName(QStringLiteral("agentModelInfo"));
    m_modelInfo->setWordWrap(true);
    m_modelInfo->setStyleSheet(QStringLiteral("color: palette(mid);"));

    m_thinking = new QCheckBox(tr("Send thinking (reasoning_content)"), this);
    m_thinking->setObjectName(QStringLiteral("agentThinking"));
    m_effort = new QComboBox(this);
    m_effort->setObjectName(QStringLiteral("agentReasoningEffort"));
    m_effort->addItems({ QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high") });

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("agentSettingsStatus"));
    m_status->setWordWrap(true);

    auto *note = new QLabel(tr("Choose the provider and model here. The Agent dock uses this model and does not ask for a model name. Refresh models loads the live catalog and advertised parameters (context length, tools, reasoning) from the server."), this);
    note->setWordWrap(true);

    layout->addRow(tr("Provider"), m_provider);
    layout->addRow(tr("Chat Completions URL"), m_baseUrl);
    layout->addRow(tr("API key"), m_apiKey);
    layout->addRow(tr("Model"), model_row);
    layout->addRow(QString(), m_modelInfo);
    layout->addRow(m_thinking);
    layout->addRow(tr("Reasoning effort"), m_effort);
    layout->addRow(m_status);
    layout->addRow(note);

    connect(m_provider, &QComboBox::currentIndexChanged, this, [this](int) { onProviderChanged(); });
    connect(m_refreshModels, &QPushButton::clicked, this, [this]() { refreshModels(); });
    connect(m_model, &QComboBox::currentTextChanged, this, [this](const QString &) { updateModelInfo(); });

    readSettings();
}

SigilAgent::AgentProviderKind AgentSettingsWidget::currentKind() const
{
    return SigilAgent::providerKindFromName(m_provider->currentData().toString());
}

void AgentSettingsWidget::rememberCurrentProvider()
{
    if (m_activeProvider.isEmpty()) return;
    m_providerKeys.insert(m_activeProvider, m_apiKey->text());
    m_providerModels.insert(m_activeProvider, selectedModelId());
    m_providerUrls.insert(m_activeProvider, m_baseUrl->text().trimmed());
}

void AgentSettingsWidget::applyStoredProvider(const QString &kind)
{
    m_activeProvider = kind;
    const SigilAgent::AgentProviderPreset preset = SigilAgent::presetFor(SigilAgent::providerKindFromName(kind));
    QString url = m_providerUrls.value(kind);
    if (url.isEmpty() && kind != QLatin1String("custom")) {
        url = SigilAgent::chatCompletionsUrl(preset.kind, QString());
    }
    m_baseUrl->setText(url);
    m_apiKey->setText(m_providerKeys.value(kind));
    m_baseUrl->setPlaceholderText(kind == QLatin1String("custom")
        ? tr("Full Chat Completions URL or API base")
        : preset.apiBase + QStringLiteral("/chat/completions"));
}

QString AgentSettingsWidget::selectedModelId() const
{
    const int index = m_model->currentIndex();
    if (index >= 0 && m_model->itemText(index) == m_model->currentText()) {
        const QString id = m_model->itemData(index).toString();
        if (!id.isEmpty()) return id;
    }
    const QString typed = m_model->currentText().trimmed();
    for (const CatalogModel &model : m_models) {
        if (model.id == typed || model.name == typed) return model.id;
    }
    return typed;
}

CatalogModel AgentSettingsWidget::selectedCatalogModel() const
{
    const QString id = selectedModelId();
    for (const CatalogModel &model : m_models) {
        if (model.id == id) return model;
    }
    CatalogModel unknown;
    unknown.id = id;
    unknown.name = id;
    return unknown;
}

void AgentSettingsWidget::fillModelCombo()
{
    const QString current = selectedModelId();
    const QSignalBlocker blocker(m_model);
    m_model->clear();
    for (const CatalogModel &model : m_models) {
        m_model->addItem(model.id, model.id);
        QStringList tip;
        if (!model.name.isEmpty() && model.name != model.id) tip.append(model.name);
        if (model.contextLength > 0) {
            tip.append(tr("%1k context").arg((model.contextLength + 999) / 1000));
        }
        if (model.tools) tip.append(tr("tools"));
        if (model.reasoning) tip.append(tr("reasoning"));
        if (!tip.isEmpty()) m_model->setItemData(m_model->count() - 1, tip.join(QStringLiteral(" · ")), Qt::ToolTipRole);
    }
    int index = m_model->findData(current);
    if (index < 0) index = m_model->findText(current);
    if (index >= 0) {
        m_model->setCurrentIndex(index);
    } else if (!current.isEmpty()) {
        m_model->setEditText(current);
    }
    updateModelInfo();
}

void AgentSettingsWidget::updateModelInfo()
{
    const CatalogModel model = selectedCatalogModel();
    if (model.id.isEmpty()) {
        m_modelInfo->setText(tr("Refresh models to load ids and parameters from the server, or type an id."));
        return;
    }
    QStringList parts;
    if (!model.name.isEmpty() && model.name != model.id) parts.append(model.name);
    if (model.contextLength > 0) {
        parts.append(tr("%1 context tokens").arg(model.contextLength));
    }
    parts.append(model.tools ? tr("tools") : tr("tools not advertised"));
    parts.append(model.reasoning ? tr("reasoning") : tr("reasoning not advertised"));
    if (!model.supportedParameters.isEmpty()) {
        parts.append(tr("parameters: %1").arg(model.supportedParameters.join(QStringLiteral(", "))));
    }
    m_modelInfo->setText(parts.join(QStringLiteral(" · ")));
}

void AgentSettingsWidget::onProviderChanged()
{
    if (m_loading) return;
    rememberCurrentProvider();
    applyStoredProvider(m_provider->currentData().toString());
    m_models.clear();
    m_catalogJson = m_providerCatalogs.value(m_activeProvider);
    if (!m_catalogJson.isEmpty()) {
        const QJsonDocument document = QJsonDocument::fromJson(m_catalogJson.toUtf8());
        if (document.isObject()) {
            CatalogResult cached = SigilAgent::AgentModelCatalog::fromCacheJson(document.object());
            SigilAgent::AgentModelCatalog::applyProviderDefaults(&cached, currentKind());
            m_models = cached.models;
        }
    }
    const QString stored_model = m_providerModels.value(m_activeProvider);
    {
        const QSignalBlocker blocker(m_model);
        m_model->clear();
        m_model->setEditText(stored_model);
    }
    fillModelCombo();
    if (m_models.isEmpty()) {
        m_status->setText(tr("Refresh models to load this provider's catalog."));
    } else {
        m_status->setText(tr("Loaded %1 cached models. Refresh to update from the server.").arg(m_models.size()));
    }
}

void AgentSettingsWidget::refreshModels()
{
    rememberCurrentProvider();
    const AgentProviderKind kind = currentKind();
    const QString url = SigilAgent::modelsUrl(kind, m_baseUrl->text());
    QString referer;
    QString title;
    if (kind == AgentProviderKind::OpenRouter) {
        referer = SigilAgent::agentHttpReferer();
        title = SigilAgent::agentHttpTitle();
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_refreshModels->setEnabled(false);
    CatalogResult result = SigilAgent::AgentModelCatalog::fetch(url, m_apiKey->text(), referer, title);
    SigilAgent::AgentModelCatalog::applyProviderDefaults(&result, kind);
    m_refreshModels->setEnabled(true);
    QApplication::restoreOverrideCursor();

    if (!result.error.isEmpty()) {
        m_status->setText(result.error);
        return;
    }
    m_models = result.models;
    m_catalogJson = QString::fromUtf8(
        QJsonDocument(SigilAgent::AgentModelCatalog::toCacheJson(result, kind)).toJson(QJsonDocument::Compact));
    m_providerCatalogs.insert(m_activeProvider, m_catalogJson);
    fillModelCombo();
    m_status->setText(tr("Loaded %1 models from the server.").arg(m_models.size()));
}

void AgentSettingsWidget::readSettings()
{
    m_loading = true;
    SigilAgent::AgentSettings settings;
    const QJsonObject secrets = settings.providerSecrets();
    const QJsonObject models = settings.providerModels();
    const QJsonObject urls = settings.providerUrls();
    const QJsonObject catalogs = settings.providerCatalogs();
    for (auto it = secrets.begin(); it != secrets.end(); ++it) {
        m_providerKeys.insert(it.key(), it.value().toString());
    }
    for (auto it = models.begin(); it != models.end(); ++it) {
        m_providerModels.insert(it.key(), it.value().toString());
    }
    for (auto it = urls.begin(); it != urls.end(); ++it) {
        m_providerUrls.insert(it.key(), it.value().toString());
    }
    for (auto it = catalogs.begin(); it != catalogs.end(); ++it) {
        m_providerCatalogs.insert(it.key(), it.value().toString());
    }

    const QString kind = settings.provider();
    m_providerKeys.insert(kind, settings.apiKey());
    m_providerModels.insert(kind, settings.model());
    m_providerUrls.insert(kind, settings.baseUrl());

    int index = m_provider->findData(kind);
    if (index < 0) index = 0;
    m_provider->setCurrentIndex(index);
    applyStoredProvider(m_provider->currentData().toString());
    m_thinking->setChecked(settings.thinkingEnabled());
    const int effort = m_effort->findText(settings.reasoningEffort());
    m_effort->setCurrentIndex(effort >= 0 ? effort : 1);

    m_catalogJson = settings.catalogJson();
    if (m_catalogJson.isEmpty()) m_catalogJson = m_providerCatalogs.value(kind);
    if (!m_catalogJson.isEmpty()) {
        m_providerCatalogs.insert(kind, m_catalogJson);
        const QJsonDocument document = QJsonDocument::fromJson(m_catalogJson.toUtf8());
        if (document.isObject()) {
            CatalogResult cached = SigilAgent::AgentModelCatalog::fromCacheJson(document.object());
            SigilAgent::AgentModelCatalog::applyProviderDefaults(&cached, currentKind());
            m_models = cached.models;
        }
    }
    fillModelCombo();
    const QString current_model = settings.model();
    if (!current_model.isEmpty()) {
        int model_index = m_model->findData(current_model);
        if (model_index < 0) model_index = m_model->findText(current_model);
        if (model_index >= 0) m_model->setCurrentIndex(model_index);
        else m_model->setEditText(current_model);
    }
    m_status->clear();
    m_loading = false;
}

PreferencesWidget::ResultActions AgentSettingsWidget::saveSettings()
{
    rememberCurrentProvider();
    SigilAgent::AgentSettings settings;
    const QString kind = m_provider->currentData().toString();
    settings.setProvider(kind);
    settings.setBaseUrl(m_baseUrl->text().trimmed());
    settings.setApiKey(m_apiKey->text());
    settings.setModel(selectedModelId());
    settings.setThinkingEnabled(m_thinking->isChecked());
    settings.setReasoningEffort(m_effort->currentText());
    settings.setCatalogJson(m_catalogJson);

    const CatalogModel selected = selectedCatalogModel();
    if (!selected.id.isEmpty() && !m_models.isEmpty()) {
        bool in_catalog = false;
        for (const CatalogModel &model : m_models) {
            if (model.id == selected.id) {
                in_catalog = true;
                settings.setModelSupportsReasoning(model.reasoning);
                settings.setModelSupportsTools(model.tools);
                settings.setModelContextLength(model.contextLength);
                break;
            }
        }
        if (!in_catalog && currentKind() == AgentProviderKind::DeepSeek) {
            settings.setModelSupportsReasoning(true);
            settings.setModelSupportsTools(true);
        }
    }

    QJsonObject secrets;
    QJsonObject models;
    QJsonObject urls;
    QJsonObject catalogs;
    for (auto it = m_providerKeys.begin(); it != m_providerKeys.end(); ++it) {
        secrets.insert(it.key(), it.value());
    }
    for (auto it = m_providerModels.begin(); it != m_providerModels.end(); ++it) {
        models.insert(it.key(), it.value());
    }
    for (auto it = m_providerUrls.begin(); it != m_providerUrls.end(); ++it) {
        urls.insert(it.key(), it.value());
    }
    for (auto it = m_providerCatalogs.begin(); it != m_providerCatalogs.end(); ++it) {
        catalogs.insert(it.key(), it.value());
    }
    settings.setProviderSecrets(secrets);
    settings.setProviderModels(models);
    settings.setProviderUrls(urls);
    settings.setProviderCatalogs(catalogs);
    return PreferencesWidget::ResultAction_None;
}
